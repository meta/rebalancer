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

#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <algorithm>

namespace facebook::rebalancer {
LpEvaluator::LpEvaluator(
    LpContext& context,
    Problem& problem,
    const folly::F14FastMap<const Expression*, PriorityInfo>& nodesPriority)
    : context_(context), problem_(problem), nodesPriority_(nodesPriority) {}

const algopt::lp::Expression& LpEvaluator::lp(
    Expression* expr,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) const {
  // If 'minimizing' is true, then the intent is to minimize the expression; if
  // 'minimizing' is false, then the intent is NEITHER / NOT SURE (and not
  // maximize)
  auto& raw_context = minimizing ? context_.lpMin() : context_.lpNotMin();
  return raw_context.getSavedOrCompute(expr->getId(), [&]() {
    auto val = expr->lp(*this, minimizing, configs);
    if (*configs.lpExprSubstitution()) {
      auto var = expr->lp_cont_var(*this);
      expr->newCtr(*this, "substitution", val == var);
      val = var;
    }
    return val;
  });
}

void LpEvaluator::computeLpIntent(ExprPtr expr, bool shouldMinimize) const {
  // record lpIntents recursively; note that the nodes are inserted into
  // optimizationIntents_ in topological order (of the directed graph which
  // includes all the nodes for which we compute lpIntent)
  auto exprIntentPair = std::make_pair(expr, shouldMinimize);
  if (!optimizationIntents_.contains(exprIntentPair)) {
    expr->lpIntent(*this, shouldMinimize);
    optimizationIntents_.emplace(exprIntentPair);
  }
}

bool LpEvaluator::isChildDynamic(Expression* parent, Expression* child) const {
  //  if 'parent' is not part of the expression tree, then its child is
  //  considered as 'dynamic'
  if (!nodesPriority_.contains(parent)) {
    return true;
  }

  auto dynamicChildrenPtr = folly::get_ptr(context_.dynamicChildren(), parent);
  return (dynamicChildrenPtr && dynamicChildrenPtr->contains(child));
}

const PackerSet<entities::ContainerId>& LpEvaluator::getDynamicContainers()
    const {
  return context_.dynamicContainers();
}

algopt::lp::Expression LpEvaluator::getAssignmentVar(
    entities::EquivalenceSetId equiv_set,
    entities::ContainerId container) const {
  return problem_.lp_assignment_var(context_, equiv_set, container);
}

std::optional<int> LpEvaluator::getMaybeFixedAssignmentValue(
    entities::EquivalenceSetId equiv_set,
    entities::ContainerId container) const {
  return problem_.get_maybe_fixed_assignment_value(
      context_, equiv_set, container);
}

const PackerMap<entities::EquivalenceSetId, double>&
LpEvaluator::getEquivSetMap(ObjectVector* objVec) const {
  return context_.getEquivSetMap(objVec, problem_.getEquivalenceSets());
}

algopt::lp::Expression LpEvaluator::makeLpExpression(double value) const {
  return problem_.lp_store.expression(value);
}

algopt::lp::Constraint LpEvaluator::addLpConstraint(
    const algopt::lp::Relation& expr,
    const std::string& name) const {
  return problem_.lp_store.addConstraint(expr, name);
}

std::vector<algopt::lp::Constraint> LpEvaluator::addLpConstraints(
    const algopt::lp::Problem::ConstraintRequests& requests) const {
  return problem_.lp_store.addConstraints(requests);
}

algopt::lp::Variable LpEvaluator::makeLpVar(
    LpVarType lpVarType,
    const std::string& name) const {
  return problem_.lp_store.makeLpVar(lpVarType, name);
}

LpContext& LpEvaluator::getContext() const {
  return context_;
}

const Problem& LpEvaluator::getProblem() const {
  return problem_;
}

double LpEvaluator::getIntegerTolerance() const {
  return problem_.lp_store.getTolerances().integer;
}

const algopt::InsertOrderedFastSet<std::pair<ExprPtr, bool>>&
LpEvaluator::getOptimizationIntentsInTopologicalOrder() const {
  // Since optimizationIntents_ maintains the order of insertion, it is stored
  // in topological order (see computeLpIntent())
  return optimizationIntents_;
}

bool LpEvaluator::supportsNativeQuadratic() const {
  return problem_.lp_store.getLpProblem().supportsNativeQuadratic();
}

bool LpEvaluator::supportsNativePwl() const {
  return problem_.lp_store.getLpProblem().supportsNativePwl();
}

bool LpEvaluator::supportsNativeMax() const {
  return problem_.lp_store.getLpProblem().supportsNativeMax();
}

bool LpEvaluator::supportsIndicatorConstraints() const {
  return problem_.lp_store.getLpProblem().supportsIndicatorConstraints();
}

bool LpEvaluator::setIndicatorOnConstraint(
    algopt::lp::Constraint& ctr,
    const algopt::lp::Variable& binaryVar,
    int dir) const {
  return problem_.lp_store.getLpProblem().setIndicatorOnConstraint(
      ctr, binaryVar, dir);
}

std::optional<algopt::lp::Expression> LpEvaluator::addNativePwlConstraint(
    const algopt::lp::Expression& x,
    const std::vector<std::pair<double, double>>& points) const {
  return problem_.lp_store.getLpProblem().addNativePwlConstraint(x, points);
}

std::optional<algopt::lp::Expression> LpEvaluator::addNativeMaxConstraint(
    const std::vector<algopt::lp::Expression>& inputs) const {
  return problem_.lp_store.getLpProblem().addNativeMaxConstraint(inputs);
}

LpConstraintBatch::LpConstraintBatch(
    const LpEvaluator& evaluator,
    size_t reserve)
    : evaluator_(evaluator),
      enabled_(
          !evaluator.getProblem().configs.enableParallelizedLpBuilding &&
          evaluator.getProblem()
              .lp_store.getLpProblem()
              .supportsConstraintBatching()) {
  if (enabled_) {
    requests_.reserve(std::min(reserve, kBatchSize));
  }
}

void LpConstraintBatch::add(
    std::pair<algopt::lp::Relation, std::string> request) {
  if (!enabled_) {
    evaluator_.addLpConstraint(request.first, request.second);
    return;
  }
  requests_.push_back(std::move(request));
  if (requests_.size() == kBatchSize) {
    flush();
  }
}

void LpConstraintBatch::flush() {
  if (!requests_.empty()) {
    evaluator_.addLpConstraints(requests_);
    requests_.clear();
  }
}

} // namespace facebook::rebalancer
