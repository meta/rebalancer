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

#include "algopt/lp/environment/Environment.h"

#include <gtest/gtest.h>

#if defined(REBALANCER_USE_XPRESS) && !defined(REBALANCER_OSS_BUILD)

#include "algopt/lp/detail/xpress/XpressProblem.h"
#include "algopt/lp/detail/xpress_v2/XpressProblemV2.h"
#include "algopt/lp/detail/xpress_v2/XpressV2Types.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/lp.h"

#include <fmt/format.h>
#include <folly/testing/TestUtil.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace facebook::algopt::lp::detail {
namespace {

class ScalarV2Problem : public XpressProblemV2 {
 public:
  bool supportsConstraintBatching() const override {
    return false;
  }

  std::vector<std::shared_ptr<ConstraintImpl>> newConstraints(
      const ConstraintRequests& requests) override {
    return ProblemImpl::newConstraints(requests);
  }

  std::shared_ptr<ConstraintImpl> newConstraint(
      std::shared_ptr<const RelationImpl> relation,
      const std::string& name) override {
    ++attempts;
    if (attempts == failAt) {
      throw std::runtime_error("injected scalar insertion failure");
    }
    return XpressProblemV2::newConstraint(std::move(relation), name);
  }

  int attempts = 0;
  int failAt = -1;
};

xpress::objects::XpressProblem& native(const Variable& variable) {
  return *dynamic_cast<const XpressV2Variable&>(*variable.get())
              .get()
              .getProblem();
}

const xpress::objects::Inequality& native(Constraint constraint) {
  return dynamic_cast<const XpressV2Constraint&>(*constraint.get()).get();
}

class ConstraintBatchTest : public ::testing::TestWithParam<bool> {
 protected:
  void SetUp() override {
    lazyLoadXpress();
  }

  static Problem makeProblem(bool nativeBulk) {
    if (nativeBulk) {
      return Problem{std::make_unique<XpressProblemV2>()};
    }
    return Problem{std::make_unique<ScalarV2Problem>()};
  }
};

TEST_P(ConstraintBatchTest, ReturnsOrderedRealHandlesAndReusesRelations) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto relation = x >= 2;
  const auto constraints = problem.newConstraints(
      {{relation, "lower"}, {x <= 7, "upper"}, {relation, "duplicate"}});
  ASSERT_EQ(constraints.size(), 3);
  EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 3);
  EXPECT_EQ(native(constraints[0]).getIndex(), 0);
  EXPECT_EQ(native(constraints[1]).getIndex(), 1);
  EXPECT_EQ(native(constraints[2]).getIndex(), 2);
  EXPECT_EQ(native(constraints[0]).getProblem(), &native(x));
  EXPECT_DOUBLE_EQ(constraints[0].getRHS(), 2);
  EXPECT_DOUBLE_EQ(constraints[1].getRHS(), 7);
  EXPECT_DOUBLE_EQ(constraints[2].getRHS(), 2);
  const auto scalar = problem.newConstraint(relation, "scalar_after_batch");
  EXPECT_EQ(native(scalar).getIndex(), 3);
}

TEST_P(ConstraintBatchTest, HandlesExposeDeferredNamesAndKeepLaterNamesEager) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto rows =
      problem.newConstraints({{x >= 0, "lower"}, {x <= 10, "upper"}});
  ASSERT_EQ(rows.size(), 2);
  const auto& retained = native(rows[0]);
  EXPECT_EQ(retained.getName(), "lower");
  EXPECT_EQ(native(rows[1]).getName(), "upper");
  const auto later = problem.newConstraints({{x <= 9, "later"}});
  ASSERT_EQ(later.size(), 1);
  const auto laterNames = retained.getProblem()->getNames(XPRS_NAMES_ROW, 2, 2);
  EXPECT_EQ(laterNames, (std::vector<std::string>{"later"}));
}

