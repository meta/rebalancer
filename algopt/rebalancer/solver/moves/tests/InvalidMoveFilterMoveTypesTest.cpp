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

#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/moves/GreedyGroupToScopeItemMoveType.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/KLSearchMoveType.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/moves/ReplicaDropMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleChainFastMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleChainMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleEndChainMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleGreedyMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleRandomObjectStratifiedMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapFullContainersMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapNMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/moves/TripleLoopMoveType.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace facebook::rebalancer::packer::tests {

namespace {
using MoveTypeFactory =
    std::function<std::unique_ptr<MoveType>(const Problem& problem)>;

struct SearchOutcome {
  std::vector<Move> bestMoves;
  int64_t evaluatedMoves{0};
  int64_t invalidMoves{0};
  int64_t prunedMoves{0};
};
} // namespace

// The only hard constraint forbids object1 in container2, and the filter, when
// enabled, marks exactly that pair. Every move is neutral for the objective, so
// the best move is decided by the deterministic tie-break among valid moves.
class InvalidMoveFilterMoveTypesTest : public MoveTestBase {
 protected:
  InvalidMoveFilterMoveTypesTest() : MoveTestBase("object", "container") {}

  folly::coro::Task<void> setUpUniverse() {
    setInitialAssignment({
        {"container1", {"object1", "object2"}},
        {"container2", {"object3"}},
        {"container3", {"object4", "object5"}},
        {"container4", {"object6"}},
    });
    co_await addPartition(
        "group",
        {
            {"group1", {"object1", "object3"}},
            {"group2", {"object2", "object4"}},
            {"group3", {"object5", "object6"}},
        });
    co_await addScope(
        "region",
        {
            {"region1", {"container1", "container2"}},
            {"region2", {"container3", "container4"}},
        });
    // container2 is the only container outside this scope.
    co_await addScope(
        "dropScope", {{"kept", {"container1", "container3", "container4"}}});
    buildUniverse();
  }

  SearchOutcome search(
      const MoveTypeFactory& makeMoveType,
      entities::ContainerId hotContainer,
      bool enableFilter) {
    const auto universe = getUniversePtr();
    const auto& containerIds = universe->getContainers().getContainerIds();
    const Assignment initialAssignment(
        universe->getContainers().getInitialAssignment());

    std::unique_ptr<InvalidMoveFilter> filter;
    if (enableFilter) {
      filter = std::make_unique<InvalidMoveFilter>(
          universe->getNumObjects(), containerIds.size());
      filter->markInvalid({object(1)}, container(2));
    }

    const auto objective = object_lookup(
        makeAllUnequalObjectVector(6),
        std::make_shared<PackerSet<entities::ContainerId>>(
            containerIds.begin(), containerIds.end()),
        initialAssignment);
    const auto constraint = object_lookup(
        makeObjectVector({{object(1), 1.0}}, *universe),
        std::make_shared<PackerSet<entities::ContainerId>>(
            PackerSet<entities::ContainerId>{container(2)}),
        initialAssignment);
    auto problem = createTestProblem(
        universe,
        {objective},
        constraint,
        /*nonAcceptingContainers=*/{},
        /*config=*/{},
        /*performInitialFullApply=*/true,
        /*enableParallelizedBoundsComputing=*/false,
        std::move(filter));
    // CoreLocalSearchSolve builds the equivalence sets when a stage starts;
    // build them here so move types evaluating in parallel do not race to
    // build them lazily.
    problem->getEquivalenceSets();

    const MovesEvaluator evaluator(*problem, 0, 1, "stage");
    MoveStatsAggregator stats(universe->getPrecision());
    const auto result = makeMoveType(*problem)->findBestMove(
        evaluator,
        hotContainer,
        stats,
        SearchHints(SearchHintsConfig()),
        std::numeric_limits<double>::max());
    return SearchOutcome{
        .bestMoves = std::vector<Move>(
            result.getMoveSet().begin(), result.getMoveSet().end()),
        .evaluatedMoves = stats.getGlobalStats().getTotalMoves(),
        .invalidMoves = stats.getGlobalStats().getInvalidMoves(),
        .prunedMoves = stats.getGlobalStats().getNumPruned(),
    };
  }

  // Without the filter the move type evaluates moves of object1 into
  // container2, all rejected by the constraint. With the filter it skips
  // exactly those, and picks the same best move.
  void expectFilterSkipsOnlyInvalidMoves(
      const MoveTypeFactory& makeMoveType,
      entities::ContainerId hotContainer) {
    const auto withoutFilter =
        search(makeMoveType, hotContainer, /*enableFilter=*/false);
    const auto withFilter =
        search(makeMoveType, hotContainer, /*enableFilter=*/true);

    EXPECT_GT(withoutFilter.invalidMoves, 0);
    EXPECT_EQ(0, withFilter.invalidMoves);
    EXPECT_EQ(0, withoutFilter.prunedMoves);
    EXPECT_GT(withFilter.prunedMoves, 0);
    EXPECT_EQ(
        withoutFilter.evaluatedMoves - withoutFilter.invalidMoves,
        withFilter.evaluatedMoves);
    EXPECT_FALSE(withFilter.bestMoves.empty());
    EXPECT_EQ(withoutFilter.bestMoves, withFilter.bestMoves);
  }
};

