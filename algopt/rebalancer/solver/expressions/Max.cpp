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

#include "algopt/rebalancer/solver/expressions/Max.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Evaluator.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/iterators/ObjectPotentialsMerge.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/Enumerate.h>

#include <sstream>
#include <string_view>

using namespace std;

namespace {
constexpr std::string_view type = "Max";
}

namespace facebook::rebalancer {
constexpr auto LARGEST_VALUE = 100000000.0;

using entities::ContainerId;
using entities::EquivalenceSetId;
using entities::ObjectId;

Max::Max(
    const std::vector<std::shared_ptr<Expression>>& exprs,
    const entities::Universe& universe)
    : Expression(universe) {
  for (const auto& it : exprs) {
    add_child(it);
  }
  if (children().size() == 0) {
    throw std::runtime_error("Max needs to have at least one child");
  }
  buildSortedAndSetInitialValue();
}

const std::string_view& Max::getType() const {
  return type;
}

bool Max::isMax() const {
  return true;
}

void Max::add(const ExprPtr& expr) {
  if (!children().contains(expr)) {
    add_child(expr);
  }
  sorted_values_.assign(expr.get(), expr->getInitialValue());
  setInitialValue(sorted_values_.begin()->second);
}

void Max::combine(const ExprPtr& expr) {
  if (expr->isMax()) {
    for (const auto& child : expr->children()) {
      combine(child);
    }
  } else {
    add(expr);
  }
}

bool Max::inner_is_integer(Context& context) {
  for (const auto& child : children()) {
    if (child->is_integer(context)) {
      continue;
    }
    return false;
  }
  return true;
}

void Max::updateEquivalenceSets(EquivalenceSets& equivalenceSets) const {
  PackerMap<ObjectId, vector<entities::ContainerId>> mapping;
  for (const auto& child : children()) {
    if (auto var = child->getVar()) {
      auto [obj, cont] = *var;
      mapping[obj].push_back(cont);
    }
  }
  for (auto& it : mapping) {
    sort(it.second.begin(), it.second.end());
  }
  equivalenceSets.mappingMerge(mapping);
}

double Max::evaluate(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  double new_max = -1 * numeric_limits<double>::max();

  // Check everything that changes
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  for (auto child : changedChildren) {
    const double val = evaluator.evaluate(child, changes);

    // Update new_max
    if (val > new_max) {
      new_max = val;
    }
  }

  // If we checked everything, we are done
  if (changedChildren.size() == children().size()) {
    return new_max;
  }

  // Check existing maxes to find largest which hasn't been changed
  for (const auto& [child, val] : sorted_values_) {
    if (!changedChildren.contains(child)) {
      if (val > new_max) {
        new_max = val;
      }
      break;
    }
  }
  return new_max;
}

double Max::innerFullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  sorted_values_.clear();
  for (const auto& child : children()) {
    const double val = evaluator.apply(child.get(), assignment);
    sorted_values_.assign(child.get(), val);
  }
  value = sorted_values_.begin()->second;
  return value;
}

double Max::innerPartialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    [[maybe_unused]] const ChangeSet& changes) {
  auto& changedChildren = evaluator.getChangedChildren((Expression*)this);
  for (auto* child : changedChildren) {
    const double val = evaluator.apply(child, assignment);
    sorted_values_.assign(child, val);
  }
  value = sorted_values_.begin()->second;
  return value;
}

Bounds Max::innerLowerAndUpperBounds(
    Context& context,
    const BoundConstraints& bc) const {
  double lb = -1 * numeric_limits<double>::max();
  double ub = -1 * numeric_limits<double>::max();
  for (const auto& child : children()) {
    auto [new_lb, new_ub] = child->lowerAndUpperBounds(context, bc);
    lb = std::max(lb, new_lb);
    ub = std::max(ub, new_ub);
  }
  return {.lower_bound = lb, .upper_bound = ub};
}

bool Max::is_each_child_binary(const Evaluator& evaluator) {
  for (const auto& child : children()) {
    if (!evaluator.isBinary(child.get())) {
      return false;
    }
  }
  return true;
}

static bool isThereAtleastOneGeneralChild(
    const LpEvaluator& evaluator,
    const PackerSet<ExprPtr>& children) {
  // a child is considered "general" if its UB - LB > 1
  bool atleastOneGeneralChild = false;
  for (auto& child : children) {
    auto [childLb, childUb] = evaluator.lowerAndUpperBounds(child.get());
    if (childUb - childLb > 1) {
      atleastOneGeneralChild = true;
      break;
    }
  }
  return atleastOneGeneralChild;
}

