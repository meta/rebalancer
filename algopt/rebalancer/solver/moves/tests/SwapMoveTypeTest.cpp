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

#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/moves/SwapMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

namespace facebook::rebalancer::packer::tests {

// this inheritance hierarchy exposes some private methods of SwapMoveType that
// we want to test
class MockSwapMoveType : public SwapMoveType {
 public:
  explicit MockSwapMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SwapMoveTypeSpec& moveTypeSpec)
      : SwapMoveType(solverConfigs, moveTypeSpec) {}

  using MoveType::parallelExecutionSelector;

  MoveResult exploreSwappingHotObjectWithObjectsInColdContainer(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      entities::ContainerId coldContainer,
      MoveStatsAggregator& stats) {
    // CoreLocalSearchSolve builds the index when the stage starts
    auto& problem = evaluator.getProblem();
    problem.assignment.maybeBuildAndGetObjectsIndexedByEquivSets(
        problem.getEquivalenceSets());
    return SwapMoveType::exploreSwappingHotObjectWithObjectsInColdContainer(
        evaluator, hotContainer, hotObject, coldContainer, stats);
  }
};

TEST(MoveTypeExecutionSelectorTest, BuiltFromSolverSpec) {
  interface::BatchingExecutionConfig batchingConfig;
  batchingConfig.batchSize() = 128;
  batchingConfig.maxConcurrency() = 7;
  interface::ParallelExecutionConfig config;
  config.set_batching(std::move(batchingConfig));
  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.parallelExecutionConfig() = std::move(config);

  MockSwapMoveType move(solverSpec, interface::SwapMoveTypeSpec{});

  const auto& selector = move.parallelExecutionSelector();
  EXPECT_EQ(selector.strategy(), ParallelExecutionSelector::Strategy::Batching);
  EXPECT_EQ(selector.batchingOptions().batchSize, 128);
  EXPECT_EQ(selector.batchingOptions().maxConcurrency, 7);
}

class SwapMoveTypeTest : public MoveTestBaseWithTwoBinaryParams {
 protected:
  const std::string kPartitionName = "tenant";

  SwapMoveTypeTest() : MoveTestBaseT("replica", "region") {}

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse() {
    setInitialAssignment({
        {"region1", {"replica1", "replica2", "replica3", "replica4"}},
        {"region2", {}},
        {"region3", {"replica5", "replica6", "replica7"}},
        {"region4", {"replica8"}},
    });

    co_await addPartition(
        kPartitionName,
        {
            {"tenant1", {"replica1", "replica3", "replica5", "replica7"}},
            {"tenant2", {"replica2", "replica4", "replica6", "replica8"}},
        });

    co_return buildUniverse();
  }

  ExprPtr getLookupExprOn(const PackerSet<entities::ContainerId>& containers) {
    auto objectValues = PackerMap<entities::ObjectId, double>();
    for (const auto id : folly::irange(1, 9)) {
      objectValues.emplace(object(id), id);
    }
    auto objectVector = makeObjectVector(objectValues, getUniverse());
    const Assignment assignment(
        getUniverse().getContainers().getInitialAssignment());
    return std::make_shared<ObjectLookup>(
        objectVector,
        std::make_shared<PackerSet<entities::ContainerId>>(containers),
        getUniverse(),
        assignment);
  }

  static interface::SwapMoveTypeSpec getMoveTypeSpec() {
    auto [greedyOnSrc, greedyOnDst] = GetParam();
    interface::SwapMoveTypeSpec swapSpec;
    SwapMoveType::makeGreedy(swapSpec, greedyOnSrc, greedyOnDst);
    return swapSpec;
  }
};

INSTANTIATE_TEST_CASE_P(
    VanillaSwap,
    SwapMoveTypeTest,
    ::testing::Values(std::make_tuple(false, false)));

INSTANTIATE_TEST_CASE_P(
    GreedyOnDst,
    SwapMoveTypeTest,
    ::testing::Values(std::make_tuple(false, true)));

