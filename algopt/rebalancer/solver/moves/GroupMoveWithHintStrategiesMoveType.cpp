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

#include "algopt/rebalancer/solver/moves/GroupMoveWithHintStrategiesMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer {

GroupMoveWithHintStrategiesMoveType::GroupMoveWithHintStrategiesMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::GroupMoveWithHintStrategiesMoveTypeSpec& spec,
    const Problem& problem)
    : GroupMoveWithHintStrategiesMoveType(
          solverConfigs,
          spec,
          problem.getUniverse()) {}

GroupMoveWithHintStrategiesMoveType::GroupMoveWithHintStrategiesMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::GroupMoveWithHintStrategiesMoveTypeSpec& spec,
    const entities::Universe& universe)
    : MoveType(solverConfigs),
      spec_(spec),
      primaryPartitionId_(universe.getPartitionId(*spec.primaryPartition())),
      secondaryPartitionId_(
          universe.getPartitionId(*spec.secondaryPartition())),
      unassignedContainerId_(
          spec.unassignedContainer()
              ? std::make_optional(
                    universe.getContainerId(*spec.unassignedContainer()))
              : std::nullopt) {}

std::string GroupMoveWithHintStrategiesMoveType::name() const {
  return std::string(kGroupMoveWithHintStrategiesMoveTypeName);
}

MoveResult GroupMoveWithHintStrategiesMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  auto& problem = evaluator.getProblem();
  const auto& universe = problem.getUniverse();
  const auto& primaryPartitionName = *spec_.primaryPartition();
  const auto& primaryPartition = universe.getPartition(primaryPartitionId_);
  const auto& objectIdToGroupIds = primaryPartition.getObjectIdToGroupIds();
  const auto& hotContainerObjects = evaluator.getDynamicObjects(hotContainer);

  const ObjectDeduper dedupedObjs(
      &problem.getEquivalenceSets(), hotContainerObjects);

  const auto& precision = universe.getPrecision();
  entities::Set<entities::GroupId> primaryGroupsExplored;

  for (const auto objectId : dedupedObjs) {
    const auto groupIdsPtr = folly::get_ptr(objectIdToGroupIds, objectId);
    if (!groupIdsPtr) {
      throw std::runtime_error(
          fmt::format(
              "object '{}' is not in primary partition '{}'",
              problem.objectName(objectId),
              primaryPartitionName));
    }

    if (groupIdsPtr->size() != 1) {
      throw std::runtime_error(
          fmt::format(
              "expected object '{}' in exactly one of the groups in primary partition '{}', but found it in {} groups",
              problem.objectName(objectId),
              primaryPartitionName,
              groupIdsPtr->size()));
    }

    const auto primaryGroupId = groupIdsPtr->at(0);
    const auto [_, inserted] = primaryGroupsExplored.insert(primaryGroupId);
    if (!inserted) {
      continue;
    }

    const auto primaryGroupIndicesExploredInserted =
        primaryGroupIndicesExplored_.insert(primaryGroupId);
    if (!primaryGroupIndicesExploredInserted.second &&
        !unassignedContainerId_.has_value()) {
      continue;
    }

    const auto secondaryGroupToObjects =
        getPrimaryGroupToExplore(primaryGroupId, problem);
    const auto bestResult = exploreMovingGroup(
        evaluator,
        stats,
        timeLimit,
        generateAllMoveSets(
            objectId, problem, secondaryGroupToObjects, hotContainer, stats));
    if (bestResult.isBetter(precision)) {
      return bestResult;
    }
  }

  return MoveResult::makeEmpty();
}

entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
GroupMoveWithHintStrategiesMoveType::getPrimaryGroupToExplore(
    entities::GroupId currentGroup,
    const Problem& problem) {
  const auto& secondaryPartitionName = *spec_.secondaryPartition();

  const auto& universe = problem.getUniverse();
  const auto& primaryPartition = universe.getPartition(primaryPartitionId_);
  const auto& objectsInCurrentGroup =
      primaryPartition.getObjectIds(currentGroup);
  const auto& secondaryPartition = universe.getPartition(secondaryPartitionId_);
  const auto& secondaryObjectIdToGroupIds =
      secondaryPartition.getObjectIdToGroupIds();

  entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
      groupToObjectIds;
  for (const auto& objectId : objectsInCurrentGroup) {
    const auto& secondaryGroupIds = secondaryObjectIdToGroupIds.at(objectId);
    if (secondaryGroupIds.size() != 1) {
      throw std::runtime_error(
          fmt::format(
              "Object '{}' expected to be in exactly one group in secondaryPartition '{}', but found it in {} groups",
              universe.getEntityName(objectId),
              secondaryPartitionName,
              secondaryGroupIds.size()));
    }
    groupToObjectIds[secondaryGroupIds.at(0)].push_back(objectId);
  }

  return groupToObjectIds;
}

template <typename ContainerIds>
MoveSet GroupMoveWithHintStrategiesMoveType::generateMoveSet(
    const ContainerIds& sampledContainers,
    const std::vector<entities::ObjectId>& objectIds,
    const Problem& problem,
    MoveStatsAggregator& stats) const {
  if (sampledContainers.size() < objectIds.size()) {
    return MoveSet();
  }
  const auto* const filter = problem.getInvalidMoveFilter();
  MoveSet candidateMoveSet;
  size_t i = 0;
  for (const auto& destinationContainer : sampledContainers) {
    const auto hotObject = objectIds.at(i++);
    const auto sourceContainer = problem.assignment.getContainer(hotObject);
    if (sourceContainer == destinationContainer) {
      continue;
    }
    if (isMoveInvalid(filter, hotObject, destinationContainer, stats)) {
      return MoveSet();
    }
    candidateMoveSet.insert(
        Move(hotObject, sourceContainer, destinationContainer));
  }

  return candidateMoveSet;
}

MoveSet
GroupMoveWithHintStrategiesMoveType::generateSampledContainersAndMoveSet(
    interface::MoveStrategyType strategy,
    const std::vector<entities::ContainerId>& scopeItemContainers,
    const std::vector<entities::ObjectId>& objectIds,
    const entities::ContainerId containerToExcludeFromSample,
    const Problem& problem,
    MoveStatsAggregator& stats) const {
  switch (strategy) {
    case interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT: {
      auto sampledContainersWithReplacement = getRandomSampleWithReplacement(
          scopeItemContainers,
          objectIds.size(),
          rng_,
          std::optional(containerToExcludeFromSample));
      return generateMoveSet(
          sampledContainersWithReplacement, objectIds, problem, stats);
    }
    case interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT: {
      auto sampledContainersWithoutReplacement = getRandomSample(
          scopeItemContainers,
          objectIds.size(),
          rng_,
          std::optional(containerToExcludeFromSample));
      return generateMoveSet(
          sampledContainersWithoutReplacement, objectIds, problem, stats);
    }
  }
}

bool GroupMoveWithHintStrategiesMoveType::notEnoughContainersForMoveSet(
    const interface::MoveStrategyType& strategy,
    int numContainers,
    int numObjects) {
  return (
      strategy ==
          interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT &&
      numContainers < numObjects);
}

