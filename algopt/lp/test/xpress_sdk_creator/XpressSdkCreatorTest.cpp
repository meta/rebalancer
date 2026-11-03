// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "license_manager/XpressSetup.h" // @manual=fbsource//third-party/xpress:xpress_cpp_setup
// These tests exercise Meta's patches to the vendored Xpress SDK headers.
// ast-grep-ignore: common/cpp/prefer-opal
#include "xpress.hpp" // @manual=fbsource//third-party/xpress:xpress

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace xpress::objects;

class RowPairDefinitionImplementation
    : public ConstraintDefinitionImplementation<Inequality> {
 public:
  RowPairDefinitionImplementation(
      const Variable& x,
      bool failAfterCommit,
      int& committedRows)
      : ConstraintDefinitionImplementation<Inequality>(std::nullopt),
        lower(x >= 4.0),
        upper(x <= 6.0),
        failAfterCommit(failAfterCommit),
        committedRows(committedRows) {}

  const IndexHandler<Inequality>* getIndexHandler(
      XpressProblem* problem) const override {
    return lower.getIndexHandler(problem);
  }

 protected:
  void createInternal(
      xpress::XPRSProblem::ConstraintCreator* creator) const override {
    lower.create(creator);
    upper.create(creator);
    if (failAfterCommit) {
      creator->commit();
      committedRows = creator->prob->attributes.getInputRows();
      throw std::runtime_error("failure after intermediate commit");
    }
  }

 private:
  const InequalityDefinition lower;
  const InequalityDefinition upper;
  const bool failAfterCommit;
  int& committedRows;
};

class XpressSdkCreatorTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    static const std::string licensePath =
        facebook::license_manager::xpress::createTemporaryLicenseFile();
    ASSERT_EQ(setenv("XPRESS", licensePath.c_str(), 1), 0);
  }
};

template <typename Array, typename T>
void checkMovedBuffer(std::vector<T> buffer, const T& value) {
  const auto initial = buffer;
  const auto* storage = buffer.data();
  Array array(std::move(buffer));
  EXPECT_EQ(array.getData().data(), storage);
  EXPECT_EQ(array.getData(), initial);
  EXPECT_EQ(array.getLength(), 0);
  EXPECT_EQ(array.getCapacity(), initial.size());
  array.append(value);
  auto alias = array;
  EXPECT_EQ(alias.copyOfRange(0), std::vector<T>{value});
  array.reset();
  EXPECT_EQ(alias.getLength(), 0);
  array.append(value);
  array.append(value);
  EXPECT_EQ(alias.copyOfRange(0), (std::vector<T>{value, value}));
}

TEST_F(XpressSdkCreatorTest, MoveIntBufferRetainsStorageAndLength) {
  checkMovedBuffer<xpress::ExtendableIntArray>(std::vector<int>(32), 7);
}

TEST_F(XpressSdkCreatorTest, MoveDoubleBufferRetainsStorageAndLength) {
  checkMovedBuffer<xpress::ExtendableDoubleArray>(std::vector<double>(32), 7.5);
}

TEST_F(XpressSdkCreatorTest, MoveTypeBufferRetainsStorageAndLength) {
  checkMovedBuffer<xpress::ExtendableTypeArray>(std::vector<char>(32), 'E');
}

TEST_F(XpressSdkCreatorTest, MoveNameBufferRetainsStorageAndLength) {
  checkMovedBuffer<xpress::ExtendableArray<std::optional<std::string>>>(
      std::vector<std::optional<std::string>>(32),
      std::optional<std::string>("row"));
}

TEST_F(XpressSdkCreatorTest, LvalueBufferStillCopies) {
  const std::vector<int> original{1, 2};
  xpress::ExtendableIntArray array(original);
  EXPECT_NE(array.getData().data(), original.data());
  EXPECT_EQ(array.getData(), original);
  array.append(7);
  EXPECT_EQ(original, (std::vector<int>{1, 2}));
}

TEST_F(XpressSdkCreatorTest, ExtractedBulkDefinitionCannotBeReusedAsScalar) {
  XpressProblem problem;
  const auto x = problem.addVariable();
  LinTermList lhs;
  lhs.addTerm(x, 1.0);
  const InequalityDefinition definition = lhs <= 1.0;
  problem.addConstraints(std::vector<InequalityDefinition>{definition});
  ASSERT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_THROW(problem.addConstraint(definition), std::runtime_error);
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
}

TEST_F(XpressSdkCreatorTest, ScalarExtractionConsumesAliasesAndKeepsName) {
  XpressProblem problem;
  const auto x = problem.addVariable();
  LinTermList lhs;
  lhs.addTerm(x, 1.0);
  InequalityDefinition definition = lhs <= 1.0;
  definition.setName("before");
  auto alias = definition;
  const auto row = problem.addConstraint(definition);
  ASSERT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_EQ(row.getName(), "before");
  EXPECT_THROW(alias.setName("after"), std::runtime_error);
  EXPECT_THROW(problem.addConstraint(alias), std::runtime_error);
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_EQ(row.getIndexForProb(&problem), 0);
}