INSTANTIATE_TEST_CASE_P(
    GreedyOnSrc,
    SwapMoveTypeTest,
    ::testing::Values(std::make_tuple(true, false)));

INSTANTIATE_TEST_CASE_P(
    GreedyOnSrcAndDst,
    SwapMoveTypeTest,
    ::testing::Values(std::make_tuple(true, true)));

CO_TEST_P(SwapMoveTypeTest, VerifyMoveSet) {
  const auto universe = co_await setUpUniverse();
  // since objective and constraint are const_expr(0, universe), all
  // objects will be equivalent
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, getMoveTypeSpec());

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          object(4) /*hotObject*/,
          container(4) /*coldContainer*/,
          getMoveStatsAggregator());

  // container(4) has only object, so only one moveSet (of 2 moves, indicating a
  // swap) is evaluated
  EXPECT_EQ(2, getTotalMovesEvaluated());

  const std::vector<Move> expectedMoveSet = {
      Move{object(4), container(1), container(4)},
      Move{object(8), container(4), container(1)}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());
}

CO_TEST_P(SwapMoveTypeTest, CheckMoveCounts) {
  const interface::LocalSearchSolverSpec solverSpec;
  const interface::SwapMoveTypeSpec swapSpec;
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, getMoveTypeSpec());
  auto [greedyOnSrc, greedyOnDst] = GetParam();

  const auto universe = co_await setUpUniverse();

  // Lookup value = 5 + 6 + 7 = 18
  auto hotContainer = container(3);
  // lookup expression ensures that no two objects are equivalent
  createProblem({getLookupExprOn({hotContainer})}, const_expr(0, *universe));

  auto bestResult = swapMoveType.findBestMove(
      getMovesEvaluator(),
      hotContainer,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // swapping any object from hot container with any object from container(1)
  // improves the objective. Candidate hot objects: objects 5,6,7
  // Candidate objects to swap with:{cold container1: objects 1,2,3,4) {cold
  // container 4: object 8}
  // we expect improvement in all cases
  const auto& precision = getUniverse().getPrecision();
  EXPECT_TRUE(bestResult.isBetter(precision));
  auto lookupValue = bestResult.getValue().get(0).objective;
  if (greedyOnSrc && greedyOnDst) {
    EXPECT_TRUE(bestResult.isBetter(precision));
    // The first hot object will yield improvement
    // swap attempts with cold container 1: 1 (any swap gives improvement)
    // swap attempts with cold container 4: 1 (no improvement)
    EXPECT_EQ(2 * 2, getTotalMovesEvaluated());
    // in worst case when we swap object5 with object4, the objective must
    // improve from 18 to at most 17
    EXPECT_LE(lookupValue, 17);
  } else if (greedyOnSrc) {
    // The first hot object will yield improvement
    // swap attempts with cold container 1: 4 (try all objects in cold
    // container, but only one object from hot container)
    // swap attempts with cold container 4: 1 (no improvement)
    EXPECT_EQ(5 * 2, getTotalMovesEvaluated());
    // object1 must move from container1 to container3
    // lookupValue must improve from 18 to at most: 1 + 6 + 7 = 14
    EXPECT_LE(lookupValue, 14);
  } else if (greedyOnDst) {
    // swap attempts with cold container 1: 3 (try all hot objects)
    // move attempts with cold container 4: 3 (no swap gives improvement)
    EXPECT_EQ(6 * 2, getTotalMovesEvaluated());
    // object5 must move from container3 to container1
    // lookupValue must improve from 18 to at most: 17
    EXPECT_LE(lookupValue, 17);
  } else {
    // swap attempts with cold container 1: 3 * 4 (try all hot/cold
    // combinations)
    // swap attempts with cold container 4: 3 (no swap gives improvement)
    EXPECT_EQ(15 * 2, getTotalMovesEvaluated());
    // must swap object7 with object1
    // lookupValue must improve from 18 to 1+5+6 = 12
    EXPECT_EQ(lookupValue, 12);
  }
}

