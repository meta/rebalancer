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

#include "algopt/rebalancer/solver/utils/OneObjectPerGroup.h"

#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <folly/container/F14Map.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/container/MapUtil.h>

#include <assert.h>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

OneObjectPerGroup::OneObjectPerGroup(
    const entities::Universe& universe,
    entities::PartitionId overlapPartitionId,
    entities::ContainerId sourceContainer,
    const Assignment& initialAssignment)
    : universe_(universe),
      overlapPartitionId_(overlapPartitionId),
      sourceContainer_(sourceContainer) {
  initializeAssignedCounts(initialAssignment);
}

bool OneObjectPerGroup::canPick(entities::ObjectId candidateObject) const {
  const auto& partition = universe_.getPartition(overlapPartitionId_);
  const auto& objectToGroupIds = partition.getObjectIdToGroupIds();

  auto groupIdsPtr = folly::get_ptr(objectToGroupIds, candidateObject);
  if (groupIdsPtr == nullptr) {
    return true; // Object not in tracked partition
  }

  // Check if all groups this object belongs to are untouched
  for (const auto& groupId : *groupIdsPtr) {
    if (getAssignedCount(groupId) != 0) {
      return false; // Some object from this group was already picked
    }
  }

  return true; // All groups are untouched, safe to pick
}

int OneObjectPerGroup::getAssignedCount(entities::GroupId groupId) const {
  return folly::get_default(assignedCounts_, groupId, 0);
}

void OneObjectPerGroup::updateOnApply(const ChangeSet& changes) {
  for (const auto& change : changes) {
    if (change.getContainer() == sourceContainer_) {
      recordObjectMove(
          change.getObject(),
          change.getValue() == 1 ? MoveDirection::ToSource
                                 : MoveDirection::FromSource);
    }
  }
}

void OneObjectPerGroup::initializeAssignedCounts(
    const Assignment& initialAssignment) {
  const auto& partition = universe_.getPartition(overlapPartitionId_);

  // Count objects that are NOT in source container (already assigned)
  for (const auto& [objectId, groupIds] : partition.getObjectIdToGroupIds()) {
    const bool isObjectInSourceContainer =
        (initialAssignment.getContainer(objectId) == sourceContainer_);

    if (!isObjectInSourceContainer) {
      for (const auto& groupId : groupIds) {
        assignedCounts_[groupId]++;
      }
    }
  }
}

void OneObjectPerGroup::recordObjectMove(
    entities::ObjectId objectId,
    MoveDirection direction) {
  const auto& partition = universe_.getPartition(overlapPartitionId_);
  const auto& objectToGroupIds = partition.getObjectIdToGroupIds();

  auto groupIdsPtr = folly::get_ptr(objectToGroupIds, objectId);
  if (groupIdsPtr == nullptr) {
    return; // Object not in tracked partition
  }

  const int delta = (direction == MoveDirection::FromSource) ? 1 : -1;

  // Update assigned counter for each group this object belongs to
  for (const auto& groupId : *groupIdsPtr) {
    auto& assignedCount = assignedCounts_[groupId];
    assignedCount += delta;
    assert(assignedCount >= 0);
  }
}

} // namespace facebook::rebalancer