TEST_P(ConstraintBatchTest, NormalizesAllConstantRelationSenses) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto rows = problem.newConstraints({
      {Relation{0, Relation::EQ_ZERO}, "eq"},
      {Relation{4, Relation::GE_ZERO}, "ge"},
      {Relation{-5, Relation::LE_ZERO}, "le"},
  });
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 3);
  EXPECT_DOUBLE_EQ(rows[0].getRHS(), 0);
  EXPECT_DOUBLE_EQ(rows[1].getRHS(), -4);
  EXPECT_DOUBLE_EQ(rows[2].getRHS(), 5);
  problem.setObjective(Expression{0});
  problem.solve(false);
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
}

TEST_P(ConstraintBatchTest, CompletedRelationsDoNotAliasMutatedExpressions) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  auto expression = x + 1;
  const auto first = expression <= 4;
  expression += 10;
  const auto rows =
      problem.newConstraints({{first, "first"}, {expression <= 20, "second"}});
  ASSERT_EQ(rows.size(), 2);
  EXPECT_DOUBLE_EQ(rows[0].getRHS(), 3);
  EXPECT_DOUBLE_EQ(rows[1].getRHS(), 9);
  problem.setObjective(-x);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 3, 1e-7);
}

TEST_P(ConstraintBatchTest, PreservesEmptyDuplicateAndEmbeddedNulNames) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto rows = problem.newConstraints({
      {x >= 0, "duplicate"},
      {x <= 10, "duplicate"},
      {x <= 11, ""},
      {x <= 12, std::string{"left\0tail", 9}},
      {x <= 13, "right"},
  });
  ASSERT_EQ(rows.size(), 5);
  EXPECT_EQ(native(rows[0]).getName(), "duplicate");
  EXPECT_EQ(native(rows[1]).getName(), "duplicate_1");
  EXPECT_FALSE(native(rows[2]).getName().empty());
  EXPECT_EQ(native(rows[3]).getName(), "left");
  EXPECT_EQ(native(rows[4]).getName(), "right");
  const auto extra = problem.newConstraint(x <= 14, "duplicate");
  EXPECT_EQ(native(extra).getName(), "duplicate_4");
}

TEST_P(ConstraintBatchTest, ReportsOriginalDuplicateNamesInIis) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto rows =
      problem.newConstraints({{x >= 5, "bound"}, {x <= 1, "bound"}});
  ASSERT_EQ(rows.size(), 2);
  problem.setObjective(x);
  problem.solve(false);
  ASSERT_EQ(problem.getStatus(), thrift::ProblemStatus::NO_SOLUTION_EXISTS);
  const auto iis = problem.getIIS();
  ASSERT_TRUE(iis.has_value());
  EXPECT_EQ(iis->constraintIds, (std::vector<std::string>{"bound", "bound"}));
}

TEST_P(ConstraintBatchTest, HandlesRemainUsableForIndicatorsAndDeletion) {
  auto problem = makeProblem(GetParam());
  const auto b = problem.makeBoolVar("b");
  const auto x = problem.makeVar("x");
  auto rows = problem.newConstraints(
      {{b == 0, "off"},
       {x <= 10, "upper"},
       {x >= 0, "lower"},
       {x <= 2, "gated"}});
  ASSERT_EQ(rows.size(), 4);
  ASSERT_TRUE(problem.setIndicatorOnConstraint(rows[3], b, 0));
  problem.setObjective(-x);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 2, 1e-7);
  problem.deleteConstraint(rows[3]);
  EXPECT_EQ(native(rows[3]).getIndex(), -1);
  EXPECT_EQ(native(rows[2]).getIndex(), 2);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 10, 1e-7);
}

TEST_P(
    ConstraintBatchTest,
    EmptyBatchPreservesSensitivityAndNonemptyInvalidatesIt) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  auto rows = problem.newConstraints({{x >= 2, "lower"}, {x <= 10, "upper"}});
  problem.setObjective(x);
  problem.solve(false);
  EXPECT_NEAR(rows[0].getDual(), 1, 1e-7);
  EXPECT_NEAR(rows[0].getSlack(), 0, 1e-7);
  EXPECT_NEAR(rows[1].getSlack(), 8, 1e-7);
  EXPECT_TRUE(problem.newConstraints({}).empty());
  EXPECT_NEAR(rows[0].getDual(), 1, 1e-7);
  auto next = problem.newConstraints({{x >= 3, "new_lower"}});
  EXPECT_THROW(rows[0].getDual(), std::runtime_error);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 3, 1e-7);
  EXPECT_NEAR(next[0].getDual(), 1, 1e-7);
}

