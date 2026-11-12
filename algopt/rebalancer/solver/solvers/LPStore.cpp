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

#include "algopt/rebalancer/solver/solvers/LPStore.h"

#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <fmt/format.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace facebook::rebalancer {

algopt::lp::Variable LPStore::makeLpVar(
    LpVarType lpVarType,
    const std::string& name) const {
  auto modifiedName = fmt::format("{}_{}", name, lpVarCounter_++);
  switch (lpVarType) {
    case LpVarType::BINARY:
      return lpProblem_->makeBoolVar(modifiedName);
    case LpVarType::CONTINUOUS:
      return lpProblem_->makeVar(modifiedName);
    case LpVarType::INTEGER:
      return lpProblem_->makeIntVar(modifiedName);
    case LpVarType::SEMI_CONTINUOUS:
      return lpProblem_->makeSemiContVar(modifiedName);
    default:
      throw std::runtime_error("unknown variable type");
  }
}

algopt::lp::Variable LPStore::getAssignmentVar(
    entities::EquivalenceSetId equivSet,
    entities::ContainerId container) const {
  auto equivSetVarsPtr = folly::get_ptr(lpVars_, equivSet);
  if (!equivSetVarsPtr || !equivSetVarsPtr->contains(container)) {
    throw std::runtime_error(
        fmt::format(
            "Expected assignment variable for equivSet {} and container {} to already be defined",
            equivSet,
            container));
  }
  return equivSetVarsPtr->at(container);
}

void LPStore::addVarStartValue(
    const algopt::lp::Variable& variable,
    uint64_t value) const {
  lpProblem_->addStartValue(variable, value);
}

algopt::lp::Expression LPStore::expression(double value) const {
  return lpProblem_->makeExpression(value);
}

static std::unique_ptr<algopt::lp::Problem> initializeLpProblem(
    interface::OptimalSolverPackage package,
    bool simplify) {
  switch (package) {
    case interface::OptimalSolverPackage::XPRESS:
      if (simplify) {
        return std::make_unique<algopt::lp::Problem>(
            algopt::lp::ProblemFactory::makeSimplifiedProblem(
                algopt::lp::ProblemFactory::makeXpressProblem));
      } else {
        return std::make_unique<algopt::lp::Problem>(
            algopt::lp::ProblemFactory::makeXpressProblem());
      }
    case interface::OptimalSolverPackage::GUROBI:
      if (simplify) {
        return std::make_unique<algopt::lp::Problem>(
            algopt::lp::ProblemFactory::makeSimplifiedProblem(
                algopt::lp::ProblemFactory::makeGurobiProblem));
      } else {
        return std::make_unique<algopt::lp::Problem>(
            algopt::lp::ProblemFactory::makeGurobiProblem());
      }
    case interface::OptimalSolverPackage::HIGHS:
      if (simplify) {
        return std::make_unique<algopt::lp::Problem>(
            algopt::lp::ProblemFactory::makeSimplifiedProblem(
                algopt::lp::ProblemFactory::makeHiGHSProblem));
      } else {
        return std::make_unique<algopt::lp::Problem>(
            algopt::lp::ProblemFactory::makeHiGHSProblem());
      }
    default:
      throw std::runtime_error(
          fmt::format(
              "Unknown or invalid optimal solver package {}",
              fmt::underlying(package)));
  }
}

void LPStore::reset(
    interface::OptimalSolverPackage package,
    bool simplify,
    const LpContext& context,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> lpExecutor) {
  lpVars_.clear();

  solverPackage_ = package;
  lpProblem_ = initializeLpProblem(package, simplify);

  // create all lpVars_
  folly::coro::blockingWait(
      CoroUtils::runEachFuncAndUpdate(
          context.dynamicEquivSets().begin(),
          context.dynamicEquivSets().end(),
          [&](auto it)
              -> PackerMap<entities::ContainerId, algopt::lp::Variable> {
            auto equivSet = *it;
            PackerMap<entities::ContainerId, algopt::lp::Variable>
                containerVars;
            for (auto& container : context.dynamicContainers()) {
              auto var = lpProblem_->makeIntVar(
                  fmt::format("assign_{}_{}", equivSet, container));
              containerVars.emplace(container, var);
            }
            return containerVars;
          },
          [&](auto&& containerVars, auto it) {
            lpVars_.emplace(*it, std::move(containerVars));
          },
          std::move(lpExecutor)));
}

