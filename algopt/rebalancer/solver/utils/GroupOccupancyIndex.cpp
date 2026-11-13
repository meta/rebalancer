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

#include "algopt/rebalancer/solver/utils/GroupOccupancyIndex.h"

#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"

#include <fmt/core.h>
#include <folly/container/MapUtil.h>

#include <stdexcept>

namespace facebook::rebalancer {

namespace {

const GroupOccupancyIndex::ObjectsByEquivSetAndOccupancy kNoObjects;

} // namespace

GroupOccupancyIndex::GroupOccupancyIndex(
    const entities::Universe& universe,
    entities::PartitionId partitionId,
    entities::PartitionId serverPartitionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    const EquivalenceSets& equivalenceSets,
    const Assignment& assignment)
    : partition_(universe.getPartition(partitionId)),
      scopeItemContainers_(
          universe.getScope(scopeId).getContainerIds(scopeItemId)) {
  if (!partition_.isDisjoint()) {
    throw std::invalid_argument(
        fmt::format(
            "GroupOccupancyIndex needs a disjoint partition, {} is not",
            universe.getEntityName(partitionId)));
  }

  const auto& objectToServers =
      universe.getPartition(serverPartitionId).getObjectIdToGroupIds();
  // Fixed objects count too, so the occupancies cover the full assignment.
  for (const auto& [objectId, containerId] :
       assignment.getObjectToContainerMap()) {
    const auto groupId = getGroup(objectId);
    if (groupId && isInScopeItem(containerId)) {
      ++groupToOccupancy_[*groupId];
    }
    if (assignment.isDynamic(objectId)) {
      objectToIndexEntry_.emplace(
          objectId,
          IndexEntry{
              .equivSetId = equivalenceSets.at(objectId),
              .groupId = groupId,
              .serverId = objectToServers.at(objectId).front(),
              .containerId = containerId});
    }
  }
  for (const auto& [objectId, entry] : objectToIndexEntry_) {
    addToIndex(objectId, entry);
  }
}

const GroupOccupancyIndex::ObjectsByEquivSetAndOccupancy&
GroupOccupancyIndex::getContainerObjects(
    entities::ContainerId containerId) const {
  return folly::get_ref_default(
      containerToIndexedObjects_, containerId, kNoObjects);
}

bool GroupOccupancyIndex::isInScopeItem(
    entities::ContainerId containerId) const {
  return scopeItemContainers_.contains(containerId);
}

std::optional<entities::GroupId> GroupOccupancyIndex::getGroup(
    entities::ObjectId objectId) const {
  const auto* groupIds =
      folly::get_ptr(partition_.getObjectIdToGroupIds(), objectId);
  if (!groupIds || groupIds->empty()) {
    return std::nullopt;
  }
  return groupIds->front();
}

GroupOccupancyIndex::EquivSetAndOccupancy GroupOccupancyIndex::getKey(
    const IndexEntry& entry) const {
  return {
      .equivSetId = entry.equivSetId,
      .occupancy = entry.groupId
          ? folly::get_default(groupToOccupancy_, *entry.groupId, 0)
          : kUngrouped};
}

void GroupOccupancyIndex::addToIndex(
    entities::ObjectId objectId,
    const IndexEntry& entry) {
  containerToIndexedObjects_[entry.containerId][getKey(entry)][entry.serverId]
      .insert(objectId);
}

void GroupOccupancyIndex::removeFromIndex(
    entities::ObjectId objectId,
    const IndexEntry& entry) {
  auto& objectsByEquivSetAndOccupancy =
      containerToIndexedObjects_.at(entry.containerId);
  const auto it = objectsByEquivSetAndOccupancy.find(getKey(entry));
  auto& objectsByServer = it->second;
  const auto serverIt = objectsByServer.find(entry.serverId);
  serverIt->second.erase(objectId);
  if (serverIt->second.empty()) {
    objectsByServer.erase(serverIt);
  }
  if (objectsByServer.empty()) {
    objectsByEquivSetAndOccupancy.erase(it);
  }
}

// The occupancy is part of the key of every indexed object of the group, so
// they all move to the new occupancy.
void GroupOccupancyIndex::addToOccupancy(
    entities::ObjectId objectId,
    int32_t delta) {
  const auto groupId = getGroup(objectId);
  if (!groupId) {
    return;
  }
  const auto& groupObjects = partition_.getObjectIds(*groupId);
  for (const auto groupObject : groupObjects) {
    if (const auto* entry = folly::get_ptr(objectToIndexEntry_, groupObject)) {
      removeFromIndex(groupObject, *entry);
    }
  }
  groupToOccupancy_[*groupId] += delta;
  for (const auto groupObject : groupObjects) {
    if (const auto* entry = folly::get_ptr(objectToIndexEntry_, groupObject)) {
      addToIndex(groupObject, *entry);
    }
  }
}

// An object stays indexed in the container it left until it enters the next,
// so the changes of a set apply in either order.
void GroupOccupancyIndex::updateOnApply(const ChangeSet& changes) {
  for (const auto& change : changes) {
    const auto objectId = change.getObject();
    const auto containerId = change.getContainer();
    const bool isAddition = change.getValue() == 1;
    if (isInScopeItem(containerId)) {
      addToOccupancy(objectId, isAddition ? 1 : -1);
    }
    if (!isAddition) {
      continue;
    }
    if (auto* entry = folly::get_ptr(objectToIndexEntry_, objectId)) {
      removeFromIndex(objectId, *entry);
      entry->containerId = containerId;
      addToIndex(objectId, *entry);
    }
  }
}

} // namespace facebook::rebalancer
