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

#include "algopt/rebalancer/materializer/spec_builder/CapacitySpecBuilder.h"

#include "algopt/rebalancer/algopt_common/DynamicBitSet.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ScopeDimension.h"
#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"
#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

namespace {

std::optional<GroupUtilizationBound> getGroupUtilizationBound(
    const CapacitySpec& spec) {
  if (spec.utilizationBound() &&
      spec.utilizationBound()->getType() ==
          UtilizationBound::Type::groupUtilizationBound) {
    return spec.utilizationBound()->groupUtilizationBound().value();
  }
  return std::nullopt;
}

using ThresholdKey = std::pair<double, double>;
using ThresholdToBlockedScopeItems =
    Map<ThresholdKey, std::vector<ScopeItemId>>;

template <
    typename ObjectIds,
    typename ScalarDimension,
    typename IsInvalid,
    typename MergeInvalidObjects>
void markByScanningEachThreshold(
    const ObjectIds& objectIds,
    const ScalarDimension& scalarDim,
    const ThresholdToBlockedScopeItems& thresholdToBlockedScopeItems,
    const IsInvalid& isInvalid,
    const MergeInvalidObjects& mergeInvalidObjects) {
  for (const auto& [thresholdKey, blockedScopeItemIds] :
       thresholdToBlockedScopeItems) {
    algopt::DynamicBitSet invalidObjects(objectIds.size());
    for (const auto objectId : objectIds) {
      if (isInvalid(scalarDim.getValue(objectId), thresholdKey)) {
        invalidObjects.set(objectId.asIndex());
      }
    }
    mergeInvalidObjects(invalidObjects, blockedScopeItemIds);
  }
}

template <
    typename ObjectIds,
    typename ScalarDimension,
    typename IsInvalid,
    typename MergeInvalidObjects>
void markBySortedPrefixes(
    const ObjectIds& objectIds,
    const ScalarDimension& scalarDim,
    const ThresholdToBlockedScopeItems& thresholdToBlockedScopeItems,
    const IsInvalid& isInvalid,
    const MergeInvalidObjects& mergeInvalidObjects) {
  std::vector<std::pair<double, ObjectId>> objectsByValue;
  objectsByValue.reserve(objectIds.size());
  for (const auto objectId : objectIds) {
    const auto value = scalarDim.getValue(objectId);
    if (!std::isnan(value)) {
      objectsByValue.emplace_back(value, objectId);
    }
  }
  std::sort(
      objectsByValue.begin(),
      objectsByValue.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

  std::vector<std::pair<std::size_t, ThresholdKey>> thresholdsByInvalidCount;
  thresholdsByInvalidCount.reserve(thresholdToBlockedScopeItems.size());
  for (const auto& [thresholdKey, _] : thresholdToBlockedScopeItems) {
    const auto invalidEnd = std::partition_point(
        objectsByValue.begin(), objectsByValue.end(), [&](const auto& object) {
          return isInvalid(object.first, thresholdKey);
        });
    thresholdsByInvalidCount.emplace_back(
        static_cast<std::size_t>(invalidEnd - objectsByValue.begin()),
        thresholdKey);
  }
  std::sort(thresholdsByInvalidCount.begin(), thresholdsByInvalidCount.end());

  algopt::DynamicBitSet invalidObjects(objectIds.size());
  std::size_t invalidCount = 0;
  for (const auto& [nextInvalidCount, thresholdKey] :
       thresholdsByInvalidCount) {
    while (invalidCount < nextInvalidCount) {
      invalidObjects.set(objectsByValue.at(invalidCount).second.asIndex());
      ++invalidCount;
    }
    mergeInvalidObjects(
        invalidObjects, thresholdToBlockedScopeItems.at(thresholdKey));
  }
}

void checkBundledUtilizationDimension(
    const Universe& universe,
    DimensionId dimensionId) {
  const auto& dimension = universe.getObjects().getDimension(dimensionId);
  if (dimension.size() != 1 || dimension.isDynamic() ||
      dimension.isRoutingConfigBased() || dimension.hasNegativeValues()) {
    throw std::runtime_error(
        fmt::format(
            "GroupBundleUtilization requires a static scalar dimension without negative values, but got dimension {}",
            universe.getEntityName(dimensionId)));
  }
}

// Groups with enough total dimension to complete a bundle.
std::shared_ptr<const std::vector<GroupId>> getBundleQualifiedGroups(
    const Universe& universe,
    PartitionId partitionId,
    DimensionId dimensionId,
    double bundleSize) {
  const auto& partition = universe.getPartition(partitionId);
  const auto& dimension =
      universe.getObjects().getDimension(dimensionId).only();
  auto groupIds = std::make_shared<std::vector<GroupId>>();
  for (const auto groupId : partition.getGroupIds()) {
    double groupValue = 0;
    for (const auto objectId : partition.getObjectIds(groupId)) {
      groupValue += dimension.getValue(objectId);
    }
    if (universe.getPrecision().compare(groupValue, bundleSize) >= 0) {
      groupIds->push_back(groupId);
    }
  }
  return groupIds;
}

} // namespace

CapacitySpecBuilder::CapacitySpecBuilder(
    std::shared_ptr<const Universe> universe,
    CapacitySpec spec)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      scopeId_(universe_->getScopeId(*spec_.scope())),
      dimensionId_(universe_->getDimensionId(*spec_.dimension())),
      limits_(*universe_, *spec_.limit(), scopeId_),
      scopeFilter_(*universe_, *spec_.filter(), scopeId_) {
  if (auto bound = getGroupUtilizationBound(spec_)) {
    initilizePerGroupUtilizationBounds(*bound);
  }
  if (auto bundleUtilization = spec_.groupBundleUtilization().as_const()) {
    initializeBundledUtilization(*bundleUtilization);
  }
}

void CapacitySpecBuilder::initializeBundledUtilization(
    const GroupBundleUtilization& bundleUtilization) {
  checkBundledUtilizationDimension(*universe_, dimensionId_);
  const auto partitionId =
      universe_->getPartitionId(*bundleUtilization.partitionName());
  if (!universe_->getPartition(partitionId).isDisjoint()) {
    throw std::runtime_error(
        fmt::format(
            "GroupBundleUtilization requires a disjoint partition, but partition {} has overlapping groups",
            *bundleUtilization.partitionName()));
  }
  BundledUtilization result{
      .partitionId = partitionId,
      .scopeId = universe_->getScopeId(*bundleUtilization.scope()),
      .scopeItemToBundleItems = {},
  };
  const auto& configuredScope = universe_->getScope(result.scopeId);
  const auto& capacityScope = universe_->getScope(scopeId_);
  const auto getContainingCapacityScopeItem =
      [&](ScopeItemId configuredScopeItemId) -> std::optional<ScopeItemId> {
    const auto& containerIds =
        configuredScope.getContainerIds(configuredScopeItemId);
    if (containerIds.empty()) {
      throw std::runtime_error(
          fmt::format(
              "GroupBundleUtilization requires {} {} to contain at least one container",
              universe_->getEntityName(result.scopeId),
              universe_->getEntityName(configuredScopeItemId)));
    }
    const auto capacityScopeItemId =
        capacityScope.getScopeItemId(*containerIds.begin());
    for (const auto containerId : containerIds) {
      if (capacityScope.getScopeItemId(containerId) != capacityScopeItemId) {
        throw std::runtime_error(
            fmt::format(
                "GroupBundleUtilization requires {} {} to be contained in a single item of scope {}",
                universe_->getEntityName(result.scopeId),
                universe_->getEntityName(configuredScopeItemId),
                universe_->getEntityName(scopeId_)));
      }
    }
    return capacityScopeItemId;
  };

  Map<double, std::shared_ptr<const std::vector<GroupId>>> sizeToGroupIds;
  for (const auto& [bundleItemName, bundleSize] :
       *bundleUtilization.bundleSizes()) {
    const auto bundleItemId =
        universe_->getScopeItemId(result.scopeId, bundleItemName);
    const auto capacityScopeItemId =
        getContainingCapacityScopeItem(bundleItemId);
    if (!capacityScopeItemId) {
      continue;
    }
    auto& groupIds = sizeToGroupIds[bundleSize];
    if (!groupIds) {
      groupIds = getBundleQualifiedGroups(
          *universe_, result.partitionId, dimensionId_, bundleSize);
    }
    result.scopeItemToBundleItems[*capacityScopeItemId].push_back(
        {.scopeItemId = bundleItemId,
         .size = bundleSize,
         .groupIds = groupIds});
  }
  bundledUtilization_ = std::move(result);
}

UtilMetric CapacitySpecBuilder::getMappedUtilMetric(
    InnerUtilMetric innerMetric) {
  switch (innerMetric) {
    case InnerUtilMetric::AFTER:
      return UtilMetric::AFTER;
    case InnerUtilMetric::DURING:
      return UtilMetric::DURING;
    case InnerUtilMetric::OLD:
      return UtilMetric::OLD;
    case InnerUtilMetric::NEW:
      return UtilMetric::NEW;
    case InnerUtilMetric::MOVED:
      return UtilMetric::MOVED;
    case InnerUtilMetric::DURING_AND_AFTER_IF_BROKEN:
      throw std::runtime_error("Unmapped InnerUtilMetric");
  }
  throw std::runtime_error("Invalid InnerUtilMetric");
}

std::vector<CapacitySpecBuilder::InnerUtilMetric>
CapacitySpecBuilder::getUtilMetrics(
    CapacitySpecDefinition definition,
    bool isGoalSpec) {
  std::vector<InnerUtilMetric> duringAfterExpanded;
  if (isGoalSpec) {
    duringAfterExpanded = {InnerUtilMetric::DURING, InnerUtilMetric::AFTER};
  } else {
    duringAfterExpanded = {InnerUtilMetric::DURING_AND_AFTER_IF_BROKEN};
  }
  switch (definition) {
    case CapacitySpecDefinition::AFTER:
      return {InnerUtilMetric::AFTER};
    case CapacitySpecDefinition::DURING_AND_AFTER:
      return duringAfterExpanded;
    case CapacitySpecDefinition::DURING:
      return {InnerUtilMetric::DURING};
    case CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER: {
      auto rVal = duringAfterExpanded;
      rVal.push_back(InnerUtilMetric::OLD);
      return rVal;
    }
    case CapacitySpecDefinition::DOUBLE_DURING:
      return {InnerUtilMetric::DURING, InnerUtilMetric::OLD};
    case CapacitySpecDefinition::NEW:
      return {InnerUtilMetric::NEW};
    case CapacitySpecDefinition::OLD:
      return {InnerUtilMetric::OLD};
    case CapacitySpecDefinition::MOVED_DATA:
      return {InnerUtilMetric::MOVED};
  }
  throw std::runtime_error("unsupported capacity definition");
}

folly::coro::Task<ExprPtr> CapacitySpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  auto metrics = getUtilMetrics(*spec_.definition(), true);
  auto exprs = co_await getConstraint(expressionBuilder, metrics);
  co_return getAggregatedConstraintViolation(exprs, *universe_);
}

