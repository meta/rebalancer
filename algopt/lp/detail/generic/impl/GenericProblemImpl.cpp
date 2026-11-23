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

#include "algopt/lp/detail/generic/impl/GenericProblemImpl.h"

#include "algopt/lp/detail/generic/impl/GenericExpressionImpl.h"
#include "algopt/lp/detail/generic/impl/thrift/gen-cpp2/model_types.h"
#include "algopt/lp/generic/Expression.h"

#include <fmt/core.h>
#include <folly/container/F14Map.h>
#include <folly/container/irange.h>
#include <folly/FileUtil.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <memory>
#include <stdexcept>

namespace facebook::algopt::lp::detail {

GenericProblemImpl::GenericProblemImpl(const std::function<Problem()>& factory)
    : factory_(factory), problem_(factory_()) {}

std::shared_ptr<VariableImpl> GenericProblemImpl::makeVar(
    const std::string& name) {
  return std::make_shared<GenericVariableImpl>(
      name, VariableImpl::Type::CONTINUOUS);
}

std::shared_ptr<VariableImpl> GenericProblemImpl::makeIntVar(
    const std::string& name) {
  return std::make_shared<GenericVariableImpl>(
      name, VariableImpl::Type::INTEGER);
}

std::shared_ptr<VariableImpl> GenericProblemImpl::makeSemiContVar(
    const std::string& name,
    double threshold) {
  auto variable = std::make_shared<GenericVariableImpl>(
      name, VariableImpl::Type::SEMI_CONTINUOUS);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> GenericProblemImpl::makeSemiIntVar(
    const std::string& name,
    double threshold) {
  auto variable = std::make_shared<GenericVariableImpl>(
      name, VariableImpl::Type::SEMI_INTEGER);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> GenericProblemImpl::makeBoolVar(
    const std::string& name) {
  return std::make_shared<GenericVariableImpl>(
      name, VariableImpl::Type::BINARY);
}

std::shared_ptr<ExpressionImpl> GenericProblemImpl::makeExpression(
    double constant) const {
  return std::make_shared<GenericExpressionImpl>(constant);
}

std::shared_ptr<ConstraintImpl> GenericProblemImpl::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) {
  auto genericRel =
      std::static_pointer_cast<const GenericRelationImpl>(relation);
  if (!genericRel) {
    throw std::runtime_error("can only add constraints using GenericRelation");
  }
  // we are guaranteed that relation is not a constant
  auto ctr = std::make_shared<GenericConstraintImpl>(*genericRel, name);
  constraints_.wlock()->insert(ctr);
  return ctr;
}

void GenericProblemImpl::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) {
  auto ctr = std::static_pointer_cast<GenericConstraintImpl>(constraint);
  if (!ctr) {
    throw std::runtime_error("only GenericConstraintImpl supported");
  }
  constraints_.withWLock(
      [&](auto& wlockedConstraints) { wlockedConstraints.erase(ctr); });
}

int GenericProblemImpl::getObjectiveSize() const {
  return objectives_.size();
}

std::shared_ptr<const ExpressionImpl> GenericProblemImpl::getObjectiveAt(
    int pos) const {
  if (pos >= getObjectiveSize()) {
    throw std::runtime_error(
        fmt::format(
            "There are only {} objectives, but attempt to access objective at pos {}",
            getObjectiveSize(),
            pos));
  }
  return objectives_.at(pos);
}

void GenericProblemImpl::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  auto objective =
      std::dynamic_pointer_cast<GenericExpressionImpl>(expression->clone());
  if (!objective) {
    throw std::runtime_error("expected to cast to GenericExpressionImpl");
  }
  objectives_.push_back(objective);
}

void GenericProblemImpl::clearObjectives() {
  objectives_.clear();
  // also clear all saved results w.r.t. those objectives
  problemResultPerObjective_.clear();
}

void GenericProblemImpl::solveForObjectiveAt(
    int /* unused */,
    std::optional<double> /* unused */) {
  throw std::runtime_error(
      "solveForObjectiveAt() should be not be used with GenericProblemImpl");
}

