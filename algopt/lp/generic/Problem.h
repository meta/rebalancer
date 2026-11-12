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

#include "algopt/lp/generic/Constraint.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Relation.h"
#include "algopt/lp/generic/thrift/gen-cpp2/problem_types.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/algopt_common/Timer.h"
#include <algopt/rebalancer/algopt_common/Concepts.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace facebook::algopt::lp {

// Sparse matrix entry for scaled max coefficient ratio computation.
// Used by solver backends (Gurobi, Xpress) to pass extracted matrix data
// to the common computeScaledMaxCoefRatio() function.
struct CoefficientEntry {
  int row;
  int col;
  double value; // must be positive (absolute value of coefficient)
};

// Computes the scaled max coefficient ratio via row/column equilibration.
// Takes sparse matrix entries, the number of rows, and the number of columns.
// Scales each row and column by its max coefficient, then returns the
// max/min ratio of the resulting coefficients.
std::optional<double> computeScaledMaxCoefRatio(
    std::vector<CoefficientEntry>& entries,
    int numRows,
    int numCols);

// Summary of numerical stability of an optimization problem.
struct NumericalStabilityInfo {
  double smallestConstraintCoef = 0.0;
  double largestConstraintCoef = 0.0;
  double smallestObjCoef = 0.0;
  double largestObjCoef = 0.0;
  double smallestRhs = 0.0;
  double largestRhs = 0.0;
  double smallestBound = 0.0;
  double largestBound = 0.0;
  double normalKappa = 0.0;
  double exactKappa = 0.0;
};

// constraint and integer tolerances in accordance with
// xpress_tolerance set in packer.thrift
struct Tolerances {
  double constraint = 2e-9; // XPRS_FEASTOL
  double integer = 1e-08; // XPRS_MIPTOL
  double absgap = 0; // XPRS_MIPABSSTOP
  double relgap = 0; // XPRS_MIPRELSTOP
  double abscut = 0; // XPRS_MIPADDCUTOFF
  double relcut = 0; // XPRS_MIPRELCUTOFF
};

// This is data passed to a callback that you can specify for Problem.
struct ProblemCallbackData {
  // Current best bound
  const double bound;
  // Current best objective
  const double objective;
  // Time in seconds since solve started
  const double walltime;
  // Difference between objective and bound
  double gap() const {
    return std::max(0.0, objective - bound);
  }
};

// These are actions that the callback can return, currently just continue
// or abort the solve.
enum ProblemCallbackAction {
  CONTINUE,
  ABORT,
};

// In case of infeasibility, the solver provides IIS
// information, containing a set of conflicting constraints,
// upper/lower bounds for variables.
struct IIS {
  std::vector<std::string> constraintIds;
  std::vector<std::string> upperBoundVars;
  std::vector<std::string> lowerBoundVars;
};

// This is to store the per objective parameters for the multi-objective solve
struct MultiObjConfig {
  folly::F14FastMap<std::string, algopt::common::thrift::PerObjectiveValue>
      paramNamesToValues;
  std::optional<algopt::common::thrift::HigherPriorityObjectivesConfig>
      higherPriObjConfig;
};

class ProblemImpl {
 public:
  ProblemImpl() = default;

  ProblemImpl(const ProblemImpl&) = default;
  ProblemImpl(ProblemImpl&&) = default;
  ProblemImpl& operator=(const ProblemImpl&) = default;
  ProblemImpl& operator=(ProblemImpl&&) = default;
  virtual ~ProblemImpl();

  // Concrete solver backend backing this problem ("GUROBI", "XPRESS",
  // "HIGHS", ...). Wrappers report the backend they delegate to, so this
  // reflects a fallback (see ProblemFactory::loadWithFallback) rather
  // than whatever backend was originally requested.
  virtual std::string_view backendName() const = 0;

  virtual std::shared_ptr<VariableImpl> makeVar(const std::string& name) = 0;
  virtual std::shared_ptr<VariableImpl> makeIntVar(const std::string& name) = 0;
  virtual std::shared_ptr<VariableImpl> makeSemiContVar(
      const std::string& name,
      double threshold) = 0;
  virtual std::shared_ptr<VariableImpl> makeSemiIntVar(
      const std::string& name,
      double threshold) = 0;
  virtual std::shared_ptr<VariableImpl> makeBoolVar(
      const std::string& name) = 0;

  virtual std::shared_ptr<ExpressionImpl> makeExpression(
      double constant) const = 0;

