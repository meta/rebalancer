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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"

#include <boost/thread/barrier.hpp>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

using namespace std;

static constexpr size_t kNumStressThreads = 30;

namespace facebook::rebalancer::packer::tests {
namespace {

Precision createPrecision() {
  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}

LocalSearchObjectiveValue globalOnlyValue(double value) {
  const entities::Universe universe;
  GlobalObjective::Builder builder;
  builder.setObjective(0, const_expr(value, universe));
  return LocalSearchObjective(
             builder.build(universe), GlobalObjective(universe), universe)
      .getValue();
}
} // namespace

TEST(MoveStatsAggregatorTest, Empty) {
  const auto precision = createPrecision();
  const MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(true), precision);
  EXPECT_EQ(0, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getInvalidMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getBetterMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getWorseMoves());
  EXPECT_EQ(
      0, aggregator.getStatsForSourceContainer(container(100)).getTotalMoves());
  EXPECT_EQ(
      0,
      aggregator.getStatsForDestinationContainer(container(100))
          .getTotalMoves());
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, AddInvalid) {
  const auto precision = createPrecision();
  auto c1 = make_shared<LabeledExpression>("c1", nullptr, 1);
  auto c2 = make_shared<LabeledExpression>("c2", nullptr, 1);
  MoveSet moves;
  moves.insert(Move(object(1000), container(100), container(200)));
  auto result = MoveResult::makeInvalid(std::move(moves), {{c1, c2}});
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(true), precision);
  aggregator.add(result);
  EXPECT_EQ(1, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(1, aggregator.getGlobalStats().getInvalidMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getBetterMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getWorseMoves());
  auto source = aggregator.getStatsForSourceContainer(container(100));
  EXPECT_EQ(1, source.getTotalMoves());
  EXPECT_EQ(1, source.getInvalidMoves());
  EXPECT_EQ(1, source.getInvalidMoveReasons().at(c1));
  EXPECT_EQ(0, source.getInvalidMoveReasons().count(c2));
  auto destination = aggregator.getStatsForDestinationContainer(container(200));
  EXPECT_EQ(1, destination.getTotalMoves());
  EXPECT_EQ(1, destination.getInvalidMoves());
  EXPECT_EQ(1, destination.getInvalidMoveReasons().at(c1));
  EXPECT_EQ(0, destination.getInvalidMoveReasons().count(c2));
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, AddBetter) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  const auto o2 = make_shared<const LabeledExpression>("o2", nullptr, 1);
  MoveSet moves;
  moves.insert(Move(object(1000), container(100), container(200)));
  auto result = MoveResult::makeValid(
      std::move(moves),
      globalOnlyValue(10),
      globalOnlyValue(5),
      std::nullopt,
      {{{{o1, 5, 6}, {o2, 12, 6}}}});
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(true), precision);
  aggregator.add(result);
  EXPECT_EQ(1, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getInvalidMoves());
  EXPECT_EQ(1, aggregator.getGlobalStats().getBetterMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getWorseMoves());
  auto source = aggregator.getStatsForSourceContainer(container(100));
  EXPECT_EQ(1, source.getTotalMoves());
  EXPECT_EQ(1, source.getBetterMoves());
  EXPECT_EQ(1, source.getBetterMoveReasons().at(o2));
  EXPECT_EQ(0, source.getBetterMoveReasons().count(o1));
  auto destination = aggregator.getStatsForDestinationContainer(container(200));
  EXPECT_EQ(1, destination.getTotalMoves());
  EXPECT_EQ(1, destination.getBetterMoves());
  EXPECT_EQ(1, destination.getBetterMoveReasons().at(o2));
  EXPECT_EQ(0, destination.getBetterMoveReasons().count(o1));
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, AddNeutral) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  const auto o2 = make_shared<const LabeledExpression>("o2", nullptr, 1);
  MoveSet moves;
  moves.insert(Move(object(1000), container(100), container(200)));
  auto result = MoveResult::makeValid(
      std::move(moves),
      globalOnlyValue(10),
      globalOnlyValue(10),
      std::nullopt,
      {{{{o1, 5, 11}, {o2, 12, 6}}}});
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(true), precision);
  aggregator.add(result);
  EXPECT_EQ(1, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getInvalidMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getBetterMoves());
  EXPECT_EQ(1, aggregator.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getWorseMoves());
  auto source = aggregator.getStatsForSourceContainer(container(100));
  EXPECT_EQ(1, source.getTotalMoves());
  EXPECT_EQ(1, source.getNeutralMoves());
  auto destination = aggregator.getStatsForDestinationContainer(container(200));
  EXPECT_EQ(1, destination.getTotalMoves());
  EXPECT_EQ(1, destination.getNeutralMoves());
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, AddWorse) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  const auto o2 = make_shared<const LabeledExpression>("o2", nullptr, 1);
  MoveSet moves;
  moves.insert(Move(object(1000), container(100), container(200)));
  auto result = MoveResult::makeValid(
      std::move(moves),
      globalOnlyValue(10),
      globalOnlyValue(20),
      std::nullopt,
      {{{{o1, 5, 5}, {o2, 5, 15}}}});
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(true), precision);
  aggregator.add(result);
  EXPECT_EQ(1, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getInvalidMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getBetterMoves());
  EXPECT_EQ(0, aggregator.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(1, aggregator.getGlobalStats().getWorseMoves());
  auto source = aggregator.getStatsForSourceContainer(container(100));
  EXPECT_EQ(1, source.getTotalMoves());
  EXPECT_EQ(1, source.getWorseMoves());
  EXPECT_EQ(1, source.getWorseMoveReasons().at(o2));
  EXPECT_EQ(0, source.getWorseMoveReasons().count(o1));
  auto destination = aggregator.getStatsForDestinationContainer(container(200));
  EXPECT_EQ(1, destination.getTotalMoves());
  EXPECT_EQ(1, destination.getWorseMoves());
  EXPECT_EQ(1, destination.getWorseMoveReasons().at(o2));
  EXPECT_EQ(0, destination.getWorseMoveReasons().count(o1));
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, Aggregate) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  MoveStatsAggregator aggregator1(
      std::make_shared<MoveStatsAggregatorConfig>(true, true), precision);
  MoveSet moves;
  moves.insert(Move(object(1), container(33), container(34)));
  aggregator1.add(MoveResult::makeInvalid(moves));
  aggregator1.add(
      MoveResult::makeValid(
          moves,
          globalOnlyValue(10),
          globalOnlyValue(10))); // neutral
  aggregator1.add(
      MoveResult::makeValid(
          moves,
          globalOnlyValue(10),
          globalOnlyValue(15))); // worse
  MoveStatsAggregator aggregator2(
      std::make_shared<MoveStatsAggregatorConfig>(true, true), precision);
  aggregator2.add(MoveResult::makeInvalid(moves));
  aggregator2.add(MoveResult::makeInvalid(moves));
  MoveSet newMoves;
  newMoves.insert(Move(object(1000), container(100), container(200)));
  aggregator2.add(
      MoveResult::makeValid(
          std::move(newMoves),
          globalOnlyValue(10),
          globalOnlyValue(5),
          std::nullopt,
          {{{{o1, 20, 15}}}}));
  EXPECT_EQ(3, aggregator1.getGlobalStats().getTotalMoves());
  EXPECT_EQ(3, aggregator2.getGlobalStats().getTotalMoves());
  aggregator1.aggregate(aggregator2);
  EXPECT_EQ(6, aggregator1.getGlobalStats().getTotalMoves());
  EXPECT_EQ(3, aggregator1.getGlobalStats().getInvalidMoves());
  EXPECT_EQ(1, aggregator1.getGlobalStats().getBetterMoves());
  EXPECT_EQ(1, aggregator1.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(1, aggregator1.getGlobalStats().getWorseMoves());
  auto source = aggregator1.getStatsForSourceContainer(container(100));
  EXPECT_EQ(1, source.getTotalMoves());
  EXPECT_EQ(1, source.getBetterMoves());
  EXPECT_EQ(1, source.getBetterMoveReasons().at(o1));
  auto destination =
      aggregator1.getStatsForDestinationContainer(container(200));
  EXPECT_EQ(1, destination.getTotalMoves());
  EXPECT_EQ(1, destination.getBetterMoves());
  EXPECT_EQ(1, destination.getBetterMoveReasons().at(o1));
  auto objectStats = aggregator1.getStatsForObject(object(1000));
  EXPECT_EQ(1, objectStats.getTotalMoves());
  EXPECT_EQ(1, objectStats.getBetterMoves());
  EXPECT_EQ(1, objectStats.getBetterMoveReasons().at(o1));
}