folly::coro::Task<std::vector<ConstraintInfo>> CapacitySpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  // initialize scopeItemsInitiallyAtOrAboveDuringLimit_ when used as a
  // constraint
  scopeItemsInitiallyAtOrAboveDuringLimit_.emplace();
  auto metrics = getUtilMetrics(*spec_.definition(), false);
  co_return co_await getConstraint(expressionBuilder, metrics);
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacitySpecBuilder::getConstraint(
    ExpressionBuilder& expressionBuilder,
    const std::vector<InnerUtilMetric>& metrics) const {
  std::vector<ConstraintInfo> result;
  for (auto metric : metrics) {
    auto exprs = co_await getConstraint(expressionBuilder, metric);
    result.insert(result.end(), exprs.begin(), exprs.end());
  }
  co_return result;
}

folly::coro::Task<ExprPtr> CapacitySpecBuilder::getMaybeBoundedUtil(
    ExpressionBuilder& expressionBuilder,
    UtilMetric metric,
    entities::ScopeItemId scopeItemId) const {
  if (auto bound = getGroupUtilizationBound(spec_)) {
    co_return co_await getBoundedUtil(
        expressionBuilder, metric, scopeItemId, *bound);
  }
  if (bundledUtilization_) {
    if (const auto* bundleItems = folly::get_ptr(
            bundledUtilization_->scopeItemToBundleItems, scopeItemId)) {
      co_return co_await expressionBuilder.getBundledAbsoluteUtil(
          metric,
          dimensionId_,
          scopeId_,
          scopeItemId,
          bundledUtilization_->partitionId,
          bundledUtilization_->scopeId,
          *bundleItems);
    }
  }
  // if no bounds were specified, return the util as is
  co_return co_await expressionBuilder.getAbsoluteUtil(
      metric, dimensionId_, scopeId_, scopeItemId);
}

