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

#include "algopt/rebalancer/solver/expressions/Operators.h"

#include "algopt/rebalancer/solver/expressions/AnyPositive.h"
#include "algopt/rebalancer/solver/expressions/BipartiteSwaps.h"
#include "algopt/rebalancer/solver/expressions/BoundsOverride.h"
#include "algopt/rebalancer/solver/expressions/Ceil.h"
#include "algopt/rebalancer/solver/expressions/Floor.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/Log.h"
#include "algopt/rebalancer/solver/expressions/Max.h"
#include "algopt/rebalancer/solver/expressions/NthLargest.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/Piecewise.h"
#include "algopt/rebalancer/solver/expressions/Power.h"
#include "algopt/rebalancer/solver/expressions/ProductOperation.h"
#include "algopt/rebalancer/solver/expressions/QuotientOperation.h"
#include "algopt/rebalancer/solver/expressions/Rectangle.h"
#include "algopt/rebalancer/solver/expressions/Square.h"
#include "algopt/rebalancer/solver/expressions/StableStayed.h"
#include "algopt/rebalancer/solver/expressions/Step.h"
#include "algopt/rebalancer/solver/expressions/SumOverThreshold.h"
#include "algopt/rebalancer/solver/expressions/Swaps.h"
#include "algopt/rebalancer/solver/expressions/Transform.h"
#include "algopt/rebalancer/solver/expressions/Variable.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <fmt/core.h>

using namespace std;

