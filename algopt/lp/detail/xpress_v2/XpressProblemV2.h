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
#include "algopt/lp/detail/xpress_v2/XpressV2Types.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace facebook::algopt::lp::detail {

class XpressV2NameBuffer {
 public:
  explicit XpressV2NameBuffer(xpress::XPRSProblem* problem);
  bool canDefer(const std::string& name) const;
  void queueColumn(int index, std::string name);
  void queueRow(int index, std::string name);
  void flush();
  void expose();
  void detach();

 private:
  xpress::XPRSProblem* problem_;
  bool exposed_{false};
  std::vector<std::optional<std::string>> pendingColumnNames_;
  std::vector<std::optional<std::string>> pendingRowNames_;
  int firstPendingColumnName_{0};
  int firstPendingRowName_{0};
};

// Xpress backend, built on the Xpress 9.9.0 typed C++ API.
//
// The model is constructed directly in xpress::objects. Native general and PWL
// constraints take Variable objects, so a constraint can be registered the
// moment it is described -- there is no separate matrix-load step to sequence
// against and no column indices to resolve by hand.
//
// xpress_v2.h documents the include-order contract this layer has to honour.
class XpressProblemV2 : public ProblemImpl {
 public:
  XpressProblemV2();
  XpressProblemV2(const XpressProblemV2&) = delete;
  XpressProblemV2& operator=(const XpressProblemV2&) = delete;
  XpressProblemV2(XpressProblemV2&&) = delete;
  XpressProblemV2& operator=(XpressProblemV2&&) = delete;
  ~XpressProblemV2() override;

  std::string_view backendName() const override {
    return "XPRESS";
  }

  bool hasLpSensitivity() const;
  void markModified();

  std::shared_ptr<VariableImpl> makeVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeIntVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeBoolVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeSemiContVar(
      const std::string& name,
      double threshold) override;
  std::shared_ptr<VariableImpl> makeSemiIntVar(
      const std::string& name,
      double threshold) override;

  std::shared_ptr<ExpressionImpl> makeExpression(
      double constant) const override;

  std::shared_ptr<ConstraintImpl> newConstraint(
      std::shared_ptr<const RelationImpl> relation,
      const std::string& name) override;
  std::vector<std::shared_ptr<ConstraintImpl>> newConstraints(
      const ConstraintRequests& requests) override;
  bool supportsConstraintBatching() const override {
    return true;
  }
  void deleteConstraint(std::shared_ptr<ConstraintImpl> constraint) override;

  void addObjective(std::shared_ptr<const ExpressionImpl> expression) override;
  int getObjectiveSize() const override;
  void clearObjectives() override;
  std::shared_ptr<const ExpressionImpl> getObjectiveAt(int pos) const override;

  double getParameter(const std::string& name) override;
  void setParameter(const std::string& name, double value) override;
  void setLogfile(const std::string& filename) override;
  void saveToFile(const std::string& filename) override;
  void saveToFileWithObjectiveAt(int pos, const std::string& filename) override;
  void saveToFileWithAllObjectives(const std::string& filename) override;
  void setDebugPath(const std::string& path) override;
  void print(const std::vector<std::string>& substringsToMatch) override;
  void disableLogs() override;

  void setTolerances(const Tolerances& tol) override;
  Tolerances getTolerances() override;

  void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback)
      override;
  void addStartValue(std::shared_ptr<const VariableImpl> variable, double value)
      override;
  std::optional<IIS> getIIS() override;

  thrift::ProblemAttributes getProblemAttributes() override;

  bool supportsNativeQuadratic() const override;
  bool supportsNativePwl() const override;
  bool supportsNativeMax() const override;
  bool supportsIndicatorConstraints() const override;
  bool setIndicatorOnConstraint(
      Constraint& ctr,
      const Variable& binaryVar,
      int dir) override;
  std::optional<Expression> addNativePwlConstraint(
      const Expression& x,
      const std::vector<std::pair<double, double>>& points) override;
  std::optional<Expression> addNativeMaxConstraint(
      const std::vector<Expression>& inputs) override;

 protected:
  void solveForObjectiveAt(int pos, std::optional<double> timeLimit) override;
  thrift::ProblemStatus getSolveStatus() override;
  thrift::ProblemResult getSolveResult() override;

 private:
  enum class DebugPhase { PreWarmStart, PostWarmStart, PreMain, PostMain };

  std::shared_ptr<VariableImpl>
  makeTypedVar(const std::string& name, VariableImpl::Type type, double lb);

  // Returns non-zero to abort the solve.
  int callbackImpl(xpress::objects::XpressProblem& prob);

  // Writes the model and the solver state to setDebugPath()'s directory. A
  // no-op until a debug path is set.
  void saveDebugData(DebugPhase phase);

  // Xpress rejects duplicate column names at solve time, and callers do reuse
  // names, so uniquify on collision rather than failing the solve. Unnamed
  // variables return nullopt so Xpress auto-names them --
  // the common case by far (Problem::makeVar defaults to ""), and tracking a
  // million synthesised names would cost more than the model itself.
  static std::optional<std::string> uniqueName(
      const std::string& name,
      folly::F14FastSet<std::string>& usedNames);

  void applyConstraintName(
      xpress::objects::Inequality& inequality,
      const std::string& name);

  // Reads the solved column values into solution_ so expressions created by
  // this problem can evaluate themselves. See XpressV2SolutionSlot.
  void publishSolution();
  void flushNames();

  // Pushes accumulated warm-start hints into the model before a solve.
  void applyStartValues();

  static thrift::WarmStartStatus toWarmStartStatus(int processingStatus);

  // Creates an auxiliary variable pinned to `expression` and returns it, so a
  // general constraint (which takes Variables) can consume an arbitrary
  // expression. Handles pure constants, whose ExpressionImpl is null.
  xpress::objects::Variable pinToVariable(
      const Expression& expression,
      const std::string& name);

  xpress::objects::XpressProblem problem_;
  std::shared_ptr<XpressV2NameBuffer> names_{
      std::make_shared<XpressV2NameBuffer>(&problem_)};

  // Shared with every variable and expression this problem creates.
  XpressV2SolutionSlot solution_{std::make_shared<std::vector<double>>()};

  // Objectives are held until solve time; index order is priority order.
  std::vector<std::shared_ptr<const ExpressionImpl>> objectives_;

  bool logsDisabled_{false};
  bool solvedAsLp_{false};
  bool modifiedSinceSolve_{true};
  std::string debugPath_;

  folly::F14FastSet<std::string> usedNames_;
  folly::F14FastSet<std::string> usedConstraintNames_;
  folly::F14FastMap<std::string, std::string> originalVariableNames_;
  folly::F14FastMap<std::string, std::string> originalConstraintNames_;

  // Warm-start hints, applied at solve time via addMipSol.
  folly::F14FastMap<std::shared_ptr<const VariableImpl>, double> initialValues_;

  std::optional<thrift::WarmStartResult> warmStartResult_;

  std::function<ProblemCallbackAction(ProblemCallbackData)> callback_;
  bool callbackAborted_{false};
  facebook::algopt::Timer timer_;
};

} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