CO_TEST_P(SwapMoveTypeTest, Sampled) {
  interface::SwapMoveTypeSpec swapSpec;
  auto [greedyOnSrc, greedyOnDst] = GetParam();
  SwapMoveType::makeGreedy(swapSpec, greedyOnSrc, greedyOnDst);

  const auto universe = co_await setUpUniverse();

  // lookup expression ensures that no two objects are equivalent
  // Lookup value = 5 + 6 + 7 = 18
  auto hotContainer = container(3);
  createProblem({getLookupExprOn({hotContainer})}, const_expr(0, *universe));

  SwapMoveType::makeSampled(swapSpec, 1);
  // make fixed destination to be container(1) to reduce randomness
  SwapMoveType::makeFixedDest(
      swapSpec,
      universe->getContainerTypeName(),
      universe->getEntityName(container(1)));
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, swapSpec);

  auto bestResult = MoveResult::makeEmpty();
  constexpr int numTrials = 100;
  int totalEvals = 0;
  for (const auto _ : folly::irange(numTrials)) {
    auto result = swapMoveType.findBestMove(
        getMovesEvaluator(),
        hotContainer,
        getMoveStatsAggregator(),
        getEmptySearchHints(),
        std::numeric_limits<double>::max() /*timelimit*/);
    totalEvals += getTotalMovesEvaluated();
    bestResult.aggregate(std::move(result));
  }

  auto lookupValue = bestResult.getValue().get(0).objective;
  XLOG(INFO) << fmt::format(
      "Attempted {} moves, objective: {} ", totalEvals, lookupValue);
  // after sufficient number of trials, we expect at least one SWAP to succeed.
  // expect objective to improve by at least 1 in all cases.
  const auto& precision = getUniverse().getPrecision();
  EXPECT_TRUE(bestResult.isBetter(precision));
  EXPECT_LE(lookupValue, 17);
}

CO_TEST_P(SwapMoveTypeTest, DestinationsToExplore) {
  interface::SwapMoveTypeSpec swapSpec = getMoveTypeSpec();
  auto [greedyOnSrc, greedyOnDst] = GetParam();
  SwapMoveType::makeGreedy(swapSpec, greedyOnSrc, greedyOnDst);

  const auto universe = co_await setUpUniverse();

  // lookup expression ensures that no two objects are equivalent
  // container3 contributes the most to objective
  // Lookup value = 5 + 6 + 7 = 18
  auto hotContainer = container(3);
  createProblem({getLookupExprOn({hotContainer})}, const_expr(0, *universe));

  SwapMoveType::makeFixedDest(
      swapSpec,
      universe->getContainerTypeName(),
      universe->getEntityName(container(1)));

  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, swapSpec);

  auto bestResult = swapMoveType.findBestMove(
      getMovesEvaluator(),
      hotContainer,
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max() /*timelimit*/);

  // swapping any object from hot container with any object from container(1)
  // improves the objective. Candidate hot objects: objects 5,6,7
  // Candidate objects to swap with:{cold container1: objects 1,2,3,4) {cold
  // container 4: object 8}
  // we expect improvement in all cases
  // because we have fixed destination to be container(1), we do not expect any
  // swap moves with container(4)
  const auto& precision = getUniverse().getPrecision();
  EXPECT_TRUE(bestResult.isBetter(precision));
  auto lookupValue = bestResult.getValue().get(0).objective;
  if (greedyOnSrc && greedyOnDst) {
    EXPECT_TRUE(bestResult.isBetter(precision));
    // The first hot object will yield improvement
    // swap attempts with cold container 1: 1 (any swap gives improvement)
    EXPECT_EQ(1 * 2, getTotalMovesEvaluated());
    // in worst case when we swap object5 with object4, the objective must
    // improve from 18 to at most 17
    EXPECT_LE(lookupValue, 17);
  } else if (greedyOnSrc) {
    // The first hot object will yield improvement
    // swap attempts with cold container 1: 4 (try all objects in cold
    // container, but only one object from hot container)
    EXPECT_EQ(4 * 2, getTotalMovesEvaluated());
    // object1 must move from container1 to container3
    // lookupValue must improve from 18 to at most: 1 + 6 + 7 = 14
    EXPECT_LE(lookupValue, 14);
  } else if (greedyOnDst) {
    // swap attempts with cold container 1: 3 (try all hot objects)
    EXPECT_EQ(3 * 2, getTotalMovesEvaluated());
    // object5 must move from container3 to container1
    // lookupValue must improve from 18 to at most: 17
    EXPECT_LE(lookupValue, 17);
  } else {
    // swap attempts with cold container 1: 3 * 4 (try all hot/cold
    // combinations)
    EXPECT_EQ(12 * 2, getTotalMovesEvaluated());
    // must swap object7 with object1
    // lookupValue must improve from 18 to 1+5+6 = 12
    EXPECT_EQ(lookupValue, 12);
  }
}