void GenericProblemImpl::multiObjectiveSolve() {
  throw std::runtime_error(
      "multiObjectiveSolve() should be not be used with GenericProblemImpl");
}

void GenericProblemImpl::addNewConstraintForObjectiveAt(int /* unused */) {
  throw std::runtime_error(
      "addNewConstraintForObjective() should be not be used with GenericProblemImpl");
}

void GenericProblemImpl::solveForObjectiveAndSaveResult(
    int /* unused */,
    std::optional<double> /* unused */) {
  throw std::runtime_error(
      "solveForObjectiveAndSaveResult() should be not be used with GenericProblemImpl");
}

void GenericProblemImpl::saveProblemResultAfterSolve() {
  throw std::runtime_error(
      "saveProblemResultAfterSolve() should be not be used with GenericProblemImpl");
}

thrift::ProblemStatus GenericProblemImpl::getSolveStatus() {
  throw std::runtime_error(
      "getSolveStatus() should be not be used with GenericProblemImpl");
}

thrift::ProblemResult GenericProblemImpl::getSolveResult() {
  throw std::runtime_error(
      "getSolveResult() should be not be used with GenericProblemImpl");
}

thrift::ProblemAttributes GenericProblemImpl::getProblemAttributes() {
  throw std::runtime_error(
      "getProblemAttributes() should be not be used with GenericProblemImpl");
}

void GenericProblemImpl::print(
    const std::vector<std::string>& substringsToMatch) {
  // setting showValues = true to show current values if they are available
  bool showValues = true;
  XLOG(INFO) << "Minimize: ";
  for (const auto idx : folly::irange(objectives_.size())) {
    XLOG(INFO) << fmt::format(
        "{:^15} :  {}", idx, objectives_.at(idx)->digest(showValues));
  }

  auto printThisConstraint = [&](const auto& constrName) {
    if (substringsToMatch.empty()) {
      return true;
    }
    for (const auto& substr : substringsToMatch) {
      if (constrName.find(substr) != std::string::npos) {
        return true;
      }
    }
    return false;
  };

  XLOG(INFO) << "Subject to constraints:";
  constraints_.withRLock([&](auto& rLockedConstraints) {
    for (auto& ctr : rLockedConstraints) {
      if (!printThisConstraint(ctr->getName())) {
        continue;
      }
      XLOG(INFO) << fmt::format(
          "{:^15} :  {}",
          ctr->getName(),
          ctr->getRelation().digest(showValues));
    }
  });
}

double GenericProblemImpl::getParameter(const std::string& name) {
  return params_.at(name);
}
void GenericProblemImpl::setParameter(const std::string& name, double value) {
  params_.emplace(name, value);
}

std::map<std::string, double> GenericProblemImpl::getSetParameters() const {
  return problem_.getSetParameters();
}

std::string GenericProblemImpl::getSolverVersion() const {
  return problem_.getSolverVersion();
}
void GenericProblemImpl::setTolerances(const Tolerances& tol) {
  tolerances_ = tol;
}
Tolerances GenericProblemImpl::getTolerances() {
  if (!tolerances_.has_value()) {
    // if tolerance values were not explicitly set, then get the default values
    // from the underlying problem
    return problem_.getTolerances();
  }

  return *tolerances_;
}

void GenericProblemImpl::setLogfile(const std::string& filename) {
  logFile_ = filename;
}

void GenericProblemImpl::saveToFileWithObjectiveAt(
    int /*pos*/,
    const std::string& fileName) {
  // nothing to explicitly set since saveToFile already writes all the
  // objectives
  saveToFile(fileName);
}

thrift::GenericProblem GenericProblemImpl::toThrift() const {
  // serialize into a thrift struct
  thrift::GenericProblem problem;
  // Write variables
  folly::F14FastMap<ConstGenericVarPtr, int64_t> genericVarToId;
  auto vars = getVars();
  problem.variables()->reserve(vars.size());
  for (auto& var : vars) {
    auto thriftVar = var->toThrift();
    genericVarToId.emplace(var, problem.variables()->size());
    problem.variables()->push_back(std::move(thriftVar));
  }
  // write objective
  for (auto& obj : objectives_) {
    problem.objectives()->push_back(obj->toThrift(genericVarToId));
  }
  // write constraints
  constraints_.withRLock([&](auto& rlockedConstraints) {
    for (auto& ctr : rlockedConstraints) {
      problem.constraints()->push_back(ctr->toThrift(genericVarToId));
    }
  });
  return problem;
}

