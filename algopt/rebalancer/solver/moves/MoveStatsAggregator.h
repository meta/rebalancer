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

#include "algopt/rebalancer/algopt_common/ThreadLocalWithReducer.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace facebook::rebalancer {

// Not thread-safe
class MoveStats {
 public:
  enum Type {
    TOTAL,
    INVALID,
    BETTER,
    NEUTRAL,
    WORSE,
    COUNT, // Must be last, do not use
  };
  static const std::string& typeName(Type type);

 public:
  MoveStats();

  int64_t getNumTimeouts() const;
  // Candidate moves skipped by the invalid move filter; not counted as moves.
  int64_t getNumPruned() const;
  int64_t getTotalMoves() const;
  int64_t getInvalidMoves() const;
  int64_t getBetterMoves() const;
  int64_t getNeutralMoves() const;
  int64_t getWorseMoves() const;
  const PackerMap<LabeledExpressionPtr, int64_t>& getInvalidMoveReasons() const;
  const PackerMap<LabeledExpressionPtr, int64_t>& getBetterMoveReasons() const;
  const PackerMap<LabeledExpressionPtr, int64_t>& getWorseMoveReasons() const;

  void
  bump(Type type, const LabeledExpressionPtr& expression, size_t moveSize = 1);
  void aggregate(const MoveStats& other);
  void incrNumTimeouts();
  void incrNumPruned();
  void clear();

 private:
  std::array<int64_t, Type::COUNT> counters_;
  std::array<PackerMap<LabeledExpressionPtr, int64_t>, Type::COUNT> reasons_;
  int64_t numTimeouts_ = 0;
  int64_t numPruned_ = 0;
};

struct MoveStatsAggregatorConfig {
  explicit MoveStatsAggregatorConfig(
      bool trackContainers,
      bool trackObjects = false,
      bool showAllChangedObjectivesInMovesSummary = false,
      std::optional<PackerSet<entities::ObjectId>> trackObjectsWhitelist =
          std::nullopt,
      bool printTrackedObjectStats = false,
      std::optional<PackerSet<entities::ContainerId>>
          printSourceContainersWhitelist = std::nullopt);
  bool trackContainers;
  bool trackObjects;
  bool showAllChangedObjectivesInMovesSummary;
  std::optional<PackerSet<entities::ObjectId>> trackObjectsWhitelist;
  bool printTrackedObjectStats;
  std::optional<PackerSet<entities::ContainerId>>
      printSourceContainersWhitelist;
};

// This class is used to aggregate move stats across multiple threads.
// It is assumed that you:
//  1. Create a MoveStatsAggregator object on a master thread.
//  2. Have many threads call `bump()` and `incrNumTimeouts()`.
//  3. Have those threads stop working.
//  4. Call `aggregate()` or `getXXX()` on the master thread for results.
//  5. Possibly use `clear()` to reset the state.
//
// ThreadLocalWithReducer will try to provide some runtime protection by
// checking that only the master thread calls `getMaster()`, but if other
// threads are still operating, expect disaster to ensue.
class MoveStatsAggregator {
 public:
  explicit MoveStatsAggregator(const Precision& precision);
  explicit MoveStatsAggregator(
      std::shared_ptr<const MoveStatsAggregatorConfig> config,
      const Precision& precision);

  /// @name Functions that should only be called by the master thread.
  /// @{
  MoveStatsAggregator& setObjectNameProvider(
      std::function<std::string(entities::ObjectId)> func);
  MoveStatsAggregator& setContainerNameProvider(
      std::function<std::string(entities::ContainerId)> func);

  void aggregate(const MoveStatsAggregator& other);
  void clear();

  const MoveStats& getGlobalStats() const;
  const MoveStats& getStatsForSourceContainer(
      entities::ContainerId container) const;
  const MoveStats& getStatsForDestinationContainer(
      entities::ContainerId container) const;
  const MoveStats& getStatsForObject(entities::ObjectId object) const;
  /// @}

  /// @name Functions that can be called by any thread. These are thread safe.
  /// @{
  void incrNumTimeouts(const MoveResult& moveResult);
  void incrNumPruned();
  void add(const MoveResult& moveResult);
  /// @}

 private:
  // Not thread safe
  /// @{
  std::shared_ptr<const MoveStatsAggregatorConfig> config_;
  const Precision& precision_;
  std::optional<std::function<std::string(entities::ContainerId)>>
      containerNameProvider_;
  std::optional<std::function<std::string(entities::ObjectId)>>
      objectNameProvider_;
  /// @}

  // Thread safe
  /// @{
  void bump(
      MoveStats::Type type,
      const LabeledExpressionPtr& expression,
      const MoveResult& moveResult);

  ThreadLocalWithReducer<MoveStats> global_;
  ThreadLocalWithReducer<PackerMap<entities::ContainerId, MoveStats>>
      sourceContainer_;
  ThreadLocalWithReducer<PackerMap<entities::ContainerId, MoveStats>>
      destinationContainer_;
  ThreadLocalWithReducer<PackerMap<entities::ObjectId, MoveStats>> object_;
  /// @}
};

} // namespace facebook::rebalancer
