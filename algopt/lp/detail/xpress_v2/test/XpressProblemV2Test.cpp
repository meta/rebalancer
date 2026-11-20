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

#ifndef REBALANCER_OSS_BUILD
#include "algopt/lp/environment/fb/SetupEnvironments.h"
#endif

#include <gtest/gtest.h>

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress_v2/XpressProblemV2.h"
#include "algopt/lp/detail/xpress_v2/XpressV2Types.h"
#include "algopt/lp/lp.h"

#include <folly/container/irange.h>
#include <folly/testing/TestUtil.h>

#include <filesystem>
#include <vector>

namespace facebook::algopt::lp::detail {
namespace {

class XpressProblemV2Test : public ::testing::Test {
 protected:
  void SetUp() override {
    lazyLoadXpress();
  }

  // The generic Problem wrapper owns the impl, which is how every other
  // backend is driven, so exercise V2 through the same public surface rather
  // than poking at the impl directly.
  static Problem makeProblem() {
    return Problem{std::make_unique<XpressProblemV2>()};
  }
};

// Minimise x subject to x >= 4. Exercises variable creation, a linear
// constraint, an objective, the solve, and reading a value back off a variable.
TEST_F(XpressProblemV2Test, SolvesLinearProgram) {
  auto problem = makeProblem();
  EXPECT_EQ(problem.backendName(), "XPRESS");
  auto x = problem.makeVar("x");

  problem.newConstraint(x >= 4.0, "lower_bound");
  problem.setObjective(x);
  problem.solve(false);

  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(x.getValue(), 4.0, 1e-6);
}

// Two binaries that cannot both be zero; minimising their sum must pick exactly
// one. Covers boolean variables and integrality.
TEST_F(XpressProblemV2Test, SolvesMixedIntegerProgram) {
  auto problem = makeProblem();
  auto a = problem.makeBoolVar("a");
  auto b = problem.makeBoolVar("b");

  problem.newConstraint(a + b >= 1.0, "cover");
  problem.setObjective(a + b);
  problem.solve(false);

  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(a.getValue() + b.getValue(), 1.0, 1e-6);
}

// An expression is not tied to a problem in the typed API, so evaluating one
// depends on the solution snapshot the problem publishes after solving.
TEST_F(XpressProblemV2Test, EvaluatesExpressionAfterSolve) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");

  problem.newConstraint(x >= 2.0, "x_lb");
  problem.newConstraint(y >= 3.0, "y_lb");
  problem.setObjective(x + y);
  problem.solve(false);

  auto total = x + y;
  EXPECT_NEAR(total.getValue(), 5.0, 1e-6);
}

// `e *= e` hands multiply() the very impl it is mutating -- the one path where
// the operand does not survive the call. The objective stays linear so this
// does not depend on the backend's quadratic support.
TEST_F(XpressProblemV2Test, EvaluatesSelfMultipliedExpression) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");

  problem.newConstraint(x >= 2.0, "x_lb");
  problem.setObjective(x);
  problem.solve(false);

  auto square = x + 1.0;
  square *= square;
  EXPECT_NEAR(square.getValue(), 9.0, 1e-6);
}

// Infeasible by construction: x cannot be both >= 5 and <= 1.
TEST_F(XpressProblemV2Test, ReportsInfeasible) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");

  problem.newConstraint(x >= 5.0, "lb");
  problem.newConstraint(x <= 1.0, "ub");
  problem.setObjective(x);
  problem.solve(false);

  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
}

// A constant operand carries a null ExpressionImpl, so max(x, 7.0) must not
// dereference it.
TEST_F(XpressProblemV2Test, NativeMaxAcceptsConstantInput) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x == 3.0, "x_fixed");

  auto result =
      problem.addNativeMaxConstraint({Expression{x}, Expression{7.0}});
  ASSERT_TRUE(result.has_value());

  problem.setObjective(*result);
  problem.solve(false);

  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(result->getValue(), 7.0, 1e-6);
}