void GenericProblemImpl::saveToFile(const std::string& fileName) {
  auto problem = toThrift();
  std::string serialized;
  serialized =
      apache::thrift::CompactSerializer::serialize<std::string>(problem);
  if (!folly::writeFile(serialized, fileName.c_str())) {
    throw std::runtime_error(
        fmt::format("error saving model to file {}", fileName));
  }
  XLOG(INFO) << fmt::format(
      "MIP model successfully saved to file {}", fileName);
}

void GenericProblemImpl::readFromThrift(const thrift::GenericProblem& problem) {
  folly::F14FastMap<int64_t, ConstGenericVarPtr> idToVar;
  // read variables
  Timer timer(true);
  int idx = 0;
  for (const auto& var : *problem.variables()) {
    idToVar.emplace(idx++, GenericVariableImpl::fromThrift(var));
  }
  XLOG(INFO) << fmt::format(
      "Finished reading {} variables in {}s",
      problem.variables()->size(),
      timer.resetSeconds());

  // read objective(s)
  for (const auto& obj : *problem.objectives()) {
    auto objExpr = GenericExpressionImpl::fromThrift(obj, idToVar);
    addObjective(objExpr);
  }

  XLOG(INFO) << fmt::format(
      "Finished reading {} objectives in {}s",
      problem.objectives()->size(),
      timer.resetSeconds());

  // read constraints
  for (auto ctr : *problem.constraints()) {
    auto relation = GenericConstraintImpl::fromThrift(ctr, idToVar);
    newConstraint(relation, *ctr.name());
  }

  XLOG(INFO) << fmt::format(
      "Finished reading {} constraints in {}s",
      problem.constraints()->size(),
      timer.resetSeconds());
}

void GenericProblemImpl::loadFromFile(const std::string& fileName) {
  XLOG(INFO) << "Loading saved model from file " << fileName;
  std::string serialized;
  Timer timer(true);
  if (!folly::readFile(fileName.c_str(), serialized)) {
    throw std::runtime_error(fmt::format("error reading file {}", fileName));
  }
  XLOG(INFO) << fmt::format(
      "Finished reading model from file in {}s", timer.resetSeconds());
  auto problem =
      apache::thrift::CompactSerializer::deserialize<thrift::GenericProblem>(
          serialized);
  XLOG(INFO) << fmt::format(
      "Finished deserializing thrift problem in {}s", timer.resetSeconds());
  readFromThrift(problem);
  XLOG(INFO) << fmt::format(
      "Building generic problem took {}s", timer.resetSeconds());
}

void GenericProblemImpl::replay(const std::string& fileName) const {
  auto genericProblem = std::make_unique<GenericProblemImpl>(factory_);
  genericProblem->loadFromFile(fileName);
  if (maxSolveTime_) {
    genericProblem->setMaxSolveTime(*maxSolveTime_);
  }
  genericProblem->solve(
      true /*useSolverSpecificMultiObjectiveSolveIfAvailable*/);
}

void GenericProblemImpl::setDebugPath(const std::string& path) {
  debugPath_ = path;
}

void GenericProblemImpl::disableLogs() {
  disableLogs_ = true;
}

void GenericProblemImpl::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) {
  auto var = std::static_pointer_cast<const GenericVariableImpl>(variable);
  var->setInitialValue(value);
}

void GenericProblemImpl::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback) {
  callback_ = std::move(callback);
}

const folly::F14FastMap<std::string, double>&
GenericProblemImpl::getSolverParams() const {
  return params_;
}

