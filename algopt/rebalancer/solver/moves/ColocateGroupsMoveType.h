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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/solver/moves/MoveType.h"

namespace facebook::rebalancer {

// Let P be a partition P and RG be a list of related groups, where each set R
// in RG is a collection of groups in P. Each time this moveType is used with a
// hot container C, the following is what happens:
//
// 1. Consider an object O in C and let G_O be the group it belongs to in P. Let
// R_O in RG be the set of groups related to G_O (the group O belongs to).
//
//    1.1. If O is not part of any group in P, ignore O and consider next
//    relevant object
//
//    1.2. If O is part of a group that has been previously
//    considered during the same invocation of the move type (which means it
//    failed), skip O and go to next relevant object
//
// 2. For each scope item S in colocationScopeItems and each group g in R_O do
// the following:
//
//      2.1. For each group G_i in R_O, pick an object O_i in G_i that
//      belongs to S, where S is the colocation scope item hot container C
//      belong to. Let C_{G_i, D} the set of valid containers in S that O_i
//      can move to. Note that C_{G_i, D} is the set of containers that belong
//      to S AND validDestinations w.r.t. group G_i in
//      colocationScopeItemToGroupToContainers (given as input).

//     2.2. Generate the following moveSets:
//     for each (C_1', C_2'..., C_k') in CrossProduct_{G_i in R_O} C_{G_i, D},
//     where k = |R_O| (basically, consider all possible combinations of
//     destinations across all C_{G_i, D}), construct moveSet M, where M = {
//     {o_1, C, C_1'}, {o_2, C, C_2'}, ..., {o_k, C, C_k'}}, where C is the
//     hotContainer and o_i is an object of G_i (in R_O) that is in the same
//     scope item as C
//
//      2.3. Note that doing the above generates |C{G_1, D} | *  |C{G_2, D} | *
//      ... * |C{G_k, D} | move sets, where each moveSet has k moves, where k =
//      |R_O|
//
// 3. Evaluate all the move sets generate in step 2 in parallel and pick the
// best if any. Note that, assuming the number of valid destinations per group
// in a colocation scope item is the same always, there are
// |colocationScopeItems| * |C{G_1, D} | *  |C{G_2, D} | * ... * |C{G_k, D} |
// move sets being tried for every move
//
// 4. If none of the moves are good, go back to step1.
//
// The version above is the most "naive" version. Depending on the problem, the
// number of move sets generated in step3 could be a lot, so we could do the
// following to manage scale
//      Instead of trying all possible containers in each C{G_i, D}, fix the
//      sample size to, say, k and sample k containers from each C{G_i, D}.
//      This results in |colocationScopeItems| * k_1 * k_2, ..., k_n movesets
//      being evaluated in step3.

constexpr inline std::string_view kColocateGroupsMoveTypeName =
    "COLOCATE_GROUPS";

class ColocateGroupsMoveType : public MoveType {
 public:
  std::string name() const override;

  ColocateGroupsMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::ColocateGroupsMoveTypeSpec& spec,
      const Problem& problem);

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;

 private:
  struct RelatedGroupsInfoId {
    std::shared_ptr<const std::vector<entities::GroupId>> relatedGroups =
        nullptr;
    std::shared_ptr<const std::vector<entities::ScopeItemId>>
        destinationScopeItemIds = nullptr;
  };

  // The spec resolved to ids (instead of names), built once at construction.
  struct SpecInfo {
    entities::PartitionId partitionId;
    entities::ScopeId colocationScopeId;
    const entities::Map<entities::GroupId, RelatedGroupsInfoId>
        groupIdToRelatedGroups;
    const entities::Map<
        entities::ScopeItemId,
        entities::Map<entities::GroupId, entities::Set<entities::ContainerId>>>
        colocationScopeItemToGroupToContainers;
    std::optional<int> defaultSampleSize;
  };

  static SpecInfo buildSpecInfo(
      const interface::ColocateGroupsMoveTypeSpec& spec,
      const Problem& problem);

  std::vector<MoveSet> getMoveSetsForRelatedGroups(
      const RelatedGroupsInfoId& relatedGroupsInfo,
      std::optional<entities::ScopeItemId> sourceScopeItemIdOpt,
      const std::vector<entities::ObjectId>& representativeObjectPerGroup,
      const Problem& problem,
      const algopt::Timer& timer,
      double timeLimit,
      MoveStatsAggregator& stats) const;

  static std::vector<entities::ObjectId> getRepresentativeObjectPerGroup(
      entities::GroupId hotObjectGroupId,
      const std::vector<entities::GroupId>& relatedGroups,
      entities::ObjectId hotObjectId,
      const entities::Set<entities::ContainerId>& sourceContainers,
      const entities::Partition& partition,
      const Problem& problem);

  static std::optional<entities::ObjectId>
  getRepresentativeObjectFromSourceScopeItem(
      const entities::Set<entities::ContainerId>& sourceContainers,
      entities::GroupId groupId,
      const entities::Partition& partition,
      const Problem& problem);

  static std::vector<MoveSet> getMoveSetsToDestinationScopeItem(
      const std::vector<std::vector<entities::ContainerId>>&
          destinationContainersPerGroup,
      const std::vector<entities::ObjectId>& representativeObjectPerGroup,
      const Problem& problem,
      const algopt::Timer& timer,
      double timeLimit);

 private:
  const SpecInfo specInfo_;
};

} // namespace facebook::rebalancer