TEST_F(XpressProblemV2Test, NativeMaxOverVariables) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.newConstraint(x == 3.0, "x_fixed");
  problem.newConstraint(y == 8.0, "y_fixed");

  auto result = problem.addNativeMaxConstraint({Expression{x}, Expression{y}});
  ASSERT_TRUE(result.has_value());

  problem.setObjective(*result);
  problem.solve(false);

  EXPECT_NEAR(result->getValue(), 8.0, 1e-6);
}

// dir == 0 means "active when the binary is 0", and must be accepted.
TEST_F(XpressProblemV2Test, IndicatorAcceptsZeroDirection) {
  auto problem = makeProblem();
  auto b = problem.makeBoolVar("b");
  auto z = problem.makeVar("z");

  problem.newConstraint(b == 0.0, "b_off");
  problem.newConstraint(z <= 10.0, "z_ub");
  problem.newConstraint(z >= 0.0, "z_lb");
  auto gated = problem.newConstraint(z <= 2.0, "z_gated");

  ASSERT_TRUE(problem.setIndicatorOnConstraint(gated, b, 0));

  problem.setObjective(-z);
  problem.solve(false);

  // b is pinned off, so the implication fires and caps z at 2 rather than 10.
  EXPECT_NEAR(z.getValue(), 2.0, 1e-6);
}

// The result variable of a native max may go unreferenced by the rest of the
// model; the constraint must still be well formed.
TEST_F(XpressProblemV2Test, NativeMaxResultMayBeUnused) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.newConstraint(x == 1.0, "x_fixed");
  problem.newConstraint(y == 2.0, "y_fixed");

  auto unused = problem.addNativeMaxConstraint({Expression{x}, Expression{y}});
  ASSERT_TRUE(unused.has_value());

  // Objective deliberately ignores the max result.
  problem.setObjective(x);
  problem.solve(false);

  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
}

// A callback registered for one solve must not survive into the next, where
// there may no longer be anything for it to call.
TEST_F(XpressProblemV2Test, ClearedCallbackIsNotCalledOnLaterSolve) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x >= 4.0, "x_lb");
  problem.setObjective(x);

  problem.setCallback(
      [](ProblemCallbackData) { return ProblemCallbackAction::CONTINUE; });
  problem.solve(false);

  problem.setCallback(nullptr);
  problem.solve(false);

  // Re-solving a model that already has an incumbent reports SOLUTION_FOUND
  // rather than OPTIMAL_FOUND, so assert on the answer instead of the status.
  EXPECT_NEAR(x.getValue(), 4.0, 1e-6);
}

// Each objective is solved separately, so the solve timer is started once per
// objective and must be stopped in between.
TEST_F(XpressProblemV2Test, SolvesMultipleObjectivesWithCallback) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  problem.newConstraint(x >= 4.0, "x_lb");
  problem.newConstraint(y >= 3.0, "y_lb");

  problem.setCallback(
      [](ProblemCallbackData) { return ProblemCallbackAction::CONTINUE; });
  problem.setObjective(std::vector<Expression>{Expression{x}, Expression{y}});
  problem.solve(false);

  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(y.getValue(), 3.0, 1e-6);
}

// An indicator gated on a continuous variable is a different model, not the
// one the caller asked for, so it must be rejected rather than built.
TEST_F(XpressProblemV2Test, IndicatorRejectsNonBinaryVariable) {
  auto problem = makeProblem();
  auto notBinary = problem.makeVar("not_binary");
  auto z = problem.makeVar("z");
  auto gated = problem.newConstraint(z <= 2.0, "z_gated");

  EXPECT_THROW(
      problem.setIndicatorOnConstraint(gated, notBinary, 1),
      std::invalid_argument);
}

// setDebugPath must dump the model and the solver state around each solve.
TEST_F(XpressProblemV2Test, WritesDebugDataAroundSolve) {
  const folly::test::TemporaryDirectory debugDir;
  const std::string debugPath = debugDir.path().string();

  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x >= 4.0, "lower_bound");
  problem.setObjective(x);
  problem.setDebugPath(debugPath);
  problem.solve(false);

  for (const auto* name :
       {"main.lp", "main.mps", "pre-main.svf", "post-main.svf"}) {
    EXPECT_TRUE(std::filesystem::exists(debugPath + "/" + name)) << name;
  }
}

