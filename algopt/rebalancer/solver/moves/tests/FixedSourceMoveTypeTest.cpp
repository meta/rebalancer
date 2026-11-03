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

#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/FixedSourceMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

namespace facebook::rebalancer::packer::tests {

class FixedSourceMoveTypeTest : public MoveTestBase {
 protected:
  FixedSourceMoveTypeTest() : MoveTestBase("object", "container") {}

  struct PartitionSpec {
    std::string name;
    int numGroups;
  };

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse(
      int numObjects,
      int initialObjectsInContainer1 = 0,
      const std::optional<PartitionSpec>& partitionSpec = std::nullopt) {
    EXPECT_LE(initialObjectsInContainer1, numObjects);
    entities::Map<std::string, std::vector<std::string>> assignment;
    std::vector<std::string> objectsContainer1;
    std::vector<std::string> objectsContainer2;

    for (const auto i : folly::irange(numObjects)) {
      const auto objectName = fmt::format("object{}", i + 1);
      if (i < initialObjectsInContainer1) {
        objectsContainer1.push_back(objectName);
      } else {
        objectsContainer2.push_back(objectName);
      }
    }
    assignment["container1"] = std::move(objectsContainer1);
    assignment["container2"] = std::move(objectsContainer2);

    setInitialAssignment(assignment);

    co_await addScope(
        "priority",
        {{"priority0", {"container1"}}, {"priority1", {"container2"}}});

    // Add partition if specified
    if (partitionSpec.has_value()) {
      const auto& [partitionName, numGroups] = *partitionSpec;
      const int objectsPerGroup = numObjects / numGroups;
      entities::Map<std::string, std::vector<std::string>> groups;
      for (const auto g : folly::irange(numGroups)) {
        const auto groupName = fmt::format("g{}", g + 1);
        std::vector<std::string> groupObjects;
        for (const auto i : folly::irange(objectsPerGroup)) {
          groupObjects.push_back(
              fmt::format("object{}", g * objectsPerGroup + i + 1));
        }
        groups[groupName] = groupObjects;
      }
      co_await addPartition(partitionName, groups);
    }

    co_return buildUniverse();
  }

  folly::coro::Task<void> setUpSamplingScenario() {
    // Build assignment: object0 thru object99 are in container0
    entities::Map<std::string, std::vector<std::string>> assignment;
    assignment["container0"] = {};
    assignment["container1"] = {};
    for (const auto i : folly::irange(100)) {
      assignment["container0"].push_back(fmt::format("object{}", i));
    }
    setInitialAssignment(assignment);

    const auto universe = buildUniverse();
    const Assignment initialAssignment(
        universe->getContainers().getInitialAssignment());

    // initial objective = 1 + 2 ... 100
    // best move is object99 to container1, will reduce objective by 100
    ExprPtr objective = const_expr(0, *universe);
    for (const auto i : folly::irange(100)) {
      objective = objective +
          variable(object(i), container(0), *universe, initialAssignment) *
              (i + 1);
    }

    const ExprPtr dummyConstraint = const_expr(0, *universe);
    createProblem({objective}, dummyConstraint);
    co_return;
  }

  static void addObjectBundleFormationHints(
      const std::string& containerName,
      const std::string& partitionName,
      int bundleSize,
      interface::SingleFixedSourceMoveTypeSpec& spec) {
    interface::ObjectBundleFormationHints hints;
    folly::F14FastMap<std::string, interface::ObjectsToExploreOptions>
        scopeItemToObjectsToExploreOptions;

    interface::GroupList groupList;
    groupList.partitionName() = partitionName;

    interface::ObjectsFromGroupsSpec objectsFromGroupsSpec;
    objectsFromGroupsSpec.groupList() = std::move(groupList);
    objectsFromGroupsSpec.bundleSize() = bundleSize;
    interface::ObjectsToExploreOptions objectsToExploreOptions;
    objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