folly::F14FastMap<ConstGenericVarPtr, Variable> GenericProblemImpl::realize(
    Problem& problem) const {
  folly::F14FastMap<ConstGenericVarPtr, Variable> oldVarsToNew;
  if (disableLogs_) {
    problem.disableLogs();
  }
  if (tolerances_.has_value()) {
    problem.setTolerances(*tolerances_);
  }
  if (maxSolveTime_) {
    problem.setMaxSolveTime(*maxSolveTime_);
  }
  if (logFile_) {
    problem.setLogfile(*logFile_);
  }
  if (debugPath_) {
    problem.setDebugPath(*debugPath_);
  }
  if (callback_) {
    problem.setCallback(*callback_);
  }
  if (multiObjConfig_.has_value()) {
    problem.setMultiObjConfig(*multiObjConfig_);
  }

  for (auto& [param, value] : params_) {
    problem.setParameter(param, value);
  }
  // create variables
  for (auto& var : getVars()) {
    oldVarsToNew.emplace(var, var->realize(problem));
  }
  // add constraints
  constraints_.withRLock([&](auto& rLockedConstraints) {
    for (auto& ctr : rLockedConstraints) {
      problem.newConstraint(
          ctr->getRelation().realize(oldVarsToNew), ctr->getName());
    }
  });
  // add objectives
  std::vector<Expression> realizedObjective;
  realizedObjective.reserve(objectives_.size());
  for (auto& objective : objectives_) {
    realizedObjective.push_back(objective->realize(oldVarsToNew));
  }
  problem.setObjective(realizedObjective);

  return oldVarsToNew;
}

std::function<Problem()> GenericProblemImpl::getFactory() const {
  return factory_;
}

folly::F14FastSet<ConstGenericVarPtr> GenericProblemImpl::getVars() const {
  folly::F14FastSet<ConstGenericVarPtr> vars;
  auto insertExprVariables = [&](std::shared_ptr<GenericExpressionImpl> expr) {
    for (auto& [var, _] : expr->getLinearCoeffMap()) {
      vars.insert(var);
    }
    if (auto& quadraticCoeff = expr->getQuadraticCoeffMap()) {
      for (auto& [varPair, _] : *quadraticCoeff) {
        vars.insert(varPair.first);
        vars.insert(varPair.second);
      }
    }
  };
  constraints_.withRLock([&](auto& rLockedConstraints) {
    for (auto& ctr : rLockedConstraints) {
      insertExprVariables(ctr->getRelation().getExpr());
    }
  });

  for (auto& objective : objectives_) {
    insertExprVariables(objective);
  }

  return vars;
}

folly::F14FastSet<std::shared_ptr<GenericConstraintImpl>>
GenericProblemImpl::getConstraints() const {
  return *constraints_.rlock();
}

void GenericProblemImpl::solve(
    bool useSolverSpecificMultiObjectiveSolveIfAvailable) {
  auto mapping = realize(problem_);
  problem_.solve(useSolverSpecificMultiObjectiveSolveIfAvailable);
  status_ = problem_.getStatus();
  problemResultPerObjective_ = problem_.getResultPerObjective();

  if (status_ == thrift::ProblemStatus::OPTIMAL_FOUND ||
      status_ == thrift::ProblemStatus::SOLUTION_FOUND) {
    for (auto& [myVar, other] : mapping) {
      myVar->setValue(other.getValue());
    }
  }
}

std::shared_ptr<GenericExpressionImpl> GenericProblemImpl::getOnlyObjective()
    const {
  if (objectives_.size() != 1) {
    throw std::runtime_error(
        fmt::format(
            "Expected exactly one objective, but there are {} objectives",
            objectives_.size()));
  }
  return objectives_.at(0);
}

void GenericProblemImpl::setSolution(thrift::ProblemResult result) {
  if (getObjectiveSize() != 1) {
    throw std::runtime_error(
        "setSolution() should only be used when there is exactly one objective");
  }
  status_ = *result.status();
  problemResultPerObjective_ = {result};
}

std::optional<IIS> GenericProblemImpl::getIIS() {
  throw std::runtime_error("getIIS() is currently not supported");
}

std::optional<std::string> GenericProblemImpl::getDebugPath() const {
  return debugPath_;
}

} // namespace facebook::algopt::lp::detail