TEST(MoveStatsAggregatorTest, NoContainerTracking) {
  const auto precision = createPrecision();
  MoveSet moves;
  moves.insert(Move(object(1000), container(100), container(200)));
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(false), precision);
  aggregator.add(
      MoveResult::makeValid(
          std::move(moves), globalOnlyValue(10), globalOnlyValue(5)));
  EXPECT_EQ(1, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(
      0, aggregator.getStatsForSourceContainer(container(100)).getTotalMoves());
  EXPECT_EQ(
      0,
      aggregator.getStatsForDestinationContainer(container(200))
          .getTotalMoves());
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, Clear) {
  const auto precision = createPrecision();
  MoveSet moves;
  moves.insert(Move(object(1000), container(100), container(200)));
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(true, true), precision);
  aggregator.add(
      MoveResult::makeValid(
          std::move(moves), globalOnlyValue(10), globalOnlyValue(10)));
  EXPECT_EQ(1, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(
      1, aggregator.getStatsForSourceContainer(container(100)).getTotalMoves());
  EXPECT_EQ(
      1,
      aggregator.getStatsForDestinationContainer(container(200))
          .getTotalMoves());
  EXPECT_EQ(1, aggregator.getStatsForObject(object(1000)).getTotalMoves());
  aggregator.clear();
  EXPECT_EQ(0, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(
      0, aggregator.getStatsForSourceContainer(container(100)).getTotalMoves());
  EXPECT_EQ(
      0,
      aggregator.getStatsForDestinationContainer(container(200))
          .getTotalMoves());
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, ObjectWhitelist) {
  const auto precision = createPrecision();
  PackerSet<entities::ObjectId> whitelist = {object(2000)};
  MoveStatsAggregator aggregator(
      std::make_shared<MoveStatsAggregatorConfig>(
          true, true, true, std::move(whitelist)),
      precision);

  MoveSet move1;
  move1.insert(Move(object(1000), container(100), container(200)));
  aggregator.add(
      MoveResult::makeValid(
          std::move(move1), globalOnlyValue(10), globalOnlyValue(10)));

  MoveSet move2;
  move2.insert(Move(object(2000), container(100), container(200)));
  aggregator.add(
      MoveResult::makeValid(
          std::move(move2), globalOnlyValue(10), globalOnlyValue(10)));

  EXPECT_EQ(2, aggregator.getGlobalStats().getTotalMoves());
  EXPECT_EQ(0, aggregator.getStatsForObject(object(1000)).getTotalMoves());
  EXPECT_EQ(1, aggregator.getStatsForObject(object(2000)).getTotalMoves());
}

TEST(MoveStatsAggregatorTest, OnlyMasterCanAggregate) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  MoveStatsAggregator aggregator1(
      std::make_shared<MoveStatsAggregatorConfig>(true, true), precision);
  MoveSet moves;
  moves.insert(Move(object(1), container(33), container(34)));
  aggregator1.add(MoveResult::makeInvalid(moves));

  std::thread t([&]() {
    REBALANCER_EXPECT_RUNTIME_ERROR(
        aggregator1.getGlobalStats(),
        "ThreadLocalWithReducer::getMaster() can only be called from the master thread!");
    REBALANCER_EXPECT_RUNTIME_ERROR(
        aggregator1.clear(),
        "ThreadLocalWithReducer::clear() can only be called from the master thread!");
  });

  t.join();
}

