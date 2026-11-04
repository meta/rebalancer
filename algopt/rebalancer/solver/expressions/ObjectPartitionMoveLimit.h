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

#include "algopt/rebalancer/algopt_common/AssociativeMap.h"
#include "algopt/rebalancer/algopt_common/ValueSortedMap.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

namespace facebook::rebalancer {

class ObjectPartitionMoveLimit : public Expression {
  struct Compare {
    bool operator()(
        const std::pair<entities::GroupId, double>& lhs,
        const std::pair<entities::GroupId, double>& rhs) const;
  };
  using SortedMap =
      facebook::algopt::ValueSortedMap<entities::GroupId, double, Compare>;

 public:
  explicit ObjectPartitionMoveLimit(
      const entities::Universe& universe,
      Assignment originalAssignment,
      entities::PartitionId partitionId,
      entities::DimensionId dimensionId,
      /* group_idx -> limit */
      PackerMap<entities::GroupId, double> groupLimits,
      entities::Set<entities::ContainerId> sourceContainerIdsNotAffectingLimit,
      entities::Set<entities::ContainerId>
          destinationContainerIdsNotAffectingLimit);

  void updateEquivalenceSets(EquivalenceSets& sets) const override;

  std::string innerDigest(size_t maxChildren = 10) const override;

  // Children (ObjectVector/ObjectPartition) have no values themselves, so have
  // no point in being sorted.
  std::vector<std::pair<Expression*, double>> get_sorted_children(
      bool) const override;

  using Expression::evaluate;
  virtual double evaluate(
      const BottomToTopEvaluator& evaluator,
      const ChangeSet& changes) const override;

  using Expression::lp;
  virtual algopt::lp::Expression lp(
      const LpEvaluator& evaluator,
      bool minimizing,
      const interface::OptimalSolverSpec& configs) override;

  void lpIntent(const LpEvaluator& evaluator, bool minimizing) override;

  virtual const std::string_view& getType() const override;

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const override;

 private:
  virtual double innerFullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment) override;

  virtual double innerPartialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes) override;

  bool doesMoveAffectLimit(
      entities::ObjectId objectId,
      std::optional<entities::ContainerId> destinationContainerId) const;

  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const override;

  bool shouldComputeDescendingChildPotentials() const override;

  void updateExprForLp();

  struct MoveCostDestination;
  const entities::Partition* getSharedCostPartition() const;
  void mergeDenseEquivalenceSets(
      EquivalenceSets& equivalenceSets,
      const std::vector<MoveCostDestination>& destinations) const;
  void mergeSparseEquivalenceSets(
      EquivalenceSets& equivalenceSets,
      const std::vector<MoveCostDestination>& destinations,
      double defaultValue) const;

  void set_directly_affected_containers();
  double moveCost(entities::GroupId group, double groupDeviation) const;

  // Returns the cost of moving the object from source to destination.
  double getObjectMoveCost(
      entities::ObjectId objectId,
      entities::ContainerId destinationContainerId) const;

  // Returns the dimension value for the object in the given container. This is
  // just a helper to convert containerId to scopeItemId and then query getValue
  // for dimension.
  double getObjectDimensionValueInContainer(
      entities::ObjectId objectId,
      entities::ContainerId containerId) const;

 private:
  // from constructor
  Assignment originalAssignment_;
  entities::PartitionId partitionId_;
  PackerMap<entities::GroupId, double> groupLimits_;
  entities::Set<entities::ContainerId> sourceContainerIdsNotAffectingLimit_;
  entities::Set<entities::ContainerId>
      destinationContainerIdsNotAffectingLimit_;

  // internal state
  PackerMap<entities::GroupId, algopt::SumMap<entities::ObjectId, double>>
      groupDeviations_;
  SortedMap groupMoveCosts_;
  ExprPtr lpProvider_;
  entities::ScopeId containerScopeId_;
  // Given that we already store a shared ptr to universe, holding a raw ptr
  // here is going to be safe.
  const entities::ObjectScalarDimension* dimension_;
  const entities::Partition* partition_;
};
} // namespace facebook::rebalancer
