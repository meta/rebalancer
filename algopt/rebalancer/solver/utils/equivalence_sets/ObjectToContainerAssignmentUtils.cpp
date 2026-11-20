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

#include "algopt/rebalancer/solver/utils/equivalence_sets/ObjectToContainerAssignmentUtils.h"

#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <random>
#include <ranges>
#include <set>
#include <stdexcept>

namespace facebook::rebalancer {

using entities::ContainerId;
using entities::EquivalenceSetId;
using entities::ObjectId;
using EquivalenceSetsIterator = EquivalenceSets::EquivalenceSetsIterator;

void ObjectToContainerAssignmentUtils::addStableContainerGroup(
    std::shared_ptr<const PackerSet<ContainerId>> containersPtr) {
  stableContainerGroups_.push_back(std::move(containersPtr));
}

std::vector<ContainerId>
ObjectToContainerAssignmentUtils::getPreferredContainers(
    ContainerId container) const {
  // in short, this API should be able to provide correct order on containers
  // so for whatever stable stayed requirements, as long as there is a solution
  // we should be able to translate to get correct assignment
  std::vector<ContainerId> preferredContainers;
  // container to how many times it appears in stableGroup
  PackerMap<ContainerId, int> appearanceCount;
  for (auto& group : stableContainerGroups_) {
    if (!group->contains(container)) {
      continue;
    }
    // only when container in this group,
    // we consider the group for this container
    for (auto groupMember : *group) {
      appearanceCount[groupMember] += 1;
    }
  }

  // sort containers based on appearance count
  std::vector<std::pair<ContainerId, int>> sortedAppearanceCount(
      appearanceCount.begin(), appearanceCount.end());
  sort(
      sortedAppearanceCount.begin(),
      sortedAppearanceCount.end(),
      [](const std::pair<ContainerId, int>& lhs,
         const std::pair<ContainerId, int>& rhs) {
        const auto& [lhsContainer, lhsCount] = lhs;
        const auto& [rhsContainer, rhsCount] = rhs;
        if (lhsCount == rhsCount) {
          return lhsContainer < rhsContainer;
        }
        return lhsCount > rhsCount;
      });
  preferredContainers.reserve(sortedAppearanceCount.size());
  for (auto [containerIndex, _] : sortedAppearanceCount) {
    preferredContainers.push_back(containerIndex);
  }
  return preferredContainers;
}

PackerMap<EquivalenceSetId, PackerMap<ContainerId, std::vector<ObjectId>>>
ObjectToContainerAssignmentUtils::getEquivSetToContainerToObjects(
    const EquivalenceSets& equivalenceSets,
    const Assignment& assignment,
    const std::optional<std::string>& shuffleSeed) {
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, std::vector<ObjectId>>>
      equivSetToContainerToObjects;
  for (auto cont : assignment.getContainers()) {
    for (auto obj : assignment.getObjects(cont)) {
      const auto equivSet = equivalenceSets.at(obj);
      equivSetToContainerToObjects[equivSet][cont].push_back(obj);
    }
  }

  if (shuffleSeed) {
    auto seed = std::seed_seq(shuffleSeed->begin(), shuffleSeed->end());
    std::mt19937 randomGenerator(seed);
    for (auto& [_, containerToObjects] : equivSetToContainerToObjects) {
      for (auto& [_2, objects] : containerToObjects) {
        std::shuffle(objects.begin(), objects.end(), randomGenerator);
      }
    }
  }
  return equivSetToContainerToObjects;
}

void ObjectToContainerAssignmentUtils::
    sanityCheckOnEquivSetToContainerToObjectCount(
        const PackerMap<
            EquivalenceSetId,
            PackerMap<ContainerId, std::vector<ObjectId>>>&
            equivSetToContainerToInitialObjects,
        const PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>&
            equivSetToContainerToObjectCount) {
  // ensure both initial and final assignments are consistent in object counts
  PackerMap<EquivalenceSetId, int> newEquivSetSize;
  for (auto& [equivSet, containerToObjectCount] :
       equivSetToContainerToObjectCount) {
    newEquivSetSize[equivSet] = 0;
    for (auto& [cont, objectCount] : containerToObjectCount) {
      newEquivSetSize[equivSet] += objectCount;
    }
  }
  PackerMap<EquivalenceSetId, int> oldEquivSetSize;
  for (auto& [equivSet, containerToObjects] :
       equivSetToContainerToInitialObjects) {
    oldEquivSetSize[equivSet] = 0;
    for (auto& [cont, objs] : containerToObjects) {
      oldEquivSetSize[equivSet] += objs.size();
    }
    if (!newEquivSetSize.contains(equivSet)) {
      throw std::runtime_error(
          fmt::format("Missing equivSet: {} from final assignment", equivSet));
    }
    if (oldEquivSetSize.at(equivSet) != newEquivSetSize.at(equivSet)) {
      throw std::runtime_error(
          fmt::format(
              "Inconsistent membership of equivSet: {}, initial count: {}, final count: {}",
              equivSet,
              oldEquivSetSize.at(equivSet),
              newEquivSetSize.at(equivSet)));
    }
  }
  if (oldEquivSetSize.size() != newEquivSetSize.size()) {
    throw std::runtime_error(
        fmt::format(
            "Mismatch in number of equiv sets in initial: {} and final assignment: {}",
            oldEquivSetSize.size(),
            newEquivSetSize.size()));
  }
}

std::vector<PackerSet<ContainerId>>
ObjectToContainerAssignmentUtils::getSourceContainerOrder() {
  if (sourceContainerOrder_.has_value()) {
    return sourceContainerOrder_.value();
  }
  sourceContainerOrder_.emplace();
  // generate a list of container with order on
  // which one we should pick first to consider
  PackerMap<int, PackerSet<ContainerId>> stableGroupSizeToContainers;
  PackerMap<ContainerId, int> containerToGroupSize;
  std::set<int> groupSizes; // ordered set
  for (auto& group : stableContainerGroups_) {
    const int groupSize = group->size();
    for (auto container : *group) {
      groupSizes.insert(groupSize);
      if (containerToGroupSize.contains(container)) {
        if (containerToGroupSize.at(container) > groupSize) {
          // erase previous saving
          stableGroupSizeToContainers[containerToGroupSize.at(container)].erase(
              container);
        } else if (containerToGroupSize.at(container) < groupSize) {
          continue; // this container belongs to a smaller group
        }
      }
      containerToGroupSize[container] = groupSize;
      stableGroupSizeToContainers[groupSize].insert(container);
    }
  }

  for (auto groupSize : groupSizes) {
    if (stableGroupSizeToContainers[groupSize].size()) {
      sourceContainerOrder_.value().push_back(
          stableGroupSizeToContainers[groupSize]);
    }
  }
  return sourceContainerOrder_.value();
}

std::optional<ContainerId>
ObjectToContainerAssignmentUtils::pickContainerToPlaceObjects(
    const PackerMap<ContainerId, int>& containerToPreferredCount,
    const PackerSet<ContainerId>& containersInSmallestGroup) {
  // 1. pick container with least choices to work on first
  std::optional<ContainerId> leastChoiceContainer = std::nullopt;
  int leastChoiceCount = std::numeric_limits<int>::max();
  for (auto container : containersInSmallestGroup) {
    if (!containerToPreferredCount.contains(container)) {
      continue;
    }
    auto& preferredContainerCount = containerToPreferredCount.at(container);
    if (preferredContainerCount < leastChoiceCount) {
      leastChoiceContainer = container;
      leastChoiceCount = preferredContainerCount;
    }
  }
  return leastChoiceContainer;
}

void ObjectToContainerAssignmentUtils::updatePreferredCount(
    const PackerSet<ContainerId>& emptyContainers,
    PackerMap<ContainerId, int>& containerToPreferredCount) const {
  // remove emptyContainers from preferred containers
  for (auto preferredContainer : emptyContainers) {
    if (!preferredContainerToContainers_.contains(preferredContainer)) {
      continue;
    }
    for (auto container :
         preferredContainerToContainers_.at(preferredContainer)) {
      if (containerToPreferredCount.contains(container)) {
        containerToPreferredCount[container] -= 1;
        if (containerToPreferredCount[container] == 0) {
          containerToPreferredCount.erase(container);
        }
      }
    }
  }
}

static void assignObjects(
    ContainerId toContainer,
    int& objectCount,
    std::vector<ObjectId>& initialObjects,
    PackerMap<ObjectId, ContainerId>& objectToContainer) {
  while (objectCount && !initialObjects.empty()) {
    objectToContainer.insert_or_assign(initialObjects.back(), toContainer);
    initialObjects.pop_back();
    objectCount -= 1;
  }
}

void ObjectToContainerAssignmentUtils::assignObjectToContainerForOneSet(
    PackerMap<ObjectId, ContainerId>& objectToContainer,
    PackerMap<ContainerId, int> containerToObjectCount,
    PackerMap<ContainerId, std::vector<ObjectId>> containerToInitialObjects,
    const std::vector<PackerSet<ContainerId>>& sourceContainerOrder) const {
  // Step 1 - mainly for performane consideration
  // check difference between containerToObjectCount and
  // containerToInitialObjects only work on difference later on
  //
  // later on process involes checking on stable groups and preferences etc
  // are expensive
  for (auto& [container, objectCount] : containerToObjectCount) {
    if (containerToInitialObjects.contains(container) &&
        containerToObjectCount.contains(container)) {
      assignObjects(
          container,
          containerToObjectCount[container],
          containerToInitialObjects[container],
          objectToContainer);
    }
    if (containerToObjectCount[container] == 0) {
      containerToObjectCount.erase(container);
    }
  }

  // check containerToInitialObjects
  // remove initially empty containers from preferred container list
  //
  // NOTE: it is very important for performance to clean up
  // containerToObjectCount, containerToInitialObjects,
  // containerToPreferredCount to avoid extra loops
  PackerSet<ContainerId> emptyContainers;
  for (auto& [container, initialObjects] : containerToInitialObjects) {
    if (initialObjects.empty()) {
      emptyContainers.insert(container);
    }
  }
  PackerMap<ContainerId, int> containerToPreferredCount;
  const PackerMap<ContainerId, std::vector<ContainerId>>&
      containerToPreferredContainers = containerToPreferredContainers_.value();
  for (auto& [container, preferredContainers] :
       containerToPreferredContainers) {
    if (!containerToObjectCount.contains(container)) {
      continue; // no need to save preferred info for this
    }
    containerToPreferredCount[container] = preferredContainers.size();
  }
  updatePreferredCount(emptyContainers, containerToPreferredCount);
  // first we assign objects from preffered locations
  auto sourceContainerGroup = sourceContainerOrder.begin();
  while (containerToPreferredCount.size() != 0 &&
         containerToObjectCount.size() != 0) {
    // work on assigning objects to container has least choices first
    auto containerOpt = pickContainerToPlaceObjects(
        containerToPreferredCount, *sourceContainerGroup);
    while (containerOpt == std::nullopt) {
      ++sourceContainerGroup;
      containerOpt = pickContainerToPlaceObjects(
          containerToPreferredCount, *sourceContainerGroup);
    }
    auto container = *containerOpt;
    if (containerToObjectCount[container] == 0) {
      containerToObjectCount.erase(
          container); // allocation for this container is done
      containerToPreferredCount.erase(container); // no need to save its choices
      continue;
    }
    PackerSet<ContainerId> toBeClearedContainers;
    for (auto preferredContainer :
         containerToPreferredContainers.at(container)) {
      auto& objects = containerToInitialObjects[preferredContainer];
      auto& objectCount = containerToObjectCount[container];
      assignObjects(container, objectCount, objects, objectToContainer);
      if (objects.empty() && !emptyContainers.contains(preferredContainer)) {
        // this preferred container has no objects left. no longer an option
        // for performance optimization, updatePreferredCount assumes
        // one container will only be passed down to clear once
        emptyContainers.insert(preferredContainer);
        toBeClearedContainers.insert(preferredContainer);
      }
      if (objectCount == 0) {
        containerToObjectCount.erase(
            container); // allocation for this container is done
        containerToPreferredCount.erase(
            container); // no need to save its choices
        break;
      }
    }
    updatePreferredCount(toBeClearedContainers, containerToPreferredCount);
  }
  // Then we randomly assign the rest objects
  // to each container.
  for (auto& [container, objectCount] : containerToObjectCount) {
    if (objectCount == 0) {
      continue;
    }
    for (auto& [fromContainer, objects] : containerToInitialObjects) {
      assignObjects(container, objectCount, objects, objectToContainer);
      if (objectCount == 0) {
        break;
      }
    }
  }
}

void ObjectToContainerAssignmentUtils::getContainerToPreferredContainers(
    const auto& containers) {
  if (containerToPreferredContainers_.has_value()) {
    return;
  }
  containerToPreferredContainers_.emplace();
  for (auto container : containers) {
    auto preferredContainers = getPreferredContainers(container);
    if (preferredContainers.size() == 0) {
      continue;
    }
    containerToPreferredContainers_.value()[container] = preferredContainers;
    for (auto preferredContainer : preferredContainers) {
      preferredContainerToContainers_[preferredContainer].insert(container);
    }
  }
}

PackerMap<ObjectId, ContainerId>
ObjectToContainerAssignmentUtils::getObjectToContainer(
    const EquivalenceSets& equivalenceSets,
    const PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>&
        equivSetToContainerToObjectCount,
    const Assignment& initialAssignment,
    const std::optional<std::string>& shuffleSeed) {
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, std::vector<ObjectId>>>
      equivSetToContainerToInitialObjects = getEquivSetToContainerToObjects(
          equivalenceSets, initialAssignment, shuffleSeed);

  sanityCheckOnEquivSetToContainerToObjectCount(
      equivSetToContainerToInitialObjects, equivSetToContainerToObjectCount);

  getContainerToPreferredContainers(initialAssignment.getContainers());
  auto sourceContainerOrder = getSourceContainerOrder();

  PackerMap<ObjectId, ContainerId> objectToContainer;
  for (const auto& [equivSetIndex, containerToObjectCount] :
       equivSetToContainerToObjectCount) {
    assignObjectToContainerForOneSet(
        objectToContainer,
        containerToObjectCount,
        equivSetToContainerToInitialObjects.at(equivSetIndex),
        sourceContainerOrder);
  }
  return objectToContainer;
}

} // namespace facebook::rebalancer