TEST_F(XpressProblemV2Test, NativePwl) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  problem.newConstraint(x == 2.0, "x_fixed");

  // y = 2x on [0, 10], sampled at three breakpoints.
  auto y = problem.addNativePwlConstraint(
      Expression{x}, {{0.0, 0.0}, {5.0, 10.0}, {10.0, 20.0}});
  ASSERT_TRUE(y.has_value());

  problem.setObjective(*y);
  problem.solve(false);

  EXPECT_NEAR(y->getValue(), 4.0, 1e-6);
}

TEST_F(XpressProblemV2Test, UsesDefaultTolerancesAndObjectiveScaling) {
  auto problem = makeProblem();
  const Tolerances expected;
  const auto actual = problem.getTolerances();
  EXPECT_DOUBLE_EQ(actual.constraint, expected.constraint);
  EXPECT_DOUBLE_EQ(actual.integer, expected.integer);
  EXPECT_DOUBLE_EQ(actual.absgap, expected.absgap);
  EXPECT_DOUBLE_EQ(actual.relgap, expected.relgap);
  EXPECT_DOUBLE_EQ(actual.abscut, expected.abscut);
  EXPECT_DOUBLE_EQ(actual.relcut, expected.relcut);
  const int scaling = problem.getParameter("XPRS_SCALING");
  EXPECT_NE(scaling & XPRS_SCALING_SIMPLEX_OBJECTIVE_SCALING, 0);
}

TEST_F(XpressProblemV2Test, ReplacingObjectivesDropsPreviousResults) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  x.setBounds(4.0, 9.0);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(*problem.getOnlyResult().bestObjective(), 4.0, 1e-6);

  problem.setObjective(-x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  ASSERT_EQ(problem.getResultPerObjective().size(), 1);
  EXPECT_NEAR(*problem.getOnlyResult().bestObjective(), -9.0, 1e-6);
}

TEST_F(XpressProblemV2Test, AppliesPerObjectiveParameters) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("x");
  const auto y = problem.makeVar("y");
  problem.newConstraint(x >= 4.0);
  problem.newConstraint(y >= 3.0);
  problem.setObjective(std::vector<Expression>{Expression{x}, Expression{y}});
  algopt::common::thrift::PerObjectiveValue threads;
  threads.defaultValue() = 1;
  threads.objectiveIndexToValue() = {{0, 1}, {1, 2}};
  MultiObjConfig config;
  config.paramNamesToValues["XPRS_THREADS"] = threads;
  problem.setMultiObjConfig(config);
  problem.solve(false);

  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_EQ(problem.getParameter("XPRS_THREADS"), 2);
  EXPECT_NEAR(x.getValue(), 4.0, 1e-6);
  EXPECT_NEAR(y.getValue(), 3.0, 1e-6);
}

TEST_F(XpressProblemV2Test, ExportsBufferedObjectiveInRequestedFormats) {
  const folly::test::TemporaryDirectory directory;
  auto problem = makeProblem();
  const auto x = problem.makeVar("x");
  problem.newConstraint(x >= 4.0, "lower_bound");
  problem.setObjective(3.0 * x + 7.0);

  for (const auto* extension : {".lp", ".mps"}) {
    const auto file =
        (directory.path() / (std::string("model") + extension)).string();
    problem.saveToFileWithAllObjectives(file);
    ASSERT_TRUE(std::filesystem::exists(file));
    xpress::objects::XpressProblem loaded;
    loaded.readProb(file, "");
    loaded.lpOptimize("");
    ASSERT_EQ(loaded.getIntAttrib(XPRS_LPSTATUS), XPRS_LP_OPTIMAL);
    EXPECT_NEAR(loaded.getDblAttrib(XPRS_LPOBJVAL), 19.0, 1e-6);
  }
  const auto stateFile = (directory.path() / "model.svf").string();
  problem.saveToFileWithAllObjectives(stateFile);
  EXPECT_TRUE(std::filesystem::exists(stateFile));
  EXPECT_FALSE(std::filesystem::exists(stateFile + ".svf"));
  EXPECT_THROW(
      problem.saveToFile((directory.path() / "model.txt").string()),
      std::runtime_error);
}