std::vector<int>
GroupMoveWithHintStrategiesMoveType::generateDestinationScopeItemIndexPerGroup(
    const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
        tertiaryGroupToObjects,
    const ReferenceList<const std::vector<entities::ContainerId>>&
        acceptingContainersPerScopeItem,
    const interface::MoveStrategyType& strategy,
    const entities::ContainerId hotContainer,
    bool distinctScopeItems) const {
  std::vector<int> scopeItemIndices(acceptingContainersPerScopeItem.size());
  std::iota(scopeItemIndices.begin(), scopeItemIndices.end(), 0);

  std::vector<int> selectedIndices;
  for (const auto& [_, tertiaryObjects] : tertiaryGroupToObjects) {
    std::shuffle(scopeItemIndices.begin(), scopeItemIndices.end(), rng_);
    int validIndex = -1;

    for (const auto index : scopeItemIndices) {
      if (distinctScopeItems &&
          std::find(selectedIndices.begin(), selectedIndices.end(), index) !=
              selectedIndices.end()) {
        continue;
      }
      const auto& scopeItemContainer =
          acceptingContainersPerScopeItem[index].get();
      if (notEnoughContainersForMoveSet(
              strategy, scopeItemContainer.size(), tertiaryObjects.size())) {
        continue;
      }
      if (scopeItemContainer.size() == 1 &&
          scopeItemContainer.at(0) == hotContainer) {
        continue;
      }
      validIndex = index;
      break;
    }
    if (validIndex == -1) {
      break;
    }
    selectedIndices.push_back(validIndex);
  }
  return selectedIndices;
}

MoveSet GroupMoveWithHintStrategiesMoveType::generateMoveSetWithScopeItemTuple(
    const std::vector<int>& scopeItemTuple,
    const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
        tertiaryGroupToObjects,
    const ReferenceList<const std::vector<entities::ContainerId>>&
        acceptingContainersPerScopeItem,
    const interface::MoveStrategyType& strategy,
    const entities::ContainerId hotContainer,
    const std::vector<entities::ObjectId>& objects,
    const Problem& problem,
    MoveStatsAggregator& stats) const {
  const auto containerToExcludeFromSample =
      unassignedContainerId_.value_or(hotContainer);
  MoveSet candidateMoveSet;
  size_t index = 0;
  for (const auto& [_, tertiaryObjects] : tertiaryGroupToObjects) {
    const auto validIndex = scopeItemTuple.at(index);
    auto partialMoveSet = generateSampledContainersAndMoveSet(
        strategy,
        acceptingContainersPerScopeItem[validIndex].get(),
        tertiaryObjects,
        containerToExcludeFromSample,
        problem,
        stats);
    if (partialMoveSet.empty()) {
      break;
    }
    for (auto& move : partialMoveSet) {
      candidateMoveSet.insert(move);
    }
    ++index;
  }
  if (candidateMoveSet.size() < objects.size()) {
    return MoveSet();
  }
  return candidateMoveSet;
}

MoveSet GroupMoveWithHintStrategiesMoveType::getAllToUnassigned(
    const std::vector<entities::ObjectId>& objects,
    entities::ContainerId unassignedContainerId,
    const Problem& problem) {
  MoveSet baseMoveSet;
  for (const auto& object : objects) {
    const auto sourceContainerId = problem.assignment.getContainer(object);
    baseMoveSet.insert(Move(object, sourceContainerId, unassignedContainerId));
  }
  return baseMoveSet;
}

std::pair<MoveSet, std::optional<entities::GroupId>>
GroupMoveWithHintStrategiesMoveType::findAllocatedSecondaryGroup(
    const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
        secondaryGroupIdToObjects,
    const std::optional<entities::ContainerId>& unassignedContainerId,
    const Problem& problem) {
  if (!unassignedContainerId.has_value()) {
    return {MoveSet(), std::nullopt};
  }

  for (const auto& [secondaryGroupId, objects] : secondaryGroupIdToObjects) {
    const bool isAllocated =
        std::any_of(objects.begin(), objects.end(), [&](const auto& obj) {
          return problem.assignment.getContainer(obj) !=
              unassignedContainerId.value();
        });
    if (isAllocated) {
      return {
          getAllToUnassigned(objects, *unassignedContainerId, problem),
          secondaryGroupId};
    }
  }

  return {MoveSet(), std::nullopt};
}