folly::coro::Task<std::optional<ExprPtr>> CapacitySpecBuilder::getExpression(
    ExpressionBuilder& expressionBuilder,
    UtilMetric metric,
    entities::ScopeItemId scopeItemId,
    double threshold,
    double normCoef) const {
  // scale util and threshold using normCoef
  auto util = normCoef *
      co_await getMaybeBoundedUtil(expressionBuilder, metric, scopeItemId);
  threshold = threshold * normCoef;

  ExprPtr expr;
  auto bound = *spec_.bound();
  if (bound == CapacitySpecBound::MAX) {
    expr = util - threshold;
  } else if (bound == CapacitySpecBound::MIN) {
    if (threshold == 0) {
      // Min capacity is always satisfied when threshold is zero.
      co_return std::nullopt;
    }

    const bool zeroAllowed = *spec_.zeroAllowed();
    if (zeroAllowed) {
      double ub = expressionBuilder.getUpperBound(*util);
      // the piecewise function simply transforms util to
      // the following function:
      //          { 0                    util = 0         }
      //   expr = { threshold - util     util > 0         }
      //          { 0                    util > threshold }
      if (ub > threshold) {
        expr = piecewise(
            {{0, 0}, {0, threshold}, {threshold, 0}, {ub, 0}}, util, false);
      } else {
        expr = piecewise({{0, 0}, {0, threshold}, {threshold, 0}}, util, false);
      }
    } else {
      expr = threshold - util;
    }
  } else {
    throw std::runtime_error("unsupported capacity bound");
  }

  co_return expr;
}

