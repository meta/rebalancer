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
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/GroupOccupancyIndex.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

namespace {

// container -> occupancy -> objects
using Snapshot =
    std::map<std::string, std::map<int32_t, std::set<std::string>>>;

constexpr int32_t kUngrouped = GroupOccupancyIndex::kUngrouped;

const std::vector<std::string> kObjects =
    {"a0", "a1", "a2", "a3", "b0", "b1", "b2", "b3", "n0"};
const std::vector<std::string> kContainers = {
    "unassigned",
    "container1",
    "container2",
    "container3"};

} // namespace

// Group a has a0..a3 and group b has b0..b3; n0 is in no group. Only container1
// and container2, the containers of scopeItem, count. All objects are
// equivalent and on one server, so the objects of a container differ only by
// occupancy.
class GroupOccupancyIndexTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {
 protected:
  void SetUp() override {
    setInitialAssignment({
        {"unassigned", {"a2", "a3", "b0", "b1", "b2", "b3", "n0"}},
        {"container1", {"a0", "a1"}},
        {"container2", {}},
        {"container3", {}},
    });
    folly::coro::blockingWait(addPartition(
        "groups",
        {{"a", {"a0", "a1", "a2", "a3"}}, {"b", {"b0", "b1", "b2", "b3"}}}));
    folly::coro::blockingWait(addPartition("servers", {{"server", kObjects}}));
    folly::coro::blockingWait(addScope(
        "scope",
        {{"scopeItem", {"container1", "container2"}},
         {"otherScopeItem", {"container3"}}}));
    buildUniverse();
  }

  Assignment makeAssignment(
      const std::vector<std::string>& fixedObjects = {}) const {
    PackerSet<entities::ObjectId> fixed;
    for (const auto& name : fixedObjects) {
      fixed.insert(objectId(name));
    }
    return Assignment(
        getUniverse().getContainers().getInitialAssignment(),
        /*objectScores=*/nullptr,
        std::move(fixed));
  }

  GroupOccupancyIndex makeIndex(const Assignment& assignment) const {
    EquivalenceSets equivalenceSets(getUniverse());
    equivalenceSets.finalize();
    return GroupOccupancyIndex(
        getUniverse(),
        partitionId("groups"),
        partitionId("servers"),
        scopeId("scope"),
        scopeItemId("scope", "scopeItem"),
        equivalenceSets,
        assignment);
  }

  void move(
      GroupOccupancyIndex& index,
      const std::string& object,
      const std::string& from,
      const std::string& to) const {
    index.updateOnApply(
        Move(objectId(object), containerId(from), containerId(to))
            .getChangeSet());
  }

  // Records a key even if it holds no objects, so a stale key fails the
  // comparison.
  Snapshot snapshot(const GroupOccupancyIndex& index) const {
    Snapshot snapshot;
    for (const auto& container : kContainers) {
      for (const auto& [key, objectsByServer] :
           index.getContainerObjects(containerId(container))) {
        auto& names = snapshot[container][key.occupancy];
        for (const auto& [_, objects] : objectsByServer) {
          for (const auto object : objects) {
            names.insert(getUniverse().getEntityName(object));
          }
        }
      }
    }
    return snapshot;
  }
};

TEST_F(GroupOccupancyIndexTest, IndexesObjectsByOccupancy) {
  const Snapshot expected = {
      {"unassigned",
       {{kUngrouped, {"n0"}},
        {0, {"b0", "b1", "b2", "b3"}},
        {2, {"a2", "a3"}}}},
      {"container1", {{2, {"a0", "a1"}}}}};
  EXPECT_EQ(snapshot(makeIndex(makeAssignment())), expected);
}

// a0 is fixed, so container1 indexes only a1, but a0 still counts.
TEST_F(GroupOccupancyIndexTest, FixedObjectsAreNotIndexedButStillCount) {
  auto index = makeIndex(makeAssignment(/*fixedObjects=*/{"a0"}));
  move(index, "a2", "unassigned", "container2");
  const Snapshot expected = {
      {"unassigned",
       {{kUngrouped, {"n0"}}, {0, {"b0", "b1", "b2", "b3"}}, {3, {"a3"}}}},
      {"container1", {{3, {"a1"}}}},
      {"container2", {{3, {"a2"}}}}};
  EXPECT_EQ(snapshot(index), expected);
}

// a0 leaves the scope item and a2 enters it, with the removals first and then
// with the additions first.
TEST_F(GroupOccupancyIndexTest, ChangesApplyInEitherOrder) {
  const std::vector<Change> changes = {
      Change(objectId("a0"), containerId("container1"), -1),
      Change(objectId("a2"), containerId("unassigned"), -1),
      Change(objectId("a0"), containerId("unassigned"), 1),
      Change(objectId("a2"), containerId("container2"), 1)};
  auto removalsFirst = makeIndex(makeAssignment());
  removalsFirst.updateOnApply(ChangeSet(changes));
  auto additionsFirst = makeIndex(makeAssignment());
  additionsFirst.updateOnApply(
      ChangeSet(std::vector<Change>(changes.rbegin(), changes.rend())));

  const Snapshot expected = {
      {"unassigned",
       {{kUngrouped, {"n0"}},
        {0, {"b0", "b1", "b2", "b3"}},
        {2, {"a0", "a3"}}}},
      {"container1", {{2, {"a1"}}}},
      {"container2", {{2, {"a2"}}}}};
  EXPECT_EQ(snapshot(removalsFirst), expected);
  EXPECT_EQ(snapshot(additionsFirst), expected);
}

// After each of 200 random steps, the index matches one built from the
// resulting assignment.
TEST_F(GroupOccupancyIndexTest, MovesMatchAnIndexBuiltFromScratch) {
  auto assignment = makeAssignment();
  auto index = makeIndex(assignment);
  std::mt19937 rng(42);
  for ([[maybe_unused]] const auto step : folly::irange(200)) {
    const auto object = objectId(kObjects.at(rng() % kObjects.size()));
    const auto to = containerId(kContainers.at(rng() % kContainers.size()));
    const auto from = assignment.getContainer(object);
    if (from == to) {
      continue;
    }
    assignment.moveTo(object, to);
    index.updateOnApply(Move(object, from, to).getChangeSet());
    EXPECT_EQ(snapshot(index), snapshot(makeIndex(assignment)));
  }
}

} // namespace facebook::rebalancer::packer::tests
