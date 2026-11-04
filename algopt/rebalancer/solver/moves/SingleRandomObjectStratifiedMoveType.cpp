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

#include "algopt/rebalancer/solver/moves/SingleRandomObjectStratifiedMoveType.h"

#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/Util.h"

namespace facebook::rebalancer {

SingleRandomObjectStratifiedMoveType::SingleRandomObjectStratifiedMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleRandomObjectStratifiedMoveTypeSpec& spec)
    : MoveType(solverConfigs), spec_(spec) {}

std::string SingleRandomObjectStratifiedMoveType::name() const {
  return std::string(kSingleRandomObjectStratifiedMoveTypeName);
}

MoveResult SingleRandomObjectStratifiedMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  auto& problem = evaluator.getProblem();
  auto similarObjectsList = getObjectsToExplore(
      *spec_.objectsToExploreOptions(), hotContainer, problem, stats);

  const std::function<MoveResult(entities::ObjectId)>
      evaluateSampledObjectToHotContainer =
          [&evaluator, hotContainer, &stats](entities::ObjectId objectId) {
            MoveSet moves;
            moves.insert(
                Move(objectId, evaluator.getContainer(objectId), hotContainer));
            auto result = evaluator.evaluate(std::move(moves));
            stats.add(result);
            return result;
          };

  return MoveHelper::findBest(
      evaluator.getProblem().configs.threadPool.get(),
      getSampledSet(similarObjectsList),
      evaluateSampledObjectToHotContainer,
      timeLimit,
      parallelExecutionSelector(),
      name());
}

PackerSet<entities::ObjectId>
SingleRandomObjectStratifiedMoveType::getSampledSet(
    const ReferenceList<const std::vector<entities::ObjectId>>&
        similarObjectList) const {
  auto sampleSize = *spec_.stratifiedSampleSize()->defaultSampleSize();
  return getEqualSizeRandomSamplesFromEachSetIn(
      similarObjectList, sampleSize, rng_);
}

} // namespace facebook::rebalancer