std::vector<MoveSet>
GroupMoveWithHintStrategiesMoveType::exploreTertiaryPartitionMoves(
    const std::vector<entities::ObjectId>& objects,
    const std::string& tertiaryPartitionName,
    int numScopeItemsToExplore,
    int moveSetsPerScopeItem,
    const ReferenceList<const std::vector<entities::ContainerId>>&
        acceptingContainersPerScopeItem,
    const interface::MoveStrategyType& strategy,
    entities::ContainerId exclusionContainer,
    const Problem& problem,
    MoveStatsAggregator& stats,
    bool distinctScopeItems) const {
  const auto& universe = problem.getUniverse();
  const auto tertiaryPartitionId =
      universe.getPartitionId(tertiaryPartitionName);
  const auto& tertiaryPartition = universe.getPartition(tertiaryPartitionId);
  const auto& tertiaryObjectIdToGroupIds =
      tertiaryPartition.getObjectIdToGroupIds();

  entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
      tertiaryGroupToObjects;
  for (const auto& object : objects) {
    const auto& tertiaryGroupIds = tertiaryObjectIdToGroupIds.at(object);
    if (tertiaryGroupIds.size() != 1) {
      throw std::runtime_error(
          fmt::format(
              "Object '{}' expected to be in exactly one group in tertiaryPartition '{}', but found it in {} groups",
              problem.objectName(object),
              tertiaryPartitionName,
              tertiaryGroupIds.size()));
    }
    tertiaryGroupToObjects[tertiaryGroupIds.at(0)].push_back(object);
  }

  std::vector<MoveSet> moveSets;
  for ([[maybe_unused]] const auto _ : folly::irange(numScopeItemsToExplore)) {
    const auto destinationIndices = generateDestinationScopeItemIndexPerGroup(
        tertiaryGroupToObjects,
        acceptingContainersPerScopeItem,
        strategy,
        exclusionContainer,
        distinctScopeItems);

    if (destinationIndices.size() < tertiaryGroupToObjects.size()) {
      continue;
    }

    for ([[maybe_unused]] const auto __ : folly::irange(moveSetsPerScopeItem)) {
      auto candidateMoveSet = generateMoveSetWithScopeItemTuple(
          destinationIndices,
          tertiaryGroupToObjects,
          acceptingContainersPerScopeItem,
          strategy,
          exclusionContainer,
          objects,
          problem,
          stats);
      if (!candidateMoveSet.empty()) {
        moveSets.push_back(std::move(candidateMoveSet));
      }
    }
  }
  return moveSets;
}

std::vector<MoveSet> GroupMoveWithHintStrategiesMoveType::exploreScopeItemMoves(
    const std::vector<entities::ObjectId>& objects,
    int moveSetsPerScopeItem,
    const ReferenceList<const std::vector<entities::ContainerId>>&
        acceptingContainersPerScopeItem,
    const interface::MoveStrategyType& strategy,
    entities::ContainerId exclusionContainer,
    const Problem& problem,
    MoveStatsAggregator& stats) const {
  const auto hasUniqueSample = [&strategy](
                                   size_t numContainers, size_t numObjects) {
    return strategy ==
        interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT &&
        numContainers == numObjects;
  };

  std::vector<MoveSet> moveSets;
  for (const auto& scopeItemContainer : acceptingContainersPerScopeItem) {
    const auto& containers = scopeItemContainer.get();
    if (notEnoughContainersForMoveSet(
            strategy, containers.size(), objects.size())) {
      continue;
    }

    const int numToGenerate = hasUniqueSample(containers.size(), objects.size())
        ? 1
        : moveSetsPerScopeItem;
    for ([[maybe_unused]] const auto _ : folly::irange(numToGenerate)) {
      auto candidateMoveSet = generateSampledContainersAndMoveSet(
          strategy, containers, objects, exclusionContainer, problem, stats);
      if (!candidateMoveSet.empty()) {
        moveSets.push_back(std::move(candidateMoveSet));
      }
    }
  }
  return moveSets;
}

