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

#include "algopt/rebalancer/entities/RoutingConfig.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/moves/GroupRoutingMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

using SortedDestinationLatencies = algopt::ValueSortedMap<
    entities::ScopeItemId,
    double,
    entities::CompareScopeItemLatencyPair>;

using LatencyTable =
    entities::Map<entities::ScopeItemId, SortedDestinationLatencies>;

using GroupToRoutingRings =
    entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>;

class MockGroupRoutingMoveType : public GroupRoutingMoveType {
 public:
  explicit MockGroupRoutingMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::GroupRoutingMoveTypeSpec& groupRoutingMoveType)
      : GroupRoutingMoveType(solverConfigs, groupRoutingMoveType) {}

  MoveSet generateMoveSetFor(
      entities::GroupId groupId,
      const std::string& partitionName,
      std::optional<entities::ContainerId> unassignedContainerId,
      const Problem& problem,
      const MovesEvaluator& evaluator) {
    MoveStatsAggregator stats(problem.getUniverse().getPrecision());
    return this->GroupRoutingMoveType::generateMoveSetFor(
        groupId,
        partitionName,
        unassignedContainerId,
        problem,
        evaluator,
        stats);
  }
};

class GroupRoutingMoveTypeTest : public MoveTestBase {
 protected:
  GroupRoutingMoveTypeTest() : MoveTestBase("replica", "region") {}

  entities::GroupId group(int id) const {
    return groupId(partitionId("partition"), fmt::format("tenant{}", id));
  }

  entities::ScopeItemId region(int id) const {
    return scopeItemId(scopeId("geo_region"), fmt::format("region{}", id));
  }

  GroupToRoutingRings getGroupToRoutingRings(
      int groupCount,
      const std::vector<int>& sourceRegions) {
    auto groupToRoutingRings = GroupToRoutingRings();

    auto getRoutingRing = [this](const auto& sourceRegions) {
      std::vector<entities::RoutingRing> groupRoutingRings;
      const std::optional<std::vector<std::vector<entities::ScopeItemId>>>
          destinationScopeItemSets;
      for (const auto& r : sourceRegions) {
        const entities::RoutingRing ring(
            region(r), 1.0, destinationScopeItemSets);
        // not populating 'originTraffic' and 'destinationScopeItemSets' as it
        // is unused by the moveType
        groupRoutingRings.push_back(ring);
      }

      return groupRoutingRings;
    };

    for (const auto i : folly::irange(groupCount)) {
      // each group has traffic from the same source regions (ones given in
      // 'sourceRegions')
      groupToRoutingRings.emplace(group(i), getRoutingRing(sourceRegions));
    }

    return groupToRoutingRings;
  }

  std::shared_ptr<LatencyTable> getLatencyTable() {
    // There are 2 source regions and each source has 3
    // destination regions; best destination for each source is the source
    // itself
    auto latencyTable = std::make_shared<LatencyTable>();
    latencyTable->emplace(
        region(0),
        SortedDestinationLatencies{
            {region(0), 1}, {region(1), 10}, {region(2), 20}});
    latencyTable->emplace(
        region(1),
        SortedDestinationLatencies{
            {region(1), 1}, {region(2), 10}, {region(0), 20}});

    return latencyTable;
  }

  void verifyMovesAreAsExpected(
      const std::string& testScenarioName,
      const MoveSet& actualMoveSet,
      const std::vector<
          std::pair<entities::ContainerId, entities::ContainerId>>&
          expectedSourceDest) {
    const auto& universe = getUniverse();
    // check for size match before conversion to sets
    EXPECT_EQ(expectedSourceDest.size(), actualMoveSet.size())
        << "Move set SIZE mismatch for " << testScenarioName;

    std::set<std::pair<std::string, std::string>> expectedSourceDestSet;
    for (const auto& [expectedSource, expectedDest] : expectedSourceDest) {
      expectedSourceDestSet.insert(
          std::make_pair(
              universe.getEntityName(expectedSource),
              universe.getEntityName(expectedDest)));
    }

    std::set<std::pair<std::string, std::string>> actualSourceDestSet;
    for (const auto& move : actualMoveSet) {
      actualSourceDestSet.insert(
          std::make_pair(
              universe.getEntityName(move.getSourceContainer()),
              universe.getEntityName(move.getDestinationContainer())));
    }

    EXPECT_EQ(expectedSourceDestSet, actualSourceDestSet)
        << "Move set mismatch for " << testScenarioName;
  }