namespace facebook::rebalancer {

static bool is_linearsum(const Expression& expr) {
  return expr.isLinearSum();
}

static LinearSum* get_linearsum(Expression& expr) {
  if (!is_linearsum(expr)) {
    throw std::runtime_error(
        fmt::format("ExprPtr is not linearsum but {}", expr.getType()));
  }
  return (LinearSum*)&expr;
}

static void combine(ExprPtr& lhs, ExprPtr rhs, double coef) {
  if (rhs == nullptr) {
    return;
  }
  if (lhs == nullptr) {
    lhs = const_expr(0, rhs->getUniverse());
  }
  const bool lhs_is_ls = is_linearsum(*lhs);
  const bool rhs_is_ls = is_linearsum(*rhs);
  if (!lhs_is_ls) {
    /* make left side a linearsum */
    PackerMap<std::shared_ptr<Expression>, double> children;
    children[lhs] = 1;
    lhs = std::make_shared<LinearSum>(lhs->getUniverse(), 0, children);
  }
  if (!rhs_is_ls) {
    /* linearsum + expression */
    lhs += std::move(rhs) * coef;
  } else {
    /* linearsum + linearsum */
    auto left = get_linearsum(*lhs);
    auto right = get_linearsum(*rhs);
    left->combine(*right, coef);
  }
}

static bool is_max(ExprPtr expr) {
  return expr->isMax();
}

static Max* get_max(ExprPtr expr) {
  if (!is_max(expr)) {
    throw std::runtime_error(
        fmt::format("ExprPtr is not max but {}", expr->getType()));
  }
  return (Max*)expr.get();
}

static bool is_any_positive(ExprPtr expr) {
  return expr->isAnyPositive();
}

static AnyPositive* get_any_positive(ExprPtr expr) {
  if (!is_any_positive(expr)) {
    throw std::runtime_error(
        fmt::format("ExprPtr is not any_positive but {}", expr->getType()));
  }
  return (AnyPositive*)expr.get();
}

ExprPtr const_expr(double value, const entities::Universe& universe) {
  return make_shared<LinearSum>(universe, value);
}

ExprPtr operator+(ExprPtr lhs, ExprPtr rhs) {
  const bool lhs_is_ls = is_linearsum(*lhs);
  const bool rhs_is_ls = is_linearsum(*rhs);

  if (!lhs_is_ls && !rhs_is_ls) {
    /* expression + expression */
    return lhs * 1 + rhs * 1;
  } else if (!lhs_is_ls && rhs_is_ls) {
    /* expression + linearsum */
    return lhs * 1 + rhs;
  } else if (lhs_is_ls && !rhs_is_ls) {
    /* linearsum + expression */
    return lhs + rhs * 1;
  } else {
    /* linearsum + linearsum */
    auto left = get_linearsum(*lhs);
    auto right = get_linearsum(*rhs);
    auto sum = *left + *right;
    return make_shared<LinearSum>(std::move(sum));
  }
}

ExprPtr operator+(ExprPtr lhs, double rhs) {
  if (lhs == nullptr) {
    throw std::runtime_error("lhs must not be null for inplace_add");
  }
  return lhs + const_expr(rhs, lhs->getUniverse());
}

ExprPtr operator+(double lhs, ExprPtr rhs) {
  return std::move(rhs) + lhs;
}

void operator+=(ExprPtr& lhs, ExprPtr rhs) {
  combine(lhs, std::move(rhs), 1);
}

void inplace_add(ExprPtr& lhs, ExprPtr rhs, double coef) {
  if (rhs == nullptr) {
    return;
  }
  const auto& universe = rhs->getUniverse();
  if (lhs == nullptr) {
    lhs = const_expr(0, universe);
  }
  const bool lhs_is_ls = is_linearsum(*lhs);

  if (!lhs_is_ls) {
    /* make left side a linearsum */
    PackerMap<std::shared_ptr<Expression>, double> children;
    children[lhs] = 1;
    lhs = std::make_shared<LinearSum>(universe, 0, children);
  }
  auto left = get_linearsum(*lhs);
  left->add(std::move(rhs), coef);
}

void operator+=(ExprPtr& lhs, double rhs) {
  if (lhs == nullptr) {
    throw std::runtime_error("lhs must not be null for inplace_add");
  }
  if (!is_linearsum(*lhs)) {
    /* make left side a linearsum */
    PackerMap<std::shared_ptr<Expression>, double> children;
    children[lhs] = 1;
    lhs = std::make_shared<LinearSum>(lhs->getUniverse(), 0, children);
  }
  /* LiearSum + const */
  LinearSum* sum = get_linearsum(*lhs);
  *sum += rhs;
}

void operator+=(ExprPtr& lhs, const vector<ExprPtr>& exprs) {
  for (const auto& expr : exprs) {
    lhs += expr;
  }
}

ExprPtr operator-(ExprPtr lhs, ExprPtr rhs) {
  return std::move(lhs) + std::move(rhs) * -1;
}

ExprPtr operator-(ExprPtr lhs, double rhs) {
  return std::move(lhs) + -rhs;
}

ExprPtr operator-(double lhs, ExprPtr rhs) {
  return lhs + std::move(rhs) * -1;
}

void operator-=(ExprPtr& lhs, ExprPtr rhs) {
  combine(lhs, std::move(rhs), -1);
}

void operator-=(ExprPtr& lhs, double rhs) {
  lhs += -rhs;
}

ExprPtr operator*(ExprPtr lhs, double rhs) {
  if (lhs == nullptr) {
    throw std::runtime_error("lhs must not be null for inplace_multiply");
  }
  if (rhs == 0 || lhs == 0) {
    return const_expr(0, lhs->getUniverse());
  }
  if (is_linearsum(*lhs)) {
    /* LinearSum * const */
    auto left = get_linearsum(*lhs);
    auto result = *left * rhs;
    return make_shared<LinearSum>(std::move(result));
  } else {
    /* other_type * const => linearsum */
    PackerMap<ExprPtr, double> expr_coef;
    expr_coef[lhs] = rhs;
    return make_shared<LinearSum>(lhs->getUniverse(), 0, expr_coef);
  }
}

ExprPtr operator*(double lhs, ExprPtr rhs) {
  return std::move(rhs) * lhs;
}

void operator*=(ExprPtr& lhs, double rhs) {
  if (lhs == nullptr) {
    throw std::runtime_error("lhs must not be null for operator*=");
  }
  const auto& precision = lhs->getPrecision();
  if (precision.isEqual(rhs, 0)) {
    lhs = const_expr(0, lhs->getUniverse());
    return;
  } else if (precision.isEqual(rhs, 1)) {
    return;
  }

  if (is_linearsum(*lhs)) {
    auto left = get_linearsum(*lhs);
    *left *= rhs;
  } else {
    PackerMap<ExprPtr, double> children;
    children[lhs] = rhs;
    lhs = std::make_shared<LinearSum>(lhs->getUniverse(), 0, children);
  }
}

ExprPtr operator/(ExprPtr lhs, double rhs) {
  return std::move(lhs) * (1 / rhs);
}

void operator/=(ExprPtr& lhs, double rhs) {
  lhs *= 1 / rhs;
}

bool operator==(ExprPtr lhs, double rhs) {
  if (!is_linearsum(*lhs)) {
    return false;
  }
  LinearSum* sum = get_linearsum(*lhs);
  return *sum == rhs;
}

ExprPtr min(ExprPtr A, ExprPtr B) {
  return -1 * max(-1 * std::move(A), -1 * std::move(B));
}

ExprPtr min(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe) {
  std::vector<ExprPtr> negated;
  negated.reserve(exprs.size());
  for (auto& expr : exprs) {
    negated.push_back(-1 * expr);
  }
  return -1 * max(negated, universe);
}

// caller needs to make sure that A and B are binary
// so indeed this is the optimization they want to apply
ExprPtr binary_min(ExprPtr A, ExprPtr B) {
  const auto& universe = A->getUniverse();
  if (A == 0 || B == 0) {
    return const_expr(0, universe);
  } else if (A == 1) {
    return B;
  } else if (B == 1) {
    return A;
  }
  return step(std::move(A) + std::move(B) - 1);
}

void inplace_binary_max(ExprPtr& A, ExprPtr B) {
  const auto& universe = A->getUniverse();
  if (A == 1 || B == 1) {
    A = const_expr(1, universe);
    return;
  }
  if (A == 0) {
    A = B;
    return;
  }
  if (B == 0) {
    return;
  }
  inplace_max(A, std::move(B));
}

ExprPtr max(ExprPtr lhs, ExprPtr rhs) {
  const auto& universe = lhs->getUniverse();
  return max({std::move(lhs), std::move(rhs)}, universe);
}

ExprPtr max(ExprPtr lhs, double rhs) {
  const auto& universe = lhs->getUniverse();
  auto constRhs = const_expr(rhs, universe);
  return max({std::move(lhs), std::move(constRhs)}, universe);
}

ExprPtr max(double lhs, ExprPtr rhs) {
  return max(std::move(rhs), lhs);
}

ExprPtr max(
    initializer_list<ExprPtr> exprs,
    const entities::Universe& universe) {
  return make_shared<Max>(exprs, universe);
}

ExprPtr max(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe) {
  return make_shared<Max>(exprs, universe);
}

void inplace_max(ExprPtr& lhs, ExprPtr rhs) {
  if (rhs == nullptr) {
    return;
  }
  if (lhs == nullptr) {
    lhs = rhs;
    return;
  }
  if (is_max(lhs)) {
    get_max(lhs)->add(rhs);
  } else {
    lhs = make_shared<Max>(
        initializer_list<ExprPtr>{lhs, std::move(rhs)}, lhs->getUniverse());
  }
}

ExprPtr any_positive(
    initializer_list<ExprPtr> exprs,
    const entities::Universe& universe) {
  return make_shared<AnyPositive>(exprs, universe);
}

ExprPtr any_positive(
    const std::vector<ExprPtr>& exprs,
    const entities::Universe& universe) {
  return make_shared<AnyPositive>(exprs, universe);
}

void inplace_any_positive(ExprPtr& lhs, ExprPtr rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    throw std::runtime_error(
        "lhs and rhs must not be null for inplace_any_positive");
  }
  get_any_positive(lhs)->combine(rhs);
}

