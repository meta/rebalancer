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

#include "algopt/rebalancer/solver/utils/AffectedByChangeInfo.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Set.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <stdexcept>
#include <utility>

namespace facebook::rebalancer {

AffectedByChange::AffectedByChange(bool affectedByAllChanges)
    : affectedByAllChanges_(affectedByAllChanges) {}

AffectedByChange::AffectedByChange(
    std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr,
    ChangeFilterFn changeFilter)
    : containersPtr_(std::move(containersPtr)),
      filter_(std::move(changeFilter)) {
  if (containersPtr_ == nullptr) {
    throw std::runtime_error("expected non nullptr as input for containersPtr");
  }
}

AffectedByChange::AffectedByChange(
    std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPtr,
    ChangeFilterFn changeFilter)
    : objectsPtr_(std::move(objectsPtr)), filter_(std::move(changeFilter)) {
  if (objectsPtr_ == nullptr) {
    throw std::runtime_error("expected non nullptr as input for objectsPtr");
  }
}

AffectedByChange::AffectedByChange(
    std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr,
    std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPtr)
    : containersPtr_(std::move(containersPtr)),
      objectsPtr_(std::move(objectsPtr)) {
  if (containersPtr_ == nullptr || objectsPtr_ == nullptr) {
    throw std::runtime_error(
        "expected non nullptr as inputs for containersPtr and objectsPtr");
  }
}

AffectedByChange::AffectedByChange(
    std::shared_ptr<const entities::Set<entities::ContainerId>> containersPtr,
    std::shared_ptr<const entities::Set<entities::ObjectId>> objectsPtr,
    const AffectedByChangeDecisionData& data) {
  if (containersPtr == nullptr || objectsPtr == nullptr) {
    throw std::runtime_error(
        "expected non nullptr as inputs for containersPtr and objectsPtr");
  }

  const auto affectedContainerSize = containersPtr->size();
  const auto affectedObjectsSize = objectsPtr->size();
  const bool allContainersAffected =
      (affectedContainerSize == data.numTotalContainers);
  const bool allObjectsAffected = (affectedObjectsSize == data.numTotalObjects);

  if (allContainersAffected && allObjectsAffected) {
    affectedByAllChanges_ = true;
  } else if (allContainersAffected) {
    objectsPtr_ = std::move(objectsPtr);
  } else if (allObjectsAffected) {
    containersPtr_ = std::move(containersPtr);
  } else {
    containersPtr_ = std::move(containersPtr);
    objectsPtr_ = std::move(objectsPtr);
  }
}

const entities::Set<entities::ContainerId>& AffectedByChange::getContainers()
    const {
  if (!containersPtr_) {
    throw std::runtime_error(
        "unexpected call to getContainers() when containerPtr_ is null");
  }

  return *containersPtr_;
}

const entities::Set<entities::ObjectId>& AffectedByChange::getObjects() const {
  if (!objectsPtr_) {
    throw std::runtime_error(
        "unexpected call to getObjects() when objectPtr_ is null");
  }

  return *objectsPtr_;
}

AffectedByChangeType AffectedByChange::getType() const {
  if (affectedByAllChanges_) {
    return AffectedByChangeType::ALL_CHANGES;
  } else if (containersPtr_ && objectsPtr_) {
    return AffectedByChangeType::ALL_GIVEN_CONTAINER_OBJECT_PAIRS;
  } else if (containersPtr_) {
    return AffectedByChangeType::CONTAINERS_ONLY;
  } else if (objectsPtr_) {
    return AffectedByChangeType::OBJECTS_ONLY;
  } else {
    throw std::runtime_error(
        "Unknown affectedByChange type. Has AffectedByChange been properly initialized?");
  }
}

} // namespace facebook::rebalancer