    scopeItemToObjectsToExploreOptions[containerName] = objectsToExploreOptions;
    hints.scopeName() = "container";
    hints.scopeItemToObjectsToExploreOptions() =
        scopeItemToObjectsToExploreOptions;
    hints.adjustBundleSizeForIncompleteBundles() = true;
    spec.objectBundleFormationHints() = hints;
  }
};

CO_TEST_F(FixedSourceMoveTypeTest, MissingMoveSpec) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/4);
  createProblem(/*objectiveTuple=*/{const_expr(0, *universe)},
                /*constraint=*/const_expr(0, *universe));

  auto singleFixedSourceMoveType =
      FixedSourceMoveType(interface::LocalSearchSolverSpec());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      singleFixedSourceMoveType.findBestMove(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          getMoveStatsAggregator(),
          getEmptySearchHints(),
          std::numeric_limits<double>::max() /*timelimit*/),
      "FixedSourceMoveType needs a special container or a list of scope items to perform moves from");
}

CO_TEST_F(FixedSourceMoveTypeTest, MissingScopeItemsInMoveSpec) {
  const auto universe = co_await setUpUniverse(/*numObjects=*/4);

  createProblem(/*objectiveTuple=*/{const_expr(0, *universe)},
                /*constraint=*/const_expr(0, *universe));

  auto singleFixedSourceMoveTypeSpec =
      interface::SingleFixedSourceMoveTypeSpec();
  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeItems() = {};
  singleFixedSourceMoveTypeSpec.scopeItemList() = scopeItemList;
  auto singleFixedSourceMoveType = FixedSourceMoveType(
      interface::LocalSearchSolverSpec(), singleFixedSourceMoveTypeSpec);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      singleFixedSourceMoveType.findBestMove(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          getMoveStatsAggregator(),
          getEmptySearchHints(),
          std::numeric_limits<double>::max() /*timelimit*/),
      "FixedSourceMoveType needs list of scope items to perform moves from");
}

CO_TEST_F(FixedSourceMoveTypeTest, VerifyMoveSetWithSpecialContainer) {
  auto singleFixedSourceMoveTypeSpec =
      interface::SingleFixedSourceMoveTypeSpec();
  singleFixedSourceMoveTypeSpec.specialContainer() = "container2";

  auto singleFixedSourceMoveType = FixedSourceMoveType(
      interface::LocalSearchSolverSpec(), singleFixedSourceMoveTypeSpec);

  const auto universe = co_await setUpUniverse(/*numObjects=*/4);

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  buildEquivalenceIndex();
  auto bestResult = singleFixedSourceMoveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(1, getTotalMovesEvaluated());

  EXPECT_EQ(1, bestResult.getMoveSet().size());
  const auto& move = bestResult.getMoveSet().at(0);
  EXPECT_EQ(container(2), move.getSourceContainer());
  EXPECT_EQ(container(1), move.getDestinationContainer());
}

CO_TEST_F(FixedSourceMoveTypeTest, VerifyMoveSetWithScopeItems) {
  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "priority";
  scopeItemList.scopeItems() = {"priority1", "priority0"};

  auto singleFixedSourceMoveTypeSpec =
      interface::SingleFixedSourceMoveTypeSpec();
  singleFixedSourceMoveTypeSpec.scopeItemList() = scopeItemList;
  singleFixedSourceMoveTypeSpec.stopEarlyAtScopeItemThatImprovesObjective() =
      true;

  auto singleFixedSourceMoveType = FixedSourceMoveType(
      interface::LocalSearchSolverSpec(), singleFixedSourceMoveTypeSpec);

  const auto universe = co_await setUpUniverse(/*numObjects=*/4);

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  buildEquivalenceIndex();
  auto bestResult = singleFixedSourceMoveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(1, getTotalMovesEvaluated());

  EXPECT_EQ(1, bestResult.getMoveSet().size());
  const auto& move = bestResult.getMoveSet().at(0);
  EXPECT_EQ(container(2), move.getSourceContainer());
  EXPECT_EQ(container(1), move.getDestinationContainer());
}