TEST_P(ConstraintBatchTest, ExportsAndPreservesPostsolveHandles) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto rows =
      problem.newConstraints({{x >= 2, "lower"}, {x <= 7, "upper"}});
  problem.setObjective(x);
  const folly::test::TemporaryDirectory directory;
  const auto path = (directory.path() / "batch.lp").string();
  problem.saveToFileWithAllObjectives(path);
  EXPECT_GT(std::filesystem::file_size(path), 0);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 2, 1e-7);
  EXPECT_DOUBLE_EQ(rows[1].getRHS(), 7);
}

TEST_P(ConstraintBatchTest, RejectsForeignAndDeletedVariableOwnership) {
  auto problem = makeProblem(GetParam());
  auto other = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto y = other.makeVar("y");
  EXPECT_THROW(
      problem.newConstraints({{x >= 0, "valid"}, {y >= 1, "foreign"}}),
      xpress::objects::NotInProblemException);
  // Inspect only to test the documented exception state, then discard model.
  EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), GetParam() ? 0 : 1);

  auto deletedProblem = makeProblem(GetParam());
  const auto z = deletedProblem.makeVar("z");
  const auto deleted = deletedProblem.makeVar("deleted");
  const auto relation = deleted >= 1;
  const auto index =
      dynamic_cast<const XpressV2Variable&>(*deleted.get()).get().getIndex();
  native(z).delCols(1, &index);
  EXPECT_THROW(
      deletedProblem.newConstraints({{z >= 0, "valid"}, {relation, "deleted"}}),
      xpress::objects::NotInProblemException);
  EXPECT_EQ(native(z).getIntAttrib(XPRS_ORIGINALROWS), GetParam() ? 0 : 1);
}

TEST_P(ConstraintBatchTest, QuadraticRowsUseRealOwnedHandles) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto rows =
      problem.newConstraints({{x * x <= 9, "square"}, {x >= 0, "nonnegative"}});
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(native(rows[0]).getIndex(), 0);
  EXPECT_DOUBLE_EQ(rows[0].getRHS(), 9);
  problem.setObjective(-x);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 3, 1e-6);
}

class AuxiliaryRowExpression
    : public xpress::objects::AbstractExpressionImplementation {
 public:
  explicit AuxiliaryRowExpression(const xpress::objects::Variable& variable)
      : variable_(variable) {}

  xpress::objects::ExpressionRTTI getRTTI() const override {
    return xpress::objects::ExpressionRTTI::Constant;
  }
  int getMaxDegree() const override {
    return 0;
  }
  double evaluate(const xpress::SizedArray<const double>&) const override {
    return 0;
  }
  std::string toString() const override {
    return "auxiliary-row-test-expression";
  }
  [[noreturn]] void extract(double, xpress::objects::PostfixExtractor*)
      const override {
    throw std::runtime_error(
        "opaque expression must not be preflight-extracted");
  }

  bool extract(double, xpress::XPRSProblem::RowCreator* creator)
      const override {
    ++extractions;
    creator->addNonzero(
        variable_.getIndexForProb(
            static_cast<xpress::objects::XpressProblem*>(creator->prob)),
        1);
    creator->addRhs(3);
    creator->setType('L');
    creator->finishRow(true);
    return false;
  }

  mutable int extractions = 0;

 private:
  xpress::objects::Variable variable_;
};

