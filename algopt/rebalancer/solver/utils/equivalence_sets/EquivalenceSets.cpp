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

#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectDimension.h"
#include "algopt/rebalancer/entities/Objects.h"
#include "algopt/rebalancer/entities/ObjectScalarDimension.h"
#include "algopt/rebalancer/entities/ObjectValueTypes.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Scope.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <fmt/core.h>
#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/container/irange.h>
#include <folly/Conv.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/Range.h>

#include <map>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>

namespace facebook::rebalancer {

using entities::EquivalenceSetId;
using entities::ObjectId;
using EquivalenceSetsIterator = EquivalenceSets::EquivalenceSetsIterator;

namespace {
[[noreturn]] void throwUnassigned(ObjectId id) {
  throw std::runtime_error(
      fmt::format(
          "object {} has not been assigned to any equivalence set; was finalize() called?",
          id.asInt()));
}
} // namespace

EquivalenceSets::EquivalenceSets(
    const entities::Universe& universe,
    std::string key)
    : universe_(&universe),
      key_(std::move(key)),
      objectIdToSetId_(universe.getNumObjects(), kUnassignedSetId) {}

const std::string& EquivalenceSets::getKey() const {
  return key_;
}

size_t EquivalenceSets::size() const {
  return sets_.size();
}

const PackerSet<ObjectId>& EquivalenceSets::getSet(EquivalenceSetId idx) const {
  return sets_[static_cast<size_t>(idx)];
}

EquivalenceSetId EquivalenceSets::at(ObjectId element) const {
  const auto setId = objectIdToSetId_[element.asIndex()];
  if (setId == kUnassignedSetId) [[unlikely]] {
    throwUnassigned(element);
  }
  return EquivalenceSetId(setId);
}

bool EquivalenceSets::hasObject(ObjectId objectId) const {
  return objectIdToSetId_[objectId.asIndex()] != kUnassignedSetId;
}

EquivalenceSetsIterator::EquivalenceSetsIterator(
    const EquivalenceSets& sets,
    size_t idx)
    : sets_(sets), iterIndex_(idx) {}
bool EquivalenceSetsIterator::operator==(
    const EquivalenceSetsIterator& other) const {
  return iterIndex_ == other.iterIndex_;
}
bool EquivalenceSetsIterator::operator!=(
    const EquivalenceSetsIterator& other) const {
  return !(*this == other);
}
EquivalenceSetsIterator& EquivalenceSetsIterator::operator++() {
  ++iterIndex_;
  return *this;
}
EquivalenceSetId EquivalenceSetsIterator::operator*() const {
  return EquivalenceSetId(iterIndex_);
}

EquivalenceSetsIterator EquivalenceSetsIterator::begin() const {
  return EquivalenceSetsIterator(sets_, 0);
}

EquivalenceSetsIterator EquivalenceSetsIterator::end() const {
  return EquivalenceSetsIterator(sets_, sets_.size());
}

EquivalenceSetsIterator EquivalenceSets::ids() const {
  return EquivalenceSetsIterator(*this, sets_.size());
}

void EquivalenceSets::print() const {
  std::stringstream stream;
  std::map<size_t, int> counts;
  for (const auto& it : sets_) {
    counts[it.size()]++;
  }
  stream << fmt::format(
      "Equivalence sets {}, set size => number of sets:\n", sets_.size());

  int printedCount = 0;
  for (auto it : counts) {
    stream << fmt::format("{:5}: {:<5} ", it.first, it.second);
    printedCount++;
    if (printedCount % 10 == 0) {
      stream << "\n";
    }
  }
  XLOG(DBG1) << stream.str();
}

void EquivalenceSets::addToNewSet(const std::vector<ObjectId>& items) {
  const auto newSetId = folly::to<int>(sets_.size());
  auto& newSet = sets_.emplace_back();
  newSet.reserve(folly::to<PackerSet<ObjectId>::size_type>(items.size()));
  for (const auto item : items) {
    objectIdToSetId_[item.asIndex()] = newSetId;
    newSet.insert(item);
  }
}

template <typename InputContainer>
void EquivalenceSets::combine(const InputContainer& input) {
  // For each existing set the given `input` touches, items in that set that are
  // also in the input are pulled out into a new set (unless they already
  // are the whole set). Items not in any set yet form one new set.
  if (combineInputBySetId_.size() < sets_.size()) {
    combineInputBySetId_.resize(sets_.size());
  }

  std::vector<int> setIdsInInput;
  std::vector<ObjectId> unseenObjects;
  for (const auto item : input) {
    const auto setId = objectIdToSetId_[item.asIndex()];
    if (setId == kUnassignedSetId) {
      unseenObjects.push_back(item);
    } else {
      auto& subset = combineInputBySetId_[setId];
      if (subset.empty()) {
        setIdsInInput.push_back(setId);
      }
      subset.push_back(item);
    }
  }

  for (const auto setId : setIdsInInput) {
    auto& subset = combineInputBySetId_[setId];
    // If the subset is the whole set, we don't need to create a new set.
    if (subset.size() != sets_[setId].size()) {
      for (const auto item : subset) {
        sets_[setId].erase(item);
      }
      addToNewSet(subset);
    }
    subset.clear();
  }

  if (!unseenObjects.empty()) {
    seenObjects_ += unseenObjects.size();
    addToNewSet(unseenObjects);
  }
}

void EquivalenceSets::finalize() {
  const auto objectIds = universe_->getObjects().getObjectIds();
  const auto unseenCount = objectIds.size() - seenObjects_;
  if (unseenCount == 0) {
    return;
  }

  const int newSetId = static_cast<int>(sets_.size());
  sets_.emplace_back();
  sets_.back().reserve(
      static_cast<PackerSet<ObjectId>::size_type>(unseenCount));
  for (const auto objectId : objectIds) {
    auto& slot = objectIdToSetId_[objectId.asIndex()];
    if (slot == kUnassignedSetId) {
      slot = newSetId;
      sets_.back().insert(objectId);
    }
  }
  seenObjects_ = objectIds.size();
}

template void EquivalenceSets::combine<std::vector<ObjectId>>(
    const std::vector<ObjectId>&);
template void EquivalenceSets::combine<entities::Set<ObjectId>>(
    const entities::Set<ObjectId>&);

void EquivalenceSets::mappingMerge(const entities::ObjectValues& values) {
  values.visit(
      [this](const entities::ObjectIdToDoubleMap& map) { mappingMerge(map); },
      [this](
          entities::PartitionId partitionId,
          const entities::GroupIdToDoubleMap& groupValues) {
        for (const auto& groupEntry : groupValues) {
          mappingMerge(partitionId, groupEntry.first);
        }
      });
}

void EquivalenceSets::mappingMerge(
    ExprId exprId,
    const entities::ObjectValues& values) {
  if (!seenExprIds_.insert(exprId).second) {
    return;
  }
  mappingMerge(values);
}

void EquivalenceSets::mappingMerge(entities::PartitionId partitionId) {
  const auto& groupIds = universe_->getPartition(partitionId).getGroupIds();
  for (const auto groupId : groupIds) {
    mappingMerge(partitionId, groupId);
  }
}

void EquivalenceSets::mappingMerge(
    entities::PartitionId partitionId,
    entities::GroupId groupId) {
  if (!seenPartitionGroups_.emplace(partitionId, groupId).second) {
    return;
  }
  combine(universe_->getPartition(partitionId).getObjectIds(groupId));
}

void EquivalenceSets::mappingMerge(entities::DimensionId dimensionId) {
  if (!seenDimensions_.insert(dimensionId).second) {
    return;
  }
  const auto& objectDim = universe_->getObjects().getDimension(dimensionId);
  if (!objectDim.isDynamic()) {
    for (const auto i : folly::irange(objectDim.size())) {
      mappingMerge(objectDim.at(i).values());
    }
    return;
  }
  for (const auto i : folly::irange(objectDim.size())) {
    const auto& scalar = objectDim.at(i);
    const auto& scopeItemIds =
        universe_->getScope(scalar.getScopeId()).getScopeItemIds();
    for (const auto scopeItemId : scopeItemIds) {
      mappingMerge(scalar.values(scopeItemId));
    }
  }
}

void EquivalenceSets::mappingMerge(
    entities::DimensionId dimensionId,
    entities::ScopeItemId scopeItemId) {
  if (!seenDimensionScopeItems_.emplace(dimensionId, scopeItemId).second) {
    return;
  }
  const auto& objectDim = universe_->getObjects().getDimension(dimensionId);
  for (const auto i : folly::irange(objectDim.size())) {
    const auto& scalar = objectDim.at(i);
    mappingMerge(
        scalar.isDynamic() ? scalar.values(scopeItemId) : scalar.values());
  }
}

} // namespace facebook::rebalancer
