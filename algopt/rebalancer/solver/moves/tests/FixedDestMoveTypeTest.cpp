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
#include "algopt/rebalancer/solver/moves/FixedDestMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class FixedDestMoveTypeTest : public MoveTestBase {
 protected:
  FixedDestMoveTypeTest() : MoveTestBase("object", "container") {}

  folly::coro::Task<void> setUpProblem() {
    // initial assignment, object0 thru object99 are in container0
    entities::Map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(100)) {
      assignment["container0"].push_back(fmt::format("object{}", i));
    }
    assignment["container1"] = {};
    setInitialAssignment(assignment);

    const auto universe = buildUniverse();
    const Assignment initialAssignment(
        universe->getContainers().getInitialAssignment());

    // objective = 1 + 2 ... 99
    // best move is object99 to container1, will reduce objective by 99
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
};

TEST_F(FixedDestMoveTypeTest, MakeSampled) {
  auto fixedDestSpec = interface::FixedDestMoveTypeSpec();
  FixedDestMoveType::makeSampled(fixedDestSpec, 1);
  EXPECT_EQ(*fixedDestSpec.sampleSize()->defaultSampleSize(), 1);
  EXPECT_TRUE(fixedDestSpec.sampleSize()->objectToSampleSize()->empty());
}

CO_TEST_F(FixedDestMoveTypeTest, NotSampled) {
  co_await setUpProblem();

  auto config = interface::LocalSearchSolverSpec();
  config.specialContainer() = "container1";

  auto fixedDestSpec = interface::FixedDestMoveTypeSpec();
  auto moveType = FixedDestMoveType(config, fixedDestSpec);

  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(0),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  const std::vector<Move> expectedMoveSet = {
      {Move{object(99), container(0), container(1)}}};
  REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, bestResult.getMoveSet());

  EXPECT_EQ(100, getTotalMovesEvaluated());
}

CO_TEST_F(FixedDestMoveTypeTest, Sampled) {
  co_await setUpProblem();

  auto config = interface::LocalSearchSolverSpec();
  config.specialContainer() = "container1";

  auto fixedDestSpec = interface::FixedDestMoveTypeSpec();
  constexpr int sampleSize = 50;
  FixedDestMoveType::makeSampled(fixedDestSpec, sampleSize);

  auto moveType = FixedDestMoveType(config, fixedDestSpec);
  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(0),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_NEAR(
      sampleSize,
      getTotalMovesEvaluated(),
      sampleSize * 0.1); // 10% error allowed
}

CO_TEST_F(FixedDestMoveTypeTest, SampledWithSpecialContainer) {
  co_await setUpProblem();

  auto config = interface::LocalSearchSolverSpec();

  auto fixedDestSpec = interface::FixedDestMoveTypeSpec();
  constexpr int sampleSize = 50;
  FixedDestMoveType::makeSampled(fixedDestSpec, sampleSize);
  fixedDestSpec.specialContainer() = "container1";

  auto moveType = FixedDestMoveType(config, fixedDestSpec);
  auto bestResult = moveType.findBestMove(
      getMovesEvaluator(),
      container(0),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_NEAR(
      sampleSize,
      getTotalMovesEvaluated(),
      sampleSize * 0.1); // 10% error allowed
}

} // namespace facebook::rebalancer::packer::tests
