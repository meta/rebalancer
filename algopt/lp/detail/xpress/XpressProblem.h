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

#include "algopt/lp/detail/xpress/xpress.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <folly/container/F14Map.h>

#include <optional>

namespace facebook::algopt::lp::detail {

class XpressProblem : public ProblemImpl {
 public:
  // True when the last solve() reached an optimal LP rather than a MIP,
  // and no row, column or objective has been changed since.
  // XpressConstraint / XpressVariable consult this before returning duals
  // or reduced costs.
  bool hasLpSensitivity() const {
    return solvedAsLp_ && !modifiedSinceSolve_ &&
        XPRBgetlpstat(problem_.getCRef()) == XPRB_LP_OPTIMAL;
  }

  void markModified() {
    modifiedSinceSolve_ = true;
  }

  XpressProblem();

  std::string_view backendName() const override {
    return "XPRESS";
  }

  std::shared_ptr<VariableImpl> makeVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeIntVar(const std::string& name) override;
  std::shared_ptr<VariableImpl> makeSemiContVar(
      const std::string& name,
      double threshold) override;
  std::shared_ptr<VariableImpl> makeSemiIntVar(
      const std::string& name,
      double threshold) override;
  std::shared_ptr<VariableImpl> makeBoolVar(const std::string& name) override;

  std::shared_ptr<ExpressionImpl> makeExpression(
      double constant) const override;

  std::shared_ptr<ConstraintImpl> newConstraint(
      std::shared_ptr<const RelationImpl> relation,
      const std::string& name) override;
  void deleteConstraint(std::shared_ptr<ConstraintImpl> constraint) override;

  virtual void addObjective(
      std::shared_ptr<const ExpressionImpl> expression) override;

  double getParameter(const std::string& name) override;
  void setParameter(const std::string& name, double value) override;
  void setTolerances(const Tolerances& tol) override;
  Tolerances getTolerances() override;

  void setLogfile(const std::string& filename) override;
  void saveToFile(const std::string& filename) override;
  // this ensures that the objective at pos is added before the model is written
  // to a file
  void saveToFileWithObjectiveAt(int pos, const std::string& filename) override;
  void saveToFileWithAllObjectives(const std::string& filename) override;
  void setDebugPath(const std::string& path) override;
  void print(const std::vector<std::string>& substringsToMatch) override;
  void disableLogs() override;

  virtual int getObjectiveSize() const override;
  virtual std::shared_ptr<const ExpressionImpl> getObjectiveAt(
      int pos) const override;
  virtual void clearObjectives() override;

  void addStartValue(std::shared_ptr<const VariableImpl> variable, double value)
      override;

  void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback)
      override;
  std::optional<IIS> getIIS() override;
  void replay(const std::string& fileName) const override;

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
  virtual void solveForObjectiveAt(int pos, std::optional<double> timeLimit)
      override;
  // getSolveStatus() and getSolveResult() provide the status and result,
  // respectively, after each solve with an objective
  virtual thrift::ProblemStatus getSolveStatus() override;
  virtual thrift::ProblemResult getSolveResult() override;
  virtual thrift::ProblemAttributes getProblemAttributes() override;

 private:
  // The order in objectives_ denotes priority and so it is important.
  // In particular, i-th expression in objectives_  is considered to
  // be a higher priority objective than the (i+1)-th expression in
  // objectives_
  std::vector<std::shared_ptr<const ExpressionImpl>> objectives_;

  void setObjective(std::shared_ptr<const ExpressionImpl> expression);
  void setTimeout(double solveTime);
  void setMultiObjectiveParameter(int pos);

  enum DebugPhase {
    PreWarmStart,
    PostWarmStart,
    PreMain,
    PostMain,
  };

  static std::optional<int> getIntParamCode(const std::string& name);
  static std::optional<int> getDoubleParamCode(const std::string& name);

  static thrift::WarmStartStatus getWarmStartStatus(int32_t processingStatus);
  static void callbackForInitialSolution(
      XPRSprob xprsProblem,
      void* warmStartResultData,
      const char* solutionName,
      int32_t processingStatus);
  static int customCallback(XPRSprob xprsProblem, void* data);
  void retainNativeColumn(const VariableImpl& variable);
  bool usesNativeExpressions() const;
  void clearNativeConstraints(XPRSprob xprob);
  void applyNativeConstraints(XPRSprob xprob);
  void applyNativePwlConstraints(XPRSprob xprob);
  void applyNativeMaxConstraints(XPRSprob xprob);
  void addMipSolFromInitialValues();
  int callbackImpl(XPRSprob& prob);

  std::shared_ptr<VariableImpl>
  makeVar(const std::string& name, int xprbType, double lb);
  std::string getAlgorithm();
  bool isLoggingDisabled() const;
  void saveDebugData(DebugPhase phase);

  // Specs for native PWL/Max constraints deferred until after loadMat().
  // XPRSaddpwlcons / XPRSaddgencons need column indices that are only valid
  // after XPRBloadmat. The lists persist across multi-objective solves.
  // clearNativeConstraints() removes our previous additions before BCL reloads
  // the matrix, and applyNativeConstraints() uses the new column indices.
  struct NativePwlSpec {
    std::shared_ptr<VariableImpl> xVar; // auxiliary BCL input variable
    std::shared_ptr<VariableImpl> yVar; // BCL result variable
    std::vector<std::pair<double, double>> points;
  };
  struct NativeMaxSpec {
    std::shared_ptr<VariableImpl> resultVar;
    std::vector<std::shared_ptr<VariableImpl>> inputVars;
  };
  std::optional<dashoptimization::XPRBctr> nativeColumns_;
  std::vector<NativePwlSpec> nativePwlConstraints_;
  std::vector<NativeMaxSpec> nativeMaxConstraints_;
  // Count and starting index of PWL / general constraints we registered in the
  // last applyNativeConstraints() call. The starting index is recorded before
  // each add (via XPRS_PWLCONS / XPRS_GENCONS attribute queries) so that
  // deletion targets exactly our additions even if other code paths have added
  // constraints before ours.
  int nOwnedPwlCons_{0};
  int nOwnedGenCons_{0};
  int ownedPwlConsStartIdx_{0};
  int ownedGenConsStartIdx_{0};

  dashoptimization::XPRBprob problem_;
  folly::F14FastMap<std::shared_ptr<const VariableImpl>, double> initialValues_;
  std::optional<thrift::WarmStartResult> warmStartResult_;
  Timer timer_;
  bool solvedAsLp_ = false;
  bool modifiedSinceSolve_ = false;
  bool callbackAborted_ = false;
  bool loggingDisabled_ = false;

  std::optional<std::function<ProblemCallbackAction(ProblemCallbackData)>>
      callback_;
  std::optional<std::string> debugPath_;
};

} // namespace facebook::algopt::lp::detail

#endif
