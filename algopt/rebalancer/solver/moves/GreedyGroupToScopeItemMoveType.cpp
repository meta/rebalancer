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

#include "algopt/rebalancer/solver/moves/GreedyGroupToScopeItemMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"

#include <folly/container/irange.h>
#include <folly/hash/Hash.h>
#include <folly/Random.h>

namespace facebook::rebalancer {

namespace {
// Use 'destinationsToExplore' if set, else all scope items of
// 'scopeItemMovesScope'.
interface::DestinationsToExploreOptions buildDestinationsToExplore(
    const interface::GreedyGroupToScopeItemMoveTypeSpec& spec) {
  if (spec.destinationsToExplore().has_value()) {
    return *spec.destinationsToExplore();
  }
  if (spec.scopeItemMovesScope()->empty()) {
    throw std::runtime_error(
        "GreedyGroupToScopeItemMoveType requires either 'destinationsToExplore' or 'scopeItemMovesScope' to be set");
  }
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = *spec.scopeItemMovesScope();
  interface::MoveToScopeItemsSpec moveToScopeItems;
  moveToScopeItems.defaultScopeItems() = std::move(defaultScopeItems);
  interface::DestinationsToExploreOptions destinationsToExplore;
  destinationsToExplore.moveToScopeItems() = std::move(moveToScopeItems);
  return destinationsToExplore;
}

// Validates and returns 'groupMovesPartition'. Called from the member init list
// so a missing partition is reported before the destination checks.
const std::string& requireGroupMovesPartition(
    const interface::GreedyGroupToScopeItemMoveTypeSpec& spec) {
  if (spec.groupMovesPartition()->empty()) {
    throw std::runtime_error(
        "GreedyGroupToScopeItemMoveType requires the parameter 'groupMovesPartition' to be set");
  }
  return *spec.groupMovesPartition();
}

// Resolve the named pruning constraints to hard-constraint components. An empty
// name list means no pruning
std::vector<const Expression*> buildPruningConstraints(
    const interface::GreedyGroupToScopeItemMoveTypeSpec& spec,
    const Problem& problem) {
  const auto& constraintNames = *spec.candidatePruning()->constraintNames();
  if (constraintNames.empty()) {
    return {};
  }
  return problem.getHardConstraintComponents(constraintNames);
}

// A local generator seeded from the scope item's containers keeps parallel
// sampling race-free and deterministic; a shared generator would race. The seed
// is order-independent, so it does not depend on the container list's order.
folly::Random::DefaultGenerator makeRngForContainers(
    const std::vector<entities::ContainerId>& containers) {
  const auto seed = folly::hash::commutative_hash_combine_range(
      containers.begin(), containers.end());
  return folly::Random::DefaultGenerator(
      static_cast<folly::Random::DefaultGenerator::result_type>(seed));
}

std::vector<entities::ContainerId> getFeasibleContainers(
    const MovesEvaluator& evaluator,
    const std::vector<entities::ContainerId>& containers,
    entities::ObjectId object,
    const std::vector<const Expression*>& pruningConstraints) {
  if (pruningConstraints.empty()) {
    return containers;
  }
  const auto source = evaluator.getProblem().assignment.getContainer(object);
  std::vector<entities::ContainerId> kept;
  kept.reserve(containers.size());
  for (const auto container : containers) {
    if (!evaluator.violatesAny(
            pruningConstraints, Move(object, source, container))) {
      kept.push_back(container);
    }
  }
  return kept;
}

// Returns an empty move set if the sample moves an object into a container the
// invalid move filter marks for it.
MoveSet generateRandomSampleAndMoveSet(
    std::vector<entities::ContainerId>& allContainers,
    const std::vector<entities::ObjectId>& groupObjectIds,
    const Problem& problem,
    folly::Random::DefaultGenerator& rng,
    MoveStatsAggregator& stats) {
  std::shuffle(allContainers.begin(), allContainers.end(), rng);

  const auto* const filter = problem.getInvalidMoveFilter();
  const int sampleSize = groupObjectIds.size();
  MoveSet candidateMoveSet;
  for (const auto i : folly::irange(sampleSize)) {
    auto hotObject = groupObjectIds.at(i);
    auto sourceContainer = problem.assignment.getContainer(hotObject);
    auto destinationContainer = allContainers.at(i);
    if (sourceContainer != destinationContainer &&
        isMoveInvalid(filter, hotObject, destinationContainer, stats)) {
      return MoveSet{};
    }

    candidateMoveSet.insert(
        Move(hotObject, sourceContainer, destinationContainer));
  }

  return candidateMoveSet;
}
} // namespace

std::string GreedyGroupToScopeItemMoveType::name() const {
  return kGreedyGroupToScopeItemMoveTypeName.str();
}

GreedyGroupToScopeItemMoveType::GreedyGroupToScopeItemMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::GreedyGroupToScopeItemMoveTypeSpec& spec,
    const Problem& problem)
    : MoveType(solverConfigs),
      partitionName_(requireGroupMovesPartition(spec)),
      nSampleSetsToExplore_(*spec.nSampleSetsToExplore()),
      destinationsToExplore_(buildDestinationsToExplore(spec)),
      pruningConstraints_(buildPruningConstraints(spec, problem)) {}