  virtual std::shared_ptr<ConstraintImpl> newConstraint(
      std::shared_ptr<const RelationImpl> relation,
      const std::string& name) = 0;
  virtual void deleteConstraint(std::shared_ptr<ConstraintImpl> constraint) = 0;

  // addObjective always *add* objectives to an existing list of objectives.
  virtual void addObjective(
      std::shared_ptr<const ExpressionImpl> expression) = 0;

  virtual void solve(bool useSolverSpecificMultiObjectiveSolveIfAvailable);

  virtual thrift::ProblemStatus getStatus();
  virtual std::vector<thrift::ProblemResult> getResultPerObjective();
  virtual thrift::ProblemResult getOnlyResult();

  virtual double getParameter(const std::string& name) = 0;
  virtual void setParameter(const std::string& name, double value) = 0;
  virtual void setLogfile(const std::string& filename) = 0;
  virtual void saveToFile(const std::string& filename) = 0;
  // this ensures that the objective at pos is added before the model is written
  // to a file
  virtual void saveToFileWithObjectiveAt(
      int pos,
      const std::string& filename) = 0;
  // Writes the model to a file with all objectives previously buffered via
  // setObjective(vector) installed on the underlying solver model. Required
  // when the caller never invokes solve(), because some backends (Gurobi,
  // Xpress) defer pushing objectives to the native model until solve time --
  // calling saveToFile() directly would emit a model with no objective.
  // For multi-objective Gurobi models this preserves the full hierarchy via
  // setObjectiveN(); Xpress only holds one objective at a time so the
  // highest-priority objective is installed.
  // Default implementation falls back to saveToFile() for backends that
  // install objectives eagerly.
  virtual void saveToFileWithAllObjectives(const std::string& filename);
  virtual void setDebugPath(const std::string& path) = 0;

  virtual void print(const std::vector<std::string>& substringsToMatch) = 0;

  virtual void disableLogs() = 0;
  virtual void setMaxSolveTime(double solveTime);
  virtual void setTolerances(const Tolerances& tol) = 0;
  virtual Tolerances getTolerances() = 0;
  virtual void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback) = 0;

  virtual void addStartValue(
      std::shared_ptr<const VariableImpl> variable,
      double value) = 0;

  virtual void replay(const std::string& fileName) const;

  virtual std::optional<IIS> getIIS() = 0;

  virtual int getObjectiveSize() const = 0;
  virtual void clearObjectives() = 0;
  virtual std::shared_ptr<const ExpressionImpl> getObjectiveAt(
      int pos) const = 0;

  virtual thrift::ProblemAttributes getProblemAttributes() = 0;
  virtual std::optional<uint32_t> getModelFingerprint();

  // Extracts the constraint matrix and computes the scaled max coefficient
  // ratio via computeScaledMaxCoefRatio(). Default returns nullopt.
  virtual std::optional<double> getScaledMaxCoefRatio();

  // Native solver constraint extensions. Default implementations return
  // false/nullopt so backends that lack support fall through to approximations.

  // Returns true if the backend supports native quadratic expressions
  // (i.e., multiplying two LP expressions directly without auxiliary
  // variables). Default false.
  virtual bool supportsNativeQuadratic() const;

  // Returns true if the backend supports native piecewise-linear constraints
  // (see addNativePwlConstraint). Default false.
  virtual bool supportsNativePwl() const;

  // Returns true if the backend supports native MAX constraints
  // (see addNativeMaxConstraint). Default false.
  virtual bool supportsNativeMax() const;

  // Returns true if the backend supports indicator constraints (see
  // setIndicatorOnConstraint). Lets callers commit to the indicator-based
  // formulation before adding any constraints. Default false.
  virtual bool supportsIndicatorConstraints() const;

  // Sets ctr as an indicator constraint: binaryVar == dir => ctr is active.
  // Returns false if unsupported.
  virtual bool
  setIndicatorOnConstraint(Constraint& ctr, const Variable& binaryVar, int dir);

  // Adds a native PWL constraint y = PWL(x) over breakpoints.
  // Returns the y expression, or nullopt if unsupported.
  virtual std::optional<Expression> addNativePwlConstraint(
      const Expression& x,
      const std::vector<std::pair<double, double>>& points);

  // Adds a native MAX constraint result = max(inputs).
  // Returns the result expression, or nullopt if unsupported.
  virtual std::optional<Expression> addNativeMaxConstraint(
      const std::vector<Expression>& inputs);

  // Extracts numerical stability information from the solved model.
  // Default returns nullopt.
  virtual std::optional<NumericalStabilityInfo> getNumericalStability(
      bool computeExactKappa);

  // this is used to store the problem result per objective
  void setMultiObjConfig(MultiObjConfig config);

 protected:
  // main method for multi-objective solves; by default it calls
  // customMultiObjectiveIterativeSolve(), but override it to use solver's
  // native support (e.g., currently done for Gurobi)
  virtual void multiObjectiveSolve();
  virtual void saveProblemResultAfterSolve();
  virtual void addNewConstraintForObjectiveAt(int pos);
  virtual void solveForObjectiveAndSaveResult(
      int pos,
      std::optional<double> timeLimit);
  virtual void solveForObjectiveAt(
      int pos,
      std::optional<double> timeLimit) = 0;
  void saveFinalSolveStatus();

  // getSolveStatus() and getSolveResult() provide the status and result,
  // respectively, after each solve with an objective
  virtual thrift::ProblemStatus getSolveStatus() = 0;
  virtual thrift::ProblemResult getSolveResult() = 0;

  // this is custom implementation that does NOT using Gurobi's or Xpress's
  // native support for multi-objective solves.
  void customMultiObjectiveIterativeSolve();

  std::vector<thrift::ProblemResult> problemResultPerObjective_;
  std::optional<thrift::ProblemStatus> status_ = std::nullopt;

  // timer used to keep track of solve time when using
  // customMultiObjectiveIterativeSolve()
  facebook::algopt::Timer iterativeSolveTimer;
  std::optional<double> maxSolveTime_;

  // storting parameters and time limits for the multi-objective solve
  // per objective
  std::optional<MultiObjConfig> multiObjConfig_;

 public:
  using ConstraintRequests =
      std::vector<std::pair<std::shared_ptr<const RelationImpl>, std::string>>;

  // Synchronous, ordered construction. Backends without bulk insertion use
  // newConstraint for each request. See Problem::newConstraints for the
  // exception contract; in particular, this fallback can retain a valid prefix.
  virtual std::vector<std::shared_ptr<ConstraintImpl>> newConstraints(
      const ConstraintRequests& requests);

  // Native construction batching is an opt-in capability, not backend
  // selection. The explicit bulk API remains available through scalar fallback.
  virtual bool supportsConstraintBatching() const;
};

