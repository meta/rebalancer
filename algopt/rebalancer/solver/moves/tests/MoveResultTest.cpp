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

#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"

#include <folly/container/irange.h>
#include <gtest/gtest.h>

#include <random>

using namespace std;

namespace facebook::rebalancer::packer::tests {
namespace {

Precision createPrecision() {
  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}
} // namespace

TEST(MoveResultTest, Basic) {
  const auto precision = createPrecision();
  auto result = MoveResult::makeEmpty();
  EXPECT_TRUE(result.isEmpty());
  MoveSet moveset1;
  moveset1.insert(Move(object(1000), container(100), container(200)));
  result.aggregate(
      MoveResult::makeValid(
          std::move(moveset1),
          GlobalObjectiveValue({1}),
          GlobalObjectiveValue({2})));
  MoveSet moveset2;
  moveset2.insert(Move(object(1000), container(200), container(300)));
  result.aggregate(
      MoveResult::makeValid(
          std::move(moveset2),
          GlobalObjectiveValue({1}),
          GlobalObjectiveValue({-1})));
  result.aggregate(MoveResult::makeInvalid({}));
  EXPECT_FALSE(result.isEmpty());
  EXPECT_TRUE(result.isValid());
  EXPECT_TRUE(result.isBetter(precision));
  EXPECT_FALSE(result.isNeutral(precision));
  EXPECT_FALSE(result.isWorse(precision));
  EXPECT_EQ(-1, result.getValue().get(0).objective);
  ASSERT_EQ(1, result.getMoveSet().size());
  EXPECT_EQ(object(1000), result.getMoveSet().at(0).getObject());
  EXPECT_EQ(container(200), result.getMoveSet().at(0).getSourceContainer());
  EXPECT_EQ(
      container(300), result.getMoveSet().at(0).getDestinationContainer());
  EXPECT_EQ(3, result.getEvalsCount());
  EXPECT_FALSE(result.hasObjectiveDeltas());
  EXPECT_EQ(nullptr, result.getSmallestDeltaObjective(precision));
  EXPECT_EQ(nullptr, result.getLargestDeltaObjective(precision));
  EXPECT_EQ(nullptr, result.getFirstInvalidConstraint());
}

TEST(MoveResultTest, BasicArbiter) {
  const auto precision = createPrecision();
  // result is better, tie break using arbiters
  {
    auto result = MoveResult::makeEmpty();
    EXPECT_TRUE(result.isEmpty());

    MoveSet moveset1;
    moveset1.insert(Move(object(1000), container(100), container(200)));
    result.aggregate(
        MoveResult::makeValid(
            std::move(moveset1),
            GlobalObjectiveValue({1}),
            GlobalObjectiveValue({-1}),
            GlobalObjectiveValue({1})));
    MoveSet moveset2;
    moveset2.insert(Move(object(1000), container(200), container(300)));
    result.aggregate(
        MoveResult::makeValid(
            std::move(moveset2),
            GlobalObjectiveValue({1}),
            GlobalObjectiveValue({-1}),
            GlobalObjectiveValue({-1})));
    EXPECT_FALSE(result.isEmpty());
    EXPECT_TRUE(result.isValid());
    EXPECT_TRUE(result.isBetter(precision));
    EXPECT_FALSE(result.isNeutral(precision));
    EXPECT_FALSE(result.isWorse(precision));
    // aggregate should have picked the second moveset due to a better arbiter,
    // without arbiters it would report first moveset since
    EXPECT_EQ(object(1000), result.getMoveSet().at(0).getObject());
    EXPECT_EQ(container(200), result.getMoveSet().at(0).getSourceContainer());
    EXPECT_EQ(
        container(300), result.getMoveSet().at(0).getDestinationContainer());
  }

  // result is neutral, arbiters used in aggregation but do not make result
  // better
  {
    auto result = MoveResult::makeEmpty();
    EXPECT_TRUE(result.isEmpty());

    MoveSet moveset1;
    moveset1.insert(Move(object(1000), container(100), container(200)));
    result.aggregate(
        MoveResult::makeValid(
            std::move(moveset1),
            GlobalObjectiveValue({1}),
            GlobalObjectiveValue({1}),
            GlobalObjectiveValue({1})));
    MoveSet moveset2;
    moveset2.insert(Move(object(1000), container(200), container(300)));
    result.aggregate(
        MoveResult::makeValid(
            std::move(moveset2),
            GlobalObjectiveValue({1}),
            GlobalObjectiveValue({1}),
            GlobalObjectiveValue({-1})));
    EXPECT_FALSE(result.isEmpty());
    EXPECT_TRUE(result.isValid());
    EXPECT_FALSE(result.isBetter(precision));
    EXPECT_TRUE(result.isNeutral(precision));
    EXPECT_FALSE(result.isWorse(precision));
    // though it was a neutral result, aggregate should have picked the first
    // moveset
    EXPECT_EQ(object(1000), result.getMoveSet().at(0).getObject());
    EXPECT_EQ(container(200), result.getMoveSet().at(0).getSourceContainer());
    EXPECT_EQ(
        container(300), result.getMoveSet().at(0).getDestinationContainer());
  }
}

TEST(MoveResultTest, ValidResultExpressionGetters) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  const auto o2 = make_shared<const LabeledExpression>("o2", nullptr, 1);
  auto result = MoveResult::makeValid(
      {},
      GlobalObjectiveValue({10}),
      GlobalObjectiveValue({21}),
      std::nullopt,
      {{{{o1, 5, 6}, {o2, 5, 15}}}});
  EXPECT_TRUE(result.isValid());
  EXPECT_TRUE(result.isWorse(precision));
  EXPECT_EQ(o1, result.getSmallestDeltaObjective(precision));
  EXPECT_EQ(o2, result.getLargestDeltaObjective(precision));
  EXPECT_EQ(nullptr, result.getFirstInvalidConstraint());
}

