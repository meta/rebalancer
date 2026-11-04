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

#include "algopt/rebalancer/solver/moves/SingleMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"

namespace facebook::rebalancer {

SingleMoveType::SingleMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleMoveTypeSpec& config)
    : AsyncSingleMovesMoveType(solverConfigs), config_(config) {}

std::string SingleMoveType::name() const {
  return kSingleMoveTypeName.str();
}

MoveResult SingleMoveType::exploreFromAllSingleMoves(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    MoveStatsAggregator& stats) {
  const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();
  if (isMoveInvalid(filter, hotObject, coldContainer, stats)) {
    return MoveResult::makeEmpty();
  }
  MoveSet moves;
  moves.insert(Move(hotObject, hotContainer, coldContainer));
  auto result = evaluator.evaluate(std::move(moves));
  stats.add(result);
  return result;
}

} // namespace facebook::rebalancer
