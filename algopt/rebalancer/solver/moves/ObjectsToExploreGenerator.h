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
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

namespace facebook::rebalancer {
using ObjectBundle = std::vector<entities::ObjectId>;

template <typename T>
using ReferenceList = std::deque<std::reference_wrapper<T>>;

class ObjectsToExploreGenerator {
 public:
  explicit ObjectsToExploreGenerator(const entities::Universe& universe);

  ~ObjectsToExploreGenerator() = default;

  // Objects of each group in `partitionName` that can move into
  // `hotContainer`: objects already there, and objects `invalidMoveFilter`
  // marks invalid for it (counted as pruned in `stats`), are left out.
  ReferenceList<const std::vector<entities::ObjectId>> getObjectsToExplore(
      const std::string& partitionName,
      entities::ContainerId hotContainer,
      const Assignment& assignment,
      const EquivalenceSets& equivalenceSets,
      MoveStatsAggregator& stats,
      const InvalidMoveFilter* invalidMoveFilter = nullptr);

  // Create ObjectBundles of `bundleSize` from objects in `srcContainer`.
  // All objects in a bundle are from the same group in `partition`.
  std::vector<ObjectBundle> getObjectBundlesToExplore(
      const std::string& partition,
      entities::ContainerId srcContainer,
      int bundleSize,
      const folly::F14FastMap<entities::GroupId, int>& groupBundleSizeOverrides,
      Assignment& assignment,
      const EquivalenceSets& equivalenceSets,
      bool createGreedyHeterogenousBundles);

 private:
  const entities::Universe& universe_;
  folly::F14NodeMap<entities::GroupId, std::vector<entities::ObjectId>>
      groupToObjects_;
};

} // namespace facebook::rebalancer
