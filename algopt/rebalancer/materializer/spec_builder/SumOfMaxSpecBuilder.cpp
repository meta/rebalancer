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

#include "algopt/rebalancer/materializer/spec_builder/SumOfMaxSpecBuilder.h"

#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {

SumOfMaxSpecBuilder::SumOfMaxSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::SumOfMaxSpec spec)
    : SpecBuilder(std::move(universe)), spec_(std::move(spec)) {}

folly::coro::Task<std::vector<ConstraintInfo>> SumOfMaxSpecBuilder::constraints(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error("SumOfMaxSpec not supported as a constraint");
}

folly::coro::Task<ExprPtr> SumOfMaxSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  auto scopeId = universe_->getScopeId(*spec_.scope());
  auto partitionId = universe_->getPartitionId(*spec_.partitionName());
  auto dimensionId = universe_->getDimensionId(*spec_.dimension());

  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId);
  auto filteredScopeItemIds = filter.getScopeItemIds();
  const auto& groupIds = universe_->getPartition(partitionId).getGroupIds();

  ExprPtr sum = const_expr(0, *universe_);
  // Max needs at least one child; without groups every scope item adds 0.
  if (groupIds.empty()) {
    co_return sum;
  }
  for (auto scopeItemId : filteredScopeItemIds) {
    std::vector<ExprPtr> partitionExprs;
    partitionExprs.reserve(groupIds.size());
    for (auto groupId : groupIds) {
      partitionExprs.push_back(
          co_await expressionBuilder.getAbsoluteUtil(
              UtilMetric::AFTER,
              dimensionId,
              scopeId,
              scopeItemId,
              partitionId,
              groupId));
    }
    sum += max(partitionExprs, *universe_);
  }
  co_return sum;
}

std::string SumOfMaxSpecBuilder::description() const {
  return fmt::format(
      "SumOfMax({}, {}) for scope {}",
      *spec_.partitionName(),
      *spec_.dimension(),
      *spec_.scope());
}

SpecParameters SumOfMaxSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .partition = *spec_.partitionName(),
      .dimension = *spec_.dimension(),
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

} // namespace facebook::rebalancer::materializer