CO_TEST_F(FixedSourceMoveTypeTest, VerifyMoveSetWithScopeItemsAndEquivSets) {
  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "priority";
  scopeItemList.scopeItems() = {"priority1", "priority0"};

  auto singleFixedSourceMoveTypeSpec =
      interface::SingleFixedSourceMoveTypeSpec();
  singleFixedSourceMoveTypeSpec.scopeItemList() = scopeItemList;
  singleFixedSourceMoveTypeSpec.stopEarlyAtScopeItemThatImprovesObjective() =
      true;

  auto singleFixedSourceMoveType = FixedSourceMoveType(
      interface::LocalSearchSolverSpec(), singleFixedSourceMoveTypeSpec);

  const auto universe = co_await setUpUniverse(/*numObjects=*/4);

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.mappingMerge(
      PackerMap<entities::ObjectId, int>({
          {object(1), 1},
          {object(2), 1},
          {object(3), 2},
          {object(4), 1},
      }));
  setEquivalenceSets(equivalenceSets);

  buildEquivalenceIndex();
  auto bestResult = singleFixedSourceMoveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(2, getTotalMovesEvaluated());

  EXPECT_EQ(1, bestResult.getMoveSet().size());
  const auto& move = bestResult.getMoveSet().at(0);
  EXPECT_EQ(container(2), move.getSourceContainer());
  EXPECT_EQ(container(1), move.getDestinationContainer());
}

TEST_F(FixedSourceMoveTypeTest, Name) {
  auto singleFixedSourceMoveType =
      FixedSourceMoveType(interface::LocalSearchSolverSpec());
  EXPECT_EQ(singleFixedSourceMoveType.name(), "SINGLE_FIXED_SOURCE");
}

TEST_F(FixedSourceMoveTypeTest, MakeSampled) {
  auto fixedSrcSpec = interface::SingleFixedSourceMoveTypeSpec();
  FixedSourceMoveType::makeSampled(fixedSrcSpec, 1);
  EXPECT_EQ(*fixedSrcSpec.sampleSize()->defaultSampleSize(), 1);
  EXPECT_FALSE(
      apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
          fixedSrcSpec.sampleSize()->objectToSampleSize()));
}

CO_TEST_F(FixedSourceMoveTypeTest, NotSampled) {
  co_await setUpSamplingScenario();

  // Not sampled, expect 100 moves
  auto fixedSrcSpec = interface::SingleFixedSourceMoveTypeSpec();
  fixedSrcSpec.specialContainer() = "container0";
  auto moveType =
      FixedSourceMoveType(interface::LocalSearchSolverSpec(), fixedSrcSpec);

  buildEquivalenceIndex();
  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(1),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(99), container(0), container(1)}}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());

  EXPECT_EQ(100, getTotalMovesEvaluated());
}

CO_TEST_F(FixedSourceMoveTypeTest, Sampled) {
  co_await setUpSamplingScenario();

  // Sampled, expect 50 moves
  constexpr int sampleSize = 50;
  auto fixedSrcSpec = interface::SingleFixedSourceMoveTypeSpec();
  fixedSrcSpec.specialContainer() = "container0";
  FixedSourceMoveType::makeSampled(fixedSrcSpec, sampleSize);
  auto moveType =
      FixedSourceMoveType(interface::LocalSearchSolverSpec(), fixedSrcSpec);

  buildEquivalenceIndex();
  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(1),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_NEAR(
      sampleSize,
      getTotalMovesEvaluated(),
      sampleSize * 0.1); // 10% error allowed
}

CO_TEST_F(FixedSourceMoveTypeTest, VerifyBundleMoveSetWithScopeItems) {
  interface::SingleFixedSourceMoveTypeSpec spec;

  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "priority";
  scopeItemList.scopeItems() = {"priority1", "priority0"};

  spec.scopeItemList() = scopeItemList;
  spec.stopEarlyAtScopeItemThatImprovesObjective() = true;

  constexpr int bundleSize = 2;
  addObjectBundleFormationHints("container1", "partition", bundleSize, spec);

  FixedSourceMoveType moveType(interface::LocalSearchSolverSpec(), spec);

  constexpr int numGroups = 4;
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/40,
      /*initialObjectsInContainer1=*/0,
      PartitionSpec{.name = "partition", .numGroups = numGroups});

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(numGroups * bundleSize, getTotalMovesEvaluated());
}

