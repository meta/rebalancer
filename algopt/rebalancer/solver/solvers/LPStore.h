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

#include "algopt/lp/detail/generic/impl/GenericVariableImpl.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h>

#include <atomic>
#include <cstddef>
#include <optional>

namespace facebook::rebalancer {

using LpVarType = algopt::lp::VariableImpl::Type;

class Problem;
class Context;

class LPStore {
 public:
  algopt::lp::Variable makeLpVar(
      LpVarType lpVarType,
      const std::string& name = "var") const;

  algopt::lp::Variable getAssignmentVar(
      entities::EquivalenceSetId equivSet,
      entities::ContainerId container) const;

  void checkInfeasible(Problem& p) const;

  void addVarStartValue(const algopt::lp::Variable& variable, uint64_t value)
      const;

  algopt::lp::Expression expression(double value = 0) const;

  algopt::lp::Problem& getLpProblem() const;

  // Submitted request attempts, including failed calls; not a native row count.
  // Batch failures may leave partial native state and require model discard.
  int getConstraintCount() const;

  algopt::lp::Constraint addConstraint(
      const algopt::lp::Relation& expr,
      const std::string& name = "constraint") const;
  std::vector<algopt::lp::Constraint> addConstraints(
      const algopt::lp::Problem::ConstraintRequests& requests) const;

  void setObjective(
      const std::vector<algopt::lp::Expression>& objectiveTuple) const;

  void solve() const;

  void print(const std::vector<std::string>& substringsToMatch = {}) const;

  void write(const std::string& filename) const;

  void setLogfile(const std::string& filename) const;

  const PackerMap<
      entities::EquivalenceSetId,
      PackerMap<entities::ContainerId, algopt::lp::Variable>>&
  getLpVars() const;

  void reset(
      interface::OptimalSolverPackage package,
      bool simplify,
      const LpContext& context,
      std::shared_ptr<algopt::treeprof::ExecutorWrapper> lpExecutor = nullptr);

  void updateParam(const std::string& paramName, double val) const;

  void setTolerances(algopt::lp::Tolerances tols) const;

  algopt::lp::Tolerances getTolerances() const;

  void disableLogs() const;

  void setMaxSolveTime(double solveTime) const;

  int getNumInternalVariables() const;

  void setMultiObjConfig(algopt::lp::MultiObjConfig multiObjConfig) const;

 private:
  friend struct LPStoreConstraintCountTestAccess;
  static int reserveConstraintAttempts(std::atomic<int>& counter, size_t count);

  // The following non-mutable data structures can only be set in reset() and
  // after that it is read-only
  PackerMap<
      entities::EquivalenceSetId,
      PackerMap<entities::ContainerId, algopt::lp::Variable>>
      lpVars_;
  std::optional<interface::OptimalSolverPackage> solverPackage_;

  mutable std::unique_ptr<algopt::lp::Problem> lpProblem_;
  mutable std::atomic<int> lpCtrCounter_ = 0;
  mutable std::atomic<int> lpVarCounter_ = 0;
};
} // namespace facebook::rebalancer
