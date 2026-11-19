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

#pragma once

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/IndexedAssignment.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/lang/Hint.h>

#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

class EquivalenceSets;
class Assignment {
 public:
  Assignment();

  // TODO: T213932536 remove 'useDynamicObjectOrdering' flag and make it the
  // default behaviour
  // TODO: decouple objectOrdering dimension, etc., from Assignment
  explicit Assignment(
      const PackerMap<entities::ContainerId, std::vector<entities::ObjectId>>&
          containerToObjects,
      std::shared_ptr<const ObjectScores> objectScores = nullptr,
      PackerSet<entities::ObjectId> fixedObjects = {},
      bool useDynamicObjectOrdering = false);

  void moveTo(entities::ObjectId objectId, entities::ContainerId containerId);

  const ObjectStore& getObjects(entities::ContainerId containerId) const;

  const ObjectStore& getDynamicObjects(entities::ContainerId containerId) const;

  bool isDynamic(entities::ObjectId objectId) const;

  bool hasContainer(entities::ContainerId containerId) const;

  bool hasObject(entities::ObjectId objectId) const;

  entities::ContainerId getContainer(entities::ObjectId objectId) const;

  // The assignment must outlive the returned view.
  auto getObjects() const {
    return std::views::keys(objectToContainer_);
  }

  inline auto getContainers() const {
    return std::views::keys(containerToObjects_);
  }

  ChangeSet getChanges() const;

  const PackerMap<entities::ObjectId, entities::ContainerId>&
  getObjectToContainerMap() const;
  const PackerMap<entities::ContainerId, ObjectStore>&
  getContainerToObjectsMap() const;

  // Number of objects moved
  int delta(const Assignment& other) const;

  template <class ContainerToObjects>
  void setContainerToObjectsMap(const ContainerToObjects& containerToObjects) {
    objectToContainer_.clear();
    containerToObjects_.clear();
    containerToObjectsDynamic_.clear();

    for (const auto& [containerId, objectIds] : containerToObjects) {
      auto objectStore = objectStoreFactory_.get();
      auto objectStoreDynamic = objectStoreFactory_.get();

      for (auto objectId : objectIds) {
        objectToContainer_.emplace(objectId, containerId);
        objectStore.insert(objectId);

        if (!fixedObjects_.contains(objectId)) {
          objectStoreDynamic.insert(objectId);
        }
      }

      containerToObjects_.emplace(containerId, std::move(objectStore));
      containerToObjectsDynamic_.emplace(
          containerId, std::move(objectStoreDynamic));
    }
  }

  // APIs for building and accessing object indices
  // Note that these indices are only built over dynamic objects
  // The indexes by equivalence sets (1 and 2 below) share the
  // EquivalenceSets::getKey() of the sets they were built from.
  void buildIndexByEquivalentSets(const EquivalenceSets& equivalenceSets);
  bool isIndexedByEquivalentSets(const EquivalenceSets& equivalenceSets) const;
  // These throw unless the index was built from sets with the same key.
  const AssignmentIndexedByEquivSet& getObjectsIndexedByEquivSets(
      const EquivalenceSets& equivalenceSets) const;
  std::vector<entities::ObjectId> getDistinctObjectsFromEquivSetIdx(
      const entities::ContainerId& containerId,
      const EquivalenceSets& equivalenceSets) const;

  void buildIndexByPartition(
      const entities::Universe& universe,
      entities::PartitionId partitionId);
  const AssignmentIndexedByGroupId& getObjectsIndexedByPartition(
      entities::PartitionId partitionId) const;

  void buildIndexByEquivalentSetsAndPartition(
      const entities::Universe& universe,
      const EquivalenceSets& equivalenceSets,
      entities::PartitionId partitionId);
  // Builds the index by equivalence sets if it is missing. For sets with
  // another key than the indexes were built from, rebuilds it and every index
  // by equivalence sets and partition built so far.
  void buildAllIndexesByEquivalentSets(
      const entities::Universe& universe,
      const EquivalenceSets& equivalenceSets);
  const AssignmentIndexedByEquivSetAndGroupId&
  getObjectsIndexedByEquivSetsAndPartition(
      entities::PartitionId partitionId) const;

  const ObjectStore::Factory& getObjectStoreFactory() const;

 private:
  void add(entities::ObjectId objectId, entities::ContainerId containerId);
  void remove(entities::ObjectId objectId, entities::ContainerId containerId);

  ObjectStore& getObjects(
      PackerMap<entities::ContainerId, ObjectStore>& containerToObjects,
      entities::ContainerId containerId);

 private:
  // Full assignment.
  PackerMap<entities::ObjectId, entities::ContainerId> objectToContainer_;
  PackerMap<entities::ContainerId, ObjectStore> containerToObjects_;

  // Assignment of dynamic objects only.
  PackerMap<entities::ContainerId, ObjectStore> containerToObjectsDynamic_;

  PackerSet<entities::ObjectId> fixedObjects_;

  // The next three sets of "IndexedAssignments" are built to facilitate local
  // search, so they are only built using dynamic objects

  // 1. EquivSetId -> Objects  (for every container)
  // The indexing of objects by equivalent set id is useful to quickly obtain
  // the set of equivalent objects present at any given time in a container.
  std::optional<AssignmentIndexedByEquivSet> containerToEquivSetsToObjects_ =
      std::nullopt;
  // EquivalenceSets::getKey() of the sets the indexes by equivalence sets (1
  // and 2) were built from.
  std::string indexedEquivalenceSetsKey_;
  // 2. EquivSetId -> GroupId -> Objects  (for every container)
  // Can be built for more than one partition
  PackerMap<entities::PartitionId, AssignmentIndexedByEquivSetAndGroupId>
      partitionToPerContainerIndexByEquivSetAndGroups_;

  // 3. GroupId -> Objects  (for every container)
  // Can be built for more than one partition
  // The indexing of objects by partition id is useful to quickly obtain the set
  // of groups and their objects in any container
  PackerMap<entities::PartitionId, AssignmentIndexedByGroupId>
      partitionToPerContainerObjectIndexByGroups_;

  std::shared_ptr<const ObjectScores> objectScores_;
  ObjectStore::Factory objectStoreFactory_;
};
} // namespace facebook::rebalancer
