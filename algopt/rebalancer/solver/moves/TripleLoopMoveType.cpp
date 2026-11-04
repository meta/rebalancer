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

#include "algopt/rebalancer/solver/moves/TripleLoopMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

TripleLoopMoveType::TripleLoopMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::TripleLoopMoveTypeSpec& config)
    : AsyncSingleMovesMoveType(solverConfigs), config_(config) {}

std::string TripleLoopMoveType::name() const {
  return kTripleLoopMoveTypeName.str();
}

MoveResult TripleLoopMoveType::exploreFromAllSingleMoves(
    const MovesEvaluator& evaluator,
    entities::ContainerId hot_container,
    entities::ObjectId hotObject,
    entities::ContainerId cold_container,
    MoveStatsAggregator& stats) {
  auto best_result = MoveResult::makeEmpty();

  const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();
  if (evaluator.getProblem().not_accepting_containers.contains(
          cold_container) ||
      isMoveInvalid(filter, hotObject, cold_container, stats)) {
    return best_result;
  }

  const auto& cold_containers = evaluator.getProblem().containers;
  const ObjectDeduper dedupedObjs(
      &evaluator.getProblem().getEquivalenceSets(),
      evaluator.getDynamicObjects(cold_container));

  for (auto coldObject : dedupedObjs) {
    if (evaluator.getProblem().fixed_objects.contains(coldObject)) {
      continue;
    }
    for (auto cold_container2 : cold_containers) {
      if (cold_container2 == hot_container ||
          cold_container2 == cold_container) {
        continue;
      }
      if (evaluator.getProblem().not_accepting_containers.contains(
              cold_container2) ||
          isMoveInvalid(filter, coldObject, cold_container2, stats)) {
        continue;
      }
      const ObjectDeduper deduped_objs2(
          &evaluator.getProblem().getEquivalenceSets(),
          evaluator.getDynamicObjects(cold_container2));
      for (auto cold_object2 : deduped_objs2) {
        if (evaluator.getProblem().fixed_objects.contains(cold_object2) ||
            isMoveInvalid(filter, cold_object2, hot_container, stats)) {
          continue;
        }
        MoveSet moves;
        moves.insert(Move(hotObject, hot_container, cold_container));
        moves.insert(Move(coldObject, cold_container, cold_container2));
        moves.insert(Move(cold_object2, cold_container2, hot_container));
        auto result = evaluator.evaluate(std::move(moves));
        stats.add(result);
        best_result.aggregate(std::move(result));
      }
    }
  }
  return best_result;
}

} // namespace facebook::rebalancer
