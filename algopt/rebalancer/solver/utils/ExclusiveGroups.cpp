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

#include "algopt/rebalancer/solver/utils/ExclusiveGroups.h"

#include "algopt/lp/environment/Environment.h"
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/thrift/gen-cpp2/problem_types.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/lp/lp.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>

#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

constexpr double kTimeoutSeconds = 30.0;

namespace facebook::rebalancer {

namespace {

algopt::lp::Problem makeAvailableMipProblem() {
  if constexpr (algopt::isGurobiAvailable()) {
    return algopt::lp::ProblemFactory::makeGurobiProblem();
  } else if constexpr (algopt::isXpressAvailable()) {
    return algopt::lp::ProblemFactory::makeXpressProblem();
  } else if constexpr (algopt::isHiGHSAvailable()) {
    return algopt::lp::ProblemFactory::makeHiGHSProblem();
  } else {
    throw std::runtime_error("No MIP solver available in this build");
  }
}

} // namespace

PackerMap<std::string, std::string> computeExclusiveGroupsAssignment(
    const PackerMap<std::string, double>& groupSizes,
    const PackerMap<std::string, double>& scopeItemSizes,
    const PackerMap<std::string, PackerMap<std::string, double>>&
        groupScopeItemFootprint) {
  auto problem = makeAvailableMipProblem();
  problem.setMaxSolveTime(kTimeoutSeconds);

  // Step 1: create boolean assignment variables of scope item to group
  PackerMap<std::string, PackerMap<std::string, algopt::lp::Variable>>
      isAssigned;
  for (auto& [scopeItem, _] : scopeItemSizes) {
    algopt::lp::Expression sum = 0;
    for (auto& [group, _2] : groupSizes) {
      auto var = problem.makeBoolVar();
      isAssigned[scopeItem].emplace(group, var);
      sum += var;
    }
    problem.newConstraint(sum == 1);
  }

  // Step 2: create deficit expression
  algopt::lp::Expression deficit = 0;
  for (auto& [group, groupSize] : groupSizes) {
    algopt::lp::Expression assigned = 0;
    for (auto& [scopeItem, scopeItemSize] : scopeItemSizes) {
      assigned += isAssigned.at(scopeItem).at(group) * scopeItemSize;
    }
    auto localDeficit = problem.makeVar();
    localDeficit.setLB(0);
    problem.newConstraint(localDeficit >= groupSize - assigned);
    deficit += localDeficit;
  }

  // Step 3: create moves expression
  algopt::lp::Expression moves = 0;
  for (auto& [group, scopeItemFootprint] : groupScopeItemFootprint) {
    for (auto& [scopeItem, footprint] : scopeItemFootprint) {
      moves += (1 - isAssigned.at(scopeItem).at(group)) * footprint;
    }
  }

  // Step 4: create deviation expression
  double totalGroupSize = 0;
  for (auto& [_, size] : groupSizes) {
    totalGroupSize += size;
  }

  double totalScopeSize = 0;
  for (auto& [_, size] : scopeItemSizes) {
    totalScopeSize += size;
  }

  algopt::lp::Expression deviation = 0;
  for (auto& [group, size] : groupSizes) {
    const double target = size / totalGroupSize * totalScopeSize;
    algopt::lp::Expression assigned = 0;
    for (auto& [scopeItem, scopeItemSize] : scopeItemSizes) {
      assigned += isAssigned.at(scopeItem).at(group) * scopeItemSize;
    }
    auto localDeviation = problem.makeVar();
    localDeviation.setLB(0);
    problem.newConstraint(localDeviation >= target - assigned);
    deviation += localDeviation;
  }

  // Step 5: solve
  problem.setObjective({deficit, moves, deviation});
  // Use Rebalancer's custom iterative multi-objective solver instead of
  // the solver's native multi-objective solver. This solve happens during the
  // materialization phase of ExclusiveGroupsSpecBuilder which runs in a
  // coroutine. When some solvers run in a non-main thread, they can run into
  // TSAN violations.
  problem.solve(/*useSolverSpecificMultiObjectiveSolveIfAvailable=*/false);

  auto solveStatus = problem.getStatus();
  if (solveStatus == algopt::lp::thrift::ProblemStatus::NO_SOLUTION_EXISTS ||
      solveStatus == algopt::lp::thrift::ProblemStatus::SOLUTION_NOT_FOUND) {
    throw std::runtime_error(
        "The problem in ExclusiveGroupsSpecBuilder is infeasible");
  }

  // Step 6: recreate the optimal assignment from the boolean variables
  PackerMap<std::string, std::string> assignment;
  for (auto& [scopeItem, groupAssigned] : isAssigned) {
    for (auto& [group, assigned] : groupAssigned) {
      if (assigned.getValue() > 0.5) {
        assignment.emplace(scopeItem, group);
        break;
      }
    }
  }

  XLOG(DBG2) << "found exclusive groups assignment with deficit "
             << deficit.getValue() << ", moves " << moves.getValue()
             << ", deviation " << deviation.getValue();

  return assignment;
}

} // namespace facebook::rebalancer
