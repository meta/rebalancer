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

#include "algopt/rebalancer/solver/moves/AsyncSingleMovesMoveType.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"

namespace facebook::rebalancer {

class Problem;

constexpr folly::StringPiece kSwapMoveTypeName = "SWAP";

class SwapMoveType : public AsyncSingleMovesMoveType {
 public:
  std::string name() const override;

  explicit SwapMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::SwapMoveTypeSpec& config);

  MoveResult exploreFromAllSingleMoves(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      entities::ContainerId coldContainer,
      MoveStatsAggregator& stats) override;

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;

  MoveResult findBestMoveWithBundleOptions(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit,
      const interface::ObjectsToExploreOptions& bundleOptions);

  // some common specializations of this move type config
  static void makeGreedy(
      interface::SwapMoveTypeSpec& spec,
      bool greedyOnSrc,
      bool greedyOnDest);
  static void makeFixedDest(
      interface::SwapMoveTypeSpec& spec,
      const std::string& containerScopeName,
      const std::string& specialContainerName);

  static void makeSampled(interface::SwapMoveTypeSpec& spec, int sampleSize);

 protected:
  MoveResult exploreSwappingHotObjectWithObjectsInColdContainer(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      entities::ContainerId coldContainer,
      MoveStatsAggregator& stats,
      bool shouldParallelizeWithinColdContainer = false,
      std::optional<double> timeLimit = std::nullopt);

  bool attemptMoveWithThisObject(
      const MovesEvaluator& evaluator,
      entities::ObjectId objectId,
      int numObjectsInContainer) const override;

 private:
  MoveResult exploreAllAndGetBestResult(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      entities::ObjectId hotObject,
      entities::ContainerId coldContainer,
      const ObjectStore& dynamicObjects,
      const EquivSetToObjects& equivSetToColdObjects,
      MoveStatsAggregator& stats,
      bool shouldParallelizeWithinColdContainer,
      std::optional<double> timeLimit);

  virtual std::optional<PackerSet<entities::ContainerId>>
  getCustomColdContainers(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer) const override;

  folly::F14FastSet<
      std::tuple<ObjectBundle, ObjectBundle, entities::ContainerId>>
  getBundleMoveCandidates(
      entities::ContainerId srcContainerId,
      entities::ContainerId dstContainerId,
      const MovesEvaluator& evaluator,
      const interface::ObjectsToExploreOptions& bundleOptions);

  bool getSamplingStatus(int numObjects) const;

 public:
  interface::SwapMoveTypeSpec config_;
};

} // namespace facebook::rebalancer