CO_TEST_P(SwapMoveTypeTest, ExploreAllDynamicObjectsIgnoreEquivalentObjects) {
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, getMoveTypeSpec());

  const auto universe = co_await setUpUniverse();

  // since objective and constraint are const_expr(0, universe), all
  // objects will be equivalent
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          object(2) /*hotObject*/,
          container(3) /*coldContainer*/,
          getMoveStatsAggregator());

  // since objective and constraint are const_expr(0, getUniverse()), all
  // objects will be equivalent 1 moveSet is evaluated, hence 2 moves in total
  EXPECT_EQ(2, getTotalMovesEvaluated());
}

CO_TEST_P(SwapMoveTypeTest, ExploreAllDynamicObjectsInEmptyColdContainer) {
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, getMoveTypeSpec());

  const auto universe = co_await setUpUniverse();
  // lookup expression ensures that no two objects are equivalent
  createProblem({getLookupExprOn({container(1)})}, const_expr(0, *universe));

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          object(2) /*hotObject*/,
          container(2) /*coldContainer*/,
          getMoveStatsAggregator());

  // container(2) is empty, so no moves will be evaluated
  EXPECT_EQ(0, getTotalMovesEvaluated());
}

CO_TEST_P(SwapMoveTypeTest, ExploreOnlyWithinSamePartition) {
  interface::SwapMoveTypeSpec swapSpec = getMoveTypeSpec();
  swapSpec.partitionNameToExploreSwapsWithinObjectGroup() = kPartitionName;

  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, swapSpec);

  const auto universe = co_await setUpUniverse();
  // lookup expression ensures that no two objects are equivalent
  createProblem({getLookupExprOn({container(1)})}, const_expr(0, *universe));

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          object(2) /*hotObject*/,
          container(3) /*coldContainer*/,
          getMoveStatsAggregator());

  // container(3) contains object(5), object(6), and object(7). However, only
  // object(6) is in the same group as object(2).
  // 1 moveSet is evaluated, hence 2 moves in total
  EXPECT_EQ(2, getTotalMovesEvaluated());
}

CO_TEST_P(
    SwapMoveTypeTest,
    ExploreOnlyWithinSamePartitionIgnoreEquivalentObjects) {
  interface::SwapMoveTypeSpec swapSpec = getMoveTypeSpec();
  swapSpec.partitionNameToExploreSwapsWithinObjectGroup() = kPartitionName;

  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, swapSpec);

  const auto universe = co_await setUpUniverse();
  // since objective and constraint are const_expr(0, universe), all objects are
  // equivalent
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1) /*hotContainer*/,
          object(1) /*hotObject*/,
          container(3) /*coldContainer*/,
          getMoveStatsAggregator());

  // container(3) contains object(5), object(6), and object(7). However, only
  // object(5) and object(7) are in the same group as object(1). Moreover,
  // since both objective and constraint are const_expr(0, getUniverse()), all
  // objects are equivalent. Hence, only 1 moveSet is evaluated
  EXPECT_EQ(2, getTotalMovesEvaluated());
}

