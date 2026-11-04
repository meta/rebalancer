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

#include "algopt/rebalancer/solver/moves/SingleGreedyMoveType.h"

#include "algopt/rebalancer/solver/iterators/ExpressionContainersIterator.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

SingleGreedyMoveType::SingleGreedyMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SingleGreedyMoveTypeSpec& spec)
    : MoveType(solverConfigs), spec_(spec) {}

std::string SingleGreedyMoveType::name() const {
  return kSingleGreedyMoveTypeName.str();
}

MoveResult SingleGreedyMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  auto bestResult = MoveResult::makeEmpty();

  const algopt::Timer timer(true);
  const auto& precision = evaluator.getProblem().getUniverse().getPrecision();

  auto& hotObjects = evaluator.getDynamicObjects(hotContainer);
  if (hotObjects.empty()) {
    return bestResult;
  }
  const ObjectDeduper dedupedObjs(
      &evaluator.getProblem().getEquivalenceSets(), hotObjects);
  const DescendingExpressionContainersTraversal descending(
      evaluator.getProblem().objective.getView());
  // traverse cold containers in ascending order of their objective contribution
  std::vector<entities::ContainerId> coldContainers(
      descending.begin(), descending.end());
  std::reverse(coldContainers.begin(), coldContainers.end());
  const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();

  for (auto hotObject : dedupedObjs) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }
    if (evaluator.getProblem().fixed_objects.contains(hotObject)) {
      continue;
    }
    for (auto coldContainer : coldContainers) {
      if (timer.getSeconds() >= timeLimit) {
        stats.incrNumTimeouts(bestResult);
        return bestResult;
      }
      if (evaluator.getProblem().not_accepting_containers.contains(
              coldContainer) ||
          isMoveInvalid(filter, hotObject, coldContainer, stats)) {
        continue;
      }

      MoveSet moves;
      moves.insert(Move(hotObject, hotContainer, coldContainer));
      auto result = evaluator.evaluate(std::move(moves));
      stats.add(result);
      bestResult.aggregate(std::move(result));

      // return after the first result that improves the objective
      if (bestResult.isBetter(precision)) {
        return bestResult;
      }
    }
  }
  return bestResult;
}

} // namespace facebook::rebalancer