TEST_F(XpressProblemV2Test, SensitivityRequiresUnmodifiedOptimalLp) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  const auto lower = problem.newConstraint(x >= 4.0, "lower");
  const auto upper = problem.newConstraint(x <= 10.0, "upper");
  problem.setObjective(x);
  EXPECT_THROW(lower.getDual(), std::runtime_error);
  EXPECT_THROW(x.getReducedCost(), std::runtime_error);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(lower.getDual(), 1.0, 1e-6);
  EXPECT_NEAR(upper.getSlack(), 6.0, 1e-6);
  EXPECT_NEAR(lower.getRHS(), 4.0, 1e-6);
  EXPECT_NEAR(x.getReducedCost(), 0.0, 1e-6);
  EXPECT_NEAR(*problem.getOnlyResult().absoluteGap(), 0.0, 1e-6);

  x.setLB(1.0);
  EXPECT_THROW(lower.getDual(), std::runtime_error);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(lower.getDual(), 1.0, 1e-6);
  x.setUB(9.0);
  EXPECT_THROW(x.getReducedCost(), std::runtime_error);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  problem.newConstraint(x >= 5.0);
  EXPECT_THROW(lower.getDual(), std::runtime_error);
}

TEST_F(XpressProblemV2Test, InterruptedLpDoesNotExposeSensitivity) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  auto y = problem.makeVar("y");
  x.setLB(0.0);
  y.setLB(0.0);
  const auto constraint = problem.newConstraint(x + y <= 10.0);
  problem.newConstraint(x <= 8.0);
  problem.newConstraint(y <= 8.0);
  problem.setObjective(-x - y);
  problem.setParameter("XPRS_PRESOLVE", 0);
  problem.setParameter("XPRS_DEFAULTALG", 3);
  problem.setParameter("XPRS_LPITERLIMIT", 1);
  problem.solve(false);

  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::SOLUTION_FOUND);
  EXPECT_LT(x.getValue() + y.getValue(), 10.0);
  EXPECT_THROW(constraint.getDual(), std::runtime_error);
  EXPECT_THROW(x.getReducedCost(), std::runtime_error);
}

TEST_F(XpressProblemV2Test, IisReportsConstraintAndBoundSide) {
  auto problem = makeProblem();
  auto x = problem.makeVar("x");
  x.setBounds(0.0, 3.0);
  problem.newConstraint(x >= 5.0, "x_ge_5");
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);

  const auto iis = problem.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(iis->constraintIds, std::vector<std::string>{"x_ge_5"});
  EXPECT_EQ(iis->upperBoundVars, std::vector<std::string>{"x"});
  EXPECT_TRUE(iis->lowerBoundVars.empty());
}

TEST_F(XpressProblemV2Test, RepeatedConstraintNamesRetainBothRows) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("bound");
  problem.newConstraint(x >= 4.0, "bound");
  problem.newConstraint(x <= 7.0, "bound");
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(x.getValue(), 4.0, 1e-6);

  problem.setObjective(-x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(x.getValue(), 7.0, 1e-6);
  EXPECT_EQ(*problem.getOnlyResult().problemAttributes()->numConstraints(), 2);
}

TEST_F(XpressProblemV2Test, IisPreservesCallerNamesForDuplicateRows) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("bound");
  problem.newConstraint(x >= 4.0, "bound");
  problem.newConstraint(x <= 1.0, "bound");
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
  const auto iis = problem.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(iis->constraintIds, (std::vector<std::string>{"bound", "bound"}));
}

