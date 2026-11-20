// Copyright (c) Meta Platforms, Inc. and affiliates.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/EntityAttributesStore.h"
#include "algopt/rebalancer/solver/utils/OneObjectPerGroup.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include "gtest/gtest.h"
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>

#include <coroutine>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

/**
 * This test setup models a simplified GPU topology scenario:
 * - 2 servers (S0, S1), each with 4 server parts (P0-P3)
 * - 2 physical GPUs with overlapping assignments:
 *   * GPU0: Contains P0 and P1 from each server
 *   * GPU1: Contains P0 and P2 from each server
 * - Critical insight: P0 appears in BOTH GPUs (overlapping resource)
 * - 3 containers: "unassigned" (special), "container0", "container1"
 *
 * Test scenarios verify the GPU topology rule: when server parts share
 * physical GPU resources, they cannot be simultaneously assigned to
 * different containers (all-or-nothing allocation).
 */
class GpuTopologyTestFixture : public ExpressionTestsBase {
 public:
  static constexpr int NUM_SERVERS = 2;
  static constexpr int PARTS_PER_SERVER = 4;
  static constexpr int NUM_GPUS = 2;
  static constexpr int TOTAL_OBJECTS = NUM_SERVERS * PARTS_PER_SERVER;

 protected:
  void SetUp() override {
    folly::coro::blockingWait(setUpUniverse());
    initializeAssignment();
  }

  folly::coro::Task<void> setUpUniverse() {
    std::vector<std::string> allObjects;
    for (const auto serverId : folly::irange(NUM_SERVERS)) {
      for (const auto partId : folly::irange(PARTS_PER_SERVER)) {
        allObjects.push_back(fmt::format("S{}_P{}", serverId, partId));
      }
    }

    const entities::Map<std::string, std::vector<std::string>>
        initialAssignment = {
            {"unassigned", allObjects},
            {"container0", {}},
            {"container1", {}},
        };

    setInitialAssignment(initialAssignment);

    // Create physical GPU overlap partition with overlapping groups
    co_await createPhysicalGpuPartitions();

    universe_ = buildUniverse();

    unassigned_ = container("unassigned");
    container0_ = container("container0");
    container1_ = container("container1");

    objects_.reserve(TOTAL_OBJECTS);
    for (const auto serverId : folly::irange(NUM_SERVERS)) {
      for (const auto partId : folly::irange(PARTS_PER_SERVER)) {
        const auto objectName = fmt::format("S{}_P{}", serverId, partId);
        objects_.push_back(object(objectName));
      }
    }
  }

  /**
   * Get server part ID with bounds checking
   */
  [[nodiscard]] entities::ObjectId getServerPartID(int serverId, int partId)
      const {
    const auto index = serverId * PARTS_PER_SERVER + partId;
    return objects_[index];
  }

  /**
   * Get the physical GPU overlap partition ID
   */
  [[nodiscard]] entities::PartitionId getPhysicalGpuOverlapPartitionID() const {
    return partitionId(physicalGpuOverlapPartitionName_);
  }

  /**
   * Get group ID for a specific server/GPU combination
   */
  [[nodiscard]] entities::GroupId getServerGpuGroupID(int serverId, int gpuId)
      const {
    const auto partitionId = getPhysicalGpuOverlapPartitionID();
    return groupId(partitionId, fmt::format("S{}_GPU{}", serverId, gpuId));
  }

  /**
   * Factory method for creating tracker instances
   */
  [[nodiscard]] std::unique_ptr<OneObjectPerGroup> physicalGPUOverlapTracker()
      const {
    return std::make_unique<OneObjectPerGroup>(
        *universe_,
        getPhysicalGpuOverlapPartitionID(),
        unassigned_,
        *assignment_);
  }

  /**
   * Helper for testing move operations with attribute store updates
   */
  void performMove(
      entities::ObjectId objectId,
      entities::ContainerId fromContainer,
      entities::ContainerId toContainer,
      EntityAttributesStore& attributeStore) {
    const auto move = Move(objectId, fromContainer, toContainer);
    assignment_->moveTo(objectId, toContainer);
    attributeStore.updateOnApply(move.getChangeSet());
  }

