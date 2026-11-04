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

#include "algopt/rebalancer/solver/moves/MoveType.h"

namespace facebook::rebalancer {

constexpr folly::StringPiece kGroupRoutingMoveTypeName = "GROUP_ROUTING";

/*
Special moveType that relies on a routing config and that evaluates a moveSet
for every group and then picks the best moveSet. At a high-level, for each group
and each source in its set of routing rings, the moveSet tries to place an
object of the group in the min latency destination from the source. For more
details on how the moveSet is generated, see the
generateAndEvaluateMoveSetForGroup() function.
*/
class GroupRoutingMoveType : public MoveType {
 public:
  std::string name() const override;

  explicit GroupRoutingMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::GroupRoutingMoveTypeSpec& config);

  MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) override;

 protected:
  MoveSet generateMoveSetFor(
      entities::GroupId groupId,
      const std::string& partitionName,
      std::optional<entities::ContainerId> unassignedContainerId,
      const Problem& problem,
      const MovesEvaluator& evaluator,
      MoveStatsAggregator& stats) const;

 private:
  std::string routingConfigName_;
  std::optional<std::string> unassignedContainerName_ = std::nullopt;
};
} // namespace facebook::rebalancer