TEST(MoveResultTest, InvalidResultExpressionGetters) {
  const auto precision = createPrecision();
  auto c1 = make_shared<LabeledExpression>("c1", nullptr, 1);
  auto c2 = make_shared<LabeledExpression>("c2", nullptr, 1);
  auto result = MoveResult::makeInvalid({}, {{c1, c2}});
  EXPECT_FALSE(result.isValid());
  EXPECT_EQ(nullptr, result.getSmallestDeltaObjective(precision));
  EXPECT_EQ(nullptr, result.getLargestDeltaObjective(precision));
  EXPECT_EQ(c1, result.getFirstInvalidConstraint());
}

TEST(
    MoveResultTest,
    AggregateMoveResultsWithSameGlobalValueAndSameArbiterValue) {
  {
    MoveSet moveset1;
    moveset1.insert(Move(object(1), container(2), container(3)));

    MoveSet moveset2;
    moveset2.insert(Move(object(1), container(2), container(3)));
    auto result1 = MoveResult::makeValid(
        std::move(moveset1),
        GlobalObjectiveValue({1}),
        GlobalObjectiveValue({1}));
    auto result2 = MoveResult::makeValid(
        std::move(moveset2),
        GlobalObjectiveValue({1}),
        GlobalObjectiveValue({1}));
    result1.aggregate(std::move(result2));

    EXPECT_EQ(1, result1.getMoveSet().size());
    EXPECT_EQ(2, result1.getEvalsCount());
    EXPECT_EQ(1, result1.getValue().get(0).objective);
    EXPECT_EQ(object(1), result1.getMoveSet().at(0).getObject());
    EXPECT_EQ(container(2), result1.getMoveSet().at(0).getSourceContainer());
    EXPECT_EQ(
        container(3), result1.getMoveSet().at(0).getDestinationContainer());
  }

  {
    // When both results have same global value and arbiter value, the moveset
    // with the least number of moves should be chosen
    MoveSet moveset1;
    moveset1.insert(Move(object(1), container(2), container(3)));
    moveset1.insert(Move(object(2), container(3), container(4)));

    MoveSet moveset2;
    moveset2.insert(Move(object(1), container(2), container(3)));

    MoveSet moveset3;
    moveset3.insert(Move(object(1), container(2), container(3)));
    moveset3.insert(Move(object(2), container(3), container(4)));
    moveset3.insert(Move(object(3), container(4), container(5)));

    auto result1 = MoveResult::makeValid(
        std::move(moveset1),
        GlobalObjectiveValue({1}),
        GlobalObjectiveValue({1}));
    auto result2 = MoveResult::makeValid(
        std::move(moveset2),
        GlobalObjectiveValue({1}),
        GlobalObjectiveValue({1}));
    auto result3 = MoveResult::makeValid(
        std::move(moveset3),
        GlobalObjectiveValue({1}),
        GlobalObjectiveValue({1}));

    result1.aggregate(std::move(result2));

    EXPECT_EQ(1, result1.getMoveSet().size());
    EXPECT_EQ(2, result1.getEvalsCount());
    EXPECT_EQ(1, result1.getValue().get(0).objective);
    EXPECT_EQ(object(1), result1.getMoveSet().at(0).getObject());
    EXPECT_EQ(container(2), result1.getMoveSet().at(0).getSourceContainer());
    EXPECT_EQ(
        container(3), result1.getMoveSet().at(0).getDestinationContainer());

    result1.aggregate(std::move(result3));
    EXPECT_EQ(1, result1.getMoveSet().size());
    EXPECT_EQ(3, result1.getEvalsCount());
    EXPECT_EQ(1, result1.getValue().get(0).objective);
    EXPECT_EQ(object(1), result1.getMoveSet().at(0).getObject());
    EXPECT_EQ(container(2), result1.getMoveSet().at(0).getSourceContainer());
    EXPECT_EQ(
        container(3), result1.getMoveSet().at(0).getDestinationContainer());
  }

  {
    // When both results have same global value and arbiter value and the
    // moveset has the same number of moves, the moveset with the smallest hash
    // value should be chosen
    vector<MoveResult> results;
    for (const auto i : folly::irange(1000)) {
      MoveSet moveset;
      moveset.insert(Move(object(i), container(i + 1), container(i + 2)));
      results.push_back(
          MoveResult::makeValid(
              std::move(moveset),
              GlobalObjectiveValue({1}),
              GlobalObjectiveValue({1})));
    }
    std::shuffle(
        results.begin(), results.end(), std::mt19937(std::random_device()()));
    for (const auto i : folly::irange(1, 1000)) {
      results[0].aggregate(std::move(results[i]));
    }

    // The final moveset is the one with the smallest hash value
    EXPECT_EQ(1, results[0].getMoveSet().size());
    EXPECT_EQ(1000, results[0].getEvalsCount());
    EXPECT_EQ(1, results[0].getValue().get(0).objective);

    // Verify the winner has the minimum hash across all candidates
    const auto& winnerMove = results[0].getMoveSet().at(0);
    for (const auto i : folly::irange(1000)) {
      Move candidate(object(i), container(i + 1), container(i + 2));
      EXPECT_LE(winnerMove.getHashValue(), candidate.getHashValue());
    }
  }
}

TEST(MoveResultTest, ToString) {
  entities::tests::UniverseBuilderTestUtils builder;
  builder.setInitialAssignment(
      {{"container1", {"object1"}}, {"container2", {"object2"}}});

  auto obj1 = builder.object("object1");
  auto container1 = builder.container("container1");
  auto container2 = builder.container("container2");

  // Create a MoveResult with a move
  MoveSet moveset;
  moveset.insert(Move(obj1, container1, container2));
  auto result = MoveResult::makeValid(
      std::move(moveset),
      GlobalObjectiveValue({10}),
      GlobalObjectiveValue({5}));

  // Test toString method
  const auto universe = builder.buildUniverse();
  const std::string resultString = result.toString(*universe);
  EXPECT_FALSE(resultString.empty());
  EXPECT_NE(resultString.find("Objective:"), std::string::npos);
  EXPECT_NE(resultString.find("eval count:"), std::string::npos);
  EXPECT_NE(resultString.find("MoveSet"), std::string::npos);
}

} // namespace facebook::rebalancer::packer::tests
