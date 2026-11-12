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

#include "algopt/lp/generic/Problem.h"

#include "algopt/lp/generic/thrift/gen-cpp2/problem_types.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"

#include "fmt/format.h"
#include <folly/container/irange.h>
#include <folly/logging/xlog.h>
#include <folly/MapUtil.h>

#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace facebook::algopt::lp {

std::optional<double> computeScaledMaxCoefRatio(
    std::vector<CoefficientEntry>& entries,
    int numRows,
    int numCols) {
  if (entries.empty()) {
    return std::nullopt;
  }

  // Build row-to-entries and col-to-entries index maps.
  std::vector<std::vector<size_t>> rowEntries(numRows);
  std::vector<std::vector<size_t>> colEntries(numCols);
  for (const auto idx : folly::irange(entries.size())) {
    rowEntries[entries[idx].row].push_back(idx);
    colEntries[entries[idx].col].push_back(idx);
  }

  auto scaleByMax =
      [&entries](const std::vector<std::vector<size_t>>& groups, int count) {
        for (const auto g : folly::irange(count)) {
          double maxVal = 0.0;
          for (const size_t idx : groups[g]) {
            maxVal = std::max(maxVal, entries[idx].value);
          }
          if (maxVal > 0.0) {
            for (const size_t idx : groups[g]) {
              entries[idx].value /= maxVal;
            }
          }
        }
      };

  scaleByMax(rowEntries, numRows);
  scaleByMax(colEntries, numCols);

  // Final max/min ratio after scaling.
  double finalMax = 0.0;
  double finalMin = std::numeric_limits<double>::max();
  for (const auto& e : entries) {
    finalMax = std::max(finalMax, e.value);
    finalMin = std::min(finalMin, e.value);
  }
  if (finalMin <= 0.0) {
    return std::nullopt;
  }
  const double ratio = finalMax / finalMin;
  XLOG(INFO) << fmt::format(
      "Scaling rows={}, cols={}, nnzs={}, max={:.6e}, min={:.6e}, ratio={:.6e}",
      numRows,
      numCols,
      entries.size(),
      finalMax,
      finalMin,
      ratio);

  return ratio;
}

ProblemImpl::~ProblemImpl() = default;

std::vector<std::shared_ptr<ConstraintImpl>> ProblemImpl::newConstraints(
    const ConstraintRequests& requests) {
  std::vector<std::shared_ptr<ConstraintImpl>> constraints;
  constraints.reserve(requests.size());
  for (const auto& [relation, name] : requests) {
    constraints.push_back(newConstraint(relation, name));
  }
  return constraints;
}

bool ProblemImpl::supportsConstraintBatching() const {
  return false;
}

void ProblemImpl::setMaxSolveTime(double solveTime) {
  maxSolveTime_ = solveTime;
}

void ProblemImpl::setMultiObjConfig(MultiObjConfig config) {
  multiObjConfig_ = std::move(config);
}

std::optional<double> ProblemImpl::getScaledMaxCoefRatio() {
  return std::nullopt;
}

bool ProblemImpl::supportsNativeQuadratic() const {
  return false;
}

bool ProblemImpl::supportsNativePwl() const {
  return false;
}

bool ProblemImpl::supportsNativeMax() const {
  return false;
}

bool ProblemImpl::supportsIndicatorConstraints() const {
  return false;
}

bool ProblemImpl::setIndicatorOnConstraint(
    Constraint& /* ctr */,
    const Variable& /* binaryVar */,
    int /* dir */) {
  return false;
}

std::optional<Expression> ProblemImpl::addNativePwlConstraint(
    const Expression& /* x */,
    const std::vector<std::pair<double, double>>& /* points */) {
  return std::nullopt;
}

std::optional<Expression> ProblemImpl::addNativeMaxConstraint(
    const std::vector<Expression>& /* inputs */) {
  return std::nullopt;
}

std::optional<NumericalStabilityInfo> ProblemImpl::getNumericalStability(
    bool /* computeExactKappa */) {
  return std::nullopt;
}