algopt::lp::Problem& LPStore::getLpProblem() const {
  return *lpProblem_;
}

const PackerMap<
    entities::EquivalenceSetId,
    PackerMap<entities::ContainerId, algopt::lp::Variable>>&
LPStore::getLpVars() const {
  return lpVars_;
}

void LPStore::setTolerances(algopt::lp::Tolerances tols) const {
  lpProblem_->setTolerances(tols);
}

algopt::lp::Tolerances LPStore::getTolerances() const {
  return lpProblem_->getTolerances();
}

void LPStore::setObjective(
    const std::vector<algopt::lp::Expression>& objectiveTuple) const {
  lpProblem_->setObjective(objectiveTuple);
}

void LPStore::solve() const {
  lpProblem_->solve();
}

void LPStore::setMaxSolveTime(double solveTime) const {
  lpProblem_->setMaxSolveTime(solveTime);
}

void LPStore::setLogfile(const std::string& filename) const {
  lpProblem_->setLogfile(filename);
}

void LPStore::setMultiObjConfig(
    algopt::lp::MultiObjConfig multiObjConfig) const {
  lpProblem_->setMultiObjConfig(std::move(multiObjConfig));
}

void LPStore::print(const std::vector<std::string>& substringsToMatch) const {
  lpProblem_->print(substringsToMatch);
}

int LPStore::getConstraintCount() const {
  return lpCtrCounter_;
}

void LPStore::disableLogs() const {
  lpProblem_->disableLogs();
}

void LPStore::write(const std::string& filename) const {
  // TODO(asamudra): use filesystem to write-out to same json folder
  lpProblem_->saveToFile(filename);
}

void LPStore::checkInfeasible(Problem& p) const {
  auto status = lpProblem_->getStatus();
  if (status == algopt::lp::thrift::ProblemStatus::NO_SOLUTION_EXISTS) {
    throw std::runtime_error(
        fmt::format("Problem {} infeasible", p.configs.runId.run_id));
  }
}

algopt::lp::Constraint LPStore::addConstraint(
    const algopt::lp::Relation& expr,
    const std::string& name) const {
  reserveConstraintAttempts(lpCtrCounter_, 1);
  return lpProblem_->newConstraint(expr, name);
}

int LPStore::reserveConstraintAttempts(
    std::atomic<int>& counter,
    size_t count) {
  constexpr int kMaximum = std::numeric_limits<int>::max();
  if (count > static_cast<size_t>(kMaximum)) {
    throw std::overflow_error(
        fmt::format(
            "constraint request count {} exceeds counter maximum {}",
            count,
            kMaximum));
  }
  const int increment = static_cast<int>(count);
  int current = counter.load(std::memory_order_relaxed);
  do {
    if (current < 0 || increment > kMaximum - current) {
      throw std::overflow_error(
          fmt::format(
              "constraint attempt counter {} cannot add {} requests (maximum {})",
              current,
              count,
              kMaximum));
    }
  } while (!counter.compare_exchange_weak(
      current, current + increment, std::memory_order_relaxed));
  return current + increment;
}

std::vector<algopt::lp::Constraint> LPStore::addConstraints(
    const algopt::lp::Problem::ConstraintRequests& requests) const {
  if (!requests.empty()) {
    reserveConstraintAttempts(lpCtrCounter_, requests.size());
  }
  return lpProblem_->newConstraints(requests);
}

void LPStore::updateParam(const std::string& paramName, double val) const {
  lpProblem_->setParameter(paramName, val);
}

int LPStore::getNumInternalVariables() const {
  return lpVarCounter_;
}

} // namespace facebook::rebalancer
