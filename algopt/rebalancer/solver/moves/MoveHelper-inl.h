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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/Timeout.h"
#include "algopt/rebalancer/solver/utils/ParallelExecution.h"

#include <functional>
#include <stdexcept>
#include <type_traits>

namespace facebook::rebalancer {

template <class Input, class InputCollection>
inline MoveResult MoveHelper::findBest(
    folly::ThreadPoolExecutor* const executor,
    const InputCollection& inputs,
    const std::function<MoveResult(Input)>& evaluate,
    const double timeout,
    ParallelExecutionSelector& executionSelector,
    const std::string_view moveTypeName) {
  const auto run = [&] {
    return execute(
        executor,
        inputs,
        evaluate,
        []() noexcept { return MoveResult::makeEmpty(); },
        [](MoveResult& acc, MoveResult&& result) noexcept {
          acc.aggregate(std::move(result));
        },
        timeout,
        executionSelector);
  };
  if (!executionSelector.isExploring()) {
    return run();
  }

  // Batching picks its single-batch fallback by input count, and one input can
  // run zero or many evaluations, so count inputs alongside the result.
  struct CountedResult {
    MoveResult result = MoveResult::makeEmpty();
    size_t inputs = 0;
  };
  struct AggregateCounted {
    void operator()(CountedResult& acc, MoveResult&& result) const noexcept {
      acc.result.aggregate(std::move(result));
      ++acc.inputs;
    }
    void operator()(CountedResult& acc, CountedResult&& other) const noexcept {
      acc.result.aggregate(std::move(other.result));
      acc.inputs += other.inputs;
    }
  };

  const algopt::Timer timer(true);
  auto counted = execute(
      executor,
      inputs,
      evaluate,
      []() noexcept { return CountedResult{}; },
      AggregateCounted{},
      timeout,
      executionSelector);
  const double durationSecs = timer.getSeconds();
  // A call that fits in one batch runs on a sliding window under both
  // strategies, so it cannot tell them apart.
  if (!fitsInOneBatch(counted.inputs, executionSelector.batchingOptions())) {
    executionSelector.record(
        {.evaluations = counted.result.getEvalsCount(),
         .durationSecs = durationSecs},
        moveTypeName);
  }
  return std::move(counted.result);
}

template <
    class Input,
    class InputCollection,
    class InitializeFn,
    class AggregateFn>
inline auto MoveHelper::execute(
    folly::ThreadPoolExecutor* const executor,
    const InputCollection& inputs,
    const std::function<MoveResult(Input)>& process,
    const InitializeFn& initialize,
    const AggregateFn& aggregate,
    const double timeout,
    const ParallelExecutionSelector& executionSelector)
    -> std::invoke_result_t<InitializeFn> {
  Timeout<InputCollection> timeoutInputs(inputs, timeout);
  timeoutInputs.start_timer();

  const auto executeSlidingWindow = [&]() {
    const int windowSize = static_cast<int>(10 + executor->numThreads());
    return executeParallelWindow(
        executor, timeoutInputs, process, initialize, aggregate, windowSize);
  };

  switch (executionSelector.strategy()) {
    case ParallelExecutionSelector::Strategy::SlidingWindow:
      return executeSlidingWindow();
    case ParallelExecutionSelector::Strategy::Batching:
      return executeParallelBatch(
          executor,
          timeoutInputs,
          process,
          initialize,
          aggregate,
          executionSelector.batchingOptions());
  }
  throw std::invalid_argument("Unknown parallel execution strategy");
}

template <typename RNG>
inline bool
MoveHelper::sampleWithProb(const int sampleSize, const int setSize, RNG& rng) {
  if (sampleSize > setSize) {
    return true;
  }
  const auto prob = static_cast<double>(sampleSize) / setSize;
  return folly::Random::randBool(prob, rng);
}

} // namespace facebook::rebalancer
