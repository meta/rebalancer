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

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/moves/MoveType.h"
#include <algopt/rebalancer/algopt_common/Concepts.h>

#include <string_view>
#include <utility>

namespace facebook::rebalancer {

class Problem;

constexpr inline std::string_view kGroupMoveWithHintStrategiesMoveTypeName{
    "GROUP_MOVE_WITH_HINT_STRATEGIES"};

/*
    Given an outer partition (O) and an inner partition (I) of objects.
    Assume for each group in O, there will be a subset of each group in I.

    Example:
    Inner partition: {{Group 1: A, B, C}, {Group2: D, E}, {Group 3: F}}
    Outer partition: {{A, D, F}}

    For each group in the outer partition, we will explore moving objects
    based on the groups in the inner partition. Each group in the inner
   partition has a specific move strategy to tell rebalancer how to explore
   moves. Rebalancer will compare all movesets generated for each group in the
   inner martition and will move one of those groups
*/
class GroupMoveWithHintStrategiesMoveType : public MoveType {
 public:
  std::string name() const override;

  GroupMoveWithHintStrategiesMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::GroupMoveWithHintStrategiesMoveTypeSpec& spec,
      const Problem& problem);

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;

 protected:
  entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
  getPrimaryGroupToExplore(
      entities::GroupId currentPrimaryGroup,
      const Problem& problem);

  template <typename ContainerIds>
  MoveSet generateMoveSet(
      const ContainerIds& sampledContainers,
      const std::vector<entities::ObjectId>& objectIds,
      const Problem& problem,
      MoveStatsAggregator& stats) const;

  MoveResult exploreMovingGroup(
      const MovesEvaluator& evaluator,
      MoveStatsAggregator& stats,
      double timeLimit,
      const std::vector<MoveSet>& possibleMoveSets);

  MoveSet generateSampledContainersAndMoveSet(
      interface::MoveStrategyType strategy,
      const std::vector<entities::ContainerId>& scopeItemContainers,
      const std::vector<entities::ObjectId>& objectIds,
      const entities::ContainerId hotContainer,
      const Problem& problem,
      MoveStatsAggregator& stats) const;

  std::vector<MoveSet> generateAllMoveSets(
      entities::ObjectId hotObjectId,
      Problem& problem,
      const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
          secondaryGroupIdToObjects,
      const entities::ContainerId hotContainer,
      MoveStatsAggregator& stats) const;

  static MoveSet getAllToUnassigned(
      const std::vector<entities::ObjectId>& objects,
      entities::ContainerId unassignedContainerId,
      const Problem& problem);

  std::vector<int> generateDestinationScopeItemIndexPerGroup(
      const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
          tertiaryGroupToObjects,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          acceptingContainersPerScopeItem,
      const interface::MoveStrategyType& strategy,
      const entities::ContainerId hotContainer,
      bool distinctScopeItems = false) const;

  MoveSet generateMoveSetWithScopeItemTuple(
      const std::vector<int>& scopeItemTuple,
      const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
          tertiaryGroupToObjects,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          acceptingContainersPerScopeItem,
      const interface::MoveStrategyType& strategy,
      const entities::ContainerId hotContainer,
      const std::vector<entities::ObjectId>& objects,
      const Problem& problem,
      MoveStatsAggregator& stats) const;

  static std::pair<MoveSet, std::optional<entities::GroupId>>
  findAllocatedSecondaryGroup(
      const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
          secondaryGroupIdToObjects,
      const std::optional<entities::ContainerId>& unassignedContainerId,
      const Problem& problem);

  std::vector<MoveSet> exploreTertiaryPartitionMoves(
      const std::vector<entities::ObjectId>& objects,
      const std::string& tertiaryPartitionName,
      int numScopeItemsToExplore,
      int moveSetsPerScopeItem,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          acceptingContainersPerScopeItem,
      const interface::MoveStrategyType& strategy,
      entities::ContainerId exclusionContainer,
      const Problem& problem,
      MoveStatsAggregator& stats,
      bool distinctScopeItems = false) const;

  std::vector<MoveSet> exploreScopeItemMoves(
      const std::vector<entities::ObjectId>& objects,
      int moveSetsPerScopeItem,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          acceptingContainersPerScopeItem,
      const interface::MoveStrategyType& strategy,
      entities::ContainerId exclusionContainer,
      const Problem& problem,
      MoveStatsAggregator& stats) const;

 private:
  GroupMoveWithHintStrategiesMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::GroupMoveWithHintStrategiesMoveTypeSpec& spec,
      const entities::Universe& universe);

  static bool notEnoughContainersForMoveSet(
      const interface::MoveStrategyType& strategy,
      int numContainers,
      int numObjects);
  // TODO: Remove this once and use IndexedAssignment for the local set
  entities::Set<entities::GroupId> primaryGroupIndicesExplored_;
  interface::GroupMoveWithHintStrategiesMoveTypeSpec spec_;
  // Partitions and the optional unassigned container, resolved from spec names
  // at construction
  const entities::PartitionId primaryPartitionId_;
  const entities::PartitionId secondaryPartitionId_;
  const std::optional<entities::ContainerId> unassignedContainerId_;
};

} // namespace facebook::rebalancer
