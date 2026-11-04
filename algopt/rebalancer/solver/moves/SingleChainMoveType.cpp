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

#include "algopt/rebalancer/solver/moves/SingleChainMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

SingleChainMoveType::SingleChainMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleChainMoveTypeSpec& spec)
    : AsyncSingleMovesMoveType(solverConfigs) {
  partitionNameToExploreChainsWithinObjectGroup_ =
      spec.partitionNameToExploreChainsWithinObjectGroup().to_optional();
  specialColdContainer_ = spec.specialColdContainer().to_optional();
}

std::string SingleChainMoveType::name() const {
  return kSingleChainMoveTypeName.str();
}

MoveResult SingleChainMoveType::exploreFromAllSingleMoves(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    MoveStatsAggregator& stats) {
  return exploreChainsWithHotObjectToColdContainer(
      evaluator, hotContainer, hotObject, coldContainer, stats, false);
}

MoveResult SingleChainMoveType::exploreChainsWithHotObjectToColdContainer(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    MoveStatsAggregator& stats,
    bool greedy) {
  const auto& precision = evaluator.getProblem().getUniverse().getPrecision();
  auto bestResult = MoveResult::makeEmpty();
  const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();
  if (isMoveInvalid(filter, hotObject, coldContainer, stats)) {
    return bestResult;
  }
  for (auto otherContainer : evaluator.getContainers()) {
    if (otherContainer == hotContainer || otherContainer == coldContainer) {
      continue;
    }

    if (getPartitionNameToExploreChainsWithinObjectGroup().has_value()) {
      bestResult.aggregate(exploreWithinGroupAndGetBestResult(
          evaluator,
          hotContainer,
          hotObject,
          coldContainer,
          otherContainer,
          *getPartitionNameToExploreChainsWithinObjectGroup(),
          stats,
          greedy));
    } else {
      bestResult.aggregate(exploreAllAndGetBestResult(
          evaluator,
          hotContainer,
          hotObject,
          coldContainer,
          otherContainer,
          stats,
          greedy));
    }
    if (greedy && bestResult.isBetter(precision)) {
      break;
    }
  }

  return bestResult;
}

MoveResult SingleChainMoveType::exploreWithinGroupAndGetBestResult(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    entities::ContainerId otherContainer,
    const std::string& partitionName,
    MoveStatsAggregator& stats,
    bool greedy) {
  const auto& precision = evaluator.getProblem().getUniverse().getPrecision();
  auto bestResult = MoveResult::makeEmpty();
  auto& problem = evaluator.getProblem();
  auto groupIdOpt = problem.getOnlyGroupIdIfExists(partitionName, hotObject);
  if (!groupIdOpt.has_value()) {
    // nothing to evaluate
    return bestResult;
  }
  auto& objectsInGroup =
      problem.getObjectIdsForGroup(partitionName, *groupIdOpt);

  // only objects that are in 'otherContainer' and can move into
  // 'hotContainer' are relevant
  const auto* const filter = problem.getInvalidMoveFilter();
  PackerSet<entities::ObjectId> relevantObjects;
  for (auto object : objectsInGroup) {
    if (problem.assignment.getContainer(object) == otherContainer &&
        !isMoveInvalid(filter, object, hotContainer, stats)) {
      relevantObjects.insert(object);
    }
  }

  const GenericObjectDeduper otherObjects(
      &problem.getEquivalenceSets(),
      relevantObjects.begin(),
      relevantObjects.end());
  for (auto otherObject : otherObjects) {
    bestResult.aggregate(generateMoveSetAndGetResult(
        evaluator,
        hotObject,
        hotContainer,
        coldContainer,
        otherObject,
        otherContainer,
        stats));
    if (greedy && bestResult.isBetter(precision)) {
      break;
    }
  }

  return bestResult;
}

MoveResult SingleChainMoveType::exploreAllAndGetBestResult(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    entities::ContainerId otherContainer,
    MoveStatsAggregator& stats,
    bool greedy) {
  const auto& precision = evaluator.getProblem().getUniverse().getPrecision();
  auto bestResult = MoveResult::makeEmpty();
  auto& problem = evaluator.getProblem();

  const auto* const filter = problem.getInvalidMoveFilter();
  const ObjectDeduper otherObjects(
      &problem.getEquivalenceSets(),
      evaluator.getDynamicObjects(otherContainer));
  for (auto otherObject : otherObjects) {
    if (isMoveInvalid(filter, otherObject, hotContainer, stats)) {
      continue;
    }
    bestResult.aggregate(generateMoveSetAndGetResult(
        evaluator,
        hotObject,
        hotContainer,
        coldContainer,
        otherObject,
        otherContainer,
        stats));
    if (greedy && bestResult.isBetter(precision)) {
      break;
    }
  }

  return bestResult;
}

MoveResult SingleChainMoveType::generateMoveSetAndGetResult(
    const MovesEvaluator& evaluator,
    entities::ObjectId hotObject,
    entities::ContainerId hotContainer,
    entities::ContainerId coldContainer,
    entities::ObjectId otherObject,
    entities::ContainerId otherContainer,
    MoveStatsAggregator& stats) {
  MoveSet moves;
  // 1. Move otherObject to hotContainer.
  moves.insert(Move(otherObject, otherContainer, hotContainer));
  // 2. Move hotObject to coldContainer.
  moves.insert(Move(hotObject, hotContainer, coldContainer));

  auto result = evaluator.evaluate(std::move(moves));
  stats.add(result);

  return result;
}

std::optional<PackerSet<entities::ContainerId>>
SingleChainMoveType::getCustomColdContainers(
    const MovesEvaluator& evaluator,
    entities::ContainerId /*hotContainer*/) const {
  if (auto specialContainer = getSpecialColdContainer()) {
    return PackerSet<entities::ContainerId>{
        evaluator.getProblem().containerId(*specialContainer)};
  }

  return std::nullopt;
}

} // namespace facebook::rebalancer
