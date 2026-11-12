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

#include "algopt/rebalancer/solver/expressions/ProductOperation.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"

#include <folly/container/irange.h>

namespace {
constexpr std::string_view type = "Product";
}

namespace facebook::rebalancer {

ProductOperation::ProductOperation(
    std::shared_ptr<Expression> expr1,
    std::shared_ptr<Expression> expr2)
    : BinaryOperation(std::move(expr1), std::move(expr2)) {
  setInitialValue(child1st->getInitialValue() * child2nd->getInitialValue());
}

const std::string_view& ProductOperation::getType() const {
  return type;
}

double ProductOperation::evaluate(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  return evaluator.evaluate(child1st.get(), changes) *
      evaluator.evaluate(child2nd.get(), changes);
}

double ProductOperation::_applyUsingChildValues(
    const Evaluator& evaluator,
    const Assignment& assignment) {
  value = evaluator.apply(child1st.get(), assignment) *
      evaluator.apply(child2nd.get(), assignment);
  return value;
}

double ProductOperation::innerFullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  return _applyUsingChildValues(evaluator, assignment);
}

double ProductOperation::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    const ChangeSet& /*unused*/) {
  return _applyUsingChildValues(evaluator, assignment);
}

void ProductOperation::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  if (children().size() != 2) {
    throw std::runtime_error("Binary operation must have exactly 2 children");
  }
  auto lhs = child1st;
  auto rhs = child2nd;
  if (!evaluator.isBinary(lhs.get())) {
    std::swap(lhs, rhs);
  }
  if (!evaluator.isBinary(lhs.get())) {
    throw std::runtime_error(
        "At least one of the operands must be a binary variable");
  }

  if (evaluator.isBinary(rhs.get())) {
    // both children are binary variables
    evaluator.computeLpIntent(lhs, minimizing);
    evaluator.computeLpIntent(rhs, minimizing);
  } else {
    // only lhs is binary
    evaluator.computeLpIntent(lhs, false);
    evaluator.computeLpIntent(rhs, false);
  }
}

algopt::lp::Expression ProductOperation::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (2 != children().size()) {
    throw std::runtime_error("Binary operation must have exactly 2 children");
  }
  auto lhs = child1st.get();
  auto rhs = child2nd.get();
  if (!evaluator.isBinary(lhs)) {
    std::swap(lhs, rhs);
  }
  if (!evaluator.isBinary(lhs)) {
    throw std::runtime_error(
        "At least one of the operands must be a binary variable");
  }

  if (evaluator.isBinary(rhs)) {
    // Both children are binary variables
    auto& x = evaluator.lp(lhs, minimizing, configs);
    auto& y = evaluator.lp(rhs, minimizing, configs);
    auto z = lp_bool_var(evaluator);
    LpConstraintBatch batch(evaluator, 4);
    batch.add(REBALANCER_CTR_REQUEST(z <= x));
    batch.add(REBALANCER_CTR_REQUEST(z <= y));
    batch.add(REBALANCER_CTR_REQUEST(z >= 0));
    batch.add(REBALANCER_CTR_REQUEST(z >= x + y - 1));
    batch.flush();
    return z;
  }

  const BoundConstraints bc;
  auto [lb, ub] = evaluator.lowerAndUpperBounds(rhs, bc);
  const double c = fmax(ub - lb, fmax(ub, -lb));
  // We cannot infer the optimization intent directly if one of the children is
  // non-binary; for example, consider step(x)*(x-2), where x =
  // 2*binaryVariable+1. In this case, even when minimizing this expression, we
  // may or may not want to minimize step(x)
  auto& x = evaluator.lp(lhs, /*minimizing=*/false, configs);
  auto& y = evaluator.lp(rhs, /*minimizing=*/false, configs);

  auto z = lp_cont_var(evaluator);
  LpConstraintBatch batch(evaluator, 4);
  batch.add(REBALANCER_CTR_REQUEST(-c * x <= z));
  batch.add(REBALANCER_CTR_REQUEST(y - c * (1 - x) <= z));
  batch.add(REBALANCER_CTR_REQUEST(z <= c * x));
  batch.add(REBALANCER_CTR_REQUEST(z <= y + c * (1 - x)));
  batch.flush();
  return z;
}

std::vector<double> ProductOperation::bound_candidates(
    Context& context,
    const BoundConstraints& bc) const {
  std::vector<std::vector<double>> bounds;
  auto [child1Lb, child1Ub] = child1st->lowerAndUpperBounds(context, bc);
  auto [child2Lb, child2Ub] = child2nd->lowerAndUpperBounds(context, bc);
  bounds.push_back({child1Lb, child1Ub});
  bounds.push_back({child2Lb, child2Ub});
  std::vector<double> candidates;

  for (const auto i : folly::irange(2)) {
    for (const auto j : folly::irange(2)) {
      candidates.push_back(bounds[0][i] * bounds[1][j]);
    }
  }

  return candidates;
}

ExpressionProperties ProductOperation::getProperties() const {
  ExpressionProperties properties;
  properties.properties()->emplace(
      "type", PropertiesHelper::makeStringValue("PRODUCT"));
  return properties;
}

} // namespace facebook::rebalancer
