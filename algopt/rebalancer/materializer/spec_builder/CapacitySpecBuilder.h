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
#include "algopt/rebalancer/materializer/spec_builder/SpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/materializer/utils/LimitWrapper.h"

namespace facebook::rebalancer::materializer {

class CapacitySpecBuilder : public SpecBuilder {
 public:
  CapacitySpecBuilder(
      std::shared_ptr<const entities::Universe> universe,
      facebook::rebalancer::interface::CapacitySpec spec);

  folly::coro::Task<ExprPtr> goalCoro(
      ExpressionBuilder& expressionBuilder) const override;

  folly::coro::Task<std::vector<ConstraintInfo>> constraints(
      ExpressionBuilder& expressionBuilder) const override;

  std::string description() const override;

  SpecParameters getSpecInfo() const override;

  entities::Set<entities::ContainerId> nonAcceptingContainers() const override;

  void populateInvalidMoveFilter(
      InvalidMoveFilter& filter,
      const Assignment& updatedInitialAssignment) const override;

 private:
  double getAverageCapacity(
      const std::vector<entities::ScopeItemId>& scopeItemIds) const;

  double getNormalizationCoefficient(
      entities::ScopeItemId scopeItemId,
      double averageCapacity) const;

  // A wrapper over enum UtilMetric to allow some optimizations such as
  // - for DURING_AND_AFTER spec, AFTER metric is redundant if the DURING
  //  was not initially broken
  enum class InnerUtilMetric {
    AFTER,
    DURING,
    DURING_AND_AFTER_IF_BROKEN,
    NEW,
    OLD,
    MOVED,
  };

  folly::coro::Task<std::vector<ConstraintInfo>> getConstraint(
      ExpressionBuilder& expressionBuilder,
      InnerUtilMetric metric) const;

  folly::coro::Task<std::vector<ConstraintInfo>> getConstraint(
      ExpressionBuilder& expressionBuilder,
      const std::vector<InnerUtilMetric>& metrics) const;

  folly::coro::Task<std::pair<std::vector<ConstraintInfo>, bool>>
  getConstraintsForScopeItem(
      ExpressionBuilder& expressionBuilder,
      InnerUtilMetric innerMetric,
      entities::ScopeItemId scopeItemId,
      double threshold,
      double normCoef) const;

  folly::coro::Task<std::optional<ExprPtr>> getExpression(
      ExpressionBuilder& expressionBuilder,
      UtilMetric metric,
      entities::ScopeItemId scopeItemId,
      double threshold,
      double normCoef) const;

  static std::vector<InnerUtilMetric> getUtilMetrics(
      interface::CapacitySpecDefinition definition,
      bool isGoalSpec);

  static UtilMetric getMappedUtilMetric(InnerUtilMetric innerMetric);

  folly::coro::Task<ExprPtr> getMaybeBoundedUtil(
      ExpressionBuilder& expressionBuilder,
      UtilMetric metric,
      entities::ScopeItemId scopeItemId) const;

  void initilizePerGroupUtilizationBounds(
      const interface::GroupUtilizationBound& bound);

  folly::coro::Task<ExprPtr> getBoundedUtil(
      ExpressionBuilder& expressionBuilder,
      UtilMetric metric,
      entities::ScopeItemId scopeItemId,
      const interface::GroupUtilizationBound& bound) const;

  struct BundledUtilization {
    entities::PartitionId partitionId;
    entities::ScopeId scopeId;
    // Bundle items by containing spec scope item.
    entities::Map<entities::ScopeItemId, std::vector<BundleItem>>
        scopeItemToBundleItems;
  };

  void initializeBundledUtilization(
      const interface::GroupBundleUtilization& bundleUtilization);

  facebook::rebalancer::interface::CapacitySpec spec_;
  entities::ScopeId scopeId_;
  entities::DimensionId dimensionId_;

  // used to store the scopeItems that are at or above limit w.r.t.
  // during utilizations when capacitySpec is used as a constraint
  mutable std::optional<folly::F14FastSet<entities::ScopeItemId>>
      scopeItemsInitiallyAtOrAboveDuringLimit_ = std::nullopt;

  // used to cap (upperbound) or threshold (lowerbound) the utilization
  // of a scopeItem by the provided limit
  std::optional<LimitWrapper> perGroupUtilizationBounds_ = std::nullopt;

  std::optional<BundledUtilization> bundledUtilization_ = std::nullopt;
  LimitWrapper limits_;
  ScopeItemFilterWrapper scopeFilter_;
};

} // namespace facebook::rebalancer::materializer