TEST_F(XpressProblemV2Test, IisPreservesCallerNamesForDuplicateColumns) {
  auto problem = makeProblem();
  [[maybe_unused]] const auto unused = problem.makeVar("x");
  auto x = problem.makeVar("x");
  x.setBounds(0.0, 3.0);
  problem.newConstraint(x >= 5.0, "lower");
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
  const auto iis = problem.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(iis->constraintIds, std::vector<std::string>{"lower"});
  EXPECT_EQ(iis->upperBoundVars, std::vector<std::string>{"x"});
}

TEST_F(XpressProblemV2Test, VariableValuesUsePublishedSolution) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("x");
  const Expression expression{x};
  problem.newConstraint(x >= 4.0);
  problem.setObjective(x);
  EXPECT_THROW(x.getValue(), std::runtime_error);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(x.getValue(), 4.0, 1e-6);
  EXPECT_DOUBLE_EQ(x.getValue(), expression.computeValue());

  problem.newConstraint(x <= 1.0);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
  EXPECT_THROW(x.getValue(), std::runtime_error);
  EXPECT_THROW(expression.computeValue(), std::runtime_error);
}

TEST_F(XpressProblemV2Test, NewVariableRequiresAnotherPublishedSolution) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("x");
  problem.newConstraint(x >= 4.0);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);

  const auto y = problem.makeVar("y");
  EXPECT_THROW(y.getValue(), std::runtime_error);
  problem.newConstraint(y >= 3.0);
  problem.setObjective(x + y);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  EXPECT_NEAR(y.getValue(), 3.0, 1e-6);
}

TEST_F(XpressProblemV2Test, NamesAreObservableBeforeSolve) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("named_column");
  const auto y = problem.makeVar("another_column");
  auto row = problem.newConstraint(x + y >= 4.0, "named_row");
  const auto& nativeX = dynamic_cast<const XpressV2Variable&>(*x.get()).get();
  const auto& nativeRow =
      dynamic_cast<const XpressV2Constraint&>(*row.get()).get();
  EXPECT_EQ(nativeX.getName(), std::string{"named_column"});
  EXPECT_EQ(nativeRow.getName(), std::string{"named_row"});
  const auto expression = x + y;
  const auto& implementation =
      dynamic_cast<const XpressV2Expression&>(*expression.get());
  const auto text = implementation.get().toString();
  EXPECT_NE(text.find("named_column"), std::string::npos);
  EXPECT_NE(text.find("another_column"), std::string::npos);
}

TEST_F(XpressProblemV2Test, PrintedExpressionsIncludeUnobservedNames) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("print_column");
  const auto expression = x + 2.0;
  const auto& implementation =
      dynamic_cast<const XpressV2Expression&>(*expression.get());
  testing::internal::CaptureStdout();
  implementation.print();
  const auto text = testing::internal::GetCapturedStdout();
  EXPECT_NE(text.find("print_column"), std::string::npos);
}

TEST_F(
    XpressProblemV2Test,
    MixedProblemExpressionKeepsOwnershipChecksAndNames) {
  auto first = makeProblem();
  auto second = makeProblem();
  const auto x = first.makeVar("first_column");
  const auto y = second.makeVar("second_column");
  EXPECT_THROW(first.newConstraint(x + y >= 4.0), std::exception);
  EXPECT_EQ(
      dynamic_cast<const XpressV2Variable&>(*x.get()).get().getName(),
      std::string{"first_column"});
  EXPECT_EQ(
      dynamic_cast<const XpressV2Variable&>(*y.get()).get().getName(),
      std::string{"second_column"});
}

TEST_F(XpressProblemV2Test, RetainedNativeHandleObservesLaterNames) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("first");
  auto* native =
      dynamic_cast<const XpressV2Variable&>(*x.get()).get().getProblem();
  [[maybe_unused]] const auto y = problem.makeVar("second");
  EXPECT_EQ(native->getColumnName(1), std::string{"second"});
}