static ExprPtr getSumOfbinaryChildren(
    const PackerSet<ExprPtr>& children,
    const entities::Universe& universe) {
  PackerMap<std::shared_ptr<Expression>, double> childToCoeff;
  for (auto& child : children) {
    childToCoeff[child] = 1;
  }
  return std::make_shared<LinearSum>(LinearSum(universe, 0, childToCoeff));
}

static ExprPtr getEquivExprForNonGeneralChildren(
    const LpEvaluator& evaluator,
    const PackerSet<ExprPtr>& children) {
  std::vector<ExprPtr> newChildren;
  double maxLB = -std::numeric_limits<double>::infinity();
  for (auto& child : children) {
    auto childLB = evaluator.lowerAndUpperBounds(child.get()).lower_bound;
    if (childLB < maxLB) {
      continue;
    }
    if (childLB > maxLB) {
      newChildren.clear();
      maxLB = childLB;
    }
    newChildren.push_back(child - maxLB);
  }
  return max(newChildren, evaluator.getProblem().getUniverse()) + maxLB;
}

algopt::lp::Expression Max::binary_inner_lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  /* when all children is binary
   * max_var <= sum of children
   * children().size * max_var >= sum of children
   */
  auto max_var = lp_bool_var(evaluator, "max");

  if (!binaryChildrenSum_) {
    binaryChildrenSum_ = getSumOfbinaryChildren(
        children(), evaluator.getProblem().getUniverse());
  }
  const auto& children_sum =
      evaluator.lp(binaryChildrenSum_.get(), minimizing, configs);

  REBALANCER_NEWCTR(max_var <= children_sum);

  for (const auto& child : children()) {
    REBALANCER_NEWCTR(
        max_var >= evaluator.lp(child.get(), minimizing, configs));
  }

  return max_var;
}

bool Max::is_each_child_integer(const Evaluator& evaluator) {
  for (const auto& child : children()) {
    if (!evaluator.isInteger(child.get())) {
      return false;
    }
  }
  return true;
}

algopt::lp::Expression Max::n_to_n_plus_one_inner_lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  /* 1. this max could be simplified to only have children with highest lb. for
   * example: max(var, var-1, var+1, -var, var+3, 2) == max(var+3)
   * 2. further more,
   * max(var+3) == max(var) + 3
   * which is max(child-lb) + lb
   * max(child-lb) has is_binary == true
   */
  if (!equivExprForNonGeneralChildren_) {
    equivExprForNonGeneralChildren_ =
        getEquivExprForNonGeneralChildren(evaluator, children());
  }

  return evaluator.lp(
      equivExprForNonGeneralChildren_.get(), minimizing, configs);
}

void Max::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  if (is_each_child_binary(evaluator)) {
    // when all children are binary, 1) max_var <= sum of children, and 2)
    // children().size * max_var >= sum of children
    if (!binaryChildrenSum_) {
      binaryChildrenSum_ = getSumOfbinaryChildren(
          children(), evaluator.getProblem().getUniverse());
    }
    return evaluator.computeLpIntent(binaryChildrenSum_, minimizing);
  }

  if (is_each_child_integer(evaluator)) {
    /* if all children are integral and all children have ub - lb <= 1 */
    atleastOneGeneralChild_ =
        isThereAtleastOneGeneralChild(evaluator, children());
    if (!atleastOneGeneralChild_.value()) {
      if (!equivExprForNonGeneralChildren_) {
        equivExprForNonGeneralChildren_ =
            getEquivExprForNonGeneralChildren(evaluator, children());
      }
      return evaluator.computeLpIntent(
          equivExprForNonGeneralChildren_, minimizing);
    }
  }

  for (const auto& child : children()) {
    if (evaluator.isChildDynamic(this, child.get()) && !child->getVar()) {
      evaluator.computeLpIntent(child, minimizing);
    }
  }
}