void any_positive_add(ExprPtr& lhs, ExprPtr rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    throw std::runtime_error(
        "lhs and rhs must not be null for inplace_any_positive");
  }
  get_any_positive(lhs)->add(rhs);
}

vector<ExprPtr> equals(ExprPtr lhs, ExprPtr rhs) {
  return {lhs - rhs, std::move(rhs) - std::move(lhs)};
}

vector<ExprPtr> equals(ExprPtr lhs, double rhs) {
  const auto& universe = lhs->getUniverse();
  return equals(std::move(lhs), const_expr(rhs, universe));
}

vector<ExprPtr> equals(double lhs, ExprPtr rhs) {
  return equals(std::move(rhs), lhs);
}

ExprPtr swaps(
    const PackerMap<entities::ObjectId, entities::ContainerId>&
        initial_assignment,
    const entities::Universe& universe,
    const folly::Optional<PackerSet<entities::ObjectId>>& subset,
    Swaps::SubsetDefinition subsetDefinition) {
  return make_shared<Swaps>(
      initial_assignment, universe, subset, subsetDefinition);
}

ExprPtr bipartite_swaps(
    PackerMap<entities::ObjectId, entities::ContainerId> initial_assignment,
    PackerSet<entities::ContainerId> left_subset,
    PackerSet<entities::ContainerId> right_subset,
    const entities::Universe& universe) {
  return make_shared<BipartiteSwaps>(
      std::move(initial_assignment),
      std::move(left_subset),
      std::move(right_subset),
      universe);
}

