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

#include "algopt/rebalancer/solver/moves/GroupRoutingMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchSolver.h"

namespace facebook::rebalancer {

namespace {
// Returns the container for a scope item, or std::nullopt if the scope item
// does not map to exactly one container.
std::optional<entities::ContainerId> getContainerForScopeItem(
    const entities::Scope& scope,
    entities::ScopeItemId scopeItemId) {
  const auto& containerIds = scope.getContainerIds(scopeItemId);
  if (containerIds.size() != 1) {
    return std::nullopt;
  }
  return *containerIds.begin();
}

// Checks whether moving a representative object to candidateContainer would
// violate hard constraints. Returns true if the move is valid.
// NOTE: This checks a single move in isolation. Constraints that require
// multiple moves to satisfy may produce false rejections. The final MoveSet
// is still validated atomically.
bool isValidDestination(
    const MovesEvaluator& evaluator,
    entities::ObjectId repObject,
    entities::ContainerId repSourceContainer,
    entities::ContainerId candidateContainer) {
  MoveSet probe;
  probe.insert(Move(repObject, repSourceContainer, candidateContainer));
  return evaluator.satisfiesConstraints(probe);
}
} // namespace

std::string GroupRoutingMoveType::name() const {
  return kGroupRoutingMoveTypeName.str();
}

GroupRoutingMoveType::GroupRoutingMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::GroupRoutingMoveTypeSpec& config)
    : MoveType(solverConfigs) {
  routingConfigName_ = *config.routingConfigName();
  if (config.unassignedContainer().has_value()) {
    unassignedContainerName_ = *config.unassignedContainer();
  }
}

MoveResult GroupRoutingMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  const auto& problem = evaluator.getProblem();
  const auto& universe = problem.getUniverse();

  const auto routingConfigId = universe.getRoutingConfigId(routingConfigName_);
  const auto& routingConfig = universe.getRoutingConfig(routingConfigId);
  const auto scopeId = routingConfig.getScopeId();
  const auto& scopeName = universe.getEntityName(scopeId);
  if (scopeName != universe.getContainerTypeName()) {
    throw std::runtime_error(
        "GroupRoutingMoveType is currently supported only when the scope of the routingConfig is the same as the container scope");
  }

  // collect all groups with at least one object in hotContainer
  const auto partitionId = routingConfig.getPartitionId();
  const auto& partitionName = universe.getEntityName(partitionId);
  PackerSet<entities::GroupId> relevantGroups;
  for (const auto objectId : evaluator.getDynamicObjects(hotContainer)) {
    const auto groupIdOpt =
        problem.getOnlyGroupIdIfExists(partitionName, objectId);
    if (groupIdOpt.has_value()) {
      relevantGroups.insert(groupIdOpt.value());
    }
  }

  std::optional<entities::ContainerId> unassignedContainerId;
  if (unassignedContainerName_) {
    unassignedContainerId = universe.getContainerId(*unassignedContainerName_);
  }

  const std::function<MoveResult(entities::GroupId groupId)>
      exploreMovingGroup = [&](entities::GroupId groupId) {
        auto result = evaluator.evaluate(generateMoveSetFor(
            groupId,
            partitionName,
            unassignedContainerId,
            problem,
            evaluator,
            stats));
        stats.add(result);
        return result;
      };

  return MoveHelper::findBest(
      problem.configs.threadPool.get(),
      relevantGroups,
      exploreMovingGroup,
      timeLimit,
      parallelExecutionSelector(),
      name());
}

