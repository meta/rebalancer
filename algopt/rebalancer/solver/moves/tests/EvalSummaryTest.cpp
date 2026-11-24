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

#include "algopt/rebalancer/solver/moves/EvalSummary.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"

#include <gtest/gtest.h>

#include <memory>

namespace facebook::rebalancer::packer::tests {

TEST(EvalSummaryTest, ReasonBreakdownSurvivesRoundReset) {
  EvalSummary summary;
  MoveStats round;
  const auto disk =
      std::make_shared<LabeledExpression>("disk_limit", nullptr, 1);
  const auto soft =
      std::make_shared<LabeledExpression>("disk_soft", nullptr, 1);
  round.bump(MoveStats::INVALID, disk, 2);
  round.bump(MoveStats::WORSE, soft, 3);
  round.incrNumPruned();
  summary.aggregate(round);
  round.clear();

  const auto sameDiskName =
      std::make_shared<LabeledExpression>("disk_limit", nullptr, 1);
  round.bump(MoveStats::INVALID, sameDiskName, 4);
  round.bump(MoveStats::NEUTRAL, nullptr);
  summary.aggregate(round);

  const auto stats = summary.getSolverEvalStats(
      1.0, 0.0, 1, interface::EvalReasonBreakdownSpec{});
  ASSERT_TRUE(stats.has_value());
  ASSERT_TRUE(stats->evalReasonBreakdown().has_value());
  interface::EvalReasonBreakdown expected;
  expected.constraintToNumInvalidEvals() = {{"disk_limit", 6}};
  expected.objectiveToNumWorseEvals() = {{"disk_soft", 3}};
  EXPECT_EQ(expected, *stats->evalReasonBreakdown());
  EXPECT_EQ(10, *stats->numEvals());
  EXPECT_EQ(1, *stats->numPrunedEvals());
}

TEST(EvalSummaryTest, ReasonBreakdownIsOptIn) {
  EvalSummary summary;
  MoveStats round;
  round.bump(
      MoveStats::INVALID,
      std::make_shared<LabeledExpression>("capacity", nullptr, 1),
      2);
  summary.aggregate(round);

  const auto disabled = summary.getSolverEvalStats(1.0, 0.0, 1);
  const auto enabled = summary.getSolverEvalStats(
      1.0, 0.0, 1, interface::EvalReasonBreakdownSpec{});
  ASSERT_TRUE(disabled.has_value());
  ASSERT_TRUE(enabled.has_value());
  EXPECT_FALSE(disabled->evalReasonBreakdown().has_value());
  ASSERT_TRUE(enabled->evalReasonBreakdown().has_value());
  auto enabledWithoutBreakdown = *enabled;
  enabledWithoutBreakdown.evalReasonBreakdown().reset();
  EXPECT_EQ(*disabled, enabledWithoutBreakdown);
}

TEST(EvalSummaryTest, ReasonBreakdownSkipsUnlabelledEvaluations) {
  EvalSummary summary;
  MoveStats round;
  round.bump(MoveStats::INVALID, nullptr, 2);
  round.bump(
      MoveStats::WORSE,
      std::make_shared<LabeledExpression>("doNotWorsenGoal", nullptr, 1));
  summary.aggregate(round);

  const auto stats = summary.getSolverEvalStats(
      1.0, 0.0, 1, interface::EvalReasonBreakdownSpec{});
  ASSERT_TRUE(stats.has_value());
  ASSERT_TRUE(stats->evalReasonBreakdown().has_value());
  interface::EvalReasonBreakdown expected;
  expected.objectiveToNumWorseEvals() = {{"doNotWorsenGoal", 1}};
  EXPECT_EQ(expected, *stats->evalReasonBreakdown());
}

TEST(EvalSummaryTest, NoEvaluationStatsForEmptyStage) {
  const EvalSummary summary;
  EXPECT_FALSE(summary.getSolverEvalStats(1.0, 0.0, 1).has_value());
  EXPECT_FALSE(
      summary
          .getSolverEvalStats(1.0, 0.0, 1, interface::EvalReasonBreakdownSpec{})
          .has_value());
}

} // namespace facebook::rebalancer::packer::tests
