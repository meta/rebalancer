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

namespace facebook::rebalancer {

class Problem;

constexpr folly::StringPiece kSwapFullContainersTypeName =
    "SWAP_FULL_CONTAINERS";

class SwapFullContainersMoveType : public AsyncDestinationsMoveType {
 public:
  std::string name() const override;

  explicit SwapFullContainersMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SwapFullContainersMoveTypeSpec& spec);
  static bool canSwapObjects(
      const MovesEvaluator& evaluator,
      entities::ContainerId hot_container,
      entities::ContainerId cold_container,
      MoveStatsAggregator& stats);
  static bool isContainerEmpty(
      const MovesEvaluator& evaluator,
      entities::ContainerId container);
  static void insertAllMovesFromSrcToDest(
      const MovesEvaluator& evaluator,
      entities::ContainerId src_container,
      entities::ContainerId dest_container,
      MoveSet& moves);
  MoveResult exploreFromAllDestinations(
      const MovesEvaluator& evaluator,
      entities::ContainerId hot_container,
      entities::ContainerId cold_container,
      MoveStatsAggregator& stats) override;

 private:
  interface::SwapFullContainersMoveTypeSpec spec_;
};
} // namespace facebook::rebalancer