TEST_F(XpressProblemV2Test, DeletingRowPreservesLaterNames) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("x");
  auto removed = problem.newConstraint(x >= 1.0, "removed");
  auto retained = problem.newConstraint(x >= 2.0, "retained");
  problem.deleteConstraint(removed);
  auto last = problem.newConstraint(x <= 8.0, "last");
  EXPECT_EQ(
      dynamic_cast<const XpressV2Constraint&>(*retained.get()).get().getName(),
      std::string{"retained"});
  EXPECT_EQ(
      dynamic_cast<const XpressV2Constraint&>(*last.get()).get().getName(),
      std::string{"last"});
  problem.setObjective(x);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 2.0, 1e-6);
}

TEST_F(XpressProblemV2Test, NativeHandleExposureAfterProblemDestructionIsSafe) {
  const auto variable = [] {
    auto problem = makeProblem();
    const auto x = problem.makeVar("retained");
    return x.get();
  }();
  EXPECT_NO_THROW(dynamic_cast<const XpressV2Variable&>(*variable).get());
}

TEST_F(XpressProblemV2Test, EmbeddedNullNameDoesNotOverwriteAdjacentName) {
  auto problem = makeProblem();
  const auto x = problem.makeVar(std::string{"prefix\0suffix", 13});
  const auto y = problem.makeVar("neighbor");
  EXPECT_EQ(
      dynamic_cast<const XpressV2Variable&>(*x.get()).get().getName(),
      std::string{"prefix"});
  EXPECT_EQ(
      dynamic_cast<const XpressV2Variable&>(*y.get()).get().getName(),
      std::string{"neighbor"});
}

TEST_F(XpressProblemV2Test, IisRetainsUnnamedRows) {
  auto problem = makeProblem();
  const auto x = problem.makeVar("");
  problem.newConstraint(x >= 4.0);
  problem.newConstraint(x <= 1.0);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
  const auto iis = problem.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(iis->constraintIds.size(), 2);
}

TEST_F(XpressProblemV2Test, InterruptedMipRetainsInputColumnValues) {
  auto problem = makeProblem();
  problem.setParameter("XPRS_THREADS", 2);
  problem.setParameter("XPRS_RANDOMSEED", 17);
  problem.setParameter("XPRS_MAXNODE", 1);
  problem.setParameter("XPRS_CUTSTRATEGY", 0);
  problem.setParameter("XPRS_HEUREMPHASIS", 0);
  problem.setMaxSolveTime(30);
  std::vector<Variable> fixed;
  fixed.reserve(64);
  for ([[maybe_unused]] const auto i : folly::irange(64)) {
    auto variable = problem.makeIntVar();
    variable.setBounds(7.0, 7.0);
    problem.addStartValue(variable, 7.0);
    fixed.push_back(variable);
  }
  std::vector<Variable> cycle;
  cycle.reserve(51);
  Expression objective;
  for ([[maybe_unused]] const auto i : folly::irange(51)) {
    const auto variable = problem.makeBoolVar();
    problem.addStartValue(variable, 0.0);
    objective -= variable;
    cycle.push_back(variable);
  }
  for (const auto i : folly::irange(51)) {
    problem.newConstraint(cycle[i] + cycle[(i + 1) % 51] <= 1.0);
  }
  problem.setObjective(objective);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::SOLUTION_FOUND);
  const auto* native =
      dynamic_cast<const XpressV2Variable&>(*cycle.back().get())
          .get()
          .getProblem();
  ASSERT_GT(
      native->getIntAttrib(XPRS_INPUTCOLS), native->getIntAttrib(XPRS_COLS));
  for (const auto& variable : fixed) {
    EXPECT_DOUBLE_EQ(variable.getValue(), 7.0);
  }
  for (const auto& variable : cycle) {
    const double value = variable.getValue();
    EXPECT_GE(value, 0.0);
    EXPECT_LE(value, 1.0);
  }
  EXPECT_DOUBLE_EQ(
      objective.computeValue(), *problem.getOnlyResult().bestObjective());
}

} // namespace
} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
