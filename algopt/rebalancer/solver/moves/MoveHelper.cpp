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

#include "algopt/rebalancer/solver/moves/MoveHelper.h"

#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"

namespace facebook::rebalancer {

MoveResult MoveHelper::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    const PackerSet<entities::ContainerId>& coldContainers,
    double timeout,
    MoveStatsAggregator& stats,
    ParallelExecutionSelector& executionSelector,
    std::string_view moveTypeName) {
  const auto& problem = evaluator.getProblem();
  const auto* filter = problem.getInvalidMoveFilter();
  auto filteredContainers = Filter(
      coldContainers,
      [&problem, &stats, filter, hotObject, hotContainer](auto container) {
        return container != hotContainer &&
            !problem.not_accepting_containers.contains(container) &&
            !isMoveInvalid(filter, hotObject, container, stats);
      });

  // WARNING: The stats object is not thread safe. For that reason, it is put
  // inside a wrapper that protects it from concurrent mutations by different
  // evaluation threads. This main thread should not attempt to interact with
  // the raw stats objects until all worker threads have completed.
  const std::function<MoveResult(entities::ContainerId)> evaluate =
      [&evaluator, &stats, hotObject, hotContainer](
          entities::ContainerId coldContainer) {
        MoveSet moves;
        moves.insert(Move(hotObject, hotContainer, coldContainer));
        auto result = evaluator.evaluate(std::move(moves));
        stats.add(result);
        return result;
      };

  return MoveHelper::findBest(
      evaluator.getProblem().configs.threadPool.get(),
      filteredContainers,
      evaluate,
      timeout,
      executionSelector,
      moveTypeName);
}

} // namespace facebook::rebalancer