TEST_P(SwapMoveTypeTest, Name) {
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, getMoveTypeSpec());
  EXPECT_EQ(swapMoveType.name(), "SWAP");
}

// --- Ratio-aware swap tests: validates shared 1:k and k:1 candidate building
// ---

struct MoveDirectionCounts {
  int toSrc = 0;
  int toDst = 0;
};

MoveDirectionCounts countMoveDirections(
    const MoveSet& moveSet,
    entities::ContainerId src,
    entities::ContainerId dst) {
  MoveDirectionCounts counts;
  for (const auto& move : moveSet) {
    if (move.getDestinationContainer() == dst) {
      ++counts.toDst;
    } else if (move.getDestinationContainer() == src) {
      ++counts.toSrc;
    }
  }
  return counts;
}

class SwapMoveTypeDirectionTest : public MoveTestBase {
 protected:
  SwapMoveTypeDirectionTest() : MoveTestBase("replica", "region") {}

  // Setup for 1:k: 1 big hot object, 3 small cold objects
  // container(1) has {replica1(3.0)}
  // container(2) has {replica2(1.0), replica3(1.0), replica4(1.0)}
  folly::coro::Task<std::shared_ptr<const entities::Universe>>
  setUpUniverseOneToK() {
    setInitialAssignment({
        {"region1", {"replica1"}},
        {"region2", {"replica2", "replica3", "replica4"}},
    });
    co_await addObjectDimension(
        "rru",
        {{"replica1", 3.0},
         {"replica2", 1.0},
         {"replica3", 1.0},
         {"replica4", 1.0}},
        0.0);
    co_return buildUniverse();
  }

  folly::coro::Task<std::shared_ptr<const entities::Universe>>
  setUpUniverseOneToKDynamic() {
    setInitialAssignment({
        {"region1", {"replica1"}},
        {"region2", {"replica2", "replica3", "replica4"}},
    });
    co_await addDynamicObjectDimension(
        "rru",
        scopeId("region"),
        {{"region1",
          {{"replica1", 3.0},
           {"replica2", 1.0},
           {"replica3", 1.0},
           {"replica4", 1.0}}},
         {"region2",
          {{"replica1", 100.0},
           {"replica2", 100.0},
           {"replica3", 100.0},
           {"replica4", 100.0}}}},
        0.0);
    co_return buildUniverse();
  }

  // Setup for k:1: 3 small hot objects, 1 big cold object
  // container(1) has {replica1(1.0), replica2(1.0), replica3(1.0)}
  // container(2) has {replica4(3.0)}
  folly::coro::Task<std::shared_ptr<const entities::Universe>>
  setUpUniverseKToOne() {
    setInitialAssignment({
        {"region1", {"replica1", "replica2", "replica3"}},
        {"region2", {"replica4"}},
    });
    co_await addObjectDimension(
        "rru",
        {{"replica1", 1.0},
         {"replica2", 1.0},
         {"replica3", 1.0},
         {"replica4", 3.0}},
        0.0);
    co_return buildUniverse();
  }

  folly::coro::Task<std::shared_ptr<const entities::Universe>>
  setUpUniverseKToOneDynamic() {
    setInitialAssignment({
        {"region1", {"replica1", "replica2", "replica3"}},
        {"region2", {"replica4"}},
    });
    co_await addDynamicObjectDimension(
        "rru",
        scopeId("region"),
        {{"region1",
          {{"replica1", 1.0},
           {"replica2", 1.0},
           {"replica3", 1.0},
           {"replica4", 3.0}}},
         {"region2",
          {{"replica1", 100.0},
           {"replica2", 100.0},
           {"replica3", 100.0},
           {"replica4", 100.0}}}},
        0.0);
    co_return buildUniverse();
  }