 protected:
  // Core test data
  std::shared_ptr<const entities::Universe> universe_;
  std::vector<entities::ObjectId> objects_;
  std::unique_ptr<Assignment> assignment_;

  // Container identifiers
  entities::ContainerId unassigned_ = entities::ContainerId(0);
  entities::ContainerId container0_ = entities::ContainerId(1);
  entities::ContainerId container1_ = entities::ContainerId(2);

  // GPU topology configuration
  std::string physicalGpuOverlapPartitionName_{
      "physical_gpu_overlap_partition"};

 private:
  /**
   * Create a single physical GPU overlap partition with overlapping groups:
   * - Each server gets multiple groups representing different GPU memberships
   * - GPU0 groups: Contain P0 and P1 from each server
   * - GPU1 groups: Contain P0 and P2 from each server
   * - Key insight: P0 appears in multiple groups (overlapping)
   */
  folly::coro::Task<void> createPhysicalGpuPartitions() {
    entities::Map<std::string, std::vector<std::string>> overlapGroups;

    // Create groups for each server/GPU combination
    for (const auto serverId : folly::irange(NUM_SERVERS)) {
      // GPU0 group for this server (contains P0, P1)
      auto gpu0GroupName = fmt::format("S{}_GPU0", serverId);
      overlapGroups[gpu0GroupName] = {
          fmt::format("S{}_P0", serverId), fmt::format("S{}_P1", serverId)};

      // GPU1 group for this server (contains P0, P2)
      auto gpu1GroupName = fmt::format("S{}_GPU1", serverId);
      overlapGroups[gpu1GroupName] = {
          fmt::format("S{}_P0", serverId), fmt::format("S{}_P2", serverId)};
    }

    co_await addPartition(physicalGpuOverlapPartitionName_, overlapGroups);
  }

  /**
   * Initialize assignment with all objects in unassigned container
   */
  void initializeAssignment() {
    const PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>
        containerAssignments{
            {unassigned_, objects_}, {container0_, {}}, {container1_, {}}};

    assignment_ = std::make_unique<Assignment>(
        containerAssignments, nullptr, PackerSet<entities::ObjectId>{});
  }
};

/**
 * Test tracker initialization and basic state validation
 */
class TrackerInitializationTest : public GpuTopologyTestFixture {};

TEST_F(TrackerInitializationTest, InitializesWithCorrectCounts) {
  const auto tracker = physicalGPUOverlapTracker();

  // Verify assigned counts per GPU/server combination
  for (const auto gpuId : folly::irange(NUM_GPUS)) {
    for (const auto serverId : folly::irange(NUM_SERVERS)) {
      const auto serverGpuGroup = getServerGpuGroupID(serverId, gpuId);

      // Initially all parts are unassigned, so assigned count should be 0
      EXPECT_EQ(0, tracker->getAssignedCount(serverGpuGroup));
    }
  }
}

TEST_F(TrackerInitializationTest, NoOverlapDetectedWhenAllUnassigned) {
  const auto tracker = physicalGPUOverlapTracker();

  // When all parts are unassigned, no overlaps should be detected
  // All server parts should be able to be allocated
  for (const auto serverId : folly::irange(NUM_SERVERS)) {
    for (const auto partId : folly::irange(PARTS_PER_SERVER)) {
      const auto serverPart = getServerPartID(serverId, partId);
      EXPECT_TRUE(tracker->canPick(serverPart));
    }
  }
}

