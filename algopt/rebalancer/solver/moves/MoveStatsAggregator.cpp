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

#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"

#include "algopt/rebalancer/solver/moves/EvalSummary.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/logging/xlog.h>

#include <iterator>

namespace facebook::rebalancer {

// Not thread-safe: main is not thread local.
template <typename T>
static void aggregate(
    PackerMap<T, MoveStats>& main,
    const PackerMap<T, MoveStats>& other) {
  for (const auto& [key, value] : other) {
    main[key].aggregate(value);
  }
}

/* static */
const std::string& MoveStats::typeName(Type type) {
  static const std::unordered_map<MoveStats::Type, std::string> typeNames = {
      {MoveStats::Type::INVALID, "invalid_move"},
      {MoveStats::Type::BETTER, "better_move"},
      {MoveStats::Type::NEUTRAL, "neutral_move"},
      {MoveStats::Type::WORSE, "worse_move"}};
  return typeNames.at(type);
}

MoveStats::MoveStats() : counters_{}, reasons_{}, numTimeouts_(0) {
  counters_.fill(0);
}

void MoveStats::clear() {
  counters_.fill(0);
  numPruned_ = 0;
  for (auto& x : reasons_) {
    x.clear();
  }
}

int64_t MoveStats::getNumTimeouts() const {
  return numTimeouts_;
}

int64_t MoveStats::getNumPruned() const {
  return numPruned_;
}

int64_t MoveStats::getTotalMoves() const {
  return counters_[TOTAL];
}

int64_t MoveStats::getInvalidMoves() const {
  return counters_[INVALID];
}

int64_t MoveStats::getBetterMoves() const {
  return counters_[BETTER];
}

int64_t MoveStats::getNeutralMoves() const {
  return counters_[NEUTRAL];
}

int64_t MoveStats::getWorseMoves() const {
  return counters_[WORSE];
}

const PackerMap<LabeledExpressionPtr, int64_t>&
MoveStats::getInvalidMoveReasons() const {
  return reasons_[INVALID];
}

const PackerMap<LabeledExpressionPtr, int64_t>&
MoveStats::getBetterMoveReasons() const {
  return reasons_[BETTER];
}

const PackerMap<LabeledExpressionPtr, int64_t>& MoveStats::getWorseMoveReasons()
    const {
  return reasons_[WORSE];
}

void MoveStats::incrNumTimeouts() {
  numTimeouts_ += 1;
}

void MoveStats::incrNumPruned() {
  numPruned_ += 1;
}

void MoveStats::bump(
    Type type,
    const LabeledExpressionPtr& expression,
    size_t moveSize) {
  counters_[TOTAL] += moveSize;
  counters_[type] += moveSize;
  if (expression) {
    reasons_[type][expression] += moveSize;
  }
}

void MoveStats::aggregate(const MoveStats& other) {
  numTimeouts_ += other.numTimeouts_;
  numPruned_ += other.numPruned_;
  for (const auto i : folly::irange(static_cast<int>(Type::COUNT))) {
    counters_[i] += other.counters_[i];
    for (const auto& [key, value] : other.reasons_[i]) {
      reasons_[i][key] += value;
    }
  }
}

MoveStatsAggregatorConfig::MoveStatsAggregatorConfig(
    bool trackContainers,
    bool trackObjects,
    bool showAllChangedObjectivesInMovesSummary,
    std::optional<PackerSet<entities::ObjectId>> trackObjectsWhitelist,
    bool printTrackedObjectStats,
    std::optional<PackerSet<entities::ContainerId>>
        printSourceContainersWhitelist)
    : trackContainers(trackContainers),
      trackObjects(trackObjects),
      showAllChangedObjectivesInMovesSummary(
          showAllChangedObjectivesInMovesSummary),
      trackObjectsWhitelist(std::move(trackObjectsWhitelist)),
      printTrackedObjectStats(printTrackedObjectStats),
      printSourceContainersWhitelist(
          std::move(printSourceContainersWhitelist)) {}

MoveStatsAggregator::MoveStatsAggregator(const Precision& precision)
    : MoveStatsAggregator(
          std::make_shared<MoveStatsAggregatorConfig>(false),
          precision) {}

MoveStatsAggregator::MoveStatsAggregator(
    std::shared_ptr<const MoveStatsAggregatorConfig> config,
    const Precision& precision)
    : config_(std::move(config)),
      precision_(precision),
      global_([](MoveStats& a, const MoveStats& b) { a.aggregate(b); }),
      sourceContainer_(
          ::facebook::rebalancer::aggregate<entities::ContainerId>),
      destinationContainer_(
          ::facebook::rebalancer::aggregate<entities::ContainerId>),
      object_(::facebook::rebalancer::aggregate<entities::ObjectId>) {}

MoveStatsAggregator& MoveStatsAggregator::setObjectNameProvider(
    std::function<std::string(entities::ObjectId)> func) {
  objectNameProvider_ = std::move(func);
  return *this;
}

MoveStatsAggregator& MoveStatsAggregator::setContainerNameProvider(
    std::function<std::string(entities::ContainerId)> func) {
  containerNameProvider_ = std::move(func);
  return *this;
}

void MoveStatsAggregator::add(const MoveResult& moveResult) {
  if (!moveResult.isValid()) {
    return bump(
        MoveStats::INVALID, moveResult.getFirstInvalidConstraint(), moveResult);
  }
  if (moveResult.isBetter(precision_)) {
    return bump(
        MoveStats::BETTER,
        moveResult.getSmallestDeltaObjective(precision_),
        moveResult);
  }
  if (moveResult.isNeutral(precision_)) {
    return bump(MoveStats::NEUTRAL, nullptr, moveResult);
  }
  if (!moveResult.isWorse(precision_)) [[unlikely]] {
    throw std::runtime_error(
        "MoveResult is neither invalid, better, neutral, or worse");
  }
  return bump(
      MoveStats::WORSE,
      moveResult.getLargestDeltaObjective(precision_),
      moveResult);
}

void MoveStatsAggregator::aggregate(const MoveStatsAggregator& other) {
  global_.getMaster().aggregate(other.global_.getMaster());
  ::facebook::rebalancer::aggregate(
      sourceContainer_.getMaster(), other.sourceContainer_.getMaster());
  ::facebook::rebalancer::aggregate(
      destinationContainer_.getMaster(),
      other.destinationContainer_.getMaster());
  ::facebook::rebalancer::aggregate(
      object_.getMaster(), other.object_.getMaster());
}

void MoveStatsAggregator::clear() {
  global_.clear();
  sourceContainer_.clear();
  destinationContainer_.clear();
  object_.clear();
}

const MoveStats& MoveStatsAggregator::getGlobalStats() const {
  return global_.getMaster();
}

const MoveStats& MoveStatsAggregator::getStatsForSourceContainer(
    entities::ContainerId container) const {
  return sourceContainer_.getMaster()[container];
}

const MoveStats& MoveStatsAggregator::getStatsForDestinationContainer(
    entities::ContainerId container) const {
  return destinationContainer_.getMaster()[container];
}

const MoveStats& MoveStatsAggregator::getStatsForObject(
    entities::ObjectId object) const {
  return object_.getMaster()[object];
}

void MoveStatsAggregator::incrNumPruned() {
  global_.getLocal().incrNumPruned();
}

void MoveStatsAggregator::incrNumTimeouts(const MoveResult& moveResult) {
  // track global number of timeouts during evaluations
  global_.getLocal().incrNumTimeouts();

  // track per container move evaluation timeouts if track containers is
  // requested
  if (!config_->trackContainers) {
    return;
  }
  for (auto& move : moveResult.getMoveSet()) {
    sourceContainer_.getLocal()[move.getSourceContainer()].incrNumTimeouts();
  }
}

void MoveStatsAggregator::bump(
    MoveStats::Type type,
    const LabeledExpressionPtr& expression,
    const MoveResult& moveResult) {
  global_.getLocal().bump(type, expression, moveResult.getMoveSet().size());

  // early exit if we are not tracking objects or containers
  if (!config_->trackContainers && !config_->trackObjects) {
    return;
  }

  for (auto& move : moveResult.getMoveSet()) {
    const auto object = !objectNameProvider_
        ? fmt::format("id({})", move.getObject())
        : (*objectNameProvider_)(move.getObject());
    const auto srcContainer = !containerNameProvider_
        ? fmt::format("id({})", move.getSourceContainer())
        : (*containerNameProvider_)(move.getSourceContainer());
    const auto dstContainer = !containerNameProvider_
        ? fmt::format("id({})", move.getDestinationContainer())
        : (*containerNameProvider_)(move.getDestinationContainer());

    if (config_->trackContainers) {
      const auto src = move.getSourceContainer();
      sourceContainer_.getLocal()[src].bump(type, expression);
      destinationContainer_.getLocal()[move.getDestinationContainer()].bump(
          type, expression);
      if (config_->printSourceContainersWhitelist &&
          config_->printSourceContainersWhitelist->count(src)) {
        fmt::memory_buffer buf;
        fmt::format_to(
            std::back_inserter(buf),
            "Tracking container {}\t{}: {} => {}\n",
            srcContainer,
            MoveStats::typeName(type),
            object,
            dstContainer);
        EvalSummary::populateSummary(
            buf,
            moveResult,
            {"tracking_container", object, srcContainer, dstContainer},
            precision_);
        XLOG(INFO) << fmt::to_string(buf);
      }
    }
    if (config_->trackObjects &&
        (!config_->trackObjectsWhitelist ||
         config_->trackObjectsWhitelist->count(move.getObject()) != 0)) {
      object_.getLocal()[move.getObject()].bump(type, expression);

      if (config_->printTrackedObjectStats) {
        fmt::memory_buffer buf;
        fmt::format_to(
            std::back_inserter(buf),
            "Tracking object {}\t{}: {} => {}\n",
            object,
            MoveStats::typeName(type),
            srcContainer,
            dstContainer);
        EvalSummary::populateSummary(
            buf,
            moveResult,
            {"tracking_object", object, srcContainer, dstContainer},
            precision_);
        XLOG(INFO) << fmt::to_string(buf);
      }
    }
  }
}

} // namespace facebook::rebalancer
