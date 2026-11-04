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

#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"

#include <folly/synchronization/DelayedInit.h>

#include <algorithm>
#include <atomic>
#include <vector>

namespace facebook::rebalancer {

class InvalidMoveFilter {
 public:
  InvalidMoveFilter(size_t numObjects, size_t numContainers);
  InvalidMoveFilter(const InvalidMoveFilter& other);
  InvalidMoveFilter(InvalidMoveFilter&& other) noexcept;
  InvalidMoveFilter& operator=(const InvalidMoveFilter& other) = delete;
  InvalidMoveFilter& operator=(InvalidMoveFilter&& other) = delete;
  ~InvalidMoveFilter() = default;

  // Safe to call concurrently. Reads, copies, and merges require caller
  // synchronization after all marking calls have finished. `objectIds` need
  // not be sorted or unique.
  void markInvalid(
      const std::vector<entities::ObjectId>& objectIds,
      entities::ContainerId containerId);

  void markInvalid(
      const algopt::DynamicBitSet& objectIds,
      entities::ContainerId containerId);

  // Union `other` into this filter: afterwards a pair is invalid if it was
  // invalid in either filter. Both must have the same object/container
  // dimensions. Not thread-safe; call it only once marking has joined.
  void mergeFrom(const InvalidMoveFilter& other);

  bool isMarkedInvalid(
      entities::ObjectId objectId,
      entities::ContainerId containerId) const {
    if (empty()) {
      return false;
    }
    const auto& row = containerToInvalidObjects_[containerId.asIndex()];
    return row.has_value() && row->isSet(objectId.asIndex());
  }

  bool anyMarkedInvalid(
      const std::vector<entities::ObjectId>& objectBundle,
      entities::ContainerId containerId) const {
    if (empty()) {
      return false;
    }
    const auto& row = containerToInvalidObjects_[containerId.asIndex()];
    if (!row.has_value()) {
      return false;
    }
    const auto& invalidObjects = *row;
    return std::any_of(
        objectBundle.begin(), objectBundle.end(), [&](auto objectId) {
          return invalidObjects.isSet(objectId.asIndex());
        });
  }

  bool empty() const {
    return isEmpty_.load(std::memory_order_relaxed);
  }

 private:
  std::vector<folly::DelayedInit<algopt::DynamicBitSet>>
      containerToInvalidObjects_;
  size_t numObjects_{0};
  std::atomic<bool> isEmpty_{true};
};

// Returns true if `objectId` is marked invalid for `destContainer`, and counts
// it as a pruned candidate in `stats`. Returns false if `filter` is null.
inline bool isMoveInvalid(
    const InvalidMoveFilter* filter,
    entities::ObjectId objectId,
    entities::ContainerId destContainer,
    MoveStatsAggregator& stats) {
  if (filter && filter->isMarkedInvalid(objectId, destContainer)) {
    stats.incrNumPruned();
    return true;
  }
  return false;
}

// Returns true if any object in `objectBundle` is marked invalid for
// `destContainer`, and counts it as a pruned candidate in `stats`. Returns
// false if `filter` is null.
inline bool anyMoveInvalid(
    const InvalidMoveFilter* filter,
    const std::vector<entities::ObjectId>& objectBundle,
    entities::ContainerId destContainer,
    MoveStatsAggregator& stats) {
  if (filter && filter->anyMarkedInvalid(objectBundle, destContainer)) {
    stats.incrNumPruned();
    return true;
  }
  return false;
}

} // namespace facebook::rebalancer