std::vector<MoveSet> GroupMoveWithHintStrategiesMoveType::generateAllMoveSets(
    entities::ObjectId hotObjectId,
    Problem& problem,
    const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
        secondaryGroupIdToObjects,
    const entities::ContainerId hotContainer,
    MoveStatsAggregator& stats) const {
  const auto& universe = problem.getUniverse();
  auto& moveStrategies = *spec_.moveStrategies();
  auto& groupToMoveStrategy = *moveStrategies.groupToMoveStrategy();

  auto [baseMoveSet, allocatedSecondaryGroupId] = findAllocatedSecondaryGroup(
      secondaryGroupIdToObjects, unassignedContainerId_, problem);

  const auto& secondaryGroupToAllowedReplacements =
      *spec_.secondaryGroupReplacementConfig()
           ->secondaryGroupToAllowedReplacements();

  const auto allowedReplacementGroupsPtr = allocatedSecondaryGroupId
      ? folly::get_ptr(
            secondaryGroupToAllowedReplacements,
            universe.getEntityName(*allocatedSecondaryGroupId))
      : nullptr;

  const auto exclusionContainer = unassignedContainerId_.value_or(hotContainer);

  std::vector<MoveSet> possibleMoveSets;
  for (const auto& [secondaryGroupId, objects] : secondaryGroupIdToObjects) {
    const auto& groupName = universe.getEntityName(secondaryGroupId);
    if (allowedReplacementGroupsPtr &&
        !allowedReplacementGroupsPtr->contains(groupName)) {
      continue;
    }

    const auto groupIdsPtr = folly::get_ptr(groupToMoveStrategy, groupName);
    if (!groupIdsPtr) {
      throw std::runtime_error(
          fmt::format(
              "Group '{}' does not have a move strategy defined", groupName));
    }
    const auto& hintOptions = *groupIdsPtr;
    const auto& strategy = hintOptions.type().value();
    const auto moveSetsPerScopeItem =
        *hintOptions.moveSetsGeneratedPerScopeItem();
    if (moveSetsPerScopeItem == 0) {
      continue;
    }

    auto acceptingContainersPerScopeItem =
        problem.getDestinationsGenerator().getAcceptingDestinations(
            *hintOptions.moveToScopeItems(), hotContainer, hotObjectId);

    std::vector<MoveSet> newMoveSets;
    if (hintOptions.tertiaryPartition().has_value() &&
        hintOptions.numScopeItemsToExplorePerTertiaryGroup().has_value()) {
      newMoveSets = exploreTertiaryPartitionMoves(
          objects,
          hintOptions.tertiaryPartition().value(),
          hintOptions.numScopeItemsToExplorePerTertiaryGroup().value(),
          moveSetsPerScopeItem,
          acceptingContainersPerScopeItem,
          strategy,
          exclusionContainer,
          problem,
          stats,
          *hintOptions.distinctScopeItemsAcrossTertiaryGroups());
    } else {
      newMoveSets = exploreScopeItemMoves(
          objects,
          moveSetsPerScopeItem,
          acceptingContainersPerScopeItem,
          strategy,
          exclusionContainer,
          problem,
          stats);
    }

    const bool needsBaseMoveSet = allocatedSecondaryGroupId.has_value() &&
        *allocatedSecondaryGroupId != secondaryGroupId;
    for (auto& moveSet : newMoveSets) {
      if (needsBaseMoveSet) {
        for (const auto& move : baseMoveSet) {
          moveSet.insert(move);
        }
      }
      possibleMoveSets.push_back(std::move(moveSet));
    }
  }
  return possibleMoveSets;
}

MoveResult GroupMoveWithHintStrategiesMoveType::exploreMovingGroup(
    const MovesEvaluator& evaluator,
    MoveStatsAggregator& stats,
    double timeLimit,
    const std::vector<MoveSet>& possibleMoveSets) {
  const std::function<MoveResult(MoveSet)> sampleContainersAndEvaluate =
      [&](MoveSet moveset) {
        auto result = evaluator.evaluate(std::move(moveset));
        stats.add(result);
        return result;
      };

  return MoveHelper::findBest(
      evaluator.getProblem().configs.threadPool.get(),
      possibleMoveSets,
      sampleContainersAndEvaluate,
      timeLimit,
      parallelExecutionSelector(),
      name());
}

} // namespace facebook::rebalancer