CO_TEST_F(FixedSourceMoveTypeTest, VerifyBundleMoveSetWithSpecialContainer) {
  interface::SingleFixedSourceMoveTypeSpec spec;
  spec.specialContainer() = "container2";

  constexpr int bundleSize = 2;
  addObjectBundleFormationHints("container1", "partition", bundleSize, spec);
  FixedSourceMoveType moveType(interface::LocalSearchSolverSpec(), spec);

  constexpr int numGroups = 4;
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/40,
      /*initialObjectsInContainer1=*/0,
      PartitionSpec{.name = "partition", .numGroups = numGroups});

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(numGroups * bundleSize, getTotalMovesEvaluated());
}

CO_TEST_F(FixedSourceMoveTypeTest, VerifyBundleMoveSetWithBundleSizeOverride) {
  interface::SingleFixedSourceMoveTypeSpec spec;
  spec.specialContainer() = "container2";

  constexpr int bundleSize = 2;
  addObjectBundleFormationHints("container1", "partition", bundleSize, spec);
  FixedSourceMoveType moveType(interface::LocalSearchSolverSpec(), spec);

  constexpr int numGroups = 4;
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/40,
      /*initialObjectsInContainer1=*/1,
      PartitionSpec{.name = "partition", .numGroups = numGroups});

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(7, getTotalMovesEvaluated());

  // Verify move structure - any object from container2 to container1 is valid
  EXPECT_EQ(1, bestResult.getMoveSet().size());
  const auto& move = bestResult.getMoveSet().at(0);
  EXPECT_EQ(container(2), move.getSourceContainer());
  EXPECT_EQ(container(1), move.getDestinationContainer());
}

CO_TEST_F(FixedSourceMoveTypeTest, NotSampledBundleMoves) {
  interface::SingleFixedSourceMoveTypeSpec spec;
  spec.specialContainer() = "container2";

  constexpr int bundleSize = 2;
  addObjectBundleFormationHints("container1", "partition", bundleSize, spec);
  FixedSourceMoveType moveType(interface::LocalSearchSolverSpec(), spec);

  constexpr int numGroups = 20;
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/100,
      /*initialObjectsInContainer1=*/0,
      PartitionSpec{.name = "partition", .numGroups = numGroups});

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(1) /*hotContainer*/,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  EXPECT_EQ(numGroups * bundleSize, getTotalMovesEvaluated());
}

CO_TEST_F(FixedSourceMoveTypeTest, SampledBundleMoves) {
  constexpr int bundleSize = 2;
  constexpr int numBundles = 20;
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/100,
      /*initialObjectsInContainer1=*/0,
      PartitionSpec{.name = "partition", .numGroups = numBundles});

  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  const auto countMovesEvaluated = [&](const int sampleSize) {
    interface::SingleFixedSourceMoveTypeSpec spec;
    spec.specialContainer() = "container2";
    addObjectBundleFormationHints("container1", "partition", bundleSize, spec);
    FixedSourceMoveType::makeSampled(spec, sampleSize);
    FixedSourceMoveType moveType(interface::LocalSearchSolverSpec(), spec);
    moveType.findBestMove(
        getMovesEvaluator(),
        container(1) /*hotContainer*/,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);
    return getTotalMovesEvaluated();
  };

  // The sample size counts bundles, so a sample size equal to the number of
  // bundles keeps every bundle, and a sample size of 0 keeps none.
  EXPECT_EQ(numBundles * bundleSize, countMovesEvaluated(numBundles));
  EXPECT_EQ(0, countMovesEvaluated(0));
}

} // namespace facebook::rebalancer::packer::tests