TEST_F(XpressSdkCreatorTest, FailedForeignExtractionKeepsRowsAndAllowsRetry) {
  XpressProblem problem;
  XpressProblem foreign;
  const auto x = problem.addVariable();
  const auto y = foreign.addVariable();
  LinTermMap lhs;
  lhs.addTerm(y, 1.0);
  InequalityDefinition definition = lhs <= 1.0;
  auto alias = definition;
  EXPECT_THROW(problem.addConstraint(definition), NotInProblemException);
  EXPECT_EQ(problem.attributes.getInputRows(), 0);
  EXPECT_NO_THROW(alias.setName("retry"));
  lhs.reset();
  lhs.addTerm(x, 1.0);
  const auto row = problem.addConstraint(definition);
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_EQ(row.getName(), "retry");
  EXPECT_EQ(row.getIndexForProb(&problem), 0);
  EXPECT_THROW(row.getIndexForProb(&foreign), NotInProblemException);
}

TEST_F(
    XpressSdkCreatorTest,
    ScalarListExtractionCombinesDuplicatesAndConstants) {
  XpressProblem problem;
  const auto x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous);
  LinTermList lhs(2.0);
  lhs.addTerm(x, 2.0);
  lhs.addTerm(x, -1.0);
  problem.addConstraint(lhs >= 6.0);
  problem.setObjective(x, xpress::ObjSense::Minimize);
  problem.optimize();
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_NEAR(problem.getSolution(x), 4.0, 1e-6);
}

TEST_F(XpressSdkCreatorTest, ScalarQuadraticConstraintKeepsTypedOwnership) {
  XpressProblem problem;
  const auto x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous);
  const auto row = problem.addConstraint(x * x <= 4.0);
  problem.setObjective(x, xpress::ObjSense::Maximize);
  problem.optimize();
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_EQ(row.getIndexForProb(&problem), 0);
  EXPECT_NEAR(problem.getSolution(x), 2.0, 1e-5);
}

TEST_F(XpressSdkCreatorTest, BulkExtractionRegistersEveryCreatedRow) {
  XpressProblem problem;
  const auto x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous);
  const std::vector<InequalityDefinition> definitions{x >= 4.0, x <= 6.0};
  const auto rows = problem.addConstraints(definitions);
  ASSERT_EQ(problem.attributes.getInputRows(), 2);
  int count = 0;
  for (const auto& row : rows) {
    EXPECT_EQ(row.getIndexForProb(&problem), count++);
  }
  EXPECT_EQ(count, 2);
  problem.setObjective(x, xpress::ObjSense::Minimize);
  problem.optimize();
  EXPECT_NEAR(problem.getSolution(x), 4.0, 1e-6);
}

TEST_F(XpressSdkCreatorTest, ScalarDefinitionCanCreateMultipleTypedRows) {
  XpressProblem problem;
  const auto x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous);
  int committedRows = 0;
  const auto implementation = std::make_shared<RowPairDefinitionImplementation>(
      x, false, committedRows);
  const ConstraintDefinition<Inequality> definition(implementation.get());
  const auto first = problem.addConstraint(definition);
  EXPECT_EQ(problem.attributes.getInputRows(), 2);
  EXPECT_EQ(first.getIndexForProb(&problem), 0);
  const auto second = problem.inequalityForIndex(1);
  EXPECT_EQ(second.getIndexForProb(&problem), 1);
  problem.setObjective(x, xpress::ObjSense::Minimize);
  problem.optimize();
  EXPECT_NEAR(problem.getSolution(x), 4.0, 1e-6);
  EXPECT_THROW(problem.addConstraint(definition), std::runtime_error);
  EXPECT_EQ(problem.attributes.getInputRows(), 2);
}

TEST_F(XpressSdkCreatorTest, IntermediateCommitFailureRollsBackOnlyNewRows) {
  XpressProblem problem;
  const auto x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous);
  const auto original = problem.addConstraint(x >= 2.0);
  int committedRows = 0;
  const auto implementation =
      std::make_shared<RowPairDefinitionImplementation>(x, true, committedRows);
  ConstraintDefinition<Inequality> definition(implementation.get());
  auto alias = definition;
  EXPECT_THROW(problem.addConstraint(definition), std::runtime_error);
  EXPECT_EQ(committedRows, 3);
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_EQ(original.getIndexForProb(&problem), 0);
  EXPECT_NO_THROW(alias.setConstraintName("not_extracted"));
  problem.setObjective(x, xpress::ObjSense::Minimize);
  problem.optimize();
  EXPECT_NEAR(problem.getSolution(x), 2.0, 1e-6);
}

TEST_F(XpressSdkCreatorTest, ScalarRangeRetainsBothBounds) {
  XpressProblem problem;
  const auto x = problem.addVariable(0.0, 10.0, xpress::ColumnType::Continuous);
  LinTermList lhs;
  lhs.addTerm(x, 1.0);
  const auto row = problem.addConstraint(lhs.in(2.0, 4.0));
  EXPECT_EQ(problem.attributes.getInputRows(), 1);
  EXPECT_EQ(row.getIndexForProb(&problem), 0);
  problem.setObjective(x, xpress::ObjSense::Minimize);
  problem.optimize();
  EXPECT_NEAR(problem.getSolution(x), 2.0, 1e-6);
  problem.setObjective(x, xpress::ObjSense::Maximize);
  problem.optimize();
  EXPECT_NEAR(problem.getSolution(x), 4.0, 1e-6);
}

} // namespace