MoveSet GroupRoutingMoveType::generateMoveSetFor(
    entities::GroupId groupId,
    const std::string& partitionName,
    std::optional<entities::ContainerId> unassignedContainerId,
    const Problem& problem,
    const MovesEvaluator& evaluator,
    MoveStatsAggregator& stats) const {
  const auto& universe = problem.getUniverse();
  const auto& objectIds = problem.getObjectIdsForGroup(partitionName, groupId);
  if (objectIds.empty()) {
    return MoveSet{};
  }

  // 1. Collect current group presence.
  PackerSet<entities::ContainerId> currentGroupPresence;
  for (const auto objectId : objectIds) {
    currentGroupPresence.insert(problem.assignment.getContainer(objectId));
  }

  // 2. For each source, find the best destination in latency order.
  // A destination is picked immediately if the group already has presence
  // there (no move needed). Otherwise, the destination must be accepting
  // new objects and not violate hard constraints.
  const auto routingConfigId = universe.getRoutingConfigId(routingConfigName_);
  const auto& routingConfig = universe.getRoutingConfig(routingConfigId);
  const auto& scope = universe.getScope(routingConfig.getScopeId());
  const auto latencyTablePtr = routingConfig.getLatencyTablePtr();
  const auto repObjectId = objectIds.front();
  const auto repSourceContainer = problem.assignment.getContainer(repObjectId);
  const auto* const filter = problem.getInvalidMoveFilter();

  // track validity of a destination container
  PackerMap<entities::ContainerId, bool> candidateToIsValid;
  std::vector<entities::ContainerId> bestDestinationsForEachSource;
  for (const auto& routingRing : routingConfig.getRoutingRings(groupId)) {
    const auto* sortedDestinations =
        folly::get_ptr(*latencyTablePtr, routingRing.getOriginScopeItem());
    if (sortedDestinations == nullptr || sortedDestinations->size() == 0) {
      continue;
    }

    for (const auto& [destScopeItemId, latency] : *sortedDestinations) {
      const auto containerOpt =
          getContainerForScopeItem(scope, destScopeItemId);
      if (!containerOpt.has_value()) {
        continue;
      }
      const auto candidate = *containerOpt;

      // Already have presence — no move needed, just keep it.
      if (currentGroupPresence.contains(candidate)) {
        bestDestinationsForEachSource.push_back(candidate);
        break;
      }

      if (problem.not_accepting_containers.contains(candidate) ||
          isMoveInvalid(filter, repObjectId, candidate, stats)) {
        continue;
      }

      // check validity of the destination container by moving a representative
      // object from its current container to the destination container
      auto [it, inserted] = candidateToIsValid.try_emplace(candidate);
      if (inserted) {
        it->second = isValidDestination(
            evaluator, repObjectId, repSourceContainer, candidate);
      }
      if (!it->second) {
        continue;
      }

      bestDestinationsForEachSource.push_back(candidate);
      break;
    }
  }

  // 3. Classify destinations into keep (already present) vs add (need move).
  PackerSet<entities::ContainerId> shouldAddPresence;
  PackerSet<entities::ContainerId> shouldKeepPresence;
  for (const auto destination : bestDestinationsForEachSource) {
    if (currentGroupPresence.contains(destination)) {
      shouldKeepPresence.insert(destination);
    } else {
      shouldAddPresence.insert(destination);
    }
  }

  /*
  4.
  Create a moveSet for the group. At a high-level, for each source, we want
  to place an object of the group in the min latency valid destination from the
  source. We do this by doing the following:

  + if the current container requires a replica (=> it is in
  shouldKeepPresence), then leave it as it is

  + if the current container does NOT require a replica (=> it is NOT in
  shouldKeepPresence), then do one of the following:
    a) if there is a destination that requires a replica (=> it is in
  shouldAddPresence and has not got a new object yet), then move the object from
  current source to new destination;

    b) if not a) and if unassignedContainer is set, then move the object to
  unassigned container,

    c) if neither a) or b), then stop
  */
  auto it = shouldAddPresence.begin();
  MoveSet moveSet;
  for (const auto objectId : objectIds) {
    const auto currContainer = problem.assignment.getContainer(objectId);
    if (shouldKeepPresence.contains(currContainer)) {
      continue;
    }

    // reaching here implies that the object can be moved from currContainer
    if (it != shouldAddPresence.end()) {
      moveSet.insert(Move(objectId, currContainer, *it));
      ++it;
    } else if (unassignedContainerId.has_value()) {
      // if the unassignedContainer is set and the currContainer is not
      // unassignedContainer, then drop (i.e., move this object to unassigned)
      if (currContainer != *unassignedContainerId) {
        moveSet.insert(Move(objectId, currContainer, *unassignedContainerId));
      }
    } else {
      // no more containers need an object and no unassignedContainer to drop
      // excess objects
      break;
    }
  }

  return moveSet;
}

} // namespace facebook::rebalancer
