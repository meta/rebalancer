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

#include "algopt/rebalancer/solver/expressions/Piecewise.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/Enumerate.h>
#include <folly/container/irange.h>

#include <sstream>

namespace {
constexpr std::string_view type = "Piecewise";
}

#define PIECEWISE_NEWCTR(x) \
  batch.add({x, folly::to<std::string>("piecewise_function_", __LINE__)})

namespace facebook::rebalancer {

namespace {
bool isNonDecreasing(const std::vector<std::pair<double, double>>& points) {
  bool isNonDecreasingY = true;
  for (const auto i : folly::irange(1, points.size())) {
    auto& [x, y] = points[i];
    auto& [prevX, prevY] = points[i - 1];
    if (x < prevX) {
      throw std::runtime_error(
          fmt::format(
              "expected x points to be in non-decreasing order, found consecutive points {}, {} which are out of order",
              prevX,
              x));
    }
    if (y < prevY) {
      isNonDecreasingY = false;
      break;
    }
  }

  return isNonDecreasingY;
}
} // namespace

Piecewise::Piecewise(
    const std::vector<std::pair<double, double>>& points,
    std::shared_ptr<Expression> expr,
    bool continuous)
    : Expression(expr->getUniverse()), points_(points) {
  if (points_.size() < 2) {
    throw std::runtime_error("Needs to define at least 2 points for Piecewise");
  }
  double prevX = points_.front().first;
  double prevY = points_.front().second;
  for (const auto i : folly::irange(1, points_.size())) {
    auto x = points_.at(i).first;
    auto y = points_.at(i).second;
    if (x < prevX) {
      throw std::runtime_error(
          "Require piecewise points to be in increasing order");
    }
    if (continuous && prevX == x) {
      throw std::runtime_error(
          "discontinuous piecewise. Not allowed unless specified");
    }
    if (x == prevX) {
      if (y == prevY) {
        throw std::runtime_error(
            fmt::format("duplicated points defined {}, {}", x, y));
      }
      slopes_.push_back(
          y > prevY ? std::numeric_limits<double>::infinity()
                    : -std::numeric_limits<double>::infinity());
    } else {
      slopes_.push_back((y - prevY) / (x - prevX));
    }
    prevX = x;
    prevY = y;
  }
  add_child(expr);

  setInitialValue(performPiecewise(getOnlyChildRawPtr()->getInitialValue()));
}

const std::string_view& Piecewise::getType() const {
  return type;
}

void Piecewise::updateEquivalenceSets(EquivalenceSets& equivalenceSets) const {
  if (auto var = getOnlyChildRawPtr()->getVar()) {
    auto [obj, cont] = *var;
    PackerMap<entities::ObjectId, entities::ContainerId> mapping;
    mapping.emplace(obj, cont);
    equivalenceSets.mappingMerge(mapping);
  }
}

double Piecewise::performPiecewise(double val) const {
  if (val < points_.front().first) {
    throw std::runtime_error("x cannot be smaller than first points");
  }
  if (val > points_.back().first) {
    throw std::runtime_error("x cannot be larger than last points");
  }

  int slopePos = -1;
  for (auto [x, y] : points_) {
    if (val == x) {
      // when val == x
      // apply returns y
      // so we will never need to use the inifinity slope value
      return y;
    }
    if (val < x) {
      // val is in range of (prevX, x]
      // should return
      // y - slope * (x - val)
      return y - slopes_.at(slopePos) * (x - val);
    }
    slopePos += 1;
  }
  throw std::runtime_error("x is larger than given points");
}

double Piecewise::evaluate(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  return performPiecewise(evaluator.evaluate(getOnlyChildRawPtr(), changes));
}

double Piecewise::_applyUsingChildValues(
    const Evaluator& evaluator,
    const Assignment& assignment) {
  value = performPiecewise(evaluator.apply(getOnlyChildRawPtr(), assignment));
  return value;
}

double Piecewise::innerFullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  return _applyUsingChildValues(evaluator, assignment);
}

double Piecewise::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    const ChangeSet& /* unused */) {
  return _applyUsingChildValues(evaluator, assignment);
}

Bounds Piecewise::innerLowerAndUpperBounds(
    Context& /*context*/,
    const BoundConstraints& /*unused*/) const {
  double lb = std::numeric_limits<double>::max();
  double ub = std::numeric_limits<double>::lowest();
  for (auto [x, y] : points_) {
    lb = std::min(lb, y);
    ub = std::max(ub, y);
  }

  return {.lower_bound = lb, .upper_bound = ub};
}

void Piecewise::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  evaluator.computeLpIntent(getOnlyChild(), minimizing);
}

algopt::lp::Expression Piecewise::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (evaluator.supportsNativePwl() && points_.size() >= 2) {
    auto* child = getOnlyChildRawPtr();
    const auto [childLb, childUb] = evaluator.lowerAndUpperBounds(child);
    const double pwlXMin = points_.front().first;
    const double pwlXMax = points_.back().first;
    // Only use native PWL when the child's proven bounds fit within the
    // breakpoint domain. The native path clamps the auxiliary input variable
    // to [pwlXMin, pwlXMax] via equality, which would render the problem
    // infeasible if the child can reach values outside that range.
    // Note: static expression bounds are a necessary but not sufficient
    // check — additional model constraints could push x outside the
    // breakpoint domain at runtime, silently causing infeasibility.
    if (childLb >= pwlXMin && childUb <= pwlXMax) {
      const auto& xLp = evaluator.lp(child, minimizing, configs);
      if (auto result = evaluator.addNativePwlConstraint(xLp, points_)) {
        return *result;
      }
    }
  }
  return build_piecewise_linear_function(
      points_, getOnlyChild(), evaluator, minimizing, configs);
}

