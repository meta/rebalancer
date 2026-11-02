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
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class CeilTest : public ExpressionTestsBase {
 protected:
  void SetUp() override {
    constexpr int kNumContainers = 20;
    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    for (const auto i : folly::irange(kNumContainers)) {
      initialAssignment[fmt::format("container{}", i)] = {
          fmt::format("object{}", i)};
    }
    setInitialAssignment(initialAssignment);
  }
};

TEST_F(CeilTest, Constants) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  {
    auto expr = ceil(const_expr(-1.01, universe));
    EXPECT_EQ(-1.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(-0.99, universe));
    EXPECT_EQ(0.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(-1e-12, universe));
    EXPECT_EQ(0.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(0, universe));
    EXPECT_EQ(0.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(1e-12, universe));
    EXPECT_EQ(0.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(1 - 1e-12, universe));
    EXPECT_EQ(1.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(1, universe));
    EXPECT_EQ(1.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(1 + 1e-12, universe));
    EXPECT_EQ(1.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(1.01, universe));
    EXPECT_EQ(2.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(1.99, universe));
    EXPECT_EQ(2.0, apply(expr, assignment));
  }
  {
    auto expr = ceil(const_expr(2.5, universe));
    EXPECT_EQ(3.0, apply(expr, assignment));
  }
}

TEST_F(CeilTest, IsInteger) {
  buildUniverse();
  Context context;
  EXPECT_TRUE(ceil(const_expr(1.5, getUniverse()))->is_integer(context));
}

TEST_F(CeilTest, NonConstant) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto expr = 2 * variable(object(1), container(0), universe, assignment) - 1.5;
  auto ceilExpr = ceil(expr);

  // expr init = 2*0 - 1.5 = -1.5, ceil(-1.5) = -1.
  EXPECT_DOUBLE_EQ(-1, ceilExpr->getInitialValue());
  EXPECT_EQ(-1, apply(ceilExpr, assignment));

  EXPECT_EQ(1, evaluate(ceilExpr, {{object(1), container(0)}}, assignment));

  EXPECT_EQ(1, applyChanges(ceilExpr, {{object(1), container(0)}}, assignment));
}

} // namespace facebook::rebalancer::packer::tests
