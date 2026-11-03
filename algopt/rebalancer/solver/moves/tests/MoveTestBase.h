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

#pragma once

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/SearchHints.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <gtest/gtest.h>

#include <optional>

namespace entities = facebook::rebalancer::entities;

namespace facebook::rebalancer::packer::tests {

#define REBALANCER_EXPECT_EQ_MOVESETS(expectedMoveSet, actualMoveSet) \
  verifyMovesAreAsExpected(__LINE__, __FILE__, expectedMoveSet, actualMoveSet)

template <typename T = ::testing::Test>
class MoveTestBaseT : public T,
                      public entities::tests::UniverseBuilderTestUtils {
 protected:
  MoveTestBaseT(
      const std::string& objectName,
      const std::string& containerName);

  void createProblem(
      const std::vector<ExprPtr>& objectiveTuple,
      ExprPtr constraint,
      const std::optional<
          algopt::common::thrift::HigherPriorityObjectivesConfig>&
          higherPriorityObjConfig = std::nullopt,
      const PackerSet<entities::ContainerId>& nonAcceptingContainers = {},
      std::unique_ptr<InvalidMoveFilter> invalidMoveFilter = nullptr);

  Problem& getProblem();

  const MovesEvaluator& getMovesEvaluator();

  MoveStatsAggregator& getMoveStatsAggregator();

  const SearchHints& getEmptySearchHints() const;

  void setEquivalenceSets(EquivalenceSets equivalenceSets);

  int64_t getTotalMovesEvaluated();

  void verifyMovesAreAsExpected(
      int lineNumber,
      std::string fileName,
      const std::vector<Move>& expectedMoveSet,
      const MoveSet& actualMoveSet) const;

  ExprPtr makeObjectLookup(
      ExprPtr objectVector,
      const PackerSet<entities::ContainerId>& containers);

  std::shared_ptr<ObjectVector> makeAllUnequalObjectVector(
      int objectCount,
      bool negateAllValues = false);

 private:
  std::unique_ptr<Problem> problem_;
  std::optional<MovesEvaluator> movesEvaluator_;
  std::unique_ptr<MoveStatsAggregator> stats_;
  SearchHints searchHints_{SearchHintsConfig()};
};

using MoveTestBase = MoveTestBaseT<>;
using MoveTestBaseWithTwoBinaryParams =
    MoveTestBaseT<::testing::TestWithParam<std::tuple<bool, bool>>>;

} // namespace facebook::rebalancer::packer::tests
