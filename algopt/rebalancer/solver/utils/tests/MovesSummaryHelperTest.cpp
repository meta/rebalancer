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

#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/MovesSummaryHelper.h"
#include "algopt/rebalancer/solver/utils/Precision.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include "multifeed/hash/HeterogeneousAccess.h"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {
namespace {

// Default absolute & relative tolerance from PrecisionTolerances.
constexpr double kTolerance = 1e-10;

// makeMovesSummary only consults the Problem for its precision (and for object/
// container names, which an empty MoveSet never touches), so a trivial Problem
// is enough.
std::unique_ptr<Problem> makeProblem() {
  entities::tests::UniverseBuilderTestUtils builder;
  builder.setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}}});
  const auto universe = builder.buildUniverse();
  return createTestProblem(
      universe, {const_expr(0, *universe)}, const_expr(0, *universe));
}

LabeledExpressionSet makeObjectives(int numObjectives) {
  LabeledExpressionSet objectives;
  objectives.reserve(numObjectives);
  for (const auto i : folly::irange(numObjectives)) {
    objectives.push_back(
        std::make_shared<LabeledExpression>(
            fmt::format("objective_{}", i), nullptr, 1));
  }
  return objectives;
}

ObjectiveDeltaSet makeDeltaSet(
    const LabeledExpressionSet& objectives,
    double perObjectiveChange) {
  ObjectiveDeltaSet deltaSet;
  deltaSet.reserve(objectives.size());
  for (const auto& objective : objectives) {
    constexpr double kOldValue = 0.01;
    deltaSet.emplace_back(objective, kOldValue, kOldValue - perObjectiveChange);
  }
  return deltaSet;
}

MoveResult makeMoveResult(const ObjectiveDeltaSet& deltaSet) {
  double oldSum = 0.0;
  double newSum = 0.0;
  for (const auto& delta : deltaSet) {
    oldSum += delta.oldValue;
    newSum += delta.newValue;
  }
  const entities::Universe universe;
  GlobalObjective::Builder oldBuilder;
  oldBuilder.setObjective(0, const_expr(oldSum, universe));
  GlobalObjective::Builder newBuilder;
  newBuilder.setObjective(0, const_expr(newSum, universe));
  return MoveResult::makeValid(
      /*moveSet=*/{},
      LocalSearchObjective(
          oldBuilder.build(universe), GlobalObjective(universe), universe)
          .getValue(),
      LocalSearchObjective(
          newBuilder.build(universe), GlobalObjective(universe), universe)
          .getValue(),
      /*arbiterValue=*/std::nullopt,
      ObjectiveDeltaSets{deltaSet});
}

} // namespace

// A move is accepted because the AGGREGATE value at a tuple position (the sum
// of its labeled sub-objectives) improves by more than the precision tolerance,
// while every INDIVIDUAL sub-objective changes by less than the tolerance.
// makeMovesSummary must still record each changed sub-objective: filtering them
// with the precision tolerance would drop the deltas that explain the move.
TEST(
    MovesSummaryHelperTest,
    RecordsChangedSubObjectivesEvenWhenIndividuallySubTolerance) {
  const auto problem = makeProblem();
  const auto& precision = problem->getUniverse().getPrecision();

  // Five sub-objectives, each changing by 0.5 * tolerance (below tolerance), so
  // their summed change is 2.5 * tolerance (above tolerance).
  constexpr int kNumObjectives = 5;
  const auto objectives = makeObjectives(kNumObjectives);
  const auto deltaSet =
      makeDeltaSet(objectives, /*perObjectiveChange=*/0.5 * kTolerance);

  // Sanity-check the setup: each individual change is sub-tolerance ...
  for (const auto& delta : deltaSet) {
    EXPECT_EQ(0, precision.compare(delta.oldValue, delta.newValue));
  }

  const auto moveResult = makeMoveResult(deltaSet);

  // ... and the aggregated move is a strict improvement.
  EXPECT_TRUE(moveResult.isBetter(precision));

  const MoveStatsAggregator moveStats(precision);
  const auto summary = MovesSummaryHelper::makeMovesSummary(
      *problem,
      moveResult.getMoveSet(),
      &moveResult.getObjectiveDeltas(),
      moveStats);

  // All changed sub-objectives are recorded, despite each being sub-tolerance.
  EXPECT_EQ(kNumObjectives, summary.objectives()->size());
}

// when a sub-objective changes by more than the tolerance, it is
// recorded in the summary.
TEST(MovesSummaryHelperTest, RecordsObjectiveWhenChangeExceedsTolerance) {
  const auto problem = makeProblem();
  const auto& precision = problem->getUniverse().getPrecision();

  const auto objectives = makeObjectives(/*numObjectives=*/1);
  const auto deltaSet =
      makeDeltaSet(objectives, /*perObjectiveChange=*/10 * kTolerance);
  const auto moveResult = makeMoveResult(deltaSet);
  EXPECT_TRUE(moveResult.isBetter(precision));

  const MoveStatsAggregator moveStats(precision);
  const auto summary = MovesSummaryHelper::makeMovesSummary(
      *problem,
      moveResult.getMoveSet(),
      &moveResult.getObjectiveDeltas(),
      moveStats);

  ASSERT_TRUE(summary.objectives()->contains("objective_0"));
  EXPECT_EQ(0, summary.objectives()->at("objective_0").tuplePos().value());
}

TEST(MovesSummaryHelperTest, LabelsPenaltyDelta) {
  const auto problem = makeProblem();
  const MoveStatsAggregator moveStats(problem->getUniverse().getPrecision());
  const auto softConstraint = std::make_shared<const LabeledExpression>(
      "soft_constraint", nullptr, /*weight=*/1);
  ObjectiveDeltaSet deltaSet;
  deltaSet.emplace_back(
      softConstraint,
      /*oldValue=*/2,
      /*newValue=*/1,
      /*isPenalty=*/true);
  const ObjectiveDeltaSets deltas{std::move(deltaSet)};

  const auto summary = MovesSummaryHelper::makeMovesSummary(
      *problem, MoveSet{}, &deltas, moveStats);

  ASSERT_TRUE(summary.objectives()->contains("soft_constraint [penalty]"));
  const auto& penaltyChange =
      summary.objectives()->at("soft_constraint [penalty]");
  EXPECT_EQ(1, *penaltyChange.newValue());
  EXPECT_EQ(1, *penaltyChange.change());
}

} // namespace facebook::rebalancer::packer::tests
