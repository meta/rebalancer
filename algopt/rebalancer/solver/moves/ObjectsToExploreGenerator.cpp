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

#include "algopt/rebalancer/solver/moves/ObjectsToExploreGenerator.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"

namespace facebook::rebalancer {

using entities::EquivalenceSetId;
using entities::ObjectId;

ObjectsToExploreGenerator::ObjectsToExploreGenerator(
    const entities::Universe& universe)
    : universe_(universe) {}

std::vector<ObjectBundle> ObjectsToExploreGenerator::getObjectBundlesToExplore(
    const std::string& partition,
    entities::ContainerId srcContainer,
    int bundleSize,
    const folly::F14FastMap<entities::GroupId, int>& groupBundleSizeOverrides,
    Assignment& assignment,
    const EquivalenceSets& equivalenceSets,
    bool createGreedyHeterogenousBundles) {
  std::vector<ObjectBundle> objectBundles;

  const entities::PartitionId partitionId = universe_.getPartitionId(partition);
  assignment.buildIndexByPartition(universe_, partitionId);

  const auto& groupToObjects =
      assignment.getObjectsIndexedByPartition(partitionId)
          .getContainerObjects(srcContainer);

  for (const auto& [groupId, objectsInGroup] : groupToObjects) {
    const size_t groupBundleSize = static_cast<size_t>(
        folly::get_default(groupBundleSizeOverrides, groupId, bundleSize));

    if (objectsInGroup.size() < groupBundleSize) {
      continue;
    }
    std::mt19937 rng(0);
    // Step 1: Dedup
    folly::F14FastMap<EquivalenceSetId, std::vector<ObjectId>>
        equivSetsToObjects;
    for (const auto object : objectsInGroup) {
      equivSetsToObjects[equivalenceSets.at(object)].push_back(object);
    }
    ReferenceList<const std::vector<ObjectId>> slidingWindow;
    const size_t numEquivSets = equivSetsToObjects.size();
    size_t numEquivSetsProcessed = 0;
    for (const auto& [_, objectsInEquivSet] : equivSetsToObjects) {
      const bool lastEquivSet = ++numEquivSetsProcessed == numEquivSets;
      // Step 2: Form homogenous bundles
      if (objectsInEquivSet.size() >= groupBundleSize) {
        ObjectBundle bundle(
            {objectsInEquivSet.begin(),
             objectsInEquivSet.begin() + groupBundleSize});
        objectBundles.push_back(std::move(bundle));
      }

      // Step 3: Heterogeneous bundles
      // If createGreedyHeterogenousBundles is true, we create a sliding window
      // of equivalence sets and generate bundles from equivalence sets inside
      // the sliding window. We aim to have the size of the sliding window to be
      // `groupBundleSize` to form diverse heterogenous bundles.
      if (createGreedyHeterogenousBundles && numEquivSets > 1) {
        slidingWindow.push_back(std::cref(objectsInEquivSet));
        if (slidingWindow.size() == groupBundleSize || lastEquivSet) {
          const PackerSet<entities::ObjectId> bundleSet =
              getEqualSizeRandomSamplesFromEachSetIn<entities::ObjectId>(
                  slidingWindow, bundleSize, rng, std::nullopt);
          std::vector<ObjectId> bundle(bundleSet.begin(), bundleSet.end());
          objectBundles.emplace_back(std::move(bundle));
          slidingWindow.pop_front();
        }
      }
    }
  }

  return objectBundles;
}

ReferenceList<const std::vector<entities::ObjectId>>
ObjectsToExploreGenerator::getObjectsToExplore(
    const std::string& partitionName,
    entities::ContainerId hotContainer,
    const Assignment& assignment,
    const EquivalenceSets& equivalenceSets,
    MoveStatsAggregator& stats,
    const InvalidMoveFilter* invalidMoveFilter) {
  auto partitionId = universe_.getPartitionId(partitionName);
  auto& partition = universe_.getPartition(partitionId);

  auto& groups = partition.getGroupToObjectIds();

  ReferenceList<const std::vector<entities::ObjectId>> objectsToExplore;
  for (auto& [groupId, objectIds] : groups) {
    std::vector<entities::ObjectId> objectsInGroup;
    for (auto& objectId : objectIds) {
      auto containerId = assignment.getContainer(objectId);
      if (containerId != hotContainer &&
          !isMoveInvalid(invalidMoveFilter, objectId, hotContainer, stats)) {
        objectsInGroup.push_back(objectId);
      }
    }

    const GenericObjectDeduper<std::vector<entities::ObjectId>::const_iterator>
        dedupedObjs(
            &equivalenceSets, objectsInGroup.begin(), objectsInGroup.end());
    groupToObjects_[groupId].clear();
    for (auto objectId : dedupedObjs) {
      groupToObjects_[groupId].push_back(objectId);
    }
    objectsToExplore.emplace_back(groupToObjects_[groupId]);
  }
  return objectsToExplore;
}

} // namespace facebook::rebalancer
