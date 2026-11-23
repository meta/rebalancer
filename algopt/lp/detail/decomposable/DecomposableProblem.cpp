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

#include "algopt/lp/detail/decomposable/DecomposableProblem.h"

#include "algopt/lp/detail/generic/impl/GenericExpressionImpl.h"
#include "algopt/lp/detail/generic/impl/GenericProblemImpl.h"
#include "algopt/lp/detail/generic/impl/GenericVariableImpl.h"

#include <fmt/core.h>
#include <folly/container/F14Set.h>
#include <folly/logging/xlog.h>

#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>

namespace facebook::algopt::lp::detail {

DecomposableProblem::DecomposableProblem(
    const std::function<Problem()>& factory)
    : GenericProblemImpl(factory) {}

DecomposableProblem::DecomposableProblem(
    const std::function<Problem()>& factory,
    const folly::F14FastSet<PartitionId>& allowedPartitions)
    : GenericProblemImpl(factory), allowedPartitions_(allowedPartitions) {}

DecomposableProblem::DecomposableProblem(
    const DecomposableProblem& masterProblem,
    const folly::F14FastSet<PartitionId>& allowedPartitions)
    : GenericProblemImpl(masterProblem.getFactory()),
      allowedPartitions_(allowedPartitions) {
  // pass on the solver parameters from master problem to child problem
  // some specific parameters such as timeouts, callbacks, tolerances
  // may be overridden later.
  for (auto& [param, value] : masterProblem.getSolverParams()) {
    setParameter(param, value);
  }
  // Also propogate the debug path information
  if (masterProblem.getDebugPath()) {
    setDebugPath(*masterProblem.getDebugPath());
  }
}

void DecomposableProblem::solve(
    bool useSolverSpecificMultiObjectiveSolveIfAvailable) {
  solve(
      true,
      false,
      std::nullopt,
      std::nullopt,
      useSolverSpecificMultiObjectiveSolveIfAvailable);
}

void DecomposableProblem::setObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  clearObjectives();
  addObjective(expression);
}

folly::F14FastMap<ConstGenericVarPtr, double> DecomposableProblem::solve(
    bool saveResults,
    bool returnInitialValuesOnFailure,
    std::optional<std::string> solverOutputFile,
    std::optional<std::string> modelOutputFile,
    bool useSolverSpecificMultiObjectiveSolveIfAvailable) {
  Problem p = factory_();
  // if solver outout was requested, enable logs before "realizing"
  if (solverOutputFile) {
    disableLogs_ = false;
  }
  auto oldVarsToNew = realize(p);

  if (solverOutputFile) {
    std::filesystem::remove(*solverOutputFile);
    p.setLogfile(*solverOutputFile);
    XLOG(INFO) << fmt::format("Solver logs saved as {}", *solverOutputFile);
  }

  if (modelOutputFile) {
    std::filesystem::remove(*modelOutputFile);
    p.saveToFile(*modelOutputFile);
    XLOG(INFO) << fmt::format("MIP model saved as {}", *modelOutputFile);
  }
  p.solve(useSolverSpecificMultiObjectiveSolveIfAvailable);
  solvedParamNameToValue_ = p.getSetParameters();
  status_ = p.getStatus();
  problemResultPerObjective_ = p.getResultPerObjective();

  folly::F14FastMap<ConstGenericVarPtr, double> ret;
  if (status_ == thrift::ProblemStatus::OPTIMAL_FOUND ||
      status_ == thrift::ProblemStatus::SOLUTION_FOUND) {
    for (const auto& [var, realizedVar] : oldVarsToNew) {
      if (saveResults) {
        var->setValue(realizedVar.getValue());
      }
      ret[var] = realizedVar.getValue();
    }
  } else if (returnInitialValuesOnFailure) {
    for (const auto& [var, _] : oldVarsToNew) {
      if (var->hasValue()) {
        // only save values of variables that are available
        ret[var] = var->getValue();
      }
    }
  }
  return ret;
}

Expression DecomposableProblem::image(
    std::shared_ptr<const GenericExpressionImpl> expr,
    RealizationRule realizationRule) const {
  if (!allowedPartitions_) {
    throw std::runtime_error("must specify partitions to take image");
  }
  return expr->image(*allowedPartitions_, realizationRule);
}

Relation DecomposableProblem::image(
    const GenericRelationImpl& rel,
    RealizationRule realizationRule) const {
  auto expr = image(rel.getExpr(), realizationRule);
  if (rel.isEqZero()) {
    return expr.makeEqualZeroRelation();
  } else if (rel.isGeqZero()) {
    return expr.makeGreaterEqualZeroRelation();
  } else {
    return expr.makeLessEqualZeroRelation();
  }
}

const folly::F14FastSet<PartitionId>&
DecomposableProblem::getAllowedPartitions() const {
  if (allowedPartitions_) {
    return *allowedPartitions_;
  }
  throw std::runtime_error(
      "Allowed partitions were not specified for this problem");
}

std::map<std::string, double> DecomposableProblem::getSetParameters() const {
  return solvedParamNameToValue_.has_value()
      ? *solvedParamNameToValue_
      : GenericProblemImpl::getSetParameters();
}

} // namespace facebook::algopt::lp::detail