std::shared_ptr<ObjectLookup> object_lookup(
    ExprPtr obj_vec,
    std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr,
    const Assignment& initialAssignment) {
  const auto& universe = obj_vec->getUniverse();
  return make_shared<ObjectLookup>(
      std::move(obj_vec),
      std::move(containersPtr),
      universe,
      initialAssignment);
}

std::shared_ptr<ObjectLookupDynamic> object_lookup_dynamic(
    ExprPtr sumOfObjectLookups,
    const entities::ObjectScalarDimension& dimension) {
  return make_shared<ObjectLookupDynamic>(
      std::move(sumOfObjectLookups), dimension);
}

std::shared_ptr<StableStayed> stable_stayed(
    std::shared_ptr<ObjectVector> initialObjectVector,
    std::shared_ptr<ObjectVector> fullObjectVector,
    std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr,
    const entities::Universe& universe,
    const Assignment& initialAssignment) {
  return make_shared<StableStayed>(
      initialObjectVector,
      fullObjectVector,
      containersPtr,
      universe,
      initialAssignment);
}

ExprPtr object_partition(
    std::shared_ptr<const PartitionInfo> partitionInfo,
    entities::DimensionId dimensionId,
    PackerMap<entities::GroupId, double> groupLimits,
    std::optional<PackerSet<entities::ScopeItemId>> scopeItemIds,
    PackerMap<entities::GroupId, double> groupCoefficients,
    double defaultGroupLimit,
    double defaultGroupCoefficient) {
  return make_shared<ObjectPartition>(
      std::move(partitionInfo),
      dimensionId,
      std::move(groupLimits),
      std::move(scopeItemIds),
      std::move(groupCoefficients),
      defaultGroupLimit,
      defaultGroupCoefficient);
}

