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

#include "algopt/rebalancer/materializer/spec_builder/MinimizeContainersSpecBuilder.h"

#include "algopt/rebalancer/materializer/utils/FilterWrapper.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

using namespace facebook::rebalancer::entities;

namespace {
struct DimensionValues {
  double sum = 0;
  std::optional<double> minPositive;
};

DimensionValues getObjectDimensionSumAndMinPositiveValue(
    const ObjectDimension& objectDimension) {
  auto throwMsg = [](const std::string& reason) {
    throw std::runtime_error(
        fmt::format(
            "{} are not supported when using MinimizeContainerSpec with NEW formula",
            reason));
  };
  if (objectDimension.hasNegativeValues()) {
    throwMsg("Negative dimension values");
  }

  if (objectDimension.isDynamic() || objectDimension.size() != 1) {
    throwMsg("Non-scalar or dynamic dimensions");
  }

  const auto& objectScalarDimension = objectDimension.at(0);
  return {
      .sum = objectScalarDimension.values().sum(),
      .minPositive = objectScalarDimension.getMinimumPositiveValue()};
}

// The continuous formulas weight each scope item by 1 / cost, which has no
// finite value for a cost of 0 (and inverts the incentive for a negative one).
double getContinuousFormulaCost(
    const std::map<std::string, double>& containerCosts,
    const std::string& scopeItemName) {
  const double cost = folly::get_default(containerCosts, scopeItemName, 1.0);
  if (!(cost > 0)) {
    throw std::runtime_error(
        fmt::format(
            "Non-positive containerCosts are not supported when using MinimizeContainerSpec with a continuous formula; '{}' has cost {}",
            scopeItemName,
            cost));
  }
  return cost;
}

} // namespace

namespace facebook::rebalancer::materializer {

MinimizeContainersSpecBuilder::MinimizeContainersSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::MinimizeContainersSpec spec,
    bool continuousExpressions)
    : SpecBuilder(std::move(universe)),
      spec_(std::move(spec)),
      continuousExpressions_(continuousExpressions),
      scopeId_(
          !spec_.scope()->empty()
              ? universe_->getScopeId(*spec_.scope())
              : universe_->getScopeId(universe_->getContainerTypeName())),
      dimensionId_(
          !spec_.dimension()->empty()
              ? universe_->getDimensionId(*spec_.dimension())
              : universe_->getDimensionId(
                    fmt::format("{}_count", universe_->getObjectTypeName()))),
      objectDimension_(universe_->getObjects().getDimension(dimensionId_)),
      containerCosts_(*spec_.containerCosts()) {}

int64_t MinimizeContainersSpecBuilder::numUsedScopeItemLimit(
    int64_t numScopeItems) const {
  // No stopping condition configured: pack down as far as possible.
  if (!spec_.target()) {
    return 0;
  }
  switch (spec_.target()->getType()) {
    case interface::MinimizeContainersTarget::Type::maxFreeLimit:
      return numScopeItems -
          std::min<int64_t>(
                 spec_.target()->maxFreeLimit().value(), numScopeItems);
    case interface::MinimizeContainersTarget::Type::minUsedLimit:
      return std::min<int64_t>(
          spec_.target()->minUsedLimit().value(), numScopeItems);
    case interface::MinimizeContainersTarget::Type::__EMPTY__:
      throw std::runtime_error("MinimizeContainersTarget is set but empty");
  }
}