std::string Piecewise::innerDigest(size_t maxChildren) const {
  std::stringstream ss;
  ss << "= PIECEWISE(";
  for (const auto& [idx, point] : folly::enumerate(points_)) {
    if (idx >= maxChildren) {
      ss << "... " << points_.size() - maxChildren << " more";
      break;
    }
    if (idx > 0) {
      ss << ", ";
    }
    ss << fmt::format("[{}, {}]", point.first, point.second);
  }
  ss << ")";
  return ss.str();
}

ExpressionProperties Piecewise::getProperties() const {
  ExpressionProperties properties;
  properties.properties()->emplace(
      "points", PropertiesHelper::makePoint2dListValue(points_));
  return properties;
}

std::vector<std::pair<double, double>> sample_real_function(
    const std::function<double(double)>& function,
    double lb,
    double ub,
    size_t count) {
  assert(count >= 2);
  std::vector<std::pair<double, double>> sample;
  for (const auto i : folly::irange(count)) {
    const double p = i / double(count - 1);
    const double x = lb + (ub - lb) * p;
    const double y = function(x);
    sample.emplace_back(x, y);
  }
  return sample;
}

algopt::lp::Expression build_piecewise_linear_function(
    const std::vector<std::pair<double, double>>& points,
    ExprPtr expr,
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (points.size() < 2) {
    throw std::runtime_error("Need at least 2 points to build piecewise");
  }

  // can only minimize if minimizing=true and the function is non-decreasing
  const bool shouldMinimize = minimizing && isNonDecreasing(points);
  auto& exprLp = evaluator.lp(expr.get(), shouldMinimize, configs);

  LpConstraintBatch batch(evaluator);

  bool strictlyIncreasingX = true;
  std::vector<algopt::lp::Variable> lambda;
  auto lambda_sum = evaluator.makeLpExpression();
  for (const auto i : folly::irange(points.size())) {
    lambda.push_back(evaluator.makeLpVar(
        LpVarType::CONTINUOUS, fmt::format("pw_lambda_{}", i)));
    lambda_sum += lambda.back();
    PIECEWISE_NEWCTR(lambda.back() >= 0);
    if (i > 0 && points[i].first <= points[i - 1].first) {
      strictlyIncreasingX = false;
    }
  }

  PIECEWISE_NEWCTR(lambda_sum == 1);

  auto x_expr = evaluator.makeLpExpression();
  assert(lambda.size() == points.size());
  for (const auto i : folly::irange(points.size())) {
    x_expr += points[i].first * lambda[i];
  }
  PIECEWISE_NEWCTR(x_expr == exprLp);

  std::vector<algopt::lp::Variable> active;
  auto active_sum = evaluator.makeLpExpression();
  for (const auto i : folly::irange(points.size() - 1)) {
    active.push_back(evaluator.makeLpVar(
        LpVarType::BINARY, fmt::format("pw_interval_{}", i)));
    active_sum += active.back();
  }
  PIECEWISE_NEWCTR(active_sum == 1);

  assert(active.size() == points.size() - 1);
  assert(lambda.size() == points.size());
  for (const auto i : folly::irange(points.size())) {
    auto pair_sum = evaluator.makeLpExpression();
    for (size_t j = i ? i - 1 : 0; j <= std::min(i, points.size() - 2); ++j) {
      pair_sum += active.at(j);
    }
    PIECEWISE_NEWCTR(lambda[i] <= pair_sum);
  }

  auto z_expr = evaluator.makeLpExpression();
  assert(lambda.size() == points.size());
  for (const auto i : folly::irange(points.size())) {
    z_expr += points[i].second * lambda[i];
  }

  // Suppose the x-points are not strictly increasing and we have points
  // [x1, y1], [x1, y2] specified in that order, which in turn denotes a jump in
  // the function. Then, when expr = x1, we want the piecewise function to take
  // the value y1 (see also innerFullApply() function). Otherwise, the
  // solver is free to set the value of the piecewise function to y1 or y2
  // (depending on optimization intent)
  if (!strictlyIncreasingX) {
    for (const auto i : folly::irange(points.size())) {
      const auto currX = points[i].first;
      if (i > 0 && currX == points[i - 1].first) {
        // only apply constraint below the first time we encounter currX
        continue;
      }

      // if expr == currX, then lambda[i] should be 1 (which in turn
      // would make z_expr = points[i].second)
      const auto diff = expr - currX;
      const auto isEqual = 1 - step(max(diff, -1 * diff));
      batch.flush();
      PIECEWISE_NEWCTR(
          lambda.at(i) >=
          evaluator.lp(isEqual.get(), /*minimizing=*/false, configs));
    }
  }

  batch.flush();
  return z_expr;
}

algopt::lp::Expression build_piecewise_linear_function(
    const std::function<double(double)>& function,
    double hard_lb,
    double hard_ub,
    double soft_lb,
    double soft_ub,
    size_t soft_segments,
    ExprPtr expr,
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  auto points =
      sample_real_function(function, soft_lb, soft_ub, soft_segments + 1);
  if (hard_lb < soft_lb) {
    points.emplace(points.begin(), hard_lb, function(hard_lb));
  }
  if (soft_ub < hard_ub) {
    points.emplace_back(hard_ub, function(hard_ub));
  }
  return build_piecewise_linear_function(
      points, std::move(expr), evaluator, minimizing, configs);
}

algopt::lp::Expression build_piecewise_linear_function(
    const std::function<double(double)>& function,
    double lb,
    double ub,
    size_t segments,
    ExprPtr expr,
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  return build_piecewise_linear_function(
      function,
      lb,
      ub,
      lb,
      ub,
      segments,
      std::move(expr),
      evaluator,
      minimizing,
      configs);
}

} // namespace facebook::rebalancer
