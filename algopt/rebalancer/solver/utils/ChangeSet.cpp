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

#include "algopt/rebalancer/solver/utils/ChangeSet.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/utils/Change.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <folly/container/MapUtil.h>

namespace facebook::rebalancer {

ChangeSet::ChangeSet() = default;

ChangeSet::ChangeSet(std::vector<Change> changes)
    : changes_(std::move(changes)) {
  for (auto& change : changes_) {
    containerToChanges_[change.getContainer()].push_back(change);
    objectToChanges_[change.getObject()].push_back(change);
  }
}

void ChangeSet::insert(const Change& change) {
  changes_.push_back(change);
  containerToChanges_[change.getContainer()].push_back(change);
  objectToChanges_[change.getObject()].push_back(change);
}

size_t ChangeSet::size() const {
  return changes_.size();
}

const Change& ChangeSet::at(size_t index) const {
  return changes_.at(index);
}

ChangeSet ChangeSet::getInverse() const {
  std::vector<Change> inverse;
  inverse.reserve(changes_.size());
  for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) {
    inverse.emplace_back(it->getObject(), it->getContainer(), -it->getValue());
  }
  return ChangeSet(std::move(inverse));
}

std::vector<Change>::const_iterator ChangeSet::begin() const {
  return changes_.begin();
}

std::vector<Change>::const_iterator ChangeSet::end() const {
  return changes_.end();
}

bool ChangeSet::empty() const {
  return changes_.empty();
}

const std::vector<Change>& ChangeSet::getChangesByContainer(
    entities::ContainerId containerId) const {
  return folly::get_ref_default(containerToChanges_, containerId, empty_);
}

const std::vector<Change>& ChangeSet::getChangesByObject(
    entities::ObjectId objectId) const {
  return folly::get_ref_default(objectToChanges_, objectId, empty_);
}

} // namespace facebook::rebalancer