TEST_F(TrackerInitializationTest, InitializesWithPreAssignedObjects) {
  // Create a custom assignment where some objects start in container0_
  auto s0_p0 = getServerPartID(0, 0);
  auto s0_p1 = getServerPartID(0, 1);
  auto s0_p2 = getServerPartID(0, 2);
  auto s0_p3 = getServerPartID(0, 3);

  // Create objects list excluding the pre-assigned ones
  std::vector<entities::ObjectId> unassignedObjects;
  for (const auto& obj : objects_) {
    if (obj != s0_p0 && obj != s0_p1) {
      unassignedObjects.push_back(obj);
    }
  }

  // Create custom assignment with S0_P0 and S0_P1 pre-assigned to container0_
  const PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>
      customAssignments{
          {unassigned_, unassignedObjects},
          {container0_, {s0_p0, s0_p1}},
          {container1_, {}}};

  auto customAssignment = std::make_unique<Assignment>(
      customAssignments, nullptr, PackerSet<entities::ObjectId>{});

  // Create tracker with custom assignment
  auto tracker = std::make_unique<OneObjectPerGroup>(
      *universe_,
      getPhysicalGpuOverlapPartitionID(),
      unassigned_,
      *customAssignment);

  // Verify that the assigned count for server0Gpu0Group is 2
  // (both S0_P0 and S0_P1 are pre-assigned and share GPU0)
  auto server0Gpu0Group = getServerGpuGroupID(0, 0);
  EXPECT_EQ(2, tracker->getAssignedCount(server0Gpu0Group));

  // Verify that S0_P2 cannot be picked (shares GPU1 with pre-assigned S0_P0)
  EXPECT_FALSE(tracker->canPick(s0_p2))
      << "S0_P2 should not be pickable (shares GPU1 with pre-assigned S0_P0)";

  // Verify that S0_P3 can be picked (doesn't share any GPU with pre-assigned
  // parts)
  EXPECT_TRUE(tracker->canPick(s0_p3))
      << "S0_P3 should be pickable (shares no GPU with pre-assigned parts)";
}

/**
 * Test core overlap detection functionality
 */
class OverlapDetectionTest : public GpuTopologyTestFixture {};

TEST_F(OverlapDetectionTest, DetectsOverlapAfterPartialAllocation) {
  EntityAttributesStore attributeStore;
  const auto tracker =
      std::shared_ptr<OneObjectPerGroup>(physicalGPUOverlapTracker());
  attributeStore.registerAttributes("gpu_tracker", tracker);

  // Move S0_P0 from unassigned to container0
  // S0_P0 is critical because it appears in BOTH GPU0 and GPU1
  const auto s0_p0 = getServerPartID(0, 0);
  performMove(s0_p0, unassigned_, container0_, attributeStore);

  // Test overlap detection for parts sharing GPUs with S0_P0
  EXPECT_FALSE(tracker->canPick(getServerPartID(0, 1)))
      << "S0_P1 should detect overlap (shares GPU0 with assigned S0_P0)";
  EXPECT_FALSE(tracker->canPick(getServerPartID(0, 2)))
      << "S0_P2 should detect overlap (shares GPU1 with assigned S0_P0)";

  // S0_P3 doesn't share any GPU with S0_P0, so no overlap
  EXPECT_TRUE(tracker->canPick(getServerPartID(0, 3)))
      << "S0_P3 should not detect overlap (shares no GPU with S0_P0)";

  // Verify assigned counts increased in both GPU groups
  const auto server0Gpu0Group = getServerGpuGroupID(0, 0);
  const auto server0Gpu1Group = getServerGpuGroupID(0, 1);

  // S0_P0 was assigned from both GPU groups, so assigned count should be 1 for
  // each
  EXPECT_EQ(1, tracker->getAssignedCount(server0Gpu0Group));
  EXPECT_EQ(1, tracker->getAssignedCount(server0Gpu1Group));
}

