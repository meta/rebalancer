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

#include "algopt/rebalancer/solver/moves/ColocateGroupsMoveType.h"

#include "algopt/rebalancer/solver/iterators/MultiCollectionCartesianProduct.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer {

namespace {

// Valid destinations for a group's representative object: candidates minus
// non-accepting containers, the object's own source container, and any pair
// the invalid-move filter forbids.
std::vector<entities::ContainerId> getValidDestinations(
    const entities::Set<entities::ContainerId>& candidates,
    entities::ObjectId object,
    const Problem& problem,
    const InvalidMoveFilter* invalidMoveFilter,
    MoveStatsAggregator& stats) {
  const auto sourceContainer = problem.assignment.getContainer(object);
  std::vector<entities::ContainerId> valid;
  valid.reserve(candidates.size());
  for (const auto containerId : candidates) {
    if (problem.not_accepting_containers.contains(containerId) ||
        containerId == sourceContainer ||
        isMoveInvalid(invalidMoveFilter, object, containerId, stats)) {
      continue;
    }
    valid.push_back(containerId);
  }
  return valid;
}

} // namespace

std::string ColocateGroupsMoveType::name() const {
  return kColocateGroupsMoveTypeName.data();
}

ColocateGroupsMoveType::ColocateGroupsMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::ColocateGroupsMoveTypeSpec& spec,
    const Problem& problem)
    : MoveType(solverConfigs), specInfo_(buildSpecInfo(spec, problem)) {}

MoveResult ColocateGroupsMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  const algopt::Timer timer(true);
  auto& problem = evaluator.getProblem();

  const auto& universe = problem.getUniverse();
  const auto& precision = universe.getPrecision();
  const auto& colocationScope = universe.getScope(specInfo_.colocationScopeId);
  const auto sourceScopeItemIdOpt =
      colocationScope.getScopeItemId(hotContainer);
  const auto& partition = universe.getPartition(specInfo_.partitionId);
  auto bestResult = MoveResult::makeEmpty();

  entities::Set<entities::GroupId> seenGroupIds;
  const ObjectDeduper dedupedObjs(
      &problem.getEquivalenceSets(), evaluator.getDynamicObjects(hotContainer));
  for (auto hotObjectId : dedupedObjs) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }

    auto groupIdOpt =
        problem.getOnlyGroupIdIfExists(specInfo_.partitionId, hotObjectId);
    if (!groupIdOpt.has_value()) {
      continue;
    }
    auto hotObjectGroupId = groupIdOpt.value();
    auto relatedGroupsInfoPtr =
        folly::get_ptr(specInfo_.groupIdToRelatedGroups, hotObjectGroupId);
    if (!relatedGroupsInfoPtr || seenGroupIds.contains(hotObjectGroupId)) {
      // not a related group for this move type or we have already seen this
      // group
      continue;
    }

    auto& relatedGroupsInfo = *relatedGroupsInfoPtr;
    auto& relatedGroups = *relatedGroupsInfo.relatedGroups;
    seenGroupIds.insert(relatedGroups.begin(), relatedGroups.end());

    // if hot container is not part of the colocation scope (e.g., it maybe a
    // dummy container), then we only consider that container as part
    // "sourceContainers"; otherwise we need to consider all containers in the
    // scope item
    auto onlySourceContainer =
        entities::Set<entities::ContainerId>{hotContainer};
    const auto& sourceContainers = sourceScopeItemIdOpt
        ? colocationScope.getContainerIds(sourceScopeItemIdOpt.value())
        : onlySourceContainer;
    auto representativeObjectPerGroup = getRepresentativeObjectPerGroup(
        hotObjectGroupId,
        relatedGroups,
        hotObjectId,
        sourceContainers,
        partition,
        problem);
    if (representativeObjectPerGroup.size() != relatedGroups.size()) {
      // if we could not find a representative object for each group, then skip
      continue;
    }

    auto moveSets = getMoveSetsForRelatedGroups(
        relatedGroupsInfo,
        sourceScopeItemIdOpt,
        representativeObjectPerGroup,
        problem,
        timer,
        timeLimit,
        stats);

    const std::function<MoveResult(MoveSet)> evaluate =
        [&stats, &evaluator](MoveSet moveset) {
          auto result = evaluator.evaluate(std::move(moveset));
          stats.add(result);
          return result;
        };

    bestResult.aggregate(
        MoveHelper::findBest(
            problem.configs.threadPool.get(),
            moveSets,
            evaluate,
            timeLimit - timer.getSeconds(),
            parallelExecutionSelector(),
            name()));

    if (bestResult.isBetter(precision)) {
      // return early if we found a better move
      return bestResult;
    }
  }

  return bestResult;
}