  folly::coro::Task<std::shared_ptr<const entities::Universe>>
  setUpUniverseEqualValues() {
    setInitialAssignment({
        {"region1", {"replica1"}},
        {"region2", {"replica2"}},
    });
    co_await addObjectDimension(
        "rru", {{"replica1", 1.5}, {"replica2", 1.5}}, 0.0);
    co_return buildUniverse();
  }

  static interface::SwapMoveTypeSpec makeRatioSpec() {
    interface::SwapMoveTypeSpec spec;
    interface::StringKeyValueMap ratioDim;
    ratioDim.defaultValue() = "rru";
    spec.swapRatioDimension() = ratioDim;
    return spec;
  }
};

// 1:k swap with k = ceil(3.0/1.0) = 3
// container(1) has replica1 (rru=3.0), container(2) has {replica2,3,4}
// (rru=1.0) Expect 1 move set of 4 moves (1 hot out + 3 cold in)
CO_TEST_F(SwapMoveTypeDirectionTest, OneToK_MoveComposition) {
  const auto universe = co_await setUpUniverseOneToK();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  const auto spec = makeRatioSpec();
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, spec);

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1),
          object(1),
          container(2),
          getMoveStatsAggregator());

  // 1:k swap: 1 move set with 4 moves (1 hot out + 3 cold in)
  EXPECT_EQ(4, getTotalMovesEvaluated());

  const auto counts =
      countMoveDirections(bestResult.getMoveSet(), container(1), container(2));
  EXPECT_EQ(1, counts.toDst); // 1 hot object moves out
  EXPECT_EQ(3, counts.toSrc); // k=3 cold objects move in
}

CO_TEST_F(
    SwapMoveTypeDirectionTest,
    OneToK_DynamicDimensionUsesHotContainerScope) {
  const auto universe = co_await setUpUniverseOneToKDynamic();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  const auto spec = makeRatioSpec();
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, spec);

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1),
          object(1),
          container(2),
          getMoveStatsAggregator());

  EXPECT_EQ(4, getTotalMovesEvaluated());

  const auto counts =
      countMoveDirections(bestResult.getMoveSet(), container(1), container(2));
  EXPECT_EQ(1, counts.toDst);
  EXPECT_EQ(3, counts.toSrc);
}

// k:1 swap with k = ceil(3.0/1.0) = 3
// The shared ratio-aware builder bundles the smaller hot side, and the chosen
// hot object stays anchored in the bundle.
CO_TEST_F(SwapMoveTypeDirectionTest, KToOne_MoveComposition) {
  // Enable strict k:1 swaps for this test
  ProblemConfigs::enableKToOneSwaps = true;

  const auto universe = co_await setUpUniverseKToOne();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  const auto spec = makeRatioSpec();
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, spec);

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1),
          object(3),
          container(2),
          getMoveStatsAggregator());

  EXPECT_EQ(4, getTotalMovesEvaluated());

  const auto counts =
      countMoveDirections(bestResult.getMoveSet(), container(1), container(2));
  EXPECT_EQ(3, counts.toDst);
  EXPECT_EQ(1, counts.toSrc);

  // Verify object(3) is in the move set going to the cold container.
  const auto hotObjectAnchored = std::any_of(
      bestResult.getMoveSet().begin(),
      bestResult.getMoveSet().end(),
      [this](const Move& move) {
        return move.getObject() == object(3) &&
            move.getDestinationContainer() == container(2);
      });
  EXPECT_TRUE(hotObjectAnchored)
      << "hotObject must always be anchored in the swap";

  // Reset to default
  ProblemConfigs::enableKToOneSwaps = false;
}

