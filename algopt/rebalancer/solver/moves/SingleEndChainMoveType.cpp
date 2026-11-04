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

#include "algopt/rebalancer/solver/moves/SingleEndChainMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

SingleEndChainMoveType::SingleEndChainMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleEndChainMoveTypeSpec& spec)
    : AsyncSingleMovesMoveType(solverConfigs), spec_(spec) {}

std::string SingleEndChainMoveType::name() const {
  return kSingleEndChainMoveTypeName.str();
}

MoveResult SingleEndChainMoveType::exploreFromAllSingleMoves(
    const MovesEvaluator& evaluator,
    entities::ContainerId hot_container,
    entities::ObjectId hotObject,
    entities::ContainerId cold_container,
    MoveStatsAggregator& stats) {
  auto best_result = MoveResult::makeEmpty();
  const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();
  if (isMoveInvalid(filter, hotObject, cold_container, stats)) {
    return best_result;
  }

  const ObjectDeduper cold_objects(
      &evaluator.getProblem().getEquivalenceSets(),
      evaluator.getDynamicObjects(cold_container));
  for (auto coldObject : cold_objects) {
    for (auto other_container : evaluator.getContainers()) {
      if (other_container == hot_container ||
          other_container == cold_container ||
          isMoveInvalid(filter, coldObject, other_container, stats)) {
        continue;
      }

      MoveSet moves;
      // 1. Move coldObject to other_container.
      moves.insert(Move(coldObject, cold_container, other_container));
      // 2. Move hotObject to cold_container.
      moves.insert(Move(hotObject, hot_container, cold_container));

      auto result = evaluator.evaluate(std::move(moves));
      stats.add(result);
      best_result.aggregate(std::move(result));
    }
  }
  return best_result;
}

} // namespace facebook::rebalancer