TEST_P(
    ConstraintBatchTest,
    OpaqueExpressionKeepsScalarHandleAndAllAuxiliaryRows) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto sdkVariable =
      dynamic_cast<const XpressV2Variable&>(*x.get()).get();
  const auto implementation =
      std::make_shared<AuxiliaryRowExpression>(sdkVariable);
  const Relation relation{std::make_shared<XpressV2Relation>(
      xpress::objects::Expression{implementation.get()},
      XpressV2Relation::Sense::LessEqualZero)};
  auto rows =
      problem.newConstraints({{relation, "first"}, {relation, "second"}});
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(implementation->extractions, 2);
  EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 4);
  EXPECT_EQ(native(rows[0]).getIndex(), 0);
  EXPECT_EQ(native(rows[1]).getIndex(), 2);
  auto allRows = native(x).getInequalities();
  ASSERT_EQ(allRows.size(), 4);
  EXPECT_TRUE(allRows[0].isSameImpl(native(rows[0])));
  EXPECT_TRUE(allRows[2].isSameImpl(native(rows[1])));
  problem.deleteConstraint(rows[0]);
  EXPECT_EQ(allRows[0].getIndex(), -1);
  EXPECT_EQ(allRows[1].getIndex(), 0);
  EXPECT_EQ(allRows[2].getIndex(), 1);
  EXPECT_EQ(allRows[3].getIndex(), 2);
}

TEST_P(ConstraintBatchTest, ListOwnershipMatchesSdkCompressionPolicy) {
  const bool oldCompress = xpress::objects::LinTermList::isAutoCompress();
  const struct RestorePolicy {
    bool value;
    ~RestorePolicy() {
      xpress::objects::LinTermList::setAutoCompress(value);
    }
  } restore{oldCompress};
  for (const bool compress : {false, true}) {
    xpress::objects::LinTermList::setAutoCompress(compress);
    auto problem = makeProblem(GetParam());
    auto other = makeProblem(GetParam());
    const auto x = problem.makeVar("x");
    const auto y = other.makeVar("y");
    const auto foreign = dynamic_cast<const XpressV2Variable&>(*y.get()).get();
    xpress::objects::LinTermList zero;
    zero.addTerm(foreign, 0);
    const Relation zeroRelation{std::make_shared<XpressV2Relation>(
        xpress::objects::LinExpression{zero},
        XpressV2Relation::Sense::LessEqualZero)};
    const auto zeroRows = problem.newConstraints({{zeroRelation, "zero"}});
    EXPECT_EQ(zeroRows.size(), 1);
    xpress::objects::LinTermList cancelled;
    cancelled.addTerm(foreign, 1);
    cancelled.addTerm(foreign, -1);
    const Relation relation{std::make_shared<XpressV2Relation>(
        xpress::objects::LinExpression{cancelled},
        XpressV2Relation::Sense::LessEqualZero)};
    if (compress) {
      const auto rows = problem.newConstraints({{relation, "cancelled"}});
      EXPECT_EQ(rows.size(), 1);
      EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 2);
    } else {
      EXPECT_THROW(
          problem.newConstraints({{relation, "cancelled"}}),
          xpress::objects::NotInProblemException);
      EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 1);
    }
  }
}

TEST_P(ConstraintBatchTest, GeneratedNameCollisionsMatchScalarSuccess) {
  auto probe = makeProblem(GetParam());
  const auto probeVar = probe.makeVar("probe");
  const auto unnamed =
      probe.newConstraints({{probeVar >= 0, ""}, {probeVar <= 10, ""}});
  std::vector<std::string> labels{"R1", "R2", "r1", "c1", "C1"};
  labels.reserve(labels.size() + unnamed.size());
  for (const auto& row : unnamed) {
    labels.push_back(native(row).getName());
  }
  for (const auto& label : labels) {
    SCOPED_TRACE(label);
    struct Outcome {
      bool succeeded;
      std::vector<std::string> names;
    };
    const auto run = [&](bool scalar) {
      auto problem = makeProblem(GetParam());
      const auto x = problem.makeVar("x");
      const Problem::ConstraintRequests requests{
          {x >= 0, label}, {x <= 10, ""}, {x <= 11, "tail"}};
      std::vector<Constraint> rows;
      rows.reserve(requests.size());
      try {
        if (scalar) {
          for (const auto& [relation, name] : requests) {
            rows.push_back(problem.newConstraint(relation, name));
          }
        } else {
          rows = problem.newConstraints(requests);
        }
      } catch (const std::exception&) {
        return Outcome{.succeeded = false, .names = {}};
      }
      std::vector<std::string> names;
      names.reserve(rows.size());
      for (const auto& row : rows) {
        names.push_back(native(row).getName());
      }
      return Outcome{.succeeded = true, .names = std::move(names)};
    };
    const auto scalar = run(true);
    const auto batch = run(false);
    EXPECT_EQ(scalar.succeeded, batch.succeeded);
    EXPECT_EQ(scalar.names, batch.names);
  }
}

