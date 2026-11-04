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

#include "algopt/rebalancer/solver/moves/ReplicaDropMoveType.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/CartesianProduct.h"
#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

ReplicaDropMoveType::ReplicaDropMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::ReplicaDropMoveTypeSpec& spec)
    : MoveType(solverConfigs) {
  if (spec.replicaDropPartition()->empty()) {
    throw std::runtime_error("missing replica drop partition");
  }
  partition_ = *spec.replicaDropPartition();

  if (spec.replicaDropScope()->empty()) {
    throw std::runtime_error("missing replica drop scope");
  }
  scope_ = *spec.replicaDropScope();
}

std::string ReplicaDropMoveType::name() const {
  return kReplicaDropMoveTypeName.str();
}

MoveResult ReplicaDropMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    [[maybe_unused]] const SearchHints& hints,
    double timeLimit) {
  auto& problem = evaluator.getProblem();

  const ObjectDeduper hotObjectIds(
      &problem.getEquivalenceSets(), evaluator.getDynamicObjects(hotContainer));

  const auto& precision = problem.getUniverse().getPrecision();

  const std::function<MoveResult(
      std::pair<entities::ObjectId, entities::ContainerId>)>
      evaluate = [&evaluator, &stats, &problem](auto singleMove) {
        auto [hotObjectId, coldContainerId] = singleMove;
        auto hotContainerId = problem.assignment.getContainer(hotObjectId);

        MoveSet moveset;
        moveset.insert(Move(hotObjectId, hotContainerId, coldContainerId));

        auto result = evaluator.evaluate(std::move(moveset));
        stats.add(result);
        return result;
      };

  const auto* const filter = problem.getInvalidMoveFilter();
  auto bestResult = MoveResult::makeEmpty();
  const algopt::Timer timer(true);
  for (auto hotObjectId : hotObjectIds) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      break;
    }
    auto groupIdOpt = problem.getOnlyGroupIdIfExists(partition_, hotObjectId);
    if (!groupIdOpt) {
      // This object does not belong to any group.
      continue;
    }

    auto groupId = *groupIdOpt;

    // Obtain objects of the same replica group and all the containers not in
    // replicaDropScope
    auto& replicaIds = problem.getObjectIdsForGroup(partition_, groupId);
    auto& outOfScopeContainerIds = problem.getOutOfScopeContainerIds(scope_);

    // Discard fixed replicas and ones that are already out of scope.
    auto dynamicReplicaIds =
        Filter(replicaIds, [&problem, &outOfScopeContainerIds](auto replicaId) {
          const bool isOutOfScope = outOfScopeContainerIds.contains(
              problem.assignment.getContainer(replicaId));
          return !isOutOfScope && problem.assignment.isDynamic(replicaId);
        });
    // Filter out any outOfScopeContainer that is non_accepting
    auto acceptingOutOfScopeContainerIds =
        Filter(outOfScopeContainerIds, [&problem](auto containerId) {
          return !problem.not_accepting_containers.contains(containerId);
        });

    auto singleMoves =
        CartesianProduct(dynamicReplicaIds, acceptingOutOfScopeContainerIds);

    auto result = MoveHelper::findBest(
        problem.configs.threadPool.get(),
        Filter(
            singleMoves,
            [filter, &stats](const auto& singleMove) {
              return !isMoveInvalid(
                  filter, singleMove.first, singleMove.second, stats);
            }),
        evaluate,
        timeLimit - timer.getSeconds(),
        parallelExecutionSelector(),
        name());

    if (result.isBetter(precision)) {
      return result;
    }
  }

  return bestResult;
}

} // namespace facebook::rebalancer
