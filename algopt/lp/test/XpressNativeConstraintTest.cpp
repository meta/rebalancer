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

#include "algopt/lp/detail/xpress/XpressProblem.h"
#include "algopt/lp/lp.h"

#ifndef REBALANCER_OSS_BUILD
#include "algopt/lp/environment/fb/SetupEnvironments.h"
#endif

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#ifdef REBALANCER_USE_XPRESS
namespace facebook::algopt::lp {
namespace {
class XpressNativeConstraintTest : public ::testing::Test {
 protected:
  void SetUp() override {
#ifndef REBALANCER_OSS_BUILD
    lazyLoadXpress();
#endif
  }

  static Problem makeProblem() {
    return Problem{std::make_unique<detail::XpressProblem>()};
  }
};

TEST_F(XpressNativeConstraintTest, RetainsUnusedResultColumn) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.newConstraint(x == 1.0, "x_fixed");
  problem.newConstraint(y == 2.0, "y_fixed");
  const auto maximum =
      problem.addNativeMaxConstraint({Expression{x}, Expression{y}});
  ASSERT_TRUE(maximum.has_value());

  problem.setObjective(x);
  problem.solve(false);

  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_DOUBLE_EQ(x.getValue(), 1.0);
  EXPECT_DOUBLE_EQ(maximum->getValue(), 2.0);
  const auto results = problem.getResultPerObjective();
  EXPECT_EQ(*results.front().problemAttributes()->numConstraints(), 4);
}

TEST_F(XpressNativeConstraintTest, RetainsResultBeforeItsObjective) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.newConstraint(x == 1.0, "x_fixed");
  problem.newConstraint(y >= 2.0, "y_lower");
  problem.newConstraint(y <= 9.0, "y_upper");
  const auto maximum =
      problem.addNativeMaxConstraint({Expression{x}, Expression{y}});
  ASSERT_TRUE(maximum.has_value());

  problem.setObjective(std::vector<Expression>{Expression{x}, *maximum});
  problem.solve(false);

  ASSERT_EQ(problem.getResultPerObjective().size(), 2);
  EXPECT_DOUBLE_EQ(x.getValue(), 1.0);
  EXPECT_DOUBLE_EQ(y.getValue(), 2.0);
  EXPECT_DOUBLE_EQ(maximum->getValue(), 2.0);
}
TEST_F(XpressNativeConstraintTest, KeepsMultipleUnusedResultsNonbinding) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  auto z = problem.makeVar("z");
  problem.newConstraint(x == -4.0, "x_fixed");
  problem.newConstraint(y == -2.0, "y_fixed");
  problem.newConstraint(z == 5.0, "z_fixed");
  const auto lower =
      problem.addNativeMaxConstraint({Expression{x}, Expression{y}});
  const auto upper =
      problem.addNativeMaxConstraint({Expression{y}, Expression{z}});
  ASSERT_TRUE(lower.has_value());
  ASSERT_TRUE(upper.has_value());

  problem.setObjective(x);
  problem.solve(false);

  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_DOUBLE_EQ(lower->getValue(), -2.0);
  EXPECT_DOUBLE_EQ(upper->getValue(), 5.0);
}

TEST_F(XpressNativeConstraintTest, RetainsResultAfterObjectiveReplacement) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.newConstraint(x == 1.0, "x_fixed");
  problem.newConstraint(y == 2.0, "y_fixed");
  problem.addStartValue(x, 1.0);
  problem.addStartValue(y, 2.0);
  const auto maximum =
      problem.addNativeMaxConstraint({Expression{x}, Expression{y}});
  ASSERT_TRUE(maximum.has_value());
  problem.setObjective(*maximum);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);

  problem.setObjective(x);
  problem.solve(false);

  ASSERT_EQ(problem.getResultPerObjective().size(), 1);
  EXPECT_DOUBLE_EQ(x.getValue(), 1.0);
  EXPECT_DOUBLE_EQ(maximum->getValue(), 2.0);
}
TEST_F(XpressNativeConstraintTest, RetainsPwlResultBeforeItsObjective) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x == 1.0, "x_fixed");
  const auto piecewise = problem.addNativePwlConstraint(
      Expression{x}, {{0.0, 0.0}, {1.0, 1.0}, {2.0, 4.0}});
  ASSERT_TRUE(piecewise.has_value());

  problem.setObjective(std::vector<Expression>{Expression{x}, *piecewise});
  problem.solve(false);

  ASSERT_EQ(problem.getResultPerObjective().size(), 2);
  EXPECT_DOUBLE_EQ(x.getValue(), 1.0);
  EXPECT_DOUBLE_EQ(piecewise->getValue(), 1.0);
}

TEST_F(XpressNativeConstraintTest, AcceptsConstantPwlInput) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x == 2.0, "x_fixed");
  const auto piecewise = problem.addNativePwlConstraint(
      Expression{1.0}, {{0.0, 0.0}, {1.0, 1.0}, {2.0, 4.0}});
  ASSERT_TRUE(piecewise.has_value());

  problem.setObjective(x);
  problem.solve(false);

  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_DOUBLE_EQ(piecewise->getValue(), 1.0);
}

TEST_F(XpressNativeConstraintTest, ReappliesMixedPwlAndMaxConstraints) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x == 1.0, "x_fixed");
  const auto piecewise = problem.addNativePwlConstraint(
      Expression{x}, {{0.0, 0.0}, {1.0, 1.0}, {2.0, 4.0}});
  ASSERT_TRUE(piecewise.has_value());
  const auto maximum =
      problem.addNativeMaxConstraint({*piecewise, Expression{3.0}});
  ASSERT_TRUE(maximum.has_value());

  problem.setObjective(std::vector<Expression>{*piecewise, *maximum});
  problem.solve(false);

  ASSERT_EQ(problem.getResultPerObjective().size(), 2);
  EXPECT_DOUBLE_EQ(piecewise->getValue(), 1.0);
  EXPECT_DOUBLE_EQ(maximum->getValue(), 3.0);
}
} // namespace
} // namespace facebook::algopt::lp
#endif