CO_TEST_F(
    SwapMoveTypeDirectionTest,
    KToOne_DynamicDimensionUsesHotContainerScope) {
  ProblemConfigs::enableKToOneSwaps = true;

  const auto universe = co_await setUpUniverseKToOneDynamic();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  const auto spec = makeRatioSpec();
  auto swapMoveType =
      MockSwapMoveType(interface::LocalSearchSolverSpec{}, spec);

  auto bestResult =
      swapMoveType.exploreSwappingHotObjectWithObjectsInColdContainer(
          getMovesEvaluator(),
          container(1),
          object(3),
          container(2),
          getMoveStatsAggregator());

  EXPECT_EQ(4, getTotalMovesEvaluated());

  const auto counts =
      countMoveDirections(bestResult.getMoveSet(), container(1), container(2));
  EXPECT_EQ(3, counts.toDst);
  EXPECT_EQ(1, counts.toSrc);

  ProblemConfigs::enableKToOneSwaps = false;
}

CO_TEST_F(
    SwapMoveTypeDirectionTest,
    CalculateSwapRatioReturnsOneOnEqualValues) {
  const auto universe = co_await setUpUniverseEqualValues();
  EXPECT_EQ(
      calculateSwapRatio(
          *universe, object(1), object(2), universe->getDimensionId("rru")),
      1);
}

class SwapMoveTypeFilterTest : public MoveTestBase {
 protected:
  SwapMoveTypeFilterTest() : MoveTestBase("object", "container") {}

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse() {
    setInitialAssignment({
        {"container1", {"object1", "object2"}},
        {"container2", {"object3"}},
        {"container3", {}},
    });
    co_return buildUniverse();
  }
};

CO_TEST_F(SwapMoveTypeFilterTest, FilterReducesEvaluatedSwaps) {
  const auto universe = co_await setUpUniverse();
  const auto numObjects = universe->getObjects().getObjectIds().size();
  const auto numContainers = universe->getContainers().getContainerIds().size();

  auto containers = std::make_shared<PackerSet<entities::ContainerId>>(
      PackerSet<entities::ContainerId>{
          container(1), container(2), container(3)});
  auto objectiveTuple = std::vector<ExprPtr>{object_lookup(
      makeAllUnequalObjectVector(3),
      containers,
      Assignment(universe->getContainers().getInitialAssignment()))};

  interface::SwapMoveTypeSpec swapSpec;
  swapSpec.greedyOnDst() = true;
  swapSpec.greedyOnSrc() = true;

  const auto findBestMove = [&] {
    return SwapMoveType(interface::LocalSearchSolverSpec{}, swapSpec)
        .findBestMove(
            getMovesEvaluator(),
            container(1),
            getMoveStatsAggregator(),
            getEmptySearchHints(),
            std::numeric_limits<double>::max());
  };

  // Without filter
  createProblem(objectiveTuple, const_expr(0, *universe));
  const auto resultNoFilter = findBestMove();
  const auto numEvalsNoFilter = getTotalMovesEvaluated();

  // With filter: block object1 from container2
  auto filter = std::make_unique<InvalidMoveFilter>(numObjects, numContainers);
  filter->markInvalid({object(1)}, container(2));
  createProblem(
      objectiveTuple,
      const_expr(0, *universe),
      /*higherPriorityObjConfig=*/std::nullopt,
      /*nonAcceptingContainers=*/{},
      std::move(filter));
  const auto resultWithFilter = findBestMove();
  const auto numEvalsWithFilter = getTotalMovesEvaluated();

  // Without filter: 4 swap evaluations. With object1 blocked from
  // container2: 2 swap pairs involving object1→container2 are skipped.
  EXPECT_EQ(numEvalsNoFilter, 4);
  EXPECT_EQ(numEvalsWithFilter, 2);
  EXPECT_EQ(
      resultNoFilter.getMoveSet().size(), resultWithFilter.getMoveSet().size());
  for (size_t i = 0; i < resultNoFilter.getMoveSet().size(); ++i) {
    EXPECT_EQ(
        resultNoFilter.getMoveSet().at(i), resultWithFilter.getMoveSet().at(i));
  }
}

} // namespace facebook::rebalancer::packer::tests
