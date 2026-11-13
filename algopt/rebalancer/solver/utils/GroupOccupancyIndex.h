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
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/utils/EntityAttributes.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <cstdint>
#include <map>
#include <optional>
#include <tuple>

namespace facebook::rebalancer {

class Assignment;
class EquivalenceSets;

// The occupancy of a group of a disjoint partition is the number of its objects
// in the containers of one scope item. This attribute indexes the dynamic
// objects of each container by (equivalence set, occupancy of their group),
// then by server, and keeps the index up to date as objects move. A move type
// can then sample objects per (equivalence set, occupancy) instead of needing
// one equivalence set per group.
class GroupOccupancyIndex : public EntityAttributes {
 public:
  // The occupancy key of objects in no group; sorts before every occupancy.
  static constexpr int32_t kUngrouped = -1;

  struct EquivSetAndOccupancy {
    entities::EquivalenceSetId equivSetId;
    int32_t occupancy;

    bool operator<(const EquivSetAndOccupancy& other) const {
      return std::tie(equivSetId, occupancy) <
          std::tie(other.equivSetId, other.occupancy);
    }
  };
  // Sorted, so the occupancies of an equivalence set are adjacent and
  // ascending. The inner map is keyed by server, as in
  // AssignmentIndexedByEquivSetAndGroupId.
  using ObjectsByEquivSetAndOccupancy = std::map<
      EquivSetAndOccupancy,
      PackerMap<entities::GroupId, PackerSet<entities::ObjectId>>>;

  GroupOccupancyIndex(
      const entities::Universe& universe,
      entities::PartitionId partitionId,
      entities::PartitionId serverPartitionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      const EquivalenceSets& equivalenceSets,
      const Assignment& assignment);

  const ObjectsByEquivSetAndOccupancy& getContainerObjects(
      entities::ContainerId containerId) const;

  bool isInScopeItem(entities::ContainerId containerId) const;

  void updateOnApply(const ChangeSet& changes) override;

 private:
  struct IndexEntry {
    entities::EquivalenceSetId equivSetId;
    std::optional<entities::GroupId> groupId;
    entities::GroupId serverId;
    entities::ContainerId containerId;
  };

  std::optional<entities::GroupId> getGroup(entities::ObjectId objectId) const;
  EquivSetAndOccupancy getKey(const IndexEntry& entry) const;
  void addToIndex(entities::ObjectId objectId, const IndexEntry& entry);
  void removeFromIndex(entities::ObjectId objectId, const IndexEntry& entry);
  void addToOccupancy(entities::ObjectId objectId, int32_t delta);

  const entities::Partition& partition_;
  const entities::Set<entities::ContainerId>& scopeItemContainers_;
  PackerMap<entities::GroupId, int32_t> groupToOccupancy_;
  PackerMap<entities::ObjectId, IndexEntry> objectToIndexEntry_;
  PackerMap<entities::ContainerId, ObjectsByEquivSetAndOccupancy>
      containerToIndexedObjects_;
};

} // namespace facebook::rebalancer