algopt::lp::Expression Max::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  if (is_each_child_binary(evaluator)) {
    return binary_inner_lp(evaluator, minimizing, configs);
  }

  if (is_each_child_integer(evaluator)) {
    /* if all children integer and all children has ub - lb <= 1 */
    if (!atleastOneGeneralChild_.has_value()) {
      atleastOneGeneralChild_ =
          isThereAtleastOneGeneralChild(evaluator, children());
    }
    if (!atleastOneGeneralChild_.value()) {
      return n_to_n_plus_one_inner_lp(evaluator, minimizing, configs);
    }
  }

  if (evaluator.supportsNativeMax()) {
    std::vector<algopt::lp::Expression> childExprs;
    childExprs.reserve(children().size());
    for (const auto& child : children()) {
      childExprs.push_back(evaluator.lp(child.get(), minimizing, configs));
    }
    if (auto result = evaluator.addNativeMaxConstraint(childExprs)) {
      return *result;
    }
    // addNativeMaxConstraint returned nullopt; fall through to Big-M below.
    // evaluator.lp() is memoized, so any re-evaluation of children in the
    // Big-M path is a cache hit — no duplicate variables or constraints.
    //
    // Contract for overrides: addNativeMaxConstraint must be all-or-nothing —
    // either return a valid expression having added the native max constraint,
    // or return nullopt having performed no model mutation. Returning nullopt
    // after partially adding auxiliary variables/constraints would leave them
    // orphaned in the model alongside the Big-M formulation added here.
  }

  auto max_var = lp_cont_var(evaluator);
  // This is only filled for max of max case
  vector<algopt::lp::Expression> child_exprs;

  for (const auto& [child, val] : sorted_values_) {
    if (evaluator.isChildDynamic(this, child)) {
      continue;
    }
    REBALANCER_NEWCTR(val <= max_var);
    if (!minimizing) {
      child_exprs.push_back(evaluator.makeLpExpression(val));
    }
    /* break here because 'val' is the max child value for dynamic_children
     */
    break;
  }

  for (const auto& child : children()) {
    if (!evaluator.isChildDynamic(this, child.get())) {
      continue;
    }

    REBALANCER_NEWCTR_WITH_RELATED(
        evaluator.lp(child.get(), minimizing, configs) <= max_var,
        {child->getId()});

    if (!minimizing) {
      child_exprs.push_back(evaluator.lp(child.get(), minimizing, configs));
    }
  }

  // Max of max case. Must ensure that max_var is less than or equal
  // to one of the child expressions
  if (!minimizing) {
    auto z_sum = evaluator.makeLpExpression();

    // largest upper_bound - smallest lower_bound among children
    double maxUB = -std::numeric_limits<double>::infinity();
    double minLB = std::numeric_limits<double>::infinity();
    for (const auto& child : children()) {
      auto [childLb, childUb] = evaluator.lowerAndUpperBounds(child.get());
      maxUB = std::max(maxUB, childUb);
      minLB = std::min(minLB, childLb);
    }
    const double bigM = (maxUB - minLB);
    if (bigM >= LARGEST_VALUE) {
      throw std::runtime_error("Huge bigM calculated, model won't be correct.");
    }
    LpConstraintBatch batch(evaluator, child_exprs.size() + 1);
    for (auto& it : child_exprs) {
      auto z = lp_bool_var(evaluator);
      z_sum += z;
      batch.add(REBALANCER_CTR_REQUEST(it + bigM * (1 - z) >= max_var));
    }
    batch.add(REBALANCER_CTR_REQUEST(z_sum == 1));
    batch.flush();
  }
  return max_var;
}

AbstractContainer<ObjectPotential> Max::getObjectPotentials(
    bool descending) const {
  std::vector<AbstractContainer<ObjectPotential>> potentials;
  for (const auto& child : children()) {
    if (child->value == value) {
      potentials.push_back(child->getObjectPotentials(descending));
    }
  }
  return makeObjectPotentialsMergeContainer(potentials, descending);
}

void Max::buildSortedAndSetInitialValue() {
  sorted_values_.clear();
  for (const auto& child : children()) {
    sorted_values_.assign(child.get(), child->getInitialValue());
  }
  setInitialValue(sorted_values_.begin()->second);
}

std::string Max::innerDigest(size_t maxChildren) const {
  std::stringstream ss;
  ss << "= MAX(";
  for (const auto& [idx, child] : folly::enumerate(children())) {
    if (idx >= maxChildren) {
      ss << "... " << children().size() - maxChildren << " more";
      break;
    }
    if (idx > 0) {
      ss << ", ";
    }
    ss << fmt::format("{}({})", child->getType(), child->value);
  }
  ss << ")";
  return ss.str();
}

} // namespace facebook::rebalancer
