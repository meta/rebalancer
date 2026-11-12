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

#include "algopt/lp/detail/xpress_v2/XpressProblemV2.h"

#ifdef REBALANCER_USE_XPRESS

#include <stdexcept>
#include <typeinfo>
#include <vector>

namespace facebook::algopt::lp::detail {
namespace {

// Custom SDK expressions may create auxiliary rows or have side effects during
// extraction. Only exact built-in linear implementations have a pure view and
// one-row definition. SDK mixed linear/quadratic bulk commits are not safe.
bool hasPureLinearView(const xpress::objects::Expression& expression) {
  using namespace xpress::objects;
  const auto* implementation = expression.impl();
  if (implementation == nullptr) {
    return false;
  }
  const auto& type = typeid(*implementation);
  return type == typeid(ConstantExpressionImplementation) ||
      type == typeid(VariableImplementation) ||
      type == typeid(LinTermImplementation) ||
      type == typeid(LinTermMapImplementation);
}

void validateVariable(
    const xpress::objects::Variable& variable,
    const xpress::objects::XpressProblem& problem) {
  if (!xpress::objects::LinQuadUtils::isConstant(variable)) {
    variable.getIndexForProb(&problem);
  }
}

void validateOwnership(
    const xpress::objects::Expression& expression,
    const xpress::objects::XpressProblem& problem) {
  const auto view = expression.getLinearView();
  for (const auto& term : *view) {
    validateVariable(term.getVariable(), problem);
  }
}

} // namespace

std::vector<std::shared_ptr<ConstraintImpl>> XpressProblemV2::newConstraints(
    const ConstraintRequests& requests) {
  if (requests.empty()) {
    return {};
  }
  bool useBulk = true;
  for (const auto& [relation, name] : requests) {
    const auto* v2Relation =
        dynamic_cast<const XpressV2Relation*>(relation.get());
    if (v2Relation == nullptr) {
      throw std::invalid_argument(
          "XpressProblemV2 expected an XpressV2Relation; a foreign RelationImpl "
          "cannot be used with this backend");
    }
    const xpress::objects::Expression& expression = v2Relation->linear_
        ? static_cast<const xpress::objects::Expression&>(*v2Relation->linear_)
        : *v2Relation->general_;
    useBulk = hasPureLinearView(expression) && useBulk;
  }
  // Deliberate same-backend scalar implementation, chosen before extraction or
  // insertion. It preserves arbitrary SDK definitions' auxiliary registration
  // and returns their scalar handle rather than zipping all native rows.
  if (!useBulk) {
    return ProblemImpl::newConstraints(requests);
  }
  for (const auto& [relation, name] : requests) {
    const auto& v2Relation = static_cast<const XpressV2Relation&>(*relation);
    validateOwnership(
        v2Relation.linear_ ? static_cast<const xpress::objects::Expression&>(
                                 *v2Relation.linear_)
                           : *v2Relation.general_,
        problem_);
  }

  std::vector<
      xpress::objects::ConstraintDefinition<xpress::objects::Inequality>>
      definitions;
  definitions.reserve(requests.size());
  for (const auto& [relation, name] : requests) {
    definitions.push_back(
        static_cast<const XpressV2Relation&>(*relation).makeDefinition());
  }
  std::vector<std::shared_ptr<ConstraintImpl>> constraints;
  constraints.reserve(requests.size());
  markModified();
  const auto inequalities = problem_.addConstraints(definitions);
  size_t index = 0;
  for (auto inequality : inequalities) {
    if (index >= requests.size()) {
      throw std::runtime_error("Xpress bulk returned unexpected extra rows");
    }
    applyConstraintName(inequality, requests[index].second);
    constraints.push_back(
        std::make_shared<XpressV2Constraint>(
            std::move(inequality), this, names_));
    ++index;
  }
  if (index != requests.size()) {
    throw std::runtime_error("Xpress bulk did not return every constraint");
  }
  return constraints;
}

} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
