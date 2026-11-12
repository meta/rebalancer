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

#pragma once

#include "algopt/lp/generic/Constraint.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/rebalancer/algopt_common/InsertOrderedFastSet.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/expressions/Evaluator.h"

namespace facebook::rebalancer {
// LpEvaluator is an evaluator that is used exclusively for the purpose of
// building LP expressions. It is a hybrid of BottomToTopEvaluator and
// TopToBottomEvaluator, where BottomToTopEvaluation is used to define node
// priorties and compute dynamicChildren, and TopToBottomEvaluation is used to
// recursively build the Lp expressions.
class LpEvaluator : public Evaluator {
 public:
  explicit LpEvaluator(
      LpContext& context,
      Problem& problem,
      const folly::F14FastMap<const Expression*, PriorityInfo>& nodesPriority);

  const algopt::lp::Expression& lp(
      Expression* expr,
      bool minimizing,
      const interface::OptimalSolverSpec& configs) const;

  void computeLpIntent(ExprPtr expr, bool shouldMinimize) const;

  bool isChildDynamic(Expression* parent, Expression* child) const;

  const PackerSet<entities::ContainerId>& getDynamicContainers() const;

  algopt::lp::Expression makeLpExpression(double value = 0) const;

  algopt::lp::Constraint addLpConstraint(
      const algopt::lp::Relation& expr,
      const std::string& name = "constraint") const;
  std::vector<algopt::lp::Constraint> addLpConstraints(
      const algopt::lp::Problem::ConstraintRequests& requests) const;

  algopt::lp::Variable makeLpVar(LpVarType lpVarType, const std::string& name)
      const;

  algopt::lp::Expression getAssignmentVar(
      entities::EquivalenceSetId equiv_set,
      entities::ContainerId container) const;

  std::optional<int> getMaybeFixedAssignmentValue(
      entities::EquivalenceSetId equiv_set,
      entities::ContainerId container) const;

  const PackerMap<entities::EquivalenceSetId, double>& getEquivSetMap(
      ObjectVector* objVec) const;

  virtual LpContext& getContext() const override;

  const Problem& getProblem() const;

  double getIntegerTolerance() const;

  const algopt::InsertOrderedFastSet<std::pair<ExprPtr, bool>>&
  getOptimizationIntentsInTopologicalOrder() const;

  bool supportsNativeQuadratic() const;
  bool supportsNativePwl() const;
  bool supportsNativeMax() const;
  bool supportsIndicatorConstraints() const;
  bool setIndicatorOnConstraint(
      algopt::lp::Constraint& ctr,
      const algopt::lp::Variable& binaryVar,
      int dir) const;
  std::optional<algopt::lp::Expression> addNativePwlConstraint(
      const algopt::lp::Expression& x,
      const std::vector<std::pair<double, double>>& points) const;
  std::optional<algopt::lp::Expression> addNativeMaxConstraint(
      const std::vector<algopt::lp::Expression>& inputs) const;

 private:
  LpContext& context_;
  Problem& problem_;
  const folly::F14FastMap<const Expression*, PriorityInfo>& nodesPriority_;

  // optimizationIntents_ stores the pair (expr, intent) and
  // maintains the order of insertion, which in turn is used to infer the
  // order in which the lp() functions need to be called when building
  // lp::Expressions bottom-up
  mutable algopt::InsertOrderedFastSet<std::pair<ExprPtr, bool>>
      optimizationIntents_;

 private:
  virtual double evaluate(
      const Expression* /*unused*/,
      const ChangeSet& /*unused*/) const override {
    throw std::runtime_error(
        "evaluate cannot be used with object of type LpEvaluator");
  }

  virtual double apply(Expression* /*unused*/, const Assignment& /*unused*/)
      const override {
    throw std::runtime_error(
        "apply cannot be used with object of type LpEvaluator");
  }

  virtual const folly::F14FastSet<Expression*>& getChangedChildren(
      const Expression* const) const override {
    throw std::runtime_error(
        "getChangedChildren cannot be used with object of type LpEvaluator");
  }

  virtual bool isPositive(
      const Expression* /*unused*/,
      const ChangeSet& /*unused*/) const override {
    throw std::runtime_error(
        "isPositive cannot be used with object of type LpEvaluator");
  }
};

// Operator-local completed requests. Flush explicitly at the end and before
// nested LP/native operations; destruction never mutates the model. Parallel
// LP builders retain eager scalar emission at each add call.
class LpConstraintBatch {
 public:
  explicit LpConstraintBatch(
      const LpEvaluator& evaluator,
      size_t reserve = 128);
  LpConstraintBatch(const LpConstraintBatch&) = delete;
  LpConstraintBatch& operator=(const LpConstraintBatch&) = delete;
  LpConstraintBatch(LpConstraintBatch&&) = delete;
  LpConstraintBatch& operator=(LpConstraintBatch&&) = delete;
  ~LpConstraintBatch() = default;

  void add(std::pair<algopt::lp::Relation, std::string> request);
  void flush();

 private:
  static constexpr size_t kBatchSize = 128;
  const LpEvaluator& evaluator_;
  const bool enabled_;
  algopt::lp::Problem::ConstraintRequests requests_;
};

} // namespace facebook::rebalancer