folly::coro::Task<std::pair<std::vector<ConstraintInfo>, bool>>
CapacitySpecBuilder::getConstraintsForScopeItem(
    ExpressionBuilder& expressionBuilder,
    InnerUtilMetric innerMetric,
    entities::ScopeItemId scopeItemId,
    double threshold,
    double normCoef) const {
  bool isAtOrAboveDuringLimit = false;

  auto isDuringBrokenAndMaybeTrackViolation =
      [this, &isAtOrAboveDuringLimit](
          Expression& duringExpr,
          [[maybe_unused]] entities::ScopeItemId scopeItemId) {
        auto duringValue = duringExpr.getInitialValue();
        auto compare = universe_->getPrecision().compare(duringValue, 0);
        auto duringBelowLimit = (compare == -1);
        if (!duringBelowLimit &&
            scopeItemsInitiallyAtOrAboveDuringLimit_.has_value()) {
          isAtOrAboveDuringLimit = true;
        }

        return (compare == 1); // during is broken, i.e., above limit
      };

  std::vector<ConstraintInfo> rVal;
  if (innerMetric == InnerUtilMetric::DURING_AND_AFTER_IF_BROKEN) {
    // Check if DURING is initially broken
    if (auto expr = co_await getExpression(
            expressionBuilder,
            UtilMetric::DURING,
            scopeItemId,
            threshold,
            normCoef)) {
      auto duringExpr = *expr;
      rVal.emplace_back(duringExpr);
      auto duringBroken =
          isDuringBrokenAndMaybeTrackViolation(*duringExpr, scopeItemId);
      if (duringBroken) {
        // DURING was initially broken, AFTER is needed
        if (auto after = co_await getExpression(
                expressionBuilder,
                UtilMetric::AFTER,
                scopeItemId,
                threshold,
                normCoef)) {
          rVal.emplace_back(*after);
        }
      }
    }
  } else {
    auto metric = getMappedUtilMetric(innerMetric);
    if (auto expr = co_await getExpression(
            expressionBuilder, metric, scopeItemId, threshold, normCoef)) {
      if (metric == UtilMetric::DURING) {
        isDuringBrokenAndMaybeTrackViolation(*expr.value(), scopeItemId);
      }

      rVal.emplace_back(*expr);
    }
  }
  co_return std::make_pair(std::move(rVal), isAtOrAboveDuringLimit);
}