  folly::coro::Task<void> setUpProblem(
      int objectCountPerTenant,
      int tenantCount,
      const std::vector<std::string>& nonAcceptingContainerNames = {}) {
    co_await setUpUniverseAndRouting(objectCountPerTenant, tenantCount);
    const auto universe = buildUniverse();
    PackerSet<entities::ContainerId> nonAccepting;
    for (const auto& name : nonAcceptingContainerNames) {
      nonAccepting.insert(container(name));
    }
    createProblem(
        /*objectiveTuple=*/{const_expr(0, *universe)},
        /*constraint=*/const_expr(0, *universe),
        /*higherPriorityObjConfig=*/std::nullopt,
        /*nonAcceptingContainers=*/nonAccepting);
  }

  static interface::GroupRoutingMoveTypeSpec getGroupRoutingMoveType() {
    interface::GroupRoutingMoveTypeSpec moveTypeSpec;
    moveTypeSpec.routingConfigName() = "routingConfig1";
    moveTypeSpec.unassignedContainer() = "unassigned";
    return moveTypeSpec;
  }

 protected:
  folly::coro::Task<void> setUpUniverseAndRouting(
      int objectCountPerTenant,
      int tenantCount) {
    entities::Map<std::string, std::vector<std::string>> assignment;
    entities::Map<std::string, std::vector<std::string>> groups;
    for (const auto i : folly::irange(tenantCount)) {
      const auto groupName = fmt::format("tenant{}", i);
      for (const auto j : folly::irange(objectCountPerTenant)) {
        const auto objectId = i * objectCountPerTenant + j;
        const auto objectName = fmt::format("replica{}", objectId);
        groups[groupName].push_back(objectName);

        // for tenant0, it has one replica in region0 and all other replicas are
        // unassigned;
        // for tenant1, it has one replica in region0, one in region1, one in
        // region2, and all other replicas are unassigned;
        // for tenant2, it has all replicas in region2
        if (i == 0 && j == 0) {
          assignment["region0"].push_back(objectName);
        } else if (i == 1 && j == 0) {
          assignment["region0"].push_back(objectName);
        } else if (i == 1 && j == 1) {
          assignment["region1"].push_back(objectName);
        } else if (i == 1 && j == 2) {
          assignment["region2"].push_back(objectName);
        } else if (i == 2) {
          assignment["region2"].push_back(objectName);
        } else {
          assignment["unassigned"].push_back(objectName);
        }
      }
    }

    setInitialAssignment(assignment);

    co_await addScope(
        "geo_region",
        {
            {"region0", {"region0"}},
            {"region1", {"region1"}},
            {"region2", {"region2"}},
        });

    co_await addPartition("partition", groups);

    auto groupRoutingRings = getGroupToRoutingRings(tenantCount, {0, 1});
    co_await addRoutingConfig(
        "routingConfig1",
        entities::RoutingConfigData{std::make_shared<entities::RoutingConfig>(
            std::move(groupRoutingRings),
            getLatencyTable(),
            scopeId("geo_region"),
            partitionId("partition"))});
  }
};

CO_TEST_F(GroupRoutingMoveTypeTest, Basic) {
  auto mockMoveType = MockGroupRoutingMoveType(
      interface::LocalSearchSolverSpec{}, getGroupRoutingMoveType());

  constexpr int objectCountPerTenant = 4;
  constexpr int tenantCount = 3;
  co_await setUpProblem(objectCountPerTenant, tenantCount);

  auto unassignedContainerId = container("unassigned");

  {
    // for tenant0, if objects are allowed to be dropped or otherwise, we only
    // expect only one move, where an object moves from unassignedContainer to
    // region-1
    auto moveSetTenant0WithDrop = mockMoveType.generateMoveSetFor(
        group(0),
        "partition",
        unassignedContainerId,
        getProblem(),
        getMovesEvaluator());
    verifyMovesAreAsExpected(
        "tenant0-WithDrop",
        moveSetTenant0WithDrop,
        {{unassignedContainerId, container(1)}});

    auto moveSetTenant0WithoutDrop = mockMoveType.generateMoveSetFor(
        group(0), "partition", std::nullopt, getProblem(), getMovesEvaluator());
    verifyMovesAreAsExpected(
        "tenant0-WithoutDrop",
        moveSetTenant0WithoutDrop,
        {{unassignedContainerId, container(1)}});
  }

  {
    // for tenant1, when objects are allowed to be dropped, we expect one move,
    // where an object is dropped from region-2; otherwise, we expect no moves
    auto moveSetTenant1WithDrop = mockMoveType.generateMoveSetFor(
        group(1),
        "partition",
        unassignedContainerId,
        getProblem(),
        getMovesEvaluator());
    verifyMovesAreAsExpected(
        "tenant1-WithDrop",
        moveSetTenant1WithDrop,
        {{container(2), unassignedContainerId}});

    auto moveSetTenant1WithoutDrop = mockMoveType.generateMoveSetFor(
        group(1), "partition", std::nullopt, getProblem(), getMovesEvaluator());
    verifyMovesAreAsExpected(
        "tenant1-WithoutDrop", moveSetTenant1WithoutDrop, {});
  }

  {
    // for tenant2, when objects are allowed to be dropped, we expect four
    // move, where one object moves from region-2 to region-0 and region-1,
    // respectively, and two objects are dropped from region-2; otherwise, we
    // expect only two moves
    auto moveSetTenant2WithDrop = mockMoveType.generateMoveSetFor(
        group(2),
        "partition",
        unassignedContainerId,
        getProblem(),
        getMovesEvaluator());
    verifyMovesAreAsExpected(
        "tenant2-WithDrop",
        moveSetTenant2WithDrop,
        {{container(2), container(0)},
         {container(2), container(1)},
         {container(2), unassignedContainerId},
         {container(2), unassignedContainerId}});

    auto moveSetTenant2WithoutDrop = mockMoveType.generateMoveSetFor(
        group(2), "partition", std::nullopt, getProblem(), getMovesEvaluator());
    verifyMovesAreAsExpected(
        "tenant2-WithoutDrop",
        moveSetTenant2WithoutDrop,
        {{container(2), container(0)}, {container(2), container(1)}});
  }
}