CO_TEST_F(InvalidMoveFilterMoveTypesTest, Single) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<SingleMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::SingleMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SingleGreedy) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<SingleGreedyMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::SingleGreedyMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SingleRandomObjectStratified) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        interface::GroupList groupList;
        groupList.partitionName() = "group";
        interface::ObjectsFromGroupsSpec objectsFromGroupsSpec;
        objectsFromGroupsSpec.groupList() = std::move(groupList);
        interface::ObjectsToExploreOptions objectsToExploreOptions;
        objectsToExploreOptions.set_objectsFromGroupsSpec(
            std::move(objectsFromGroupsSpec));
        interface::SampleSize sampleSize;
        sampleSize.defaultSampleSize() = 10;
        interface::SingleRandomObjectStratifiedMoveTypeSpec spec;
        spec.objectsToExploreOptions() = std::move(objectsToExploreOptions);
        spec.stratifiedSampleSize() = std::move(sampleSize);
        return std::make_unique<SingleRandomObjectStratifiedMoveType>(
            interface::LocalSearchSolverSpec{}, spec);
      },
      container(2));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SingleChain) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<SingleChainMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::SingleChainMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SingleChainFast) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<SingleChainFastMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::SingleChainFastMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SingleEndChain) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<SingleEndChainMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::SingleEndChainMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, TripleLoop) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<TripleLoopMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::TripleLoopMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, KLSearch) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<KLSearchMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::KLSearchMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SwapFullContainers) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem&) {
        return std::make_unique<SwapFullContainersMoveType>(
            interface::LocalSearchSolverSpec{},
            interface::SwapFullContainersMoveTypeSpec{});
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, GreedyGroupToScopeItem) {
  co_await setUpUniverse();
  expectFilterSkipsOnlyInvalidMoves(
      [](const Problem& problem) {
        interface::GreedyGroupToScopeItemMoveTypeSpec spec;
        spec.groupMovesPartition() = "group";
        spec.scopeItemMovesScope() = "region";
        spec.nSampleSetsToExplore() = 8;
        return std::make_unique<GreedyGroupToScopeItemMoveType>(
            interface::LocalSearchSolverSpec{}, spec, problem);
      },
      container(1));
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, ReplicaDrop) {
  co_await setUpUniverse();
  const MoveTypeFactory makeMoveType = [](const Problem&) {
    interface::ReplicaDropMoveTypeSpec spec;
    spec.replicaDropPartition() = "group";
    spec.replicaDropScope() = "dropScope";
    return std::make_unique<ReplicaDropMoveType>(
        interface::LocalSearchSolverSpec{}, spec);
  };
  const auto withoutFilter =
      search(makeMoveType, container(1), /*enableFilter=*/false);
  const auto withFilter =
      search(makeMoveType, container(1), /*enableFilter=*/true);

  // Dropping into container2: object1 (invalid), object2 and object4.
  EXPECT_EQ(3, withoutFilter.evaluatedMoves);
  EXPECT_EQ(1, withoutFilter.invalidMoves);
  EXPECT_EQ(2, withFilter.evaluatedMoves);
  EXPECT_EQ(0, withFilter.invalidMoves);
  // ReplicaDrop proposes only improving moves, and no drop improves here.
  EXPECT_TRUE(withoutFilter.bestMoves.empty());
  EXPECT_TRUE(withFilter.bestMoves.empty());
}

CO_TEST_F(InvalidMoveFilterMoveTypesTest, SwapN) {
  co_await setUpUniverse();
  const MoveTypeFactory makeMoveType = [](const Problem& problem) {
    interface::SwapNMoveTypeSpec spec;
    spec.swapNSourceObjects() = {"object1"};
    spec.swapNDestinationScope() = {{"container2"}};
    spec.swapNIterations() = 5;
    return std::make_unique<SwapNMoveType>(
        interface::LocalSearchSolverSpec{}, spec, problem);
  };
  const auto withoutFilter =
      search(makeMoveType, container(1), /*enableFilter=*/false);
  const auto withFilter =
      search(makeMoveType, container(1), /*enableFilter=*/true);

  // Every generated move set swaps object1 into container2.
  EXPECT_GT(withoutFilter.evaluatedMoves, 0);
  EXPECT_EQ(withoutFilter.evaluatedMoves, withoutFilter.invalidMoves);
  EXPECT_EQ(0, withFilter.evaluatedMoves);
  EXPECT_TRUE(withFilter.bestMoves.empty());
}

} // namespace facebook::rebalancer::packer::tests