folly::coro::Task<ExprPtr>
MinimizeContainersSpecBuilder::getMinimizeContainersContinuousFormulaExpr(
    ExpressionBuilder& expressionBuilder) const {
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId_);
  const auto& filteredScopeItemIds = filter.getScopeItemIds();
  const auto usedLimit = numUsedScopeItemLimit(filteredScopeItemIds.size());

  auto [objectDimensionSum, minPositiveDimensionValue] =
      getObjectDimensionSumAndMinPositiveValue(objectDimension_);
  if (universe_->getPrecision().compare(objectDimensionSum, 0) == 0) {
    co_return const_expr(0, *universe_);
  }
  /*
    objective funtion: minimize the following, where afterUtil() is a single
  lookup over all containers that are to be freed.

    objective = (expr1 - expr2) * expr3, where
    expr1 = coeff1 * afterUtil(scopeItemsToFree)

    expr2 = coeff2 * sum_{c \in scopeItemsToFree} (absoluteUtil(c))^1.1)

    expr3 = step(occupiedScopeItems - usedLimit)

    coeff1 = 2 / minPositiveDimensionValue_or_1

    coeff2 = 0.5 / (objectDimensionSum)^1.1

  NOTE:
  a) 0 <= 2 / minPositiveDimensionValue_or_1 <= 2 for
  minPositiveDimensionValue_or_1 >= 1

  b) 0 <= 0.5/(objectDimensionSum^1.1 <= 0.5
  when objectDimensionSum >= 1

  c) afterUtil(scopeItemsToFree) >= 1 as long
  there is at least one object in scopeItemToFree, and it decreases by at least
  2 any time an object with non-zero dimension value moves from a scopeItem in
  scopeItemsToFree to a scopeItem outside scopeItemsToFree. Therefore, there is
  always an incentive to move an object with non-zero dimension value out.

  d)  - coeff2 * sum_{c \in scopeItemsToFree}
  (absoluteUtil(c))^1.1) will always incitivize moving objects from smaller
  containers to bigger containers when absoluteUtil(c) are >= 1

  e) expr3's value indicates whether the number of occupied scopeItems is still
  above usedLimit (the used scope item limit). usedLimit collapses both stopping
  conditions: maxFreeLimit=F => numScopeItems - F, minUsedLimit=U => U.

  f) This objective function assumes that local search is implemented in a
  certain way. In particular,
    (i) in the object_lookup expression that computes
    afterUtil(scopeItemsToFree), all the underlying containers are considered to
  be equally "hot"

    (ii) In the objective expression expr1 - expr2, expr1 has higher potential
    than expr2, because expr2's potential is at most 1, whereas expr1's
  potential is at least (2 - 0.5 = 1.5) (potential here is distance from lower
  bound; see Expression.cpp)

    (iii) as a result of (i) and (ii), the hottest container c will the one that
    has absoluteUtil(c) closest to zero; this is because -(absoluteUtil(c))^1.1)
    expression associated with such a container c will have the largest
  potential
  */

  ExprPtr occupiedScopeItemsCount = const_expr(0, *universe_);
  ExprPtr sumOverUtilSquared = const_expr(0, *universe_);
  for (auto& scopeItemId : filteredScopeItemIds) {
    const double cost = getContinuousFormulaCost(
        containerCosts_, universe_->getEntityName(scopeItemId));
    auto absoluteUtil = co_await expressionBuilder.getAbsoluteUtil(
        UtilMetric::AFTER, dimensionId_, scopeId_, scopeItemId);

    inplace_add(sumOverUtilSquared, (1 / cost) * power(absoluteUtil, 1.1));

    // if absoluteUtil > 0 then step(absoluteUtil) = 1
    // else step(absoluteUtil) = 0
    inplace_add(occupiedScopeItemsCount, step(absoluteUtil));
  }

  // create a single lookup expr over all scopeItemIdsToFree
  const ExprPtr totalAfterUtil = co_await expressionBuilder.getAbsoluteUtil(
      UtilMetric::AFTER, dimensionId_, scopeId_, filteredScopeItemIds);
  const double coeff1 = 2 / minPositiveDimensionValue.value();
  const double coeff2 = 0.5 / pow(objectDimensionSum, 1.1);

  auto isAboveLimit = step(occupiedScopeItemsCount - usedLimit) /
      (2 * filteredScopeItemIds.size());

  // this formula will continue until the number of occupied scopeItems reaches
  // the used scope item limit or until all objects are in 1 scopeItem
  co_return product(
      coeff1 * totalAfterUtil - coeff2 * sumOverUtilSquared, isAboveLimit);
}

