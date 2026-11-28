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

#include "algopt/rebalancer/materializer/spec_builder/AggregatedGroupSpecBuilder.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include <algopt/rebalancer/materializer/utils/ExpressionBuilder.h>
#include <algopt/rebalancer/materializer/utils/LimitWrapper.h>

#include <memory>

namespace facebook::rebalancer::materializer {

AggregatedGroupSpecBuilder::AggregatedGroupSpecBuilder(
    std::shared_ptr<const entities::Universe> universe,
    interface::AggregatedGroupSpec spec)
    : SpecBuilder(std::move(universe)), spec_(std::move(spec)) {}

folly::coro::Task<ExprPtr> AggregatedGroupSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return getAggregatedConstraintViolation(
      co_await constraints(expressionBuilder), *universe_);
}

folly::coro::Task<std::vector<ConstraintInfo>>
AggregatedGroupSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());
  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto& scope = universe_->getScope(scopeId);
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId);
  auto scopeItemIds = filter.getScopeItemIds();
  auto partitionId = universe_->getPartitionId(*spec_.partitionName());
  auto& partition = universe_->getPartition(partitionId);
  auto& limit = *spec_.limit();
  auto containerAggregationType = *spec_.containerAggregationType();
  auto withinGroupAggregationType = *spec_.withinGroupAggregationType();
  auto groupAggregationType = *spec_.groupAggregationType();

  // if contribution has been set, we cache a map of scopeItemId and
  // LimitWrapper
  std::map<entities::ScopeItemId, LimitWrapper> coeMap;
  if (spec_.contributions()) {
    for (auto& [itemName, contribution] : *spec_.contributions()) {
      auto scopeItemId = universe_->getScopeItemId(scopeId, itemName);
      LimitWrapper lim(*universe_, contribution, scopeId);
      coeMap.emplace(scopeItemId, std::move(lim));
    }
  }

  const LimitWrapper wrapper(*universe_, limit, scopeId);
  const std::map<entities::ContainerId, entities::ScopeItemId>
      containerReverseMap;

  auto& objects = universe_->getObjects();
  auto& dimensions = objects.getDimension(dimensionId);
  if (dimensions.size() > 1) {
    throw std::runtime_error(fmt::format("vector dimensions not supported."));
  }
  std::vector<ConstraintInfo> results;
  for (auto scopeItemId : scopeItemIds) {
    ExprPtr scopeItemValue;
    const double scopeItemLimit = wrapper.getLimit(scopeItemId);
    auto coefficients = folly::get_ptr(coeMap, scopeItemId);
    for (auto containerId : scope.getContainerIds(scopeItemId)) {
      ExprPtr containerValue;
      for (auto groupId : partition.getGroupIds()) {
        ExprPtr groupValue;
        for (auto objectId : partition.getObjectIds(groupId)) {
          double coefficient = 1;
          if (coefficients) {
            coefficient = coefficients->getLimit(scopeItemId, groupId);
          }
          auto withinGroupRes =
              expressionBuilder.isAssigned(containerId, objectId);
          double weight = dimensions.at(0).getValueSafe(
              objectId,
              entities::ScopeScopeItemPair{
                  .scopeId = scopeId, .scopeItemId = scopeItemId});
          if (weight == 0) {
            continue;
          }
          weight *= coefficient;
          if (withinGroupAggregationType ==
              interface::AggregatedGroupSpecAggType::MAX) {
            inplace_max(groupValue, withinGroupRes * weight);
          } else if (
              withinGroupAggregationType ==
              interface::AggregatedGroupSpecAggType::SUM) {
            groupValue += withinGroupRes * weight;
          } else {
            throw std::runtime_error(
                fmt::format(
                    "{} is not supported yet",
                    fmt::underlying(withinGroupAggregationType)));
          }
        }
        if (groupAggregationType ==
            interface::AggregatedGroupSpecAggType::MAX) {
          inplace_max(containerValue, groupValue);
        } else if (
            groupAggregationType ==
            interface::AggregatedGroupSpecAggType::SUM) {
          containerValue += groupValue;
        } else {
          throw std::runtime_error(
              fmt::format(
                  "{} is not supported yet",
                  fmt::underlying(groupAggregationType)));
        }
      }

      if (containerAggregationType ==
          interface::AggregatedGroupSpecAggType::MAX) {
        inplace_max(scopeItemValue, containerValue);
      } else if (
          containerAggregationType ==
          interface::AggregatedGroupSpecAggType::SUM) {
        scopeItemValue += containerValue;
      } else {
        throw std::runtime_error(
            fmt::format(
                "{} is not supported yet",
                fmt::underlying(containerAggregationType)));
      }
    }
    if (!scopeItemValue) {
      // no object has a non-zero weight in this scope item
      scopeItemValue = const_expr(0, *universe_);
    }
    results.emplace_back(scopeItemValue - scopeItemLimit);
  }
  co_return results;
}

std::string AggregatedGroupSpecBuilder::description() const {
  return fmt::format(
      "Minimize aggregated groupe size of {}s weighted by {}",
      *spec_.scope(),
      *spec_.dimension());
}

SpecParameters AggregatedGroupSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .partition = *spec_.partitionName(),
      .dimension = *spec_.dimension(),
      .limitType = apache::thrift::util::enumNameSafe(*spec_.limit()->type()),
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

} // namespace facebook::rebalancer::materializer