std::string CapacitySpecBuilder::description() const {
  std::string definition;
  switch (*spec_.definition()) {
    case CapacitySpecDefinition::AFTER:
      definition = "after";
      break;
    case CapacitySpecDefinition::DURING_AND_AFTER:
      definition = "during + after";
      break;
    case CapacitySpecDefinition::DURING:
      definition = "during";
      break;
    case CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER:
      definition = "during + old + after";
      break;
    case CapacitySpecDefinition::DOUBLE_DURING:
      definition = "during + old";
      break;
    case CapacitySpecDefinition::NEW:
      definition = "new";
      break;
    case CapacitySpecDefinition::OLD:
      definition = "old";
      break;
    case CapacitySpecDefinition::MOVED_DATA:
      definition = "new + old";
      break;
  }

  std::string bound;
  switch (*spec_.bound()) {
    case CapacitySpecBound::MAX:
      bound = "<=";
      break;
    case CapacitySpecBound::MIN:
      bound = ">=";
      break;
  }

  return fmt::format(
      "{}({}) {} {} for scope {}",
      definition,
      *spec_.dimension(),
      bound,
      *spec_.limit()->globalLimit(),
      *spec_.scope());
}

folly::coro::Task<std::vector<ConstraintInfo>>
CapacitySpecBuilder::getConstraint(
    ExpressionBuilder& expressionBuilder,
    InnerUtilMetric metric) const {
  auto& scopeDimension =
      universe_->getScope(scopeId_).getDimension(dimensionId_);

  auto scopeItemIds = scopeFilter_.getScopeItemIds();

  if (scopeItemIds.empty()) {
    co_return std::vector<ConstraintInfo>{};
  }

  const double averageCapacity = getAverageCapacity(scopeItemIds);

  // {scopeItemId, threshold, normCoef}
  using ScopeItemParams = std::tuple<entities::ScopeItemId, double, double>;
  std::vector<ScopeItemParams> params;
  params.reserve(scopeItemIds.size());

  for (auto scopeItemId : scopeItemIds) {
    double threshold = limits_.getLimit(scopeItemId);
    if (limits_.getType() == LimitType::RELATIVE) {
      threshold *= scopeDimension.getValue(scopeItemId);
    }
    params.emplace_back(
        scopeItemId,
        threshold,
        getNormalizationCoefficient(scopeItemId, averageCapacity));
  }

  std::vector<ConstraintInfo> result;
  result.reserve(scopeItemIds.size());

  co_await CoroUtils::runEachTaskAndUpdate<size_t>(
      0,
      params.size(),
      [&](size_t i) {
        const auto& [scopeItemId, threshold, normCoef] = params[i];
        return getConstraintsForScopeItem(
            expressionBuilder, metric, scopeItemId, threshold, normCoef);
      },
      [&](auto&& taskResult, size_t i) {
        auto& [constraints, isAtOrAboveDuringLimit] = taskResult;
        result.insert(result.end(), constraints.begin(), constraints.end());
        if (isAtOrAboveDuringLimit &&
            scopeItemsInitiallyAtOrAboveDuringLimit_.has_value()) {
          const auto& [scopeItemId, threshold, normCoef] = params[i];
          scopeItemsInitiallyAtOrAboveDuringLimit_->emplace(scopeItemId);
        }
      });
  co_return result;
}

