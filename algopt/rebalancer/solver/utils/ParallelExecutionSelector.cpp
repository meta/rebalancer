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

#include "algopt/rebalancer/solver/utils/ParallelExecutionSelector.h"

#include <fmt/core.h>
#include <folly/logging/xlog.h>

#include <array>
#include <stdexcept>

namespace facebook::rebalancer {

namespace {

using Strategy = ParallelExecutionSelector::Strategy;

// Throughput climbs over a move type's first calls, so each strategy takes one
// early and one late position in every round, and one round is not enough.
constexpr std::array<Strategy, 4> kAbbaRound = {
    Strategy::SlidingWindow,
    Strategy::Batching,
    Strategy::Batching,
    Strategy::SlidingWindow,
};
constexpr size_t kExperimentRounds = 6;
constexpr size_t kExperimentSamples = kExperimentRounds * kAbbaRound.size();

constexpr std::string_view strategyName(const Strategy strategy) {
  switch (strategy) {
    case Strategy::SlidingWindow:
      return "sliding_window";
    case Strategy::Batching:
      return "batching";
  }
  throw std::invalid_argument("Unknown parallel execution strategy");
}

BatchingExecutionOptions makeBatchingOptions(
    const interface::BatchingExecutionConfig& config) {
  BatchingExecutionOptions options;
  if (*config.batchSize() != 0) {
    options.batchSize = static_cast<size_t>(*config.batchSize());
  }
  if (*config.maxConcurrency() > 0) {
    options.maxConcurrency = static_cast<size_t>(*config.maxConcurrency());
  }
  return options;
}

} // namespace

double ParallelExecutionSelector::Measurements::evaluationsPerSecond() const {
  // record() only counts calls with evaluations and positive duration, and an
  // ignored call keeps the current strategy, so both strategies are measured
  // before AUTO locks.
  if (evaluations <= 0 || durationSecs <= 0) {
    throw std::logic_error(
        fmt::format(
            "AUTO parallel execution compared an unmeasured strategy: evaluations={}, durationSecs={}",
            evaluations,
            durationSecs));
  }
  return static_cast<double>(evaluations) / durationSecs;
}

ParallelExecutionSelector::Measurements&
ParallelExecutionSelector::Experiment::measurements(const Strategy strategy) {
  switch (strategy) {
    case Strategy::SlidingWindow:
      return slidingWindow;
    case Strategy::Batching:
      return batching;
  }
  throw std::invalid_argument("Unknown parallel execution strategy");
}

ParallelExecutionSelector::ParallelExecutionSelector(
    const std::optional<interface::ParallelExecutionConfig>& config) {
  if (!config) {
    return;
  }

  switch (config->getType()) {
    case interface::ParallelExecutionConfig::Type::slidingWindow:
      return;
    case interface::ParallelExecutionConfig::Type::batching:
      strategy_ = Strategy::Batching;
      batchingOptions_ = makeBatchingOptions(config->batching().value());
      return;
    case interface::ParallelExecutionConfig::Type::autoExecution:
      strategy_ = kAbbaRound.front();
      batchingOptions_ =
          makeBatchingOptions(*config->autoExecution()->batching());
      experiment_ = Experiment{};
      return;
    case interface::ParallelExecutionConfig::Type::__EMPTY__:
      throw std::invalid_argument("Parallel execution config cannot be empty");
  }
  throw std::invalid_argument("Unknown parallel execution config type");
}

void ParallelExecutionSelector::record(
    const Observation& observation,
    const std::string_view moveTypeName) {
  if (std::this_thread::get_id() != ownerThread_) {
    throw std::logic_error(
        fmt::format(
            "AUTO parallel execution recorded off the thread that built the selector: moveType={}",
            moveTypeName));
  }
  if (!experiment_ || observation.evaluations <= 0 ||
      observation.durationSecs <= 0) {
    return;
  }

  auto& experiment = *experiment_;
  auto& measurements = experiment.measurements(strategy_);
  measurements.evaluations += observation.evaluations;
  measurements.durationSecs += observation.durationSecs;
  ++experiment.samples;
  XLOGF(
      DBG2,
      "AUTO parallel execution sample: moveType={}, strategy={}, evaluations={}, durationSecs={:.6f}",
      moveTypeName,
      strategyName(strategy_),
      observation.evaluations,
      observation.durationSecs);

  if (experiment.samples < kExperimentSamples) {
    strategy_ = kAbbaRound.at(experiment.samples % kAbbaRound.size());
    return;
  }

  const double slidingWindowPerSecond =
      experiment.slidingWindow.evaluationsPerSecond();
  const double batchingPerSecond = experiment.batching.evaluationsPerSecond();
  strategy_ = batchingPerSecond > slidingWindowPerSecond
      ? Strategy::Batching
      : Strategy::SlidingWindow;
  XLOGF(
      INFO,
      "AUTO parallel execution locked: moveType={}, strategy={}, slidingWindowEvalsPerSec={:.0f}, batchingEvalsPerSec={:.0f}",
      moveTypeName,
      strategyName(strategy_),
      slidingWindowPerSecond,
      batchingPerSecond);
  experiment_.reset();
}

} // namespace facebook::rebalancer
