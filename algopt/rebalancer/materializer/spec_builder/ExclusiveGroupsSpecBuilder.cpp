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

#include "algopt/rebalancer/materializer/spec_builder/ExclusiveGroupsSpecBuilder.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/utils/ExclusiveGroups.h"
#include <algopt/rebalancer/materializer/utils/ExpressionBuilder.h>

#include <folly/String.h>

#include <memory>

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {

ExclusiveGroupsSpecBuilder::ExclusiveGroupsSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::ExclusiveGroupsSpec spec,
    std::shared_ptr<LogCollector> logger)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      logger_(std::move(logger)) {}

folly::coro::Task<PackerMap<std::string, std::string>>
ExclusiveGroupsSpecBuilder::computeScopeItemToGroupAssignment(
    ExpressionBuilder& expressionBuilder) const {
  // We pre-assign scope items to groups based on 3 metrics (deficit, moves,
  // deviation) and store in scopeItemGroupAssignment_. We do this using an
  // auxiliary MIP model.
  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto& scope = universe_->getScope(scopeId);
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  auto& scopeDimension = scope.getDimension(dimensionId);
  // 1. Get scopeItem capacities
  PackerMap<std::string, double> scopeItemSize;
  for (auto scopeItemId : scope.getScopeItemIds()) {
    scopeItemSize.emplace(
        universe_->getEntityName(scopeItemId),
        scopeDimension.getValue(scopeItemId));
  }
  // 2. Get number of objects in each group
  auto partitionId = universe_->getPartitionId(*spec_.partitionName());
  auto& partition = universe_->getPartition(partitionId);
  PackerMap<std::string, double> groupSize;
  for (auto groupId : partition.getGroupIds()) {
    groupSize.emplace(
        universe_->getEntityName(groupId),
        partition.getObjectIds(groupId).size());
  }
  // 3. compute initial allocation for each (group, scopeItem) pair
  PackerMap<std::string, PackerMap<std::string, double>>
      groupScopeItemFootprint;
  for (auto groupId : partition.getGroupIds()) {
    for (auto scopeItemId : scope.getScopeItemIds()) {
      auto utilForGroup = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::AFTER,
          dimensionId,
          scopeId,
          scopeItemId,
          partitionId,
          groupId);

      const double footprint = utilForGroup->getInitialValue();
      groupScopeItemFootprint[universe_->getEntityName(groupId)]
                             [universe_->getEntityName(scopeItemId)] =
                                 footprint;
    }
  }
  // This function computes the mapping of groups using a MIP solver minimizing
  // deficit, moves, and deviation. See the exclusive group spec builder unit
  // tests for more details and examples
  co_return computeExclusiveGroupsAssignment(
      groupSize, scopeItemSize, groupScopeItemFootprint);
}

folly::coro::Task<ExprPtr> ExclusiveGroupsSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

folly::coro::Task<std::vector<ConstraintInfo>>
ExclusiveGroupsSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  auto scopeItemGroupAssignment =
      co_await computeScopeItemToGroupAssignment(expressionBuilder);

  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto& scope = universe_->getScope(scopeId);
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  auto partitionId = universe_->getPartitionId(*spec_.partitionName());

  double totalCapacity = 0;
  for (auto scopeItemId : scope.getScopeItemIds()) {
    totalCapacity += scope.getDimension(dimensionId).getValue(scopeItemId);
  }
  auto numScopeItems = scope.getScopeItemIds().size();
  if (totalCapacity <= 0 && numScopeItems != 0) {
    throw std::runtime_error(
        fmt::format(
            "expected total capacity of all scope items in scope {} to be positive",
            *spec_.scope()));
  }
  const double normalizationCoeff = (totalCapacity == 0)
      ? 1.0
      : numScopeItems / totalCapacity; // (1 / avgCapacity)

  auto containerScopeId =
      universe_->getScopeId(universe_->getContainerTypeName());
  std::vector<ConstraintInfo> result;
  for (auto scopeItemId : scope.getScopeItemIds()) {
    auto itemName = universe_->getEntityName(scopeItemId);
    auto& assignedGroup = scopeItemGroupAssignment.at(itemName);
    auto assignedGroupId = universe_->getGroupId(partitionId, assignedGroup);

    // NOTE: instead of directly computing the util(scopeItemId), which would
    // involve a single objectLookup, we split it into lookups across individual
    // containers so that if, for example, only one of the containers in the
    // scopeItem has a violation, then local search can accurately pin-point to
    // that container, rather than have to look at all containers in the
    // scopeItem
    for (auto containerId : scope.getContainerIds(scopeItemId)) {
      auto containerScopeItemId = universe_->getScopeItemId(
          containerScopeId, universe_->getEntityName(containerId));

      // ensure that objects outside of 'assignedGroupId' are not in container
      auto totalContainerUtil = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::AFTER,
          dimensionId,
          containerScopeId,
          containerScopeItemId);
      auto groupContainerUtil = co_await expressionBuilder.getAbsoluteUtil(
          UtilMetric::AFTER,
          dimensionId,
          containerScopeId,
          containerScopeItemId,
          partitionId,
          assignedGroupId);

      result.emplace_back(
          normalizationCoeff * (totalContainerUtil - groupContainerUtil));
    }
  }

  // Log the computed mapping of scope items with groups.
  interface::SpecMetadata metadata;
  metadata.specName() = *spec_.name();
  metadata.exclusiveGroupsTagging() = interface::ExclusiveGroupsTagging();
  metadata.exclusiveGroupsTagging()->scopeItemToGroup() =
      std::map<std::string, std::string>(
          scopeItemGroupAssignment.begin(), scopeItemGroupAssignment.end());
  logger_->log(std::move(metadata));

  std::vector<std::string> taggingInfo;
  taggingInfo.reserve(scopeItemGroupAssignment.size());
  for (auto& [scopeItem, group] : scopeItemGroupAssignment) {
    taggingInfo.push_back(fmt::format("{} -> {}", scopeItem, group));
  }
  taggingDescription_ = folly::join(", ", taggingInfo);
  co_return result;
}

std::string ExclusiveGroupsSpecBuilder::description() const {
  return fmt::format(
      "exclusive groups spec {} on dimension {}, scope {}, partition {}, tagging: {}",
      *spec_.name(),
      *spec_.dimension(),
      *spec_.scope(),
      *spec_.partitionName(),
      taggingDescription_);
}

SpecParameters ExclusiveGroupsSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .partition = *spec_.partitionName(),
      .dimension = *spec_.dimension()};
}

} // namespace facebook::rebalancer::materializer
