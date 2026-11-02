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

#include "algopt/rebalancer/solver/expressions/Floor.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"

namespace {
constexpr std::string_view type = "Floor";
}

namespace facebook::rebalancer {

Floor::Floor(std::shared_ptr<Expression> expr) : Transform(std::move(expr)) {
  setInitialValue(perform_transform(getOnlyChildRawPtr()->getInitialValue()));
}

const std::string_view& Floor::getType() const {
  return type;
}

double Floor::perform_transform(double val) const {
  const double closestInteger = round(val);
  if (getPrecision().compare(val, closestInteger) == 0) {
    return closestInteger;
  }
  return floor(val);
}

bool Floor::inner_is_integer(Context& /* not used */) {
  return true;
}

void Floor::lpIntent(const LpEvaluator& evaluator, bool minimizing) {
  evaluator.computeLpIntent(getOnlyChild(), minimizing);
}

algopt::lp::Expression Floor::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& configs) {
  auto floorVar = lp_int_var(evaluator, "floor");
  auto childExpr = getOnlyChildRawPtr();
  auto& childLp = evaluator.lp(childExpr, minimizing, configs);

  // floorVar should be at least child-1+epsilon, where epsilon is 1 if child
  // is integral, and some small value otherwise
  // NOTE: as in Ceil, there is a multiplication by 2 below since Xpress seems
  // to ignore the 'smallestPositiveValue' from the relation below when it is
  // set equal to the integer tolerance
  auto epsilon = childExpr->is_integer(evaluator.getContext())
      ? 1.0
      : 2 * evaluator.getIntegerTolerance();
  REBALANCER_NEWCTR(childLp - (1 - epsilon) <= floorVar);

  if (!minimizing) {
    REBALANCER_NEWCTR(floorVar <= childLp);
  }

  return floorVar;
}

} // namespace facebook::rebalancer