double CapacitySpecBuilder::getAverageCapacity(
    const std::vector<entities::ScopeItemId>& scopeItemIds) const {
  const auto& scopeDimension =
      universe_->getScope(scopeId_).getDimension(dimensionId_);
  double totalCapacity = 0;
  for (const auto scopeItemId : scopeItemIds) {
    totalCapacity += scopeDimension.getValue(scopeItemId);
  }
  return totalCapacity / scopeItemIds.size();
}

double CapacitySpecBuilder::getNormalizationCoefficient(
    entities::ScopeItemId scopeItemId,
    double averageCapacity) const {
  if (!*spec_.useLegacyFormula()) {
    return averageCapacity == 0 ? 1 : 1 / averageCapacity;
  }

  const double capacity = universe_->getScope(scopeId_)
                              .getDimension(dimensionId_)
                              .getValue(scopeItemId);
  if (capacity == 0) {
    throw std::runtime_error(
        fmt::format(
            "{} {} has zero capacity which is not compatible with legacy formula",
            universe_->getEntityName(scopeId_),
            universe_->getEntityName(scopeItemId)));
  }
  return 1 / capacity;
}

entities::Set<entities::ContainerId>
CapacitySpecBuilder::nonAcceptingContainers() const {
  if (!scopeItemsInitiallyAtOrAboveDuringLimit_.has_value() ||
      scopeItemsInitiallyAtOrAboveDuringLimit_->empty()) {
    return {};
  }

  // Check if the dimension is NOT dynamic and has objects with zero value or is
  // routingConfigBased; if yes, then we cannot use
  // scopeItemsInitiallyAtOrAboveDuringLimit_ to figure out the non-accepting
  // containers. Dynamic dimensions are handled below
  auto& objectDimension = universe_->getObjects().getDimension(dimensionId_);
  if (objectDimension.isRoutingConfigBased() ||
      (!objectDimension.isDynamic() &&
       objectDimension.hasZeroValuedObjects())) {
    return {};
  }

  entities::Set<entities::ContainerId> nonAcceptingContainers;
  for (auto scopeItemId : *scopeItemsInitiallyAtOrAboveDuringLimit_) {
    auto& containerIds =
        universe_->getScope(scopeId_).getContainerIds(scopeItemId);
    if (containerIds.size() > 1 ||
        objectDimension.hasZeroValuedObjects(scopeItemId)) {
      // note that we cannot mark all containers in the scopeItem as
      // non-accepting since objects can move between containers of the same
      // scopeItem without changing the during utilization
      // also, if some object has zero dimension value w.r.t. this scopeitem,
      // then it cannot be marked as non-accepting
      continue;
    }
    nonAcceptingContainers.insert(containerIds.begin(), containerIds.end());
  }

  return nonAcceptingContainers;
}

SpecParameters CapacitySpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension(),
      .definition = apache::thrift::util::enumNameSafe(*spec_.definition()),
      .boundType = apache::thrift::util::enumNameSafe(*spec_.bound()),
      .limitType = apache::thrift::util::enumNameSafe(*spec_.limit()->type()),
      .zeroAllowed = *spec_.zeroAllowed() ? "yes" : "no",
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

void CapacitySpecBuilder::initilizePerGroupUtilizationBounds(
    const interface::GroupUtilizationBound& bound) {
  auto partitionId = universe_->getPartitionId(*bound.partitionName());
  perGroupUtilizationBounds_ =
      LimitWrapper(*universe_, *bound.perGroupValues(), scopeId_, partitionId);
}

