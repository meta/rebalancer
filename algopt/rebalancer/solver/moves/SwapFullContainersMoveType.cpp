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

#include "algopt/rebalancer/solver/moves/SwapFullContainersMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"

namespace facebook::rebalancer {

SwapFullContainersMoveType::SwapFullContainersMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SwapFullContainersMoveTypeSpec& spec)
    : AsyncDestinationsMoveType(solverConfigs), spec_(spec) {}

std::string SwapFullContainersMoveType::name() const {
  return kSwapFullContainersTypeName.str();
}

bool SwapFullContainersMoveType::canSwapObjects(
    const MovesEvaluator& evaluator,
    entities::ContainerId hot_container,
    entities::ContainerId cold_container,
    MoveStatsAggregator& stats) {
  const auto& problem = evaluator.getProblem();
  if (problem.not_accepting_containers.contains(hot_container) &&
      !isContainerEmpty(evaluator, cold_container)) {
    return false;
  }
  const auto* const filter = problem.getInvalidMoveFilter();
  const auto canMoveAllObjects = [&](entities::ContainerId src,
                                     entities::ContainerId dst) {
    for (auto object : problem.assignment.getObjects(src)) {
      if (problem.fixed_objects.contains(object) ||
          isMoveInvalid(filter, object, dst, stats)) {
        return false;
      }
    }
    return true;
  };
  return canMoveAllObjects(hot_container, cold_container) &&
      canMoveAllObjects(cold_container, hot_container);
}

bool SwapFullContainersMoveType::isContainerEmpty(
    const MovesEvaluator& evaluator,
    entities::ContainerId container) {
  return evaluator.getProblem().assignment.getObjects(container).size() == 0;
}

void SwapFullContainersMoveType::insertAllMovesFromSrcToDest(
    const MovesEvaluator& evaluator,
    entities::ContainerId src_container,
    entities::ContainerId dest_container,
    MoveSet& moves) {
  for (auto object :
       evaluator.getProblem().assignment.getObjects(src_container)) {
    moves.insert(Move(object, src_container, dest_container));
  }
}

MoveResult SwapFullContainersMoveType::exploreFromAllDestinations(
    const MovesEvaluator& evaluator,
    entities::ContainerId hot_container,
    entities::ContainerId cold_container,
    MoveStatsAggregator& stats) {
  if (!canSwapObjects(evaluator, hot_container, cold_container, stats)) {
    return MoveResult::makeEmpty();
  }

  MoveSet moves;
  insertAllMovesFromSrcToDest(evaluator, cold_container, hot_container, moves);
  insertAllMovesFromSrcToDest(evaluator, hot_container, cold_container, moves);

  auto result = evaluator.evaluate(std::move(moves));
  stats.add(result);
  return result;
}

} // namespace facebook::rebalancer