// When the best destination violates a hard constraint, the MoveSet should
// still be valid because constraint-aware destination selection routes around
// the blocked destination. Without it, the entire MoveSet is rejected.
CO_TEST_F(GroupRoutingMoveTypeTest, ConstraintBlockedDestination) {
  constexpr int objectCountPerTenant = 4;
  constexpr int tenantCount = 3;

  co_await setUpUniverseAndRouting(objectCountPerTenant, tenantCount);
  const auto universe = buildUniverse();

  // region0 currently has 2 objects. Constraint blocks adding more.
  auto countInRegion0 = makeObjectLookup(
      makeObjectVector(
          PackerMap<entities::ObjectId, double>{},
          1,
          tenantCount * objectCountPerTenant,
          *universe),
      {container("region0")});
  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/countInRegion0 - 2.0);

  // tenant2 has all 4 replicas in region2. Source0's min-latency destination
  // is region0, but moving there violates the constraint.
  auto mockMoveType = MockGroupRoutingMoveType(
      interface::LocalSearchSolverSpec{}, getGroupRoutingMoveType());
  auto moveSet = mockMoveType.generateMoveSetFor(
      group(2),
      "partition",
      container("unassigned"),
      getProblem(),
      getMovesEvaluator());
  auto result = getMovesEvaluator().evaluate(MoveSet(moveSet));

  // The evaluator probe skips region0, picks region1 → valid MoveSet.
  EXPECT_TRUE(result.isValid());
}

// When the best destination is in not_accepting_containers, the MoveSet should
// skip that destination and pick the next best one.
CO_TEST_F(GroupRoutingMoveTypeTest, NotAcceptingContainerSkipped) {
  constexpr int objectCountPerTenant = 4;
  constexpr int tenantCount = 3;

  // Mark region0 as not accepting new objects
  co_await setUpProblem(
      objectCountPerTenant,
      tenantCount,
      /*nonAcceptingContainerNames=*/{"region0"});

  auto unassignedContainerId = container("unassigned");
  auto region0ContainerId = container("region0");

  // tenant2 has all 4 replicas in region2. Source0's min-latency destination
  // is region0, but it's not accepting. Should skip to region1 instead.
  auto mockMoveType = MockGroupRoutingMoveType(
      interface::LocalSearchSolverSpec{}, getGroupRoutingMoveType());
  auto moveSet = mockMoveType.generateMoveSetFor(
      group(2),
      "partition",
      unassignedContainerId,
      getProblem(),
      getMovesEvaluator());

  // Verify the move set size is exactly 4
  EXPECT_EQ(moveSet.size(), 4);

  // Verify no moves have region0 as destination (since it's not accepting)
  for (const auto& move : moveSet) {
    EXPECT_NE(move.getDestinationContainer(), region0ContainerId)
        << "Move should not have region0 as destination since it's not accepting";
  }

  // Since region0 is not accepting, source0 should route to region1 (next
  // best). Source1's best destination is region1 (already covered). So we
  // expect: one move to region1, and three drops to unassigned.
  verifyMovesAreAsExpected(
      "tenant2-NotAcceptingRegion0",
      moveSet,
      {{container(2), container(1)},
       {container(2), unassignedContainerId},
       {container(2), unassignedContainerId},
       {container(2), unassignedContainerId}});
}

} // namespace facebook::rebalancer::packer::tests