TEST_P(
    ConstraintBatchTest,
    MixedLinearQuadraticRequestsKeepScalarMatrixAssociation) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  const auto y = problem.makeVar("y");
  auto rows = problem.newConstraints({
      {x >= 1, "linear_first"},
      {y * y <= 4, "quadratic_second"},
      {y <= 3, "linear_third"},
  });
  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(native(rows[0]).getIndex(), 0);
  EXPECT_EQ(native(rows[1]).getIndex(), 1);
  EXPECT_EQ(native(rows[2]).getIndex(), 2);
  EXPECT_DOUBLE_EQ(rows[0].getRHS(), 1);
  EXPECT_DOUBLE_EQ(rows[1].getRHS(), 4);
  EXPECT_DOUBLE_EQ(rows[2].getRHS(), 3);
  problem.setObjective(-y);
  problem.solve(false);
  EXPECT_NEAR(y.getValue(), 2, 1e-6);
  auto aliases = native(x).getInequalities();
  ASSERT_EQ(aliases.size(), 3);
  EXPECT_TRUE(aliases[1].isSameImpl(native(rows[1])));
  problem.deleteConstraint(rows[0]);
  EXPECT_EQ(aliases[1].getIndex(), 0);
  EXPECT_EQ(native(rows[1]).getIndex(), 0);
}

TEST_P(ConstraintBatchTest, EmptyBatchDoesNotConsumePendingCallbackException) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  auto& sdk = native(x);
  sdk.setCBException(
      std::make_exception_ptr(std::runtime_error("pending callback")));
  EXPECT_TRUE(problem.newConstraints({}).empty());
  EXPECT_THROW(sdk.consumeCBException(), std::runtime_error);
}

TEST_P(
    ConstraintBatchTest,
    PendingCallbackErrorIsConsumedAtNativeEntryWithoutRetry) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  auto& sdk = native(x);
  sdk.setCBException(
      std::make_exception_ptr(std::runtime_error("pending callback")));
  EXPECT_THROW(
      problem.newConstraints({{x >= 0, "first"}, {x <= 2, "second"}}),
      std::runtime_error);
  EXPECT_NO_THROW(sdk.consumeCBException());
  EXPECT_EQ(sdk.getIntAttrib(XPRS_ORIGINALROWS), 0);
}

TEST_P(
    ConstraintBatchTest,
    NewBulkTypePreflightHasExplicitCallbackErrorPrecedence) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  auto& sdk = native(x);
  const Relation foreign{std::make_shared<RelationImpl>()};
  sdk.setCBException(
      std::make_exception_ptr(std::runtime_error("pending callback")));
  if (GetParam()) {
    EXPECT_THROW(
        problem.newConstraints({{x >= 0, "first"}, {foreign, "foreign"}}),
        std::invalid_argument);
    EXPECT_THROW(sdk.consumeCBException(), std::runtime_error);
  } else {
    EXPECT_THROW(
        problem.newConstraints({{x >= 0, "first"}, {foreign, "foreign"}}),
        std::runtime_error);
    EXPECT_NO_THROW(sdk.consumeCBException());
  }
  EXPECT_EQ(sdk.getIntAttrib(XPRS_ORIGINALROWS), 0);
}

TEST_P(
    ConstraintBatchTest,
    PostNativeNamingFailureRequiresDiscardAndRetainsRows) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  auto& sdk = native(x);
  EXPECT_THROW(
      problem.newConstraints(
          {{x >= 0, std::string{"left\0tail", 9}}, {x <= 2, "left"}}),
      xpress::XPRSException);
  EXPECT_EQ(sdk.getIntAttrib(XPRS_ORIGINALROWS), 2);
  auto rows = sdk.getInequalities();
  ASSERT_EQ(rows.size(), 2);
  EXPECT_EQ(rows[0].getName(), "left");
  EXPECT_NE(rows[1].getName(), "left");
  EXPECT_EQ(rows[0].getIndex(), 0);
  EXPECT_EQ(rows[1].getIndex(), 1);
}

