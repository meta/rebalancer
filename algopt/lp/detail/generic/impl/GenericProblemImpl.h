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

#include "algopt/lp/detail/generic/impl/GenericConstraintImpl.h"
#include "algopt/lp/detail/generic/impl/GenericExpressionImpl.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/Synchronized.h>

namespace facebook::algopt::lp::detail {

class GenericProblemImpl : public ProblemImpl {
 public:
  explicit GenericProblemImpl(const std::function<Problem()>& factory);

  // `problem_` is realized in the constructor, so the backend it reports is
  // already final here -- including a HiGHS fallback.
  std::string_view backendName() const override {
    return problem_.backendName();
  }

  // Variables are created via problem interface but are not owned by a specific
  // problem and may be shared across problems
  virtual std::shared_ptr<VariableImpl> makeVar(
      const std::string& name) override;
  virtual std::shared_ptr<VariableImpl> makeIntVar(
      const std::string& name) override;
  virtual std::shared_ptr<VariableImpl> makeSemiContVar(
      const std::string& name,
      double threshold) override;
  virtual std::shared_ptr<VariableImpl> makeSemiIntVar(
      const std::string& name,
      double threshold) override;
  virtual std::shared_ptr<VariableImpl> makeBoolVar(
      const std::string& name) override;

  virtual std::shared_ptr<ExpressionImpl> makeExpression(
      double constant) const override;

  virtual std::shared_ptr<ConstraintImpl> newConstraint(
      std::shared_ptr<const RelationImpl> relation,
      const std::string& name) override;
  virtual void deleteConstraint(
      std::shared_ptr<ConstraintImpl> constraint) override;

  virtual void addObjective(
      std::shared_ptr<const ExpressionImpl> expression) override;

  virtual double getParameter(const std::string& name) override;
  virtual void setParameter(const std::string& name, double value) override;
  // What the realized problem received; parameters reach it when solve()
  // realizes the problem.
  std::map<std::string, double> getSetParameters() const override;
  std::string getSolverVersion() const override;

  virtual void setLogfile(const std::string& filename) override;
  virtual void saveToFile(const std::string& filename) override;
  virtual void saveToFileWithObjectiveAt(int pos, const std::string& filename)
      override;
  virtual void setDebugPath(const std::string& path) override;
  virtual void print(
      const std::vector<std::string>& substringsToMatch) override;
  virtual void disableLogs() override;
  virtual void setTolerances(const Tolerances& tol) override;
  virtual Tolerances getTolerances() override;
  void setSolution(thrift::ProblemResult result);
  virtual void setCallback(
      std::function<ProblemCallbackAction(ProblemCallbackData)> callback)
      override;

  virtual int getObjectiveSize() const override;
  virtual std::shared_ptr<const ExpressionImpl> getObjectiveAt(
      int pos) const override;
  virtual void clearObjectives() override;

  virtual void addStartValue(
      std::shared_ptr<const VariableImpl> variable,
      double value) override;

  std::function<Problem()> getFactory() const;
  /** realize the problem using factory (GurobiProblem or XpressProblem) and
   * call solve on it, recording resulting variable values if the solve was
   * successful **/
  virtual void solve(
      bool useSolverSpecificMultiObjectiveSolveIfAvailable) override;

  // expensive: iterates over all the constraints
  folly::F14FastSet<ConstGenericVarPtr> getVars() const;

  // Returns a copy: constraints_ is a Synchronized set that deleteConstraint()
  // can erase from concurrently, so a reference into it would only be valid
  // while the internal lock is held.
  folly::F14FastSet<std::shared_ptr<GenericConstraintImpl>> getConstraints()
      const;

  std::shared_ptr<GenericExpressionImpl> getOnlyObjective() const;

  const folly::F14FastMap<std::string, double>& getSolverParams() const;

  /** realizes the current problem into @param problem and returns a mapping of
  variables of this problem to the new problem */
  folly::F14FastMap<ConstGenericVarPtr, Variable> realize(
      Problem& problem) const;

  /* aftersolve, in case the solution is infeasible, we call this to find IIS
  constraints */
  virtual std::optional<IIS> getIIS() override;

  std::optional<std::string> getDebugPath() const;

  thrift::GenericProblem toThrift() const;
  // override the current model with that read from thrift
  void readFromThrift(const thrift::GenericProblem& problem);
  // overwrite the current model with one that's read from the file
  void loadFromFile(const std::string& fileName);
  // replay the model read from the file, without overriding the current model
  virtual void replay(const std::string& fileName) const override;

 protected:
  virtual void solveForObjectiveAt(int pos, std::optional<double> timeLimit)
      override;

  virtual void multiObjectiveSolve() override;
  virtual void saveProblemResultAfterSolve() override;
  virtual void addNewConstraintForObjectiveAt(int pos) override;
  virtual void solveForObjectiveAndSaveResult(
      int pos,
      std::optional<double> timeLimit) override;

  virtual thrift::ProblemStatus getSolveStatus() override;
  virtual thrift::ProblemResult getSolveResult() override;
  virtual thrift::ProblemAttributes getProblemAttributes() override;

 protected:
  folly::Synchronized<folly::F14FastSet<std::shared_ptr<GenericConstraintImpl>>>
      constraints_;

  // NOTE: none of the data structures below are thread-safe

  // The order in objectives_ denotes priority and so it is important.
  // In particular, i-th expression in objectives_  is considered to
  // be a higher priority objective than the (i+1)-th expression in
  // objectives_
  std::vector<std::shared_ptr<GenericExpressionImpl>> objectives_;

  // following are used to create subproblems as requested
  folly::F14FastMap<std::string, double> params_;
  std::optional<Tolerances> tolerances_ = std::nullopt;
  std::optional<std::string> logFile_;
  bool disableLogs_ = false;
  std::optional<std::function<ProblemCallbackAction(ProblemCallbackData)>>
      callback_;
  std::optional<std::string> debugPath_;
  std::function<Problem()> factory_;
  // underlying Xpress or Gurobi problem
  algopt::lp::Problem problem_;
};

} // namespace facebook::algopt::lp::detail
