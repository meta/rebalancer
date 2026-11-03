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
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/Precision.h"

#include <boost/thread/barrier.hpp>
#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <folly/system/HardwareConcurrency.h>

#include <memory>
#include <thread>
#include <vector>

using namespace facebook::rebalancer;
using namespace facebook::rebalancer::packer::tests;

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

const int nThreads = folly::available_concurrency();
constexpr int nLoopsPerThread = 10000;
constexpr int numAggregations = 100;

template <typename Op>
void stress(Op op) {
  boost::barrier barrier(nThreads + 1);

  std::vector<std::thread> threads;
  threads.reserve(nThreads);
  for (const auto _ : folly::irange(nThreads)) {
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

BENCHMARK(MoveStatsAggregatorBenchmark) {
  const auto o1 = std::make_shared<const LabeledExpression>("o1", nullptr, 1);
  const auto precision = createPrecision();
  MoveStatsAggregator aggregator1(
      std::make_shared<MoveStatsAggregatorConfig>(true, true), precision);

  auto globalStatsOriginal = aggregator1.getGlobalStats();

  for (const auto _ : folly::irange(numAggregations)) {
    stress([&] {
      for (const auto _ : folly::irange(nLoopsPerThread)) {
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
  }
  globalStatsOriginal.aggregate(aggregator1.getGlobalStats());
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
