// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "license_manager/XpressSetup.h" // @manual=fbsource//third-party/xpress:xpress_cpp_setup
// These tests exercise Meta's patches to the vendored Xpress SDK headers.
// ast-grep-ignore: common/cpp/prefer-opal
#include "xpress.hpp" // @manual=fbsource//third-party/xpress:xpress

#include <gtest/gtest.h>

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace xpress::objects;

class XpressSdkEvaluationTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    static const std::string licensePath =
        facebook::license_manager::xpress::createTemporaryLicenseFile();
    ASSERT_EQ(setenv("XPRESS", licensePath.c_str(), 1), 0);
  }

  void SetUp() override {
    x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous, "x");
    problem.addConstraint(x >= 4.0);
    problem.setObjective(x, xpress::ObjSense::Minimize);
    problem.optimize();
    snapshot = {problem.getSolution(x)};
    ASSERT_DOUBLE_EQ(snapshot[0], 4.0);
    y = problem.addVariable(
        0.0, 10.0, xpress::ColumnType::Continuous, "unsolved_y");
    ASSERT_EQ(y.getIndex(), snapshot.size());
  }

  XpressProblem problem;
  Variable x;
  Variable y;
  std::vector<double> snapshot;
};

TEST_F(XpressSdkEvaluationTest, ZeroCoefficientIgnoresUnsolvedVariable) {
  LinTermList list;
  list.addTerm(x, 1.0);
  list.addTerm(y, 0.0);
  LinTermMap reference;
  reference.addTerms(list);
  ASSERT_DOUBLE_EQ(reference.evaluate(snapshot), 4.0);
  EXPECT_DOUBLE_EQ(list.evaluate(snapshot), 4.0);
}

TEST_F(XpressSdkEvaluationTest, CancelledCoefficientIgnoresUnsolvedVariable) {
  LinTermList list;
  list.addTerm(x, 1.0);
  list.addTerm(y, 1.0);
  list.addTerm(y, -1.0);
  LinTermMap reference;
  reference.addTerms(list);
  ASSERT_DOUBLE_EQ(reference.evaluate(snapshot), 4.0);
  const auto before = list.toString();
  EXPECT_DOUBLE_EQ(list.evaluate(snapshot), 4.0);
  EXPECT_EQ(list.toString(), before);
}

TEST_F(XpressSdkEvaluationTest, NonzeroUnsolvedVariableThrows) {
  LinTermList list;
  list.addTerm(x, 1.0);
  list.addTerm(y, 1.0);
  EXPECT_THROW(list.evaluate(snapshot), std::out_of_range);
}

TEST_F(XpressSdkEvaluationTest, AlmostCancelledUnsolvedVariableThrows) {
  LinTermList list;
  list.addTerm(y, 1.0);
  list.addTerm(y, -0.999999);
  EXPECT_THROW(list.evaluate(snapshot), std::out_of_range);
}

TEST_F(XpressSdkEvaluationTest, SingletonZeroAcceptsEmptySnapshot) {
  LinTermList list;
  list.addTerm(y, 0.0);
  const std::vector<double> empty;
  EXPECT_DOUBLE_EQ(list.evaluate(empty), 0.0);
}

TEST_F(XpressSdkEvaluationTest, CancellationAcceptsEmptySnapshot) {
  LinTermList list(7.0);
  list.addTerm(y, 2.0);
  list.addTerm(x, 3.0);
  list.addTerm(y, -2.0);
  list.addTerm(x, -3.0);
  const std::vector<double> empty;
  EXPECT_DOUBLE_EQ(list.evaluate(empty), 7.0);
}

TEST_F(XpressSdkEvaluationTest, PresentCoordinatesKeepDuplicateTerms) {
  LinTermList list(2.0);
  list.addTerm(x, 3.0);
  list.addTerm(x, -1.0);
  const auto before = list.toString();
  EXPECT_DOUBLE_EQ(list.evaluate(snapshot), 10.0);
  EXPECT_DOUBLE_EQ(list.evaluate(snapshot), 10.0);
  EXPECT_EQ(list.toString(), before);
}

TEST_F(XpressSdkEvaluationTest, SolvingAddedVariableSuppliesMissingCoordinate) {
  LinTermList list;
  list.addTerm(x, 1.0);
  list.addTerm(y, 1.0);
  EXPECT_THROW(list.evaluate(snapshot), std::out_of_range);
  problem.addConstraint(y >= 2.0);
  problem.setObjective(x + y, xpress::ObjSense::Minimize);
  problem.optimize();
  const std::vector<double> newSnapshot{
      problem.getSolution(x), problem.getSolution(y)};
  EXPECT_DOUBLE_EQ(list.evaluate(newSnapshot), 6.0);
  EXPECT_THROW(list.evaluate(snapshot), std::out_of_range);
}

} // namespace