void ProblemImpl::saveFinalSolveStatus() {
  if (problemResultPerObjective_.empty()) {
    throw std::runtime_error(
        "Expected at least one problemResult before saving final status");
  }

  // Unless we run out of time or run into infeasible solve, we will perform
  // as many solves as the number of objectives.
  const int numObjectives = getObjectiveSize();
  std::unordered_map<thrift::ProblemStatus, int> numSolvesPerType;
  for (auto& result : problemResultPerObjective_) {
    numSolvesPerType[*result.status()]++;
  }

  auto numObjectivesSolvedOptimally = folly::get_default(
      numSolvesPerType, thrift::ProblemStatus::OPTIMAL_FOUND, 0);
  auto numSolvedObjectives =
      numObjectivesSolvedOptimally +
      folly::get_default(
          numSolvesPerType, thrift::ProblemStatus::SOLUTION_FOUND, 0);
  auto numInfeasible = folly::get_default(
      numSolvesPerType, thrift::ProblemStatus::NO_SOLUTION_EXISTS, 0);

  if (numInfeasible > 0) {
    // the model was proved to be infeasible
    status_ = thrift::ProblemStatus::NO_SOLUTION_EXISTS;
  } else if (numObjectivesSolvedOptimally == numObjectives) {
    // we were able to solve and find an optimal solution for all objectives
    status_ = thrift::ProblemStatus::OPTIMAL_FOUND;
  } else if (numSolvedObjectives > 0) {
    // we found a solution for at least one of the objectives, so that is a
    // valid solution for multi-objective problem (note that not all objectives
    // may have a status due to time limits)
    status_ = thrift::ProblemStatus::SOLUTION_FOUND;
  } else {
    // we neither found a solution nor could prove that one does not exist
    // this happens if the solve is interrupted by timeout or callbacks
    status_ = thrift::ProblemStatus::SOLUTION_NOT_FOUND;
  }
}

thrift::ProblemStatus ProblemImpl::getStatus() {
  if (!status_.has_value()) {
    throw std::runtime_error(
        "No final solve status available. Did you solve the problem by calling solve()?");
  }

  return status_.value();
}

std::vector<thrift::ProblemResult> ProblemImpl::getResultPerObjective() {
  return problemResultPerObjective_;
}

thrift::ProblemResult ProblemImpl::getOnlyResult() {
  if (problemResultPerObjective_.size() != 1) {
    throw std::runtime_error(
        fmt::format(
            "Expected exactly one problemResult, but found {} results",
            problemResultPerObjective_.size()));
  }
  return problemResultPerObjective_.at(0);
}

void ProblemImpl::saveProblemResultAfterSolve() {
  auto latestResult = getSolveResult();
  problemResultPerObjective_.push_back(latestResult);
}

void ProblemImpl::addNewConstraintForObjectiveAt(int pos) {
  auto obj = getObjectiveAt(pos)->clone();
  auto currValue = obj->computeValue();

  auto allowedWorsenUntilValue = currValue;
  if (multiObjConfig_.has_value() &&
      multiObjConfig_->higherPriObjConfig.has_value()) {
    auto allowedWorseningPtr = folly::get_ptr(
        *multiObjConfig_->higherPriObjConfig->tuplePosToAllowedWorsening(),
        pos);
    if (allowedWorseningPtr) {
      allowedWorsenUntilValue =
          algopt::common::thriftUtils::getAllowedWorsenUntilValue(
              currValue, *allowedWorseningPtr);
    }
  }

  // get the expression (obj - allowedWorsenUntilValue) and the relation (obj -
  // allowedWorsenUntilValue) <= 0
  obj->add(-allowedWorsenUntilValue);
  auto objAtMostObjValue = obj->makeLessEqualZeroRelation();

  auto constrName = fmt::format(
      "do not worsen objective {} by more than allowedWorsenUntilValue {}",
      pos + 1,
      allowedWorsenUntilValue);
  // impose that (obj - allowedWorsenUntilValue) <= 0
  newConstraint(objAtMostObjValue, constrName);
}

void ProblemImpl::solveForObjectiveAndSaveResult(
    int pos,
    std::optional<double> timeLimit) {
  solveForObjectiveAt(pos, timeLimit);
  saveProblemResultAfterSolve();
}

