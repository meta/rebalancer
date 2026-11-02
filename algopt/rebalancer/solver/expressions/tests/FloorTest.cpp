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

class FloorTest : public ExpressionTestsBase {
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

TEST_F(FloorTest, Constants) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const std::vector<std::pair<double, double>> valueToFloor = {
      {-1.01, -2.0},
      {-0.99, -1.0},
      {-1e-12, 0.0},
      {0, 0.0},
      {1e-12, 0.0},
      {1 - 1e-12, 1.0},
      {1, 1.0},
      {1.01, 1.0},
      {1.99, 1.0},
      {2.5, 2.0},
  };
  for (const auto& [value, expected] : valueToFloor) {
    EXPECT_EQ(expected, apply(floor(const_expr(value, universe)), assignment))
        << value;
  }
}

TEST_F(FloorTest, IsInteger) {
  buildUniverse();
  Context context;
  EXPECT_TRUE(floor(const_expr(1.5, getUniverse()))->is_integer(context));
}

TEST_F(FloorTest, NonConstant) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  auto expr = 2 * variable(object(1), container(0), universe, assignment) - 1.5;
  auto floorExpr = floor(expr);

  // expr init = 2*0 - 1.5 = -1.5, floor(-1.5) = -2.
  EXPECT_DOUBLE_EQ(-2, floorExpr->getInitialValue());
  EXPECT_DOUBLE_EQ(-2, apply(floorExpr, assignment));
  EXPECT_DOUBLE_EQ(
      0, evaluate(floorExpr, {{object(1), container(0)}}, assignment));
  EXPECT_DOUBLE_EQ(
      0, applyChanges(floorExpr, {{object(1), container(0)}}, assignment));
}

} // namespace facebook::rebalancer::packer::tests