TEST_F(OverlapDetectionTest, HandlesMultipleAllocationsInSameGpu) {
  EntityAttributesStore attributeStore;
  const auto tracker =
      std::shared_ptr<OneObjectPerGroup>(physicalGPUOverlapTracker());
  attributeStore.registerAttributes("gpu_tracker", tracker);

  // Allocate both parts from GPU0 (S0_P0 and S0_P1)
  const auto s0_p0 = getServerPartID(0, 0);
  const auto s0_p1 = getServerPartID(0, 1);

  performMove(s0_p0, unassigned_, container0_, attributeStore);
  performMove(s0_p1, unassigned_, container1_, attributeStore);

  // GPU0 should now have both parts assigned for server S0
  const auto server0Gpu0Group = getServerGpuGroupID(0, 0);

  // Both parts from GPU0 are assigned, so assigned count should be 2
  EXPECT_EQ(2, tracker->getAssignedCount(server0Gpu0Group));
}

/**
 * Test server independence and isolation
 */
class ServerIndependenceTest : public GpuTopologyTestFixture {};

TEST_F(ServerIndependenceTest, ServersTrackIndependently) {
  const auto tracker = physicalGPUOverlapTracker();

  // Cross-server parts should not interfere with overlap detection
  EXPECT_TRUE(tracker->canPick(getServerPartID(0, 0)));
  EXPECT_TRUE(tracker->canPick(getServerPartID(1, 0)));

  const auto server0Gpu0Group = getServerGpuGroupID(0, 0);
  const auto server1Gpu0Group = getServerGpuGroupID(1, 0);

  // Both servers should have no assigned parts initially
  EXPECT_EQ(0, tracker->getAssignedCount(server0Gpu0Group));
  EXPECT_EQ(0, tracker->getAssignedCount(server1Gpu0Group));
}

TEST_F(ServerIndependenceTest, ServerAllocationsDoNotInterfere) {
  EntityAttributesStore attributeStore;
  const auto tracker =
      std::shared_ptr<OneObjectPerGroup>(physicalGPUOverlapTracker());
  attributeStore.registerAttributes("gpu_tracker", tracker);

  // Allocate S0_P0 but not S1_P0
  const auto s0_p0 = getServerPartID(0, 0);
  performMove(s0_p0, unassigned_, container0_, attributeStore);

  // S0_P1 should detect overlap (same server, shares GPU with S0_P0)
  EXPECT_FALSE(tracker->canPick(getServerPartID(0, 1)));

  // S1_P1 should NOT detect overlap (different server)
  EXPECT_TRUE(tracker->canPick(getServerPartID(1, 1)));
}

/**
 * Test edge cases and error conditions
 */
class EdgeCaseTest : public GpuTopologyTestFixture {};

TEST_F(EdgeCaseTest, HandlesNonExistentServerGpuCombinations) {
  const auto tracker = physicalGPUOverlapTracker();

  // Create fake group ID that doesn't exist in the topology
  const auto fakeGroupId = entities::GroupId(9999);

  // Should return 0 for non-existent groups
  EXPECT_EQ(0, tracker->getAssignedCount(fakeGroupId));
}

/**
 * This case cannot happen as this invariant is overall preserved by LocalSearch
 */
TEST_F(EdgeCaseTest, DISABLED_MultipleMovesAreIdempotent) {
  EntityAttributesStore attributeStore;
  const auto tracker =
      std::shared_ptr<OneObjectPerGroup>(physicalGPUOverlapTracker());
  attributeStore.registerAttributes("gpu_tracker", tracker);

  // Allocate S0_P0 from GPU0
  const auto s0_p0 = getServerPartID(0, 0);

  performMove(s0_p0, unassigned_, container0_, attributeStore);

  // GPU0 should now have 1 assigned part for server S0
  const auto server0Gpu0Group = getServerGpuGroupID(0, 0);

  EXPECT_EQ(1, tracker->getAssignedCount(server0Gpu0Group));

  // Repeat same move multiple times
  performMove(s0_p0, unassigned_, container0_, attributeStore);
  performMove(s0_p0, unassigned_, container0_, attributeStore);

  // Tracker should still have 1 assigned part
  EXPECT_EQ(1, tracker->getAssignedCount(server0Gpu0Group));
}
} // namespace facebook::rebalancer::packer::tests