void ProblemImpl::customMultiObjectiveIterativeSolve() {
  auto nObjs = getObjectiveSize();
  std::optional<double> remainingSolveTime = maxSolveTime_;

  iterativeSolveTimer.start();
  // 1. Solve the problem by using objective at pos, 'objI', as the objective
  // 2. If V is the value of objI after the solve, then add a newConstraint with
  // objI <= V
  // 3. Go to the objective at (pos + 1) and repeat the steps
  for (const auto pos : folly::irange(nObjs)) {
    auto printInfoForIterativeSolve = [&]() {
      std::string objDesc = fmt::format("objective {}", pos + 1);
      auto iterationDesc =
          fmt::format("\t Iterative solve w.r.t. {} \t", objDesc);
      auto dashes = std::string(iterationDesc.length() + 10, '-');
      auto printInfo =
          fmt::format("\n {} \n {} \n {} \n", dashes, iterationDesc, dashes);

      XLOG(INFO) << printInfo;
    };
    printInfoForIterativeSolve();
    // if timeLimit_ is set and we are at a position where we have a time limit
    // then use it, otherwise use remainingSolveTime
    if (multiObjConfig_) {
      std::vector<algopt::common::thrift::PerObjectiveValue> objValues;
      if (multiObjConfig_->paramNamesToValues.contains("GRB_TimeLimit")) {
        objValues.push_back(
            multiObjConfig_->paramNamesToValues["GRB_TimeLimit"]);
      }
      if (multiObjConfig_->paramNamesToValues.contains("XPRS_MAXTIME")) {
        objValues.push_back(
            multiObjConfig_->paramNamesToValues["XPRS_MAXTIME"]);
      }
      if (multiObjConfig_->paramNamesToValues.contains("XPRS_TIMELIMIT")) {
        objValues.push_back(
            multiObjConfig_->paramNamesToValues["XPRS_TIMELIMIT"]);
      }
      if (objValues.size() > 1) {
        throw std::runtime_error(
            "Multiple time limit parameters are not supported");
      }
      if (objValues.size() == 0) {
        solveForObjectiveAndSaveResult(pos, remainingSolveTime);
      } else {
        const double posTime =
            algopt::common::thriftUtils::getObjectiveValue(pos, objValues[0]);
        solveForObjectiveAndSaveResult(pos, posTime);
        XLOG(DBG) << fmt::format(
            "Solve time limit for Objective at {} : {}s", pos, posTime);
      }
    } else {
      solveForObjectiveAndSaveResult(pos, remainingSolveTime);
    }

    auto curSolveStatus = getSolveStatus();
    // if we cannot find a solution for the current solve for whatever reason
    // (because the solve was infeasible or we timed out), exit early
    std::optional<std::string> exitEarlyMessage;
    if (curSolveStatus == thrift::ProblemStatus::NO_SOLUTION_EXISTS ||
        curSolveStatus == thrift::ProblemStatus::SOLUTION_NOT_FOUND) {
      exitEarlyMessage =
          fmt::format("Could not find solution for objective {}", pos + 1);
    }

    // update timeLimit to be remainingSolveTime and return if it is close to
    // 0
    if (maxSolveTime_.has_value()) {
      remainingSolveTime =
          maxSolveTime_.value() - iterativeSolveTimer.getSeconds();
      if (remainingSolveTime < 1e-10) {
        exitEarlyMessage = "Hit maxSolveTime";
      }
    }

    if (exitEarlyMessage) {
      XLOG(INFO) << fmt::format(
          "{}; exiting iterative solve after looking at {} objective(s)",
          *exitEarlyMessage,
          pos + 1);
      break;
    }

    // for every objective except the last one, add a new constraint imposing
    // that it does not get worse than the value obtained after the current
    // solve
    if (pos < nObjs - 1) {
      addNewConstraintForObjectiveAt(pos);
    }
  }
}

void ProblemImpl::multiObjectiveSolve() {
  customMultiObjectiveIterativeSolve();
}

void ProblemImpl::solve(bool useSolverSpecificMultiObjectiveSolveIfAvailable) {
  if (getObjectiveSize() == 0) {
    // if there is no objective, then add a zero objective
    addObjective(makeExpression(0));
  }

  if (getObjectiveSize() == 1) {
    solveForObjectiveAndSaveResult(0 /* tuplePosition */, maxSolveTime_);
  } else { // multi-objective case
    useSolverSpecificMultiObjectiveSolveIfAvailable
        ? multiObjectiveSolve()
        : customMultiObjectiveIterativeSolve();
  }

  saveFinalSolveStatus();
}

