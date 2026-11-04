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

#include "algopt/rebalancer/solver/moves/SingleChainFastMoveType.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

namespace {
interface::SingleChainMoveTypeSpec toSingleChainMoveTypeSpec(
    const interface::SingleChainFastMoveTypeSpec& spec) {
  interface::SingleChainMoveTypeSpec baseSpec;
  baseSpec.partitionNameToExploreChainsWithinObjectGroup().copy_from(
      spec.partitionNameToExploreFastChainsWithinObjectGroup());
  baseSpec.specialColdContainer().copy_from(spec.specialFastColdContainer());
  return baseSpec;
}
} // namespace

std::string SingleChainFastMoveType::name() const {
  return kSingleChainFastMoveTypeName.str();
}

SingleChainFastMoveType::SingleChainFastMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleChainFastMoveTypeSpec& spec)
    : SingleChainMoveType(solverConfigs, toSingleChainMoveTypeSpec(spec)) {}

MoveResult SingleChainFastMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  auto& dynamicObjects = evaluator.getDynamicObjects(hotContainer);
  auto& problem = evaluator.getProblem();
  const ObjectDeduper dedupedObjs(
      &problem.getEquivalenceSets(), dynamicObjects);

  const auto customColdContainers =
      getCustomColdContainers(evaluator, hotContainer);
  auto coldContainers = Filter(
      customColdContainers ? *customColdContainers : problem.containers,
      [&problem, hotContainer](entities::ContainerId container) {
        return container != hotContainer &&
            !problem.not_accepting_containers.contains(container);
      });

  const auto* const filter = problem.getInvalidMoveFilter();
  auto bestResult = MoveResult::makeEmpty();
  const auto& precision = problem.getUniverse().getPrecision();

  const algopt::Timer timer(true);
  for (const auto hotObject : dedupedObjs) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      break;
    }
    const std::function<MoveResult(entities::ContainerId)> evaluate =
        [this, &evaluator, &stats, hotContainer, hotObject](
            auto coldContainer) {
          return exploreChainsWithHotObjectToColdContainer(
              evaluator,
              hotContainer,
              hotObject,
              coldContainer,
              stats,
              true /* greedy */);
        };

    auto result = MoveHelper::findBest(
        problem.configs.threadPool.get(),
        Filter(
            coldContainers,
            [filter, hotObject, &stats](entities::ContainerId coldContainer) {
              return !isMoveInvalid(filter, hotObject, coldContainer, stats);
            }),
        evaluate,
        timeLimit - timer.getSeconds(),
        parallelExecutionSelector(),
        name());
    bestResult.aggregate(std::move(result));
    if (bestResult.isBetter(precision)) {
      break;
    }
  }
  return bestResult;
}

} // namespace facebook::rebalancer
