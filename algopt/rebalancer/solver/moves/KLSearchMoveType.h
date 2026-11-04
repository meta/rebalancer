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

#include "algopt/rebalancer/solver/moves/AsyncDestinationsMoveType.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"

namespace facebook::rebalancer {

class Problem;

constexpr folly::StringPiece kKLSearchMoveTypeName = "KL_SEARCH";

/*
 * Motivated by Kernighan-Lin's Algorithm:
 * https://en.wikipedia.org/wiki/Kernighan%E2%80%93Lin_algorithm
 * For a given worst container, for all second containers:
 * Sequentially pick a move in either direction that has the best
 * objective change regardless of improvement or not, pick then
 * the point with the minimum objective.
 */
class KLSearchMoveType : public AsyncDestinationsMoveType {
 public:
  std::string name() const override;

  explicit KLSearchMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::KLSearchMoveTypeSpec& config);

  MoveResult exploreFromAllDestinations(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ContainerId coldContainer,
      MoveStatsAggregator& stats) override;

 private:
  static MoveResult exploreAllMovesFromContainer(
      const MovesEvaluator& evaluator,
      entities::ContainerId fromContainer,
      entities::ContainerId toContainer,
      const MoveSet& baseMoves,
      const PackerSet<entities::ObjectId>& objectsTried,
      MoveStatsAggregator& stats);
  // Objects in `sourceIds` that are not in `removeIds` and are not marked
  // invalid for `toContainer`.
  static std::vector<entities::ObjectId> filter(
      const ObjectStore& sourceIds,
      const PackerSet<entities::ObjectId>& removeIds,
      const InvalidMoveFilter* invalidMoveFilter,
      entities::ContainerId toContainer,
      MoveStatsAggregator& stats);

 public:
  interface::KLSearchMoveTypeSpec config_;
};

} // namespace facebook::rebalancer