std::optional<uint32_t> ProblemImpl::getModelFingerprint() {
  return std::nullopt;
}

void ProblemImpl::saveToFileWithAllObjectives(const std::string& filename) {
  saveToFile(filename);
}

void ProblemImpl::replay(const std::string& /* fileName */) const {
  throw std::runtime_error("replay is not implemented");
}

Problem::Problem(std::unique_ptr<ProblemImpl> problem)
    : problem_(std::move(problem)) {}

Variable Problem::makeVar(const std::string& name) {
  return Variable(problem_->makeVar(name));
}

Variable Problem::makeIntVar(const std::string& name) {
  return Variable(problem_->makeIntVar(name));
}

Variable Problem::makeSemiContVar(const std::string& name, double threshold) {
  return Variable(problem_->makeSemiContVar(name, threshold));
}

Variable Problem::makeSemiIntVar(const std::string& name, double threshold) {
  return Variable(problem_->makeSemiIntVar(name, threshold));
}

Variable Problem::makeBoolVar(const std::string& name) {
  return Variable(problem_->makeBoolVar(name));
}

Expression Problem::makeExpression(double constant) {
  return Expression(constant);
}

Constraint Problem::newConstraint(
    const Relation& relation,
    const std::string& name) {
  if (relation.isConstant()) {
    auto expression = problem_->makeExpression(relation.getConstantValue());
    switch (relation.getConstantType()) {
      case Relation::ConstantType::EQ_ZERO:
        return Constraint(
            problem_->newConstraint(expression->makeEqualZeroRelation(), name));
      case Relation::ConstantType::GE_ZERO:
        return Constraint(problem_->newConstraint(
            expression->makeGreaterEqualZeroRelation(), name));
      case Relation::ConstantType::LE_ZERO:
        return Constraint(problem_->newConstraint(
            expression->makeLessEqualZeroRelation(), name));
    }
    throw std::runtime_error("unsupported constant relation type");
  }
  return Constraint(problem_->newConstraint(relation.get(), name));
}

std::vector<Constraint> Problem::newConstraints(
    const ConstraintRequests& requests) {
  ProblemImpl::ConstraintRequests normalized;
  normalized.reserve(requests.size());
  for (const auto& [relation, name] : requests) {
    auto implementation = relation.get();
    if (relation.isConstant()) {
      const auto expression =
          problem_->makeExpression(relation.getConstantValue());
      switch (relation.getConstantType()) {
        case Relation::ConstantType::EQ_ZERO:
          implementation = expression->makeEqualZeroRelation();
          break;
        case Relation::ConstantType::GE_ZERO:
          implementation = expression->makeGreaterEqualZeroRelation();
          break;
        case Relation::ConstantType::LE_ZERO:
          implementation = expression->makeLessEqualZeroRelation();
          break;
        default:
          throw std::runtime_error("unsupported constant relation type");
      }
    }
    normalized.emplace_back(std::move(implementation), name);
  }
  auto implementations = problem_->newConstraints(normalized);
  if (implementations.size() != requests.size()) {
    throw std::runtime_error(
        fmt::format(
            "backend returned {} constraints for {} requests",
            implementations.size(),
            requests.size()));
  }
  std::vector<Constraint> constraints;
  constraints.reserve(implementations.size());
  for (const auto index : folly::irange(implementations.size())) {
    auto& implementation = implementations[index];
    if (!implementation) {
      throw std::runtime_error(
          fmt::format(
              "backend returned a null constraint at request index {}", index));
    }
    constraints.emplace_back(std::move(implementation));
  }
  return constraints;
}

bool Problem::supportsConstraintBatching() const {
  return problem_->supportsConstraintBatching();
}

void Problem::deleteConstraint(Constraint& constraint) {
  problem_->deleteConstraint(constraint.get());
}

int Problem::getObjectiveSize() {
  return problem_->getObjectiveSize();
}

void Problem::addObjective(const Expression& objective) {
  if (objective.isConstant()) {
    problem_->addObjective(problem_->makeExpression(objective.getConstant()));
  } else {
    problem_->addObjective(objective.get());
  }
}

void Problem::setObjective(const Expression& objective) {
  problem_->clearObjectives();
  addObjective(objective);
}

