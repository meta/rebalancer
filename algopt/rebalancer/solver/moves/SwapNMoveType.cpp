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

#include "algopt/rebalancer/solver/moves/SwapNMoveType.h"

#include "algopt/rebalancer/solver/iterators/Range.h"
#include "algopt/rebalancer/solver/iterators/StlWrapper.h"
#include "algopt/rebalancer/solver/iterators/Transform.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"

#include <folly/container/irange.h>
#include <folly/Conv.h>
#include <folly/Random.h>

namespace facebook::rebalancer {

namespace {
PackerSet<entities::ObjectId> resolveSourceObjectIds(
    const interface::SwapNMoveTypeSpec& spec,
    const Problem& problem) {
  PackerSet<entities::ObjectId> objectIds;
  objectIds.reserve(
      folly::to<PackerSet<entities::ObjectId>::size_type>(
          spec.swapNSourceObjects()->size()));
  for (const auto& objectName : *spec.swapNSourceObjects()) {
    objectIds.insert(problem.objectId(objectName));
  }
  return objectIds;
}
} // namespace

SwapNMoveType::SwapNMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SwapNMoveTypeSpec& spec,
    const Problem& problem)
    : MoveType(solverConfigs),
      spec_(spec),
      sourceObjectIds_(resolveSourceObjectIds(spec, problem)) {}

std::string SwapNMoveType::name() const {
  return kSwapNMoveTypeName.str();
}

MoveResult SwapNMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hot_container,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  // number of parallel swaps of objects
  const int concurrentObjects = *spec_.swapNConcurrentObjects();

  // get all objects in the hot container
  auto& hotObjectIds = evaluator.getDynamicObjects(hot_container);

  // out of the hot objects, pick the ones that are allowed to move
  std::vector<entities::ObjectId> objectIdsToMove;
  for (auto hotObjectId : hotObjectIds) {
    if (static_cast<int>(objectIdsToMove.size()) >= concurrentObjects) {
      break;
    }
    if (sourceObjectIds_.contains(hotObjectId)) {
      objectIdsToMove.push_back(hotObjectId);
    }
  }

  if (static_cast<int>(objectIdsToMove.size()) < concurrentObjects) {
    // no valid moves possible because there are fewer objects to move than the
    // concurrency number
    return MoveResult::makeEmpty();
  }

  // A destination container always swaps its first dynamic object into the hot
  // container, and with one concurrent object it always receives
  // objectIdsToMove[0]; drop containers where either move is marked invalid.
  const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();
  const auto isInvalidDestination =
      [&](entities::ContainerId containerId,
          entities::ObjectId destinationObjectId) {
        return isMoveInvalid(
                   filter, destinationObjectId, hot_container, stats) ||
            (concurrentObjects == 1 &&
             isMoveInvalid(
                 filter, objectIdsToMove.front(), containerId, stats));
      };

  std::vector<std::vector<entities::ContainerId>> destinationScopeIds;
  destinationScopeIds.reserve(spec_.swapNDestinationScope()->size());
  for (auto& destinationScope : *spec_.swapNDestinationScope()) {
    std::vector<entities::ContainerId> containerIds;
    containerIds.reserve(destinationScope.size());
    for (auto& containerName : destinationScope) {
      auto containerId = evaluator.getProblem().containerId(containerName);
      const auto& destinationObjects = evaluator.getDynamicObjects(containerId);
      if (destinationObjects.empty() ||
          isInvalidDestination(containerId, *destinationObjects.begin())) {
        continue;
      }
      containerIds.push_back(containerId);
    }

    if (static_cast<int>(containerIds.size()) < concurrentObjects) {
      // no valid moves possible because there are fewer containers to pick from
      // in this scope item than the concurrency number
      continue;
    }

    destinationScopeIds.push_back(std::move(containerIds));
  }

  if (destinationScopeIds.empty()) {
    // no valid moves possible because there aren't any valid destination scope
    // items to pick from
    return MoveResult::makeEmpty();
  }

  return findBestMove(
      evaluator, objectIdsToMove, destinationScopeIds, stats, timeLimit);
}

MoveResult SwapNMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    const std::vector<entities::ObjectId>& objectIdsToMove,
    const std::vector<std::vector<entities::ContainerId>>& destinationScopeIds,
    MoveStatsAggregator& stats,
    double timeLimit) {
  const std::function<MoveResult(MoveSet)> evaluate = [&evaluator,
                                                       &stats](auto moveset) {
    if (moveset.empty()) {
      return MoveResult::makeEmpty();
    }
    auto result = evaluator.evaluate(std::move(moveset));
    stats.add(result);
    return result;
  };

  const std::function<MoveSet(int)> generate = [this,
                                                &evaluator,
                                                &stats,
                                                &objectIdsToMove,
                                                &destinationScopeIds](
                                                   int /* index */) {
    const int concurrentObjects = *spec_.swapNConcurrentObjects();
    const int scopeId = folly::Random::rand32(0, destinationScopeIds.size());
    auto& containerIds = destinationScopeIds.at(scopeId);
    auto indices = pickRandom(containerIds.size(), concurrentObjects);
    std::vector<entities::ObjectId> destinationObjectIds;
    destinationObjectIds.reserve(indices.size());
    for (const int index : indices) {
      auto containerId = containerIds.at(index);
      auto& objectIds = evaluator.getDynamicObjects(containerId);
      destinationObjectIds.push_back(*objectIds.begin());
    }
    const auto* const filter = evaluator.getProblem().getInvalidMoveFilter();
    MoveSet moveset;
    for (const auto i : folly::irange(concurrentObjects)) {
      auto object1 = objectIdsToMove.at(i);
      auto object2 = destinationObjectIds.at(i);
      auto container1 = evaluator.getProblem().assignment.getContainer(object1);
      auto container2 = evaluator.getProblem().assignment.getContainer(object2);
      // object2 -> container1 (the hot container) was already checked when
      // destinationScopeIds was built.
      if (isMoveInvalid(filter, object1, container2, stats)) {
        return MoveSet{};
      }
      moveset.insert(Move(object1, container1, container2));
      moveset.insert(Move(object2, container2, container1));
    }
    return moveset;
  };

  auto moves = makeTransformContainer(
      makeStlWrapperContainer(Range<int>(0, *spec_.swapNIterations())),
      generate);

  return MoveHelper::findBest(
      evaluator.getProblem().configs.threadPool.get(),
      moves,
      evaluate,
      timeLimit,
      parallelExecutionSelector(),
      name());
}

PackerSet<int> SwapNMoveType::pickRandom(int n, int k) {
  if (k > n) {
    throw std::runtime_error(
        fmt::format("impossible to pick {} out of {}", k, n));
  }
  PackerSet<int> result;
  for ([[maybe_unused]] const auto _ : folly::irange(k)) {
    while (true) {
      const int r = folly::Random::rand32(0, n);
      if (!result.contains(r)) {
        result.insert(r);
        break;
      }
    }
  }
  return result;
}

} // namespace facebook::rebalancer