class Problem {
 public:
  Problem() = delete;
  explicit Problem(std::unique_ptr<ProblemImpl> problem);

  // Prevent copies
  Problem(const Problem& problem) = delete;
  Problem& operator=(const Problem& problem) = delete;
  // Other operators are as usual.
  Problem(Problem&& problem) noexcept = default;
  Problem& operator=(Problem&& problem) = default;
  ~Problem() = default;

  // See ProblemImpl::backendName().
  std::string_view backendName() const;

  Variable makeVar(const std::string& name = "");
  Variable makeIntVar(const std::string& name = "");
  Variable makeSemiContVar(const std::string& name = "", double threshold = 0);
  Variable makeSemiIntVar(const std::string& name = "", double threshold = 0);
  Variable makeBoolVar(const std::string& name = "");

  static Expression makeExpression(double constant = 0);

  using ConstraintRequests = std::vector<std::pair<Relation, std::string>>;

  Constraint newConstraint(
      const Relation& relation,
      const std::string& name = "");

  // Constructs completed requests synchronously and returns real handles in
  // request order. Empty names have the same meaning as in newConstraint.
  // No handle or insertion is deferred beyond this call. Access to the same
  // model must be serialized, including other model/handle/native operations.
  // Backends may deliberately use scalar construction for unsupported bulk
  // representations without selecting a different backend.
  //
  // Basic exception guarantee: on ANY exception discard the entire Problem;
  // do not retry the batch or continue using the model. Backends may retain a
  // valid prefix or undo native rows, but extraction, callbacks and naming
  // state need not roll back. This is not scalar failure equivalence.
  std::vector<Constraint> newConstraints(const ConstraintRequests& requests);

  // True only for backends that implement native construction batching.
  bool supportsConstraintBatching() const;

  void deleteConstraint(Constraint& constraint);

  // setObjective() always *sets* an objective---meaning all the previously
  // added objectives are cleared.
  // When setObjective() is used with a vector, the order in `objectiveTuple`
  // denotes priority and so it is important. In particular, i-th expression in
  // `objectiveTuple`  is considered to be a higher priority objective than the
  // (i+1)-th expression in `objectiveTuple`
  void setObjective(const Expression& objective);
  void setObjective(const std::vector<Expression>& objectiveTuple);