void Problem::setObjective(const std::vector<Expression>& objectiveTuple) {
  problem_->clearObjectives();
  for (auto& expr : objectiveTuple) {
    addObjective(expr);
  }
}

std::shared_ptr<const ExpressionImpl> Problem::getObjectiveAt(int pos) {
  return problem_->getObjectiveAt(pos);
}

void Problem::solve(bool useSolverSpecificMultiObjectiveSolveIfAvailable) {
  problem_->solve(useSolverSpecificMultiObjectiveSolveIfAvailable);
}

thrift::ProblemStatus Problem::getStatus() {
  return problem_->getStatus();
}

std::vector<thrift::ProblemResult> Problem::getResultPerObjective() {
  return problem_->getResultPerObjective();
}

thrift::ProblemResult Problem::getOnlyResult() {
  return problem_->getOnlyResult();
}

thrift::ProblemAttributes Problem::getProblemAttributes() {
  return problem_->getProblemAttributes();
}

std::optional<uint32_t> Problem::getModelFingerprint() {
  return problem_->getModelFingerprint();
}

std::optional<double> Problem::getScaledMaxCoefRatio() {
  return problem_->getScaledMaxCoefRatio();
}

std::optional<NumericalStabilityInfo> Problem::getNumericalStability(
    bool computeExactKappa) {
  return problem_->getNumericalStability(computeExactKappa);
}

double Problem::getParameter(const std::string& name) {
  return problem_->getParameter(name);
}

void Problem::setParameter(const std::string& name, double value) {
  problem_->setParameter(name, value);
}

void Problem::setTolerances(const Tolerances& tol) {
  problem_->setTolerances(tol);
}

void Problem::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback) {
  problem_->setCallback(std::move(callback));
}

Tolerances Problem::getTolerances() {
  return problem_->getTolerances();
}

void Problem::setLogfile(const std::string& filename) {
  problem_->setLogfile(filename);
}

void Problem::saveToFile(const std::string& filename) {
  problem_->saveToFile(filename);
}
void Problem::saveToFileWithObjectiveAt(int pos, const std::string& filename) {
  problem_->saveToFileWithObjectiveAt(pos, filename);
}

void Problem::saveToFileWithAllObjectives(const std::string& filename) {
  problem_->saveToFileWithAllObjectives(filename);
}

void Problem::setDebugPath(const std::string& path) {
  problem_->setDebugPath(path);
}

void Problem::print(const std::vector<std::string>& substringsToMatch) {
  problem_->print(substringsToMatch);
}
void Problem::disableLogs() {
  problem_->disableLogs();
}

void Problem::setMaxSolveTime(double solveTime) {
  problem_->setMaxSolveTime(solveTime);
}

void Problem::setMultiObjConfig(MultiObjConfig config) {
  problem_->setMultiObjConfig(std::move(config));
}

void Problem::replay(const std::string& fileName) const {
  problem_->replay(fileName);
}

void Problem::addStartValue(const Variable& variable, double value) {
  problem_->addStartValue(variable.get(), value);
}

const ProblemImpl& Problem::get() const {
  return *problem_;
}

std::string_view Problem::backendName() const {
  return problem_->backendName();
}

std::optional<IIS> Problem::getIIS() {
  return problem_->getIIS();
}

bool Problem::supportsNativeQuadratic() const {
  return problem_->supportsNativeQuadratic();
}

bool Problem::supportsNativePwl() const {
  return problem_->supportsNativePwl();
}

bool Problem::supportsNativeMax() const {
  return problem_->supportsNativeMax();
}

bool Problem::supportsIndicatorConstraints() const {
  return problem_->supportsIndicatorConstraints();
}

bool Problem::setIndicatorOnConstraint(
    Constraint& ctr,
    const Variable& binaryVar,
    int dir) {
  return problem_->setIndicatorOnConstraint(ctr, binaryVar, dir);
}

std::optional<Expression> Problem::addNativePwlConstraint(
    const Expression& x,
    const std::vector<std::pair<double, double>>& points) {
  return problem_->addNativePwlConstraint(x, points);
}

std::optional<Expression> Problem::addNativeMaxConstraint(
    const std::vector<Expression>& inputs) {
  return problem_->addNativeMaxConstraint(inputs);
}

} // namespace facebook::algopt::lp