folly::coro::Task<ExprPtr> CapacitySpecBuilder::getBoundedUtil(
    ExpressionBuilder& expressionBuilder,
    UtilMetric metric,
    entities::ScopeItemId scopeItemId,
    const interface::GroupUtilizationBound& bound) const {
  const bool isUpperBound = *bound.boundType() == UtilizationBoundType::UPPER;
  auto partitionId = universe_->getPartitionId(*bound.partitionName());
  assert(perGroupUtilizationBounds_);
  std::optional<entities::ScopeId> aggregationScopeId = std::nullopt;
  if (auto scopeNameRef = bound.aggregationScope()) {
    aggregationScopeId = universe_->getScopeId(*scopeNameRef);
  }
  const auto defaultLimit = perGroupUtilizationBounds_->getLimit(scopeItemId);
  co_return co_await expressionBuilder.getBoundedAbsoluteUtil(
      metric,
      dimensionId_,
      scopeId_,
      scopeItemId,
      partitionId,
      perGroupUtilizationBounds_->getGroupsOverride(scopeItemId),
      isUpperBound,
      defaultLimit,
      aggregationScopeId);
}

void CapacitySpecBuilder::populateInvalidMoveFilter(
    InvalidMoveFilter& invalidMoveFilter,
    const Assignment& updatedInitialAssignment) const {
  const auto def = *spec_.definition();
  if (*spec_.bound() != CapacitySpecBound::MAX ||
      (def != CapacitySpecDefinition::AFTER &&
       def != CapacitySpecDefinition::DURING &&
       def != CapacitySpecDefinition::DURING_AND_AFTER &&
       def != CapacitySpecDefinition::NEW)) {
    return;
  }
  const auto& objDim = universe_->getObjects().getDimension(dimensionId_);
  if (objDim.isDynamic() || objDim.isRoutingConfigBased() ||
      objDim.size() != 1 || spec_.utilizationBound().has_value() ||
      spec_.groupBundleUtilization().has_value() ||
      objDim.only().hasNegativeValues()) {
    return;
  }

  const auto& scope = universe_->getScope(scopeId_);
  const auto& scalarDim = objDim.only();
  const auto scopeItemIds = scopeFilter_.getScopeItemIds();
  if (scopeItemIds.empty()) {
    return;
  }

  const auto isRelative = limits_.getType() == LimitType::RELATIVE;
  const auto& scopeDimension = scope.getDimension(dimensionId_);
  const auto isAfter = def == CapacitySpecDefinition::AFTER;
  const auto& precision = universe_->getPrecision();
  const auto averageCapacity = getAverageCapacity(scopeItemIds);
  const auto maxObjValue = scalarDim.getMaximumValue();

  // Block (object, container) pairs that can never be valid: an object is
  // blocked from a scope item's containers when its own value is above the
  // threshold: the largest value an object can have and still move into the
  // scope item.
  const auto getThreshold = [def](double limit, double initialUtil) {
    switch (def) {
      case CapacitySpecDefinition::AFTER: {
        // Other objects can move out, so only the object itself must fit.
        return std::max(limit, initialUtil);
      }

      case CapacitySpecDefinition::DURING:
      case CapacitySpecDefinition::DURING_AND_AFTER: {
        // The initial objects stay counted while the object moves in.
        return std::max(limit - initialUtil, 0.0);
      }

      case CapacitySpecDefinition::NEW: {
        // Only objects that were not initially in the scope item count.
        return std::max(limit, 0.0);
      }

      case CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER:
      case CapacitySpecDefinition::DOUBLE_DURING:
      case CapacitySpecDefinition::OLD:
      case CapacitySpecDefinition::MOVED_DATA: {
        break;
      }
    }
    throw std::runtime_error(
        fmt::format(
            "unsupported capacity definition {} for the invalid move filter",
            apache::thrift::util::enumNameSafe(def)));
  };

  // AFTER can skip per-scope processing when every object fits by itself.
  if (isAfter && limits_.getType() == LimitType::ABSOLUTE &&
      limits_.onlyHasGlobalLimit() && limits_.getGlobalLimit() >= maxObjValue) {
    return;
  }

  const auto objectIds = universe_->getObjects().getObjectIds();
  const auto isInvalid = [&](double value, const ThresholdKey& thresholdKey) {
    const auto& [threshold, normCoef] = thresholdKey;
    return !std::isnan(value) &&
        precision.isStrictlyGtZero((value - threshold) * normCoef);
  };
  ThresholdToBlockedScopeItems thresholdToBlockedScopeItems;
  // Objects won't be blocked from a scope item they belonged to in either the
  // original or updated assignment, allowing moves within its containers.
  Map<ScopeItemId, std::vector<ObjectId>> scopeItemToExemptObjects;
  for (const auto scopeItemId : scopeItemIds) {
    auto limit = limits_.getLimit(scopeItemId);
    if (isRelative) {
      limit *= scopeDimension.getValue(scopeItemId);
    }
    if (*spec_.useLegacyFormula() &&
        scopeDimension.getValue(scopeItemId) == 0.0) {
      continue;
    }

    double initialUtil = 0.0;
    for (const auto& cid : scope.getContainerIds(scopeItemId)) {
      const auto collectObject = [&](const auto objectId) {
        const auto value = scalarDim.getValue(objectId);
        // Zero-valued objects won't be blocked anyway.
        if (value != 0.0) {
          scopeItemToExemptObjects[scopeItemId].push_back(objectId);
        }
        return value;
      };
      if (isAfter) {
        for (const auto objectId : updatedInitialAssignment.getObjects(cid)) {
          initialUtil += collectObject(objectId);
        }
      } else {
        for (const auto objectId :
             universe_->getContainers().getInitialObjectIds(cid)) {
          initialUtil += collectObject(objectId);
        }
        for (const auto objectId : updatedInitialAssignment.getObjects(cid)) {
          collectObject(objectId);
        }
      }
    }
    const auto threshold = getThreshold(limit, initialUtil);
    const auto normCoef =
        getNormalizationCoefficient(scopeItemId, averageCapacity);
    if (!(normCoef > 0)) {
      continue;
    }
    const auto thresholdKey = ThresholdKey{threshold, normCoef};
    if (!isInvalid(maxObjValue, thresholdKey)) {
      // No object exceeds this scope item's bound; nothing to block.
      continue;
    }
    thresholdToBlockedScopeItems[thresholdKey].push_back(scopeItemId);
  }

  if (thresholdToBlockedScopeItems.empty()) {
    return;
  }

  const auto mergeInvalidObjects = [&](const auto& invalidObjectsForThreshold,
                                       const auto& blockedScopeItemIds) {
    for (const auto scopeItemId : blockedScopeItemIds) {
      auto invalidObjects = invalidObjectsForThreshold;
      if (const auto* exemptObjects =
              folly::get_ptr(scopeItemToExemptObjects, scopeItemId)) {
        for (const auto objectId : *exemptObjects) {
          invalidObjects.clear(objectId.asIndex());
        }
      }
      for (const auto containerId : scope.getContainerIds(scopeItemId)) {
        invalidMoveFilter.markInvalid(invalidObjects, containerId);
      }
    }
  };

  // Threshold-major construction performs O(objectCount * numThresholds)
  // predicate checks. Sorting is dominated by O(objectCount * log(objectCount))
  // because objectCount generally exceeds numThresholds.
  const auto shouldSort =
      static_cast<double>(thresholdToBlockedScopeItems.size()) >
      std::log2(
          static_cast<double>(std::max<std::size_t>(objectIds.size(), 2)));
  if (shouldSort) {
    markBySortedPrefixes(
        objectIds,
        scalarDim,
        thresholdToBlockedScopeItems,
        isInvalid,
        mergeInvalidObjects);
  } else {
    markByScanningEachThreshold(
        objectIds,
        scalarDim,
        thresholdToBlockedScopeItems,
        isInvalid,
        mergeInvalidObjects);
  }
}

} // namespace facebook::rebalancer::materializer