  void solve(bool useSolverSpecificMultiObjectiveSolveIfAvailable = true);

  thrift::ProblemStatus getStatus();
  std::vector<thrift::ProblemResult> getResultPerObjective();
  thrift::ProblemResult getOnlyResult();
  thrift::ProblemAttributes getProblemAttributes();
  // return modelfingerprint if available and uniquely identifies model
  std::optional<uint32_t> getModelFingerprint();
  // Returns the scaled max coefficient ratio of the constraint matrix.
  std::optional<double> getScaledMaxCoefRatio();
  // Returns numerical stability information from the solved model.
  std::optional<NumericalStabilityInfo> getNumericalStability(
      bool computeExactKappa);

  double getParameter(const std::string& name);
  void setParameter(const std::string& name, double value);

  void setTolerances(const Tolerances& tol);
  Tolerances getTolerances();

  void setLogfile(const std::string& filename);
  void saveToFile(const std::string& filename);
  // this ensures that the objective at pos is added before the model is written
  // to a file
  void saveToFileWithObjectiveAt(int pos, const std::string& filename);
  // Writes the model with all objectives buffered via setObjective(vector)
  // installed. Use this instead of saveToFile() when the caller never invokes
  // solve(), since Gurobi/Xpress only push objectives to the native model at
  // solve time. For Gurobi multi-objective models the full setObjectiveN()
  // hierarchy is preserved; Xpress installs only the highest-priority
  // objective (its native model holds one objective at a time).
  void saveToFileWithAllObjectives(const std::string& filename);
  void setDebugPath(const std::string& path);

  // The following print method can be used to examine the solver-specific
  // internal representation of the model. It is generally used to investigate
  // constraints that are numerically sensitive, so one can specify a list of
  // allowed substrings and only print those constraints which contain these
  // allowed substrings.
  void print(const std::vector<std::string>& substringsToMatch = {});

  void disableLogs();
  void setMaxSolveTime(double solveTime);

  template <typename NameToObjectiveParams>
  void setMultiObjConfig(const NameToObjectiveParams& multiObjectiveParams)
    requires IsIterableOverPairs<
        NameToObjectiveParams,
        std::string,
        algopt::common::thrift::PerObjectiveValue>;

  void setMultiObjConfig(MultiObjConfig config);

  int getObjectiveSize();
  std::shared_ptr<const ExpressionImpl> getObjectiveAt(int pos);

  // replays the MIP model saved as fileName, the existing model will
  // not be affected
  void replay(const std::string& fileName) const;

  void addStartValue(const Variable& variable, double value);

  // This callback is called frequently throughout the solve, the frequency
  // depends somewhat on the solver. Thus, the callback should strive to be
  // efficient to perform its purpose. Note that once a callback returns
  // ABORT, it is guaranteed not to be called again for that solve.
  //
  // Potential use-cases:
  //  1) Abort early based on current gap and time
  //  2) Abort early by looking at current memory pressure
  //  3) Log to a live monitoring like ods for current progress
  //  4) Record gap over time and log later to scuba
  //  5) Implement API on service to complete solve with its current solution
  void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback);

  std::optional<IIS> getIIS();

  const ProblemImpl& get() const;

  // Native solver constraint extensions. Forward to the underlying ProblemImpl.
  bool supportsNativeQuadratic() const;
  bool supportsNativePwl() const;
  bool supportsNativeMax() const;
  bool supportsIndicatorConstraints() const;
  bool
  setIndicatorOnConstraint(Constraint& ctr, const Variable& binaryVar, int dir);
  std::optional<Expression> addNativePwlConstraint(
      const Expression& x,
      const std::vector<std::pair<double, double>>& points);
  std::optional<Expression> addNativeMaxConstraint(
      const std::vector<Expression>& inputs);

 private:
  std::unique_ptr<ProblemImpl> problem_;
  // addObjective always *add* objectives to an existing list of objectives.
  void addObjective(const Expression& objective);
};

template <typename NameToObjectiveParams>
void Problem::setMultiObjConfig(
    const NameToObjectiveParams& multiObjectiveParams)
  requires IsIterableOverPairs<
      NameToObjectiveParams,
      std::string,
      algopt::common::thrift::PerObjectiveValue>
{
  MultiObjConfig config;
  for (auto& [name, values] : multiObjectiveParams) {
    config.paramNamesToValues[name] = std::move(values);
  }
  problem_->setMultiObjConfig(std::move(config));
}

} // namespace facebook::algopt::lp