std::vector<MoveSet> ColocateGroupsMoveType::getMoveSetsForRelatedGroups(
    const RelatedGroupsInfoId& relatedGroupsInfo,
    std::optional<entities::ScopeItemId> sourceScopeItemIdOpt,
    const std::vector<entities::ObjectId>& representativeObjectPerGroup,
    const Problem& problem,
    const algopt::Timer& timer,
    double timeLimit,
    MoveStatsAggregator& stats) const {
  const auto& universe = problem.getUniverse();
  const auto& colocationScope = universe.getScope(specInfo_.colocationScopeId);
  const auto* const invalidMoveFilter = problem.getInvalidMoveFilter();
  // if the group has a specific set of colocation scope items, then only those
  // are considered as potential destinations; else consider all scope items in
  // colocationScope
  const auto& destinationScopeItemIds =
      relatedGroupsInfo.destinationScopeItemIds
      ? *relatedGroupsInfo.destinationScopeItemIds
      : colocationScope.getScopeItemIds();

  std::vector<MoveSet> moveSets;
  for (auto destinationScopeItem : destinationScopeItemIds) {
    if (timer.getSeconds() >= timeLimit) {
      // Out of time before enumerating the remaining destination scope items;
      // return whatever was collected so far.
      break;
    }
    if (sourceScopeItemIdOpt &&
        sourceScopeItemIdOpt.value() == destinationScopeItem) {
      continue;
    }

    std::vector<std::vector<entities::ContainerId>>
        destinationContainersPerGroup;
    auto groupToContainersPtr = folly::get_ptr(
        specInfo_.colocationScopeItemToGroupToContainers, destinationScopeItem);
    auto& relatedGroups = *relatedGroupsInfo.relatedGroups;
    destinationContainersPerGroup.reserve(relatedGroups.size());
    for (const auto i : folly::irange(relatedGroups.size())) {
      const auto group = relatedGroups[i];
      auto containersPtr = groupToContainersPtr
          ? folly::get_ptr(*groupToContainersPtr, group)
          : nullptr;
      const auto& unsampledContainers = containersPtr
          ? *containersPtr
          : colocationScope.getContainerIds(destinationScopeItem);

      auto validContainers = getValidDestinations(
          unsampledContainers,
          representativeObjectPerGroup[i],
          problem,
          invalidMoveFilter,
          stats);

      destinationContainersPerGroup.emplace_back(
          specInfo_.defaultSampleSize
              ? getRandomSample<std::vector>(
                    validContainers, *specInfo_.defaultSampleSize, rng_)
              : std::move(validContainers));
    }

    auto moveSetsToDestinationScopeItem = getMoveSetsToDestinationScopeItem(
        destinationContainersPerGroup,
        representativeObjectPerGroup,
        problem,
        timer,
        timeLimit);

    moveSets.insert(
        moveSets.end(),
        std::make_move_iterator(moveSetsToDestinationScopeItem.begin()),
        std::make_move_iterator(moveSetsToDestinationScopeItem.end()));
  }

  return moveSets;
}

std::vector<entities::ObjectId>
ColocateGroupsMoveType::getRepresentativeObjectPerGroup(
    entities::GroupId hotObjectGroupId,
    const std::vector<entities::GroupId>& relatedGroups,
    entities::ObjectId hotObjectId,
    const entities::Set<entities::ContainerId>& sourceContainers,
    const entities::Partition& partition,
    const Problem& problem) {
  // for each group in relatedGroups, take a representative object from
  // sourceScopeItem; if no such object exists, we will skip the group
  // TODO: make this more efficient by perhaps using IndexedAssignment?
  std::vector<entities::ObjectId> representativeObjectPerGroup;
  representativeObjectPerGroup.reserve(relatedGroups.size());
  for (auto relatedGroupId : relatedGroups) {
    auto repObjectOpt = (relatedGroupId == hotObjectGroupId)
        ? hotObjectId
        : getRepresentativeObjectFromSourceScopeItem(
              sourceContainers, relatedGroupId, partition, problem);
    if (!repObjectOpt.has_value()) {
      break;
    } else {
      representativeObjectPerGroup.emplace_back(repObjectOpt.value());
    }
  }

  return representativeObjectPerGroup;
}

std::optional<entities::ObjectId>
ColocateGroupsMoveType::getRepresentativeObjectFromSourceScopeItem(
    const entities::Set<entities::ContainerId>& sourceContainers,
    entities::GroupId groupId,
    const entities::Partition& partition,
    const Problem& problem) {
  // for each group, take an object from the given sourceScopeItem
  for (auto repObject : partition.getObjectIds(groupId)) {
    auto currContainer = problem.assignment.getContainer(repObject);
    if (sourceContainers.contains(currContainer)) {
      return repObject;
    }
  }

  return std::nullopt;
}

