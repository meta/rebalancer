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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress_v2/xpress_v2.h"
#include "algopt/lp/generic/Constraint.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Relation.h"
#include "algopt/lp/generic/Variable.h"

#include <memory>
#include <optional>
#include <vector>

namespace facebook::algopt::lp::detail {

class XpressProblemV2;
class XpressV2NameBuffer;

// Unlike xpress::objects::Variable, which knows the problem that created it, a
// typed Expression is a free-standing algebraic object. Evaluating one requires
// the solved column vector, so the
// problem publishes its solution here after each solve and hands the same slot
// to every variable and expression it creates. An empty slot means "not solved
// yet".
using XpressV2SolutionSlot = std::shared_ptr<std::vector<double>>;

// Impl wrappers over the Xpress 9.9.0 typed C++ API (xpress_objects.hpp). The
// types here hold xpress::objects values, which are shared_ptr-backed handles,
// so they copy cheaply and share the underlying model object.

class XpressV2Variable : public VariableImpl {
 public:
  XpressV2Variable(
      const xpress::objects::Variable& variable,
      Type type,
      XpressV2SolutionSlot solution,
      XpressProblemV2* problem,
      std::shared_ptr<XpressV2NameBuffer> names = {});

  void setLB(double lb) override;
  void setUB(double ub) override;
  void setThreshold(double threshold) override;
  double getValue() const override;
  double getReducedCost() const override;
  std::shared_ptr<ExpressionImpl> makeExpression(double coeff) const override;

  const xpress::objects::Variable& get() const;

 private:
  friend class XpressProblemV2;
  const xpress::objects::Variable& getNative() const;

  xpress::objects::Variable variable_;
  XpressV2SolutionSlot solution_;
  XpressProblemV2* problem_;
  std::shared_ptr<XpressV2NameBuffer> names_;
};

// Accumulates linear terms in plain vectors and materialises an Xpress
// expression only when one is needed.
//
// The generic layer builds expressions a term at a time (`sum = sum + var`), so
// `add` runs once per term. Two earlier shapes were measured and rejected:
// rebuilding the Xpress expression per call was ~10x slower overall, and
// accumulating incrementally into a LinExpression made row construction
// quadratic in terms-per-row -- 200k terms spread over 200 wide rows took 122s
// in addConstraint versus 0.8s over 2000 narrow ones. P2450701860 has the
// measurements in full.
//
// Holding the terms locally keeps `add` O(1) amortised and lets the row be
// handed to Xpress in a single bulk addTerms call.
//
// Multiplying by another expression yields a quadratic, which these vectors
// cannot represent; the value then moves into the general `quadratic_` slot and
// the fast path is abandoned for that expression.
class XpressV2Expression : public ExpressionImpl {
 public:
  XpressV2Expression(
      const xpress::objects::Expression& expression,
      XpressV2SolutionSlot solution,
      std::shared_ptr<XpressV2NameBuffer> names = {});
  // Single term: coefficient * variable.
  XpressV2Expression(
      const xpress::objects::Variable& variable,
      double coefficient,
      XpressV2SolutionSlot solution,
      std::shared_ptr<XpressV2NameBuffer> names = {});
  explicit XpressV2Expression(
      double constant,
      XpressV2SolutionSlot solution,
      std::shared_ptr<XpressV2NameBuffer> names = {});

  std::shared_ptr<ExpressionImpl> clone() const override;
  void add(double constant) override;
  void add(std::shared_ptr<const ExpressionImpl> expression) override;
  void multiply(double constant) override;
  void multiply(std::shared_ptr<const ExpressionImpl> expression) override;
  std::shared_ptr<RelationImpl> makeEqualZeroRelation() const override;
  std::shared_ptr<RelationImpl> makeLessEqualZeroRelation() const override;
  std::shared_ptr<RelationImpl> makeGreaterEqualZeroRelation() const override;
  double getValue() const override;
  double computeValue() const override;
  double getConstant() const override;
  void print() const override;

  // Materialises the value; cheap for the linear case.
  xpress::objects::Expression get() const;

 private:
  friend class XpressProblemV2;
  xpress::objects::Expression getNative() const;

  bool isLinear() const;

  // True when the value is just `constant_`: no terms, and not quadratic. Such
  // an expression has a value even before the problem is solved.
  bool isPureConstant() const;

  // Drops the linear terms once they have been folded into `quadratic_`, which
  // from then on is the sole representation.
  void clearLinearTerms();

  // Builds the Xpress linear expression from the accumulated terms in one call.
  xpress::objects::LinExpression materialise() const;

  std::vector<xpress::objects::Variable> variables_;
  std::vector<double> coefficients_;
  double constant_{0.0};
  std::optional<xpress::objects::Expression> quadratic_;
  XpressV2SolutionSlot solution_;
  std::shared_ptr<XpressV2NameBuffer> names_;
};

// RelationImpl is a marker base, so the relation carries the comparison itself:
// the stored expression is always compared against zero in the given sense.
class XpressV2Relation : public RelationImpl {
 public:
  enum class Sense { EqualZero, LessEqualZero, GreaterEqualZero };

  // Keeps the concrete LinExpression when the relation is linear. Widening to
  // the base Expression first costs ~177x in addConstraint, because Xpress then
  // has to treat the row as a general expression instead of taking its linear
  // fast path.
  XpressV2Relation(xpress::objects::LinExpression expression, Sense sense);
  XpressV2Relation(xpress::objects::Expression expression, Sense sense);

  Sense sense() const;

  // Builds the typed inequality definition this relation denotes, ready to hand
  // to XpressProblem::addConstraint.
  xpress::objects::InequalityDefinition makeDefinition() const;

 private:
  friend class XpressProblemV2;

  std::optional<xpress::objects::LinExpression> linear_;
  std::optional<xpress::objects::Expression> general_;
  Sense sense_;
};

class XpressV2Constraint : public ConstraintImpl {
 public:
  XpressV2Constraint(
      const xpress::objects::Inequality& inequality,
      const XpressProblemV2* problem,
      std::shared_ptr<XpressV2NameBuffer> names = {});

  double getDual() const override;
  double getSlack() const override;
  double getRHS() const override;
  const xpress::objects::Inequality& get() const;

 private:
  friend class XpressProblemV2;
  const xpress::objects::Inequality& getNative() const;

  xpress::objects::Inequality inequality_;
  const XpressProblemV2* problem_;
  std::shared_ptr<XpressV2NameBuffer> names_;
};

} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
