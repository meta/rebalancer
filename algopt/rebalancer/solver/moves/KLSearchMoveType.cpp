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

#include "algopt/rebalancer/solver/moves/KLSearchMoveType.h"

#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

#include <folly/logging/xlog.h>

namespace facebook::rebalancer {

KLSearchMoveType::KLSearchMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::KLSearchMoveTypeSpec& config)
    : AsyncDestinationsMoveType(solverConfigs), config_(config) {}

std::string KLSearchMoveType::name() const {
  return kKLSearchMoveTypeName.str();
}

MoveResult KLSearchMoveType::exploreFromAllDestinations(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ContainerId coldContainer,
    MoveStatsAggregator& stats) {
  auto bestResult = MoveResult::makeEmpty();

  MoveSet allMoves;
  PackerSet<entities::ObjectId> objectsTried;
  auto hotContainerSize = evaluator.getDynamicObjects(hotContainer).size();
  auto coldContainerSize = evaluator.getDynamicObjects(coldContainer).size();

  while (objectsTried.size() < coldContainerSize + hotContainerSize) {
    auto currentPassResults = MoveResult::makeEmpty();
    /*
     * We'll first go through moving any of the hot objects to cold container
     */
    currentPassResults.aggregate(exploreAllMovesFromContainer(
        evaluator, hotContainer, coldContainer, allMoves, objectsTried, stats));

    /*
     * Then, all cold objects to hot container
     */
    currentPassResults.aggregate(exploreAllMovesFromContainer(
        evaluator, coldContainer, hotContainer, allMoves, objectsTried, stats));

    if (currentPassResults.isEmpty()) {
      XLOG_EVERY_MS(DBG1, 10000) << "No object found to be moved";
      break;
    }

    allMoves = currentPassResults.getMoveSet();
    objectsTried.insert((allMoves.end() - 1)->getObject());

    bestResult.aggregate(std::move(currentPassResults));
  }
  return bestResult;
}

MoveResult KLSearchMoveType::exploreAllMovesFromContainer(
    const MovesEvaluator& evaluator,
    entities::ContainerId fromContainer,
    entities::ContainerId toContainer,
    const MoveSet& baseMoves,
    const PackerSet<entities::ObjectId>& objectsTried,
    MoveStatsAggregator& stats) {
  auto filteredObjs = filter(
      evaluator.getDynamicObjects(fromContainer),
      objectsTried,
      evaluator.getProblem().getInvalidMoveFilter(),
      toContainer,
      stats);
  const GenericObjectDeduper<std::vector<entities::ObjectId>::const_iterator>
      dedupedObjs(
          &evaluator.getProblem().getEquivalenceSets(),
          filteredObjs.begin(),
          filteredObjs.end());

  auto bestResult = MoveResult::makeEmpty();
  for (auto obj : dedupedObjs) {
    if (objectsTried.contains(obj)) {
      continue;
    }
    if (evaluator.getProblem().fixed_objects.contains(obj)) {
      continue;
    }

    MoveSet moves = baseMoves;
    moves.insert(Move(obj, fromContainer, toContainer));

    auto result = evaluator.evaluate(std::move(moves));
    stats.add(result);
    bestResult.aggregate(std::move(result));
  }
  return bestResult;
}

std::vector<entities::ObjectId> KLSearchMoveType::filter(
    const ObjectStore& sourceIds,
    const PackerSet<entities::ObjectId>& removeIds,
    const InvalidMoveFilter* invalidMoveFilter,
    entities::ContainerId toContainer,
    MoveStatsAggregator& stats) {
  std::vector<entities::ObjectId> resultIds;
  for (auto id : sourceIds) {
    if (!removeIds.contains(id) &&
        !isMoveInvalid(invalidMoveFilter, id, toContainer, stats)) {
      resultIds.push_back(id);
    }
  }
  return resultIds;
}

} // namespace facebook::rebalancer