std::vector<MoveSet> ColocateGroupsMoveType::getMoveSetsToDestinationScopeItem(
    const std::vector<std::vector<entities::ContainerId>>&
        destinationContainersPerGroup,
    const std::vector<entities::ObjectId>& representativeObjectPerGroup,
    const Problem& problem,
    const algopt::Timer& timer,
    double timeLimit) {
  std::vector<MoveSet> moveSets;
  auto cartestianProduct =
      MultiCollectionCartesianProduct(destinationContainersPerGroup);
  // Sample the clock every kTimeCheckInterval iterations rather than on every
  // one: the candidate space can be ~1B, so reading the clock per iteration
  // adds measurable overhead. This still bounds the overrun to at most
  // kTimeCheckInterval extra move sets.
  constexpr size_t kTimeCheckInterval = 1024;
  size_t numProcessed = 0;
  for (const auto& destinationContainers : cartestianProduct) {
    if ((numProcessed++ & (kTimeCheckInterval - 1)) == 0 &&
        timer.getSeconds() >= timeLimit) {
      return moveSets;
    }
    MoveSet moveSet;
    for (const auto j : folly::irange(destinationContainers.size())) {
      const auto objectId = representativeObjectPerGroup[j];
      const auto destinationContainer = destinationContainers[j];
      const auto sourceContainer = problem.assignment.getContainer(objectId);
      moveSet.insert(Move(objectId, sourceContainer, destinationContainer));
    }
    moveSets.emplace_back(std::move(moveSet));
  }

  return moveSets;
}

ColocateGroupsMoveType::SpecInfo ColocateGroupsMoveType::buildSpecInfo(
    const interface::ColocateGroupsMoveTypeSpec& spec,
    const Problem& problem) {
  const auto& universe = problem.getUniverse();
  const auto& partitionName = *spec.partitionName();
  const auto partitionId = universe.getPartitionId(partitionName);
  const auto& partition = universe.getPartition(partitionId);
  const auto& colocationScopeName = *spec.colocationScopeName();
  auto colocationScopeId = universe.getScopeId(colocationScopeName);
  const auto& relatedGroupsList = *spec.relatedGroupsList();

  // check that partition is disjoint
  if (!partition.isDisjoint()) {
    throw std::runtime_error(
        fmt::format(
            "Partition '{}' used in ColocateGroupsMoveTyope is not disjoint, but it is expected to be so",
            partitionName));
  }

  entities::Map<entities::GroupId, RelatedGroupsInfoId> groupIdToRelatedGroups;
  for (auto& relatedGroupsInfo : relatedGroupsList) {
    // convert each group in relatedGroups to ids
    auto relatedGroupsIds = std::make_shared<std::vector<entities::GroupId>>();
    relatedGroupsIds->reserve(relatedGroupsInfo.relatedGroups()->size());
    for (auto& groupName : *relatedGroupsInfo.relatedGroups()) {
      auto groupId = universe.getGroupId(partitionId, groupName);
      relatedGroupsIds->push_back(groupId);
    }

    // convert each colocation scope item in relatedGroups to ids
    std::shared_ptr<std::vector<entities::ScopeItemId>>
        destinationScopeItemIds = nullptr;
    if (relatedGroupsInfo.destinationScopeItems().has_value()) {
      destinationScopeItemIds =
          std::make_shared<std::vector<entities::ScopeItemId>>();
      destinationScopeItemIds->reserve(
          relatedGroupsInfo.destinationScopeItems()->size());
      for (auto& scopeItemName : *relatedGroupsInfo.destinationScopeItems()) {
        auto scopeItemId =
            universe.getScopeItemId(colocationScopeId, scopeItemName);
        destinationScopeItemIds->push_back(scopeItemId);
      }
    }

    for (auto groupId : *relatedGroupsIds) {
      groupIdToRelatedGroups.emplace(
          groupId,
          RelatedGroupsInfoId{
              .relatedGroups = relatedGroupsIds,
              .destinationScopeItemIds = destinationScopeItemIds,
          });
    }
  }

  auto& colocationScopeItemToGroupToContainers =
      *spec.colocationScopeItemToGroupToContainers();
  entities::Map<
      entities::ScopeItemId,
      entities::Map<entities::GroupId, entities::Set<entities::ContainerId>>>
      colocationScopeItemIdToGroupIdToContainerIds;

  for (auto& [scopeItemName, groupToContainers] :
       colocationScopeItemToGroupToContainers) {
    auto scopeItemId =
        universe.getScopeItemId(colocationScopeId, scopeItemName);
    for (auto& [groupName, containers] : groupToContainers) {
      auto groupId = universe.getGroupId(partitionId, groupName);

      // convert containers to ids
      entities::Set<entities::ContainerId> containerIds;
      for (auto& containerName : containers) {
        containerIds.insert(universe.getContainerId(containerName));
      }
      colocationScopeItemIdToGroupIdToContainerIds[scopeItemId][groupId] =
          std::move(containerIds);
    }
  }

  return SpecInfo{
      .partitionId = partitionId,
      .colocationScopeId = colocationScopeId,
      .groupIdToRelatedGroups = std::move(groupIdToRelatedGroups),
      .colocationScopeItemToGroupToContainers =
          std::move(colocationScopeItemIdToGroupIdToContainerIds),
      .defaultSampleSize = spec.defaultSampleSize().to_optional(),
  };
}

} // namespace facebook::rebalancer
