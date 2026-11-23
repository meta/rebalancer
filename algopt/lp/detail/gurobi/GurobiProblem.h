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

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/generic/impl/thrift/gen-cpp2/model_types.h"
#include "algopt/lp/detail/gurobi/gurobi.h"
#include "algopt/lp/fast/FastProblemImpl.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <map>
#include <string>
#include <string_view>

namespace facebook::algopt::lp::detail {

enum GurobiWarmStartType {
  // user provided variable hints - https://fburl.com/0a8tvkub
  VARIABLE_HINT = 0,
  // initial feasible solution for the MIP - https://fburl.com/46yb76zi
  MIP_START = 1,
  // set the initial feasible solution as both hint and start
  VARIABLE_HINT_AND_MIP_START = 2
};

class GurobiProblem : public ProblemImpl {
 public:
  explicit GurobiProblem();

  std::string_view backendName() const override {
    return "GUROBI";
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
  std::map<std::string, double> getSetParameters() const override;
  std::string getSolverVersion() const override;
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

  // Extracts the model directly from Gurobi's native representation into thrift
  // format, avoiding the intermediate GenericProblemImpl layer.
  lp::thrift::GenericProblem toThrift() const;

  // Bulk-loads variables, constraints, and objectives from a FastProblemImpl.
  void loadFromFastProblem(const FastProblemImpl& fast);

  struct LpShadowPrices {
    // Original Gurobi row order.
    std::vector<double> shadowPrices;
    double objectiveValue = 0;
  };

  struct LpShadowPricesSnapshot {
    std::vector<std::string> constraintNames;
    // Each objective's values use the original Gurobi row order.
    std::vector<LpShadowPrices> objectives;
  };
  void enableLpShadowPrices() {
    collectLpShadowPrices_ = true;
  }
  std::optional<LpShadowPricesSnapshot> takeLpShadowPrices();

  // Returns a map of variable name -> solved value for all Gurobi variables.
  folly::F14FastMap<std::string, double> getSolvedVariableValues() const;

  // Returns the post-presolve nonzero count parsed from a Gurobi log message
  // of the form "Presolved: R rows, C columns, N nonzeros", or `current` if
  // the message is not such a line or a count was already recorded (`current`
  // >= 0). Only the first line counts, so the value describes the same
  // presolve as the cumulative row/column deletion counters.
  static int updatePresolvedNonZeros(int current, std::string_view message);

 protected:
  virtual void solveForObjectiveAt(int pos, std::optional<double> timeLimit)
      override;
  // getSolveStatus() and getSolveResult() provide the status and result,
  // respectively, after each solve with an objective
  virtual thrift::ProblemStatus getSolveStatus() override;
  virtual thrift::ProblemResult getSolveResult() override;
  virtual thrift::ProblemAttributes getProblemAttributes() override;
  virtual std::optional<uint32_t> getModelFingerprint() override;
  std::optional<double> getScaledMaxCoefRatio() override;
  std::optional<NumericalStabilityInfo> getNumericalStability(
      bool computeExactKappa) override;

 private:
  // The order in objectives_ denotes priority and so it is important.
  // In particular, i-th expression in objectives_  is considered to
  // be a higher priority objective than the (i+1)-th expression in
  // objectives_
  std::vector<std::shared_ptr<const ExpressionImpl>> objectives_;

  void multiObjectiveSolve() override;
  void captureLpShadowPrices(int pos);
  void solveModel(std::optional<double> timeLimit);
  void setObjective(std::shared_ptr<const ExpressionImpl> expression);
  void setMultiObjective();
  void setTimeout(double solveTime);
  // Sets a parameter on the environment of the multi-objective objective at
  // `index`. Unknown names are ignored.
  void setObjectiveParameter(
      GRBEnv& env,
      int index,
      const std::string& name,
      double value);
  void setMultiObjectiveParameter(
      int pos,
      std::optional<GRBEnv*> env = std::nullopt);

  enum DebugPhase {
    Pre,
    Post,
  };

  static bool isValidHiddenParam(const std::string& name);
  static std::optional<GRB_IntParam> getIntParamCode(const std::string& name);
  static std::optional<GRB_DoubleParam> getDoubleParamCode(
      const std::string& name);
  static std::optional<GurobiWarmStartType> getGurobiWarmStartType(
      int32_t value);

  void saveDebugData(DebugPhase phase);

  GRBVar
  addVar(double lb, double ub, double obj, char vtype, std::string vname);

  // Gurobi's C++ API is not const-correct: read-only methods like getRow() and
  // getQCRow() are non-const. We mark model_ mutable so that toThrift() can be
  // a const method.
  mutable GRBModel model_;
  GurobiWarmStartType warmStartType_;
  // Stores only the parameters that are explicitly set during a run.
  std::map<std::string, double> setParamNameToValue_;

  // Variable names in Gurobi (load) order, captured by loadFromFastProblem so
  // getSolvedVariableValues can zip values with names without a second
  // GRB_StringAttr_VarName fetch (which would allocate one std::string per
  // variable -- millions on large models). Consumed (moved out) on readback.
  // Mutable so the const readback can drain it. Tradeoff: the names are held
  // for the duration of the solve, duplicating Gurobi's internal copy.
  mutable std::vector<std::string> loadedVarNames_;
  bool collectLpShadowPrices_ = false;
  std::optional<LpShadowPricesSnapshot> lpShadowPrice_;

  class CallbackWrapper : public GRBCallback {
   public:
    explicit CallbackWrapper(
        std::function<ProblemCallbackAction(ProblemCallbackData)> callback);
    void callback() override;
    int getPresolveColDel() const;
    int getPresolveRowDel() const;
    int getPresolveNnz() const;

   private:
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback_;
    double bound_;
    double objective_;
    Timer timer_;
    bool aborted_;
    int presolveColDel_;
    int presolveRowDel_;
    int presolveNnz_;
  };

  std::optional<std::function<ProblemCallbackAction(ProblemCallbackData)>>
      callback_;
  std::optional<std::string> debugPath_;
  int presolveColDel_ = 0;
  int presolveRowDel_ = 0;
  int presolveNnz_ = -1;
};

} // namespace facebook::algopt::lp::detail

#endif