MoveResult GreedyGroupToScopeItemMoveType::exploreMovingGroup(
    const MovesEvaluator& evaluator,
    const std::vector<entities::ObjectId>& groupObjectIds,
    const ReferenceList<const std::vector<entities::ContainerId>>& destinations,
    MoveStatsAggregator& stats,
    double timeLimit) {
  const std::function<MoveResult(
      std::reference_wrapper<const std::vector<entities::ContainerId>>)>
      sampleContainersAndEvaluate =
          [&](std::reference_wrapper<const std::vector<entities::ContainerId>>
                  scopeItemContainers) {
            const auto& problem = evaluator.getProblem();
            const auto& containers = scopeItemContainers.get();
            // This moveType tries to move each object
            // in the group to a separate container.
            // Therefore, a candidateScopeItem should
            // have at least as many containers as the
            // objects in the group
            if (containers.size() < groupObjectIds.size()) {
              return MoveResult::makeEmpty();
            }

            // Prune destinations a representative group member cannot move
            // into.
            auto containerIds = getFeasibleContainers(
                evaluator,
                containers,
                groupObjectIds.front(),
                pruningConstraints_);
            if (containerIds.size() < groupObjectIds.size()) {
              return MoveResult::makeEmpty();
            }

            auto rng = makeRngForContainers(containerIds);
            MoveResult bestResult = MoveResult::makeEmpty();
            for (const auto _ : folly::irange(nSampleSetsToExplore_)) {
              auto candidateMoveSet = generateRandomSampleAndMoveSet(
                  containerIds, groupObjectIds, problem, rng, stats);
              if (candidateMoveSet.empty()) {
                continue;
              }
              auto result = evaluator.evaluate(std::move(candidateMoveSet));
              stats.add(result);
              bestResult.aggregate(std::move(result));
            }
            return bestResult;
          };

  return MoveHelper::findBest(
      evaluator.getProblem().configs.threadPool.get(),
      destinations,
      sampleContainersAndEvaluate,
      timeLimit,
      parallelExecutionSelector(),
      name());
}

MoveResult GreedyGroupToScopeItemMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /* hints */,
    double timeLimit) {
  const algopt::Timer timer(true);

  auto& problem = evaluator.getProblem();
  const auto& universe = problem.getUniverse();

  // Do NOT deduplicate using equivalent sets because this moveType is
  // essentially impossing a constraint that all objects in a group must move
  // together. So it is possible that someone might use this moveType in lieu of
  // such a constraint. Therefore, in the absence of such a constraint it is
  // possible that a set of objects is deemed equivalent even though they are
  // not all part of the same group. (See
  // GreedyGroupToScopeItemMoveTypeTest.Basic where this case exactly happens.)
  auto& hotObjectIds = evaluator.getDynamicObjects(hotContainer);

  auto bestMoves = MoveResult::makeEmpty();
  PackerSet<entities::GroupId> seenGroupIds;
  for (auto hotObjectId : hotObjectIds) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestMoves);
      return bestMoves;
    }

    auto groupIdOpt =
        problem.getOnlyGroupIdIfExists(partitionName_, hotObjectId);
    if (!groupIdOpt.has_value()) {
      continue;
    }

    auto groupId = groupIdOpt.value();
    if (seenGroupIds.contains(groupId)) {
      continue;
    }
    seenGroupIds.insert(groupId);
    auto& groupObjectIds =
        problem.getObjectIdsForGroup(partitionName_, groupId);

    auto bestGroupMove = exploreMovingGroup(
        evaluator,
        groupObjectIds,
        getDestinationsToExplore(
            destinationsToExplore_, hotContainer, hotObjectId, problem),
        stats,
        timeLimit - timer.getSeconds());

    bestMoves.aggregate(std::move(bestGroupMove));

    // return the first set of moves that improve the objective
    if (bestMoves.isBetter(universe.getPrecision())) {
      return bestMoves;
    }
  }
  return bestMoves;
}

} // namespace facebook::rebalancer