ExprPtr object_partition_lookup(
    ExprPtr objectPartition,
    std::shared_ptr<const PackerSet<entities::ContainerId>> lookupContainersPtr,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    const Assignment& initialAssignment,
    PackerMap<entities::GroupId, double> groupLimitOverrides,
    PackerSet<entities::ObjectId> initialDuringObjects,
    std::optional<double> defaultGroupLimitOverride,
    ObjectPartitionLookupPenaltyTransform penaltyTransform,
    int groupsAllowed,
    bool minBound) {
  const auto& universe = objectPartition->getUniverse();
  return make_shared<ObjectPartitionLookupDefault>(
      objectPartition,
      lookupContainersPtr,
      scopeId,
      scopeItemId,
      universe,
      initialAssignment,
      std::move(groupLimitOverrides),
      std::move(initialDuringObjects),
      defaultGroupLimitOverride,
      penaltyTransform,
      groupsAllowed,
      minBound ? ObjectPartitionLookupDefault::Bound::MIN
               : ObjectPartitionLookupDefault::Bound::MAX,
      std::monostate{});
}

std::shared_ptr<ObjectVector> object_vector(
    entities::ObjectValues objectValues,
    const entities::Universe& universe) {
  return make_shared<ObjectVector>(std::move(objectValues), universe);
}

std::shared_ptr<ObjectVector> object_vector(
    std::shared_ptr<const entities::ObjectIdToDoubleMap>
        objectToNonDefaultValue,
    const entities::Universe& universe) {
  return object_vector(
      entities::ObjectValues(std::move(objectToNonDefaultValue)), universe);
}

ExprPtr power(ExprPtr base, double exponent) {
  return make_shared<Power>(base, exponent);
}

ExprPtr product(ExprPtr lhs, ExprPtr rhs) {
  return make_shared<ProductOperation>(lhs, rhs);
}

ExprPtr quotient(ExprPtr lhs, ExprPtr rhs) {
  return make_shared<QuotientOperation>(lhs, rhs);
}

ExprPtr square(ExprPtr expr) {
  return make_shared<Square>(expr);
}

ExprPtr square(ExprPtr expr, const ApproximationHint& hint) {
  return make_shared<Square>(expr, hint);
}

ExprPtr step(ExprPtr expr) {
  auto transform = dynamic_pointer_cast<Step>(expr);
  if (transform != nullptr) {
    return transform;
  }
  return make_shared<Step>(expr);
}

ExprPtr ceil(ExprPtr expr) {
  return make_shared<Ceil>(expr);
}

ExprPtr floor(ExprPtr expr) {
  return make_shared<Floor>(expr);
}

ExprPtr step_mod_k(ExprPtr expr, int k) {
  if (k == 0) {
    throw std::runtime_error("step_mod_k with k=0 is not supported");
  } else if (k == 1) {
    return expr;
  } else {
    auto quotient = std::move(expr) / k;
    auto nextInt = ceil(quotient);
    // (nextInt - quotient) is not same as expr % k
    // but step(nextInt - quotient) is equivalent to step(expr % k)
    return step(std::move(nextInt) - std::move(quotient));
  }
}

ExprPtr log(ExprPtr expr) {
  return make_shared<Log>(expr);
}

ExprPtr rectangle(ExprPtr expr, const double lb, const double ub) {
  return make_shared<Rectangle>(expr, lb, ub);
}

ExprPtr sum_over_threshold(
    ExprPtr threshold,
    const std::vector<ExprPtr>& values,
    bool square) {
  return make_shared<SumOverThreshold>(threshold, values, square);
}

ExprPtr variable(
    entities::ObjectId obj,
    entities::ContainerId con,
    const entities::Universe& universe,
    const Assignment& initialAssignment) {
  return make_shared<Variable>(obj, con, universe, initialAssignment);
}

ExprPtr piecewise(
    const std::vector<std::pair<double, double>>& points,
    ExprPtr x,
    bool continuous) {
  return make_shared<Piecewise>(points, x, continuous);
}

ExprPtr nth_largest(
    const std::vector<ExprPtr>& values,
    int n,
    const entities::Universe& universe) {
  return make_shared<NthLargest>(values, n, false, universe);
}

ExprPtr boundsOverride(
    ExprPtr expr,
    std::optional<double> lb,
    std::optional<double> ub) {
  return make_shared<BoundsOverride>(expr, lb, ub);
}

} // namespace facebook::rebalancer