folly::coro::Task<ExprPtr> MinimizeContainersSpecBuilder::getObjectiveExpr(
    ExpressionBuilder& expressionBuilder) const {
  if (!continuousExpressions_) {
    co_return co_await getMinimizeContainersDiscreteFormulaExpr(
        expressionBuilder);
  } else {
    switch (*spec_.formula()) {
      case interface::MinimizeContainerSpecFormula::LEGACY:
        co_return co_await getMinimizeContainerLegacyFormulaExpr(
            expressionBuilder);
      case interface::MinimizeContainerSpecFormula::NEW:
        co_return co_await getMinimizeContainersContinuousFormulaExpr(
            expressionBuilder);
    }
  }
}

folly::coro::Task<ExprPtr>
MinimizeContainersSpecBuilder::getMinimizeContainersDiscreteFormulaExpr(
    ExpressionBuilder& expressionBuilder) const {
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId_);
  const auto& filteredScopeItemIds = filter.getScopeItemIds();
  const auto usedLimit = numUsedScopeItemLimit(filteredScopeItemIds.size());

  auto result = const_expr(0, *universe_);
  for (auto scopeItemId : filteredScopeItemIds) {
    // Each container may have some cost depending on usecase
    // and we minimize total sum of the cost of containers being used
    const double containerCost = folly::get_default(
        containerCosts_, universe_->getEntityName(scopeItemId), 1.0);
    auto hasObject = step(
        co_await expressionBuilder.getRelativeUtil(
            UtilMetric::AFTER, dimensionId_, scopeId_, scopeItemId));
    result += hasObject * containerCost;
  }

  co_return max({const_expr(0, *universe_), result - usedLimit}, *universe_);
}

folly::coro::Task<ExprPtr>
MinimizeContainersSpecBuilder::getMinimizeContainerLegacyFormulaExpr(
    ExpressionBuilder& expressionBuilder) const {
  const ScopeItemFilterWrapper filter(*universe_, *spec_.filter(), scopeId_);
  const auto& filteredScopeItemIds = filter.getScopeItemIds();
  const auto usedLimit = numUsedScopeItemLimit(filteredScopeItemIds.size());

  if (continuousExpressions_ && usedLimit != 0) {
    throw std::runtime_error(
        "Custom stopping condition (maxFreeLimit/minUsedLimit) not supported in "
        "minimize containers goal in LEGACY formula but is supported in NEW formula");
  }

  double dimensionSum = 1;
  const Context context;
  for (auto scopeItemId : filteredScopeItemIds) {
    auto sum = co_await expressionBuilder.getRelativeUtil(
        UtilMetric::AFTER, dimensionId_, scopeId_, scopeItemId);
    dimensionSum += sum->getInitialValue();
  }

  auto result = const_expr(0, *universe_);
  for (auto scopeItemId : filteredScopeItemIds) {
    // Each container may have some cost depending on usecase
    // and we minimize total sum of the cost of containers being used

    const double containerCost = getContinuousFormulaCost(
        containerCosts_, universe_->getEntityName(scopeItemId));
    const double coeff = 1.0 / containerCost;
    auto expr = coeff *
        power(co_await expressionBuilder.getRelativeUtil(
                  UtilMetric::AFTER, dimensionId_, scopeId_, scopeItemId),
              1.25);
    result += expr;
  }

  co_return (-1 * result) / dimensionSum;
}

folly::coro::Task<ExprPtr> MinimizeContainersSpecBuilder::goalCoro(
    ExpressionBuilder& expressionBuilder) const {
  co_return co_await getObjectiveExpr(expressionBuilder);
}

folly::coro::Task<std::vector<ConstraintInfo>>
MinimizeContainersSpecBuilder::constraints(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error(
      "MinimizeContainersSpec not supported as a constraint");
}

std::string MinimizeContainersSpecBuilder::description() const {
  return fmt::format(
      "For {}, minimize used {}s, formula={}",
      *spec_.dimension(),
      *spec_.scope(),
      apache::thrift::util::enumNameSafe(*spec_.formula()));
}

SpecParameters MinimizeContainersSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .dimension = *spec_.dimension(),
      .filterAllowListSize = spec_.filter()->itemsWhitelist()
          ? static_cast<int>(spec_.filter()->itemsWhitelist()->size())
          : 0,
      .filterBlockListSize = spec_.filter()->itemsBlacklist()
          ? static_cast<int>(spec_.filter()->itemsBlacklist()->size())
          : 0};
}

} // namespace facebook::rebalancer::materializer