template <typename Op>
void stress(Op op) {
  boost::barrier barrier(kNumStressThreads + 1);

  std::vector<std::thread> threads;
  threads.reserve(kNumStressThreads);
  for (const auto _ : folly::irange(kNumStressThreads)) {
    threads.emplace_back([&]() {
      barrier.wait();
      op();
    });
  }

  // wait for the threads to be up and running
  barrier.wait();
  for (auto& t : threads) {
    t.join();
  }
}

TEST(MoveStatsAggregatorTest, ClobberAndAggregate) {
  const auto precision = createPrecision();
  const auto o1 = make_shared<const LabeledExpression>("o1", nullptr, 1);
  MoveStatsAggregator aggregator1(
      std::make_shared<MoveStatsAggregatorConfig>(true, true), precision);

  static constexpr size_t kNumLoops = 50;

  stress([&] {
    for (const auto _ : folly::irange(kNumLoops)) {
      MoveSet moves;
      moves.insert(Move(object(1), container(33), container(34)));
      aggregator1.add(MoveResult::makeInvalid(moves));
      aggregator1.add(
          MoveResult::makeValid(
              moves,
              globalOnlyValue(10),
              globalOnlyValue(10))); // neutral
      aggregator1.add(
          MoveResult::makeValid(
              moves,
              globalOnlyValue(10),
              globalOnlyValue(15))); // worse
    }
  });

  EXPECT_EQ(
      3 * kNumStressThreads * kNumLoops,
      aggregator1.getGlobalStats().getTotalMoves());
  EXPECT_EQ(
      kNumStressThreads * kNumLoops,
      aggregator1.getStatsForSourceContainer(container(33)).getInvalidMoves());
  EXPECT_EQ(
      kNumStressThreads * kNumLoops,
      aggregator1.getGlobalStats().getNeutralMoves());
  EXPECT_EQ(
      kNumStressThreads * kNumLoops,
      aggregator1.getGlobalStats().getWorseMoves());
}

} // namespace facebook::rebalancer::packer::tests