TEST_P(ConstraintBatchTest, DeferredNamingFailureOccursOnHandleExposure) {
  auto problem = makeProblem(GetParam());
  const auto x = problem.makeVar("x");
  std::vector<Constraint> rows;
  ASSERT_NO_THROW(
      rows = problem.newConstraints(
          {{x >= 0, std::string{"left\0tail", 9}}, {x <= 2, "left"}}));
  ASSERT_EQ(rows.size(), 2);
  EXPECT_THROW(native(rows[1]), xpress::XPRSException);
}

TEST_P(
    ConstraintBatchTest,
    RawNamesAndExportAssociationMatchScalarAtBatchBoundaries) {
  const auto signature = [](xpress::objects::XpressProblem& sdk) {
    std::vector<std::string> result;
    for (auto row : sdk.getInequalities()) {
      auto value = fmt::format(
          "{}:{}:{:.17g}",
          row.getName(),
          xpress::to_string(row.getType()),
          row.getRhs());
      const auto view = row.getLhs().getLinearView();
      for (const auto& term : *view) {
        if (!xpress::objects::LinQuadUtils::isConstant(term.getVariable())) {
          value += fmt::format(
              ":{}={:.17g}",
              term.getVariable().getName(),
              term.getCoefficient());
        }
      }
      result.push_back(std::move(value));
    }
    std::sort(result.begin(), result.end());
    return result;
  };
  for (const size_t count : {127, 128, 129}) {
    for (const bool baseline : {false, true}) {
      for (const bool nameSecond : {false, true}) {
        const auto run = [&](bool scalar) {
          auto problem = makeProblem(GetParam());
          const auto x = problem.makeVar("x");
          if (baseline) {
            problem.newConstraint(x >= 0, "baseline");
          }
          Problem::ConstraintRequests requests;
          requests.reserve(count);
          for (size_t i = 0; i < count; ++i) {
            const auto name =
                i == 0 ? "R2" : (i == 1 && nameSecond ? "R1" : "");
            requests.emplace_back((i + 1) * x <= i + 2, name);
          }
          if (scalar) {
            for (const auto& [relation, name] : requests) {
              problem.newConstraint(relation, name);
            }
          } else {
            const auto rows = problem.newConstraints(requests);
            EXPECT_EQ(rows.size(), count);
          }
          auto& sdk = native(x);
          std::array<std::vector<std::string>, 4> result;
          result[0] = sdk.getNameList(
              XPRS_NAMES_ROW, 0, sdk.getIntAttrib(XPRS_ORIGINALROWS) - 1);
          result[1] = signature(sdk);
          problem.setObjective(x);
          const folly::test::TemporaryDirectory directory;
          for (size_t i = 0; i < 2; ++i) {
            const auto extension = i == 0 ? "lp" : "mps";
            const auto path =
                (directory.path() / (std::string{"model."} + extension))
                    .string();
            problem.saveToFileWithAllObjectives(path);
            xpress::objects::XpressProblem imported;
            imported.readProb(path, i == 0 ? "l" : "");
            result[i + 2] = signature(imported);
          }
          return result;
        };
        SCOPED_TRACE(
            fmt::format(
                "count={} baseline={} second_name={}",
                count,
                baseline,
                nameSecond));
        EXPECT_EQ(run(true), run(false));
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(ScalarAndBulk, ConstraintBatchTest, ::testing::Bool());

TEST(ConstraintBatchFallbackTest, RetainsValidPrefixOnScalarInsertionFailure) {
  lazyLoadXpress();
  auto implementation = std::make_unique<ScalarV2Problem>();
  auto* const impl = implementation.get();
  impl->failAt = 2;
  Problem problem{std::move(implementation)};
  const auto x = problem.makeVar("x");
  EXPECT_THROW(
      problem.newConstraints(
          {{x >= 0, "first"}, {x <= 1, "second"}, {x <= 2, "third"}}),
      std::runtime_error);
  EXPECT_EQ(impl->attempts, 2);
  EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 1);
}

TEST(ConstraintBatchFallbackTest, PrevalidatesRelationTypesBeforeNativeBulk) {
  lazyLoadXpress();
  Problem problem{std::make_unique<XpressProblemV2>()};
  const auto x = problem.makeVar("x");
  const Relation foreign{std::make_shared<RelationImpl>()};
  EXPECT_THROW(
      problem.newConstraints({{x >= 0, "first"}, {foreign, "foreign"}}),
      std::invalid_argument);
  EXPECT_EQ(native(x).getIntAttrib(XPRS_ORIGINALROWS), 0);
}

TEST(
    ConstraintBatchFallbackTest,
    SdkExtractionFailureUndoesRowsButConsumesDefinitions) {
  lazyLoadXpress();
  xpress::objects::XpressProblem problem;
  const auto x = problem.addVariable(0, 10, xpress::ColumnType::Continuous);
  xpress::objects::InequalityDefinition definition = x <= 1;
  const std::vector<
      xpress::objects::ConstraintDefinition<xpress::objects::Inequality>>
      requests{definition, definition};
  EXPECT_THROW(problem.addConstraints(requests), std::runtime_error);
  EXPECT_EQ(problem.getIntAttrib(XPRS_ORIGINALROWS), 0);
  EXPECT_THROW(definition.setName("consumed"), std::runtime_error);
}

TEST(
    ConstraintBatchCapabilityTest,
    BclIsNotNativeBulkButExplicitFallbackStillWorks) {
  lazyLoadXpress();
  auto problem = ProblemFactory::makeXpressProblem();
  ASSERT_NE(dynamic_cast<const XpressProblem*>(&problem.get()), nullptr);
  EXPECT_FALSE(problem.supportsConstraintBatching());
  const auto x = problem.makeVar("x");
  auto rows = problem.newConstraints({{x >= 2, "lower"}, {x <= 5, "upper"}});
  ASSERT_EQ(rows.size(), 2);
  EXPECT_DOUBLE_EQ(rows[0].getRHS(), 2);
  EXPECT_DOUBLE_EQ(rows[1].getRHS(), 5);
  problem.setObjective(x);
  problem.solve(false);
  EXPECT_NEAR(x.getValue(), 2, 1e-7);
}

TEST(
    ConstraintBatchCapabilityTest,
    TypedNativeCapabilityAndGenericDefaultAreDistinct) {
  lazyLoadXpress();
  auto typed = ProblemFactory::makeXpressV2Problem();
  EXPECT_TRUE(typed.supportsConstraintBatching());
  auto generic =
      ProblemFactory::makeGenericProblem(ProblemFactory::makeXpressV2Problem);
  EXPECT_FALSE(generic.supportsConstraintBatching());
  const ProblemImpl& impl = typed.get();
  EXPECT_TRUE(impl.supportsConstraintBatching());
}

class MalformedBatchProblem : public ScalarV2Problem {
 public:
  explicit MalformedBatchProblem(bool wrongCount) : wrongCount_(wrongCount) {}

  std::vector<std::shared_ptr<ConstraintImpl>> newConstraints(
      const ConstraintRequests& requests) override {
    auto rows = ProblemImpl::newConstraints(requests);
    if (wrongCount_) {
      rows.pop_back();
    } else {
      rows.back().reset();
    }
    return rows;
  }

 private:
  bool wrongCount_;
};

TEST(
    ConstraintBatchCapabilityTest,
    MalformedBackendErrorsIncludeCountsOrIndex) {
  lazyLoadXpress();
  for (const bool wrongCount : {false, true}) {
    Problem problem{std::make_unique<MalformedBatchProblem>(wrongCount)};
    const auto x = problem.makeVar("x");
    try {
      problem.newConstraints({{x >= 0, "first"}, {x <= 2, "second"}});
      FAIL() << "malformed backend result was accepted";
    } catch (const std::runtime_error& error) {
      EXPECT_STREQ(
          error.what(),
          wrongCount ? "backend returned 1 constraints for 2 requests"
                     : "backend returned a null constraint at request index 1");
    }
  }
}

} // namespace
} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
