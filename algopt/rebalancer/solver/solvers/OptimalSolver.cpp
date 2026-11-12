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

#include "algopt/rebalancer/solver/solvers/OptimalSolver.h"

#include "algopt/lp/generic/Expression.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"
#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/solvers/LPStore.h"
#include "algopt/rebalancer/solver/solvers/Solver.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/MovesSummaryHelper.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/Precision.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h>

#include "multifeed/hash/QuickHashMap.h"

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Collect.h>
#include <folly/coro/Task.h>
#include <folly/coro/ViaIfAsync.h>
#include <folly/ExceptionWrapper.h>
#include <folly/lang/Hint.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/ScopeGuard.h>
#include <folly/tracing/AsyncStack.h>
#include <thrift/lib/cpp2/FieldRef.h>
#ifndef _WIN32
#include <folly/Subprocess.h>
#include <folly/system/Shell.h>
#endif

#include <algorithm>
#include <filesystem>
#include <functional>
#include <map>
#include <math.h>
#include <memory>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <stdio.h>
#include <string.h>
#include <tuple>
#include <utility>

#ifndef _WIN32
using namespace folly::literals::shell_literals;
#endif

namespace facebook::rebalancer {

namespace {

bool isTar(const std::filesystem::path& path) {
  auto base = path.extension() == ".gz" ? path.stem() : path;
  return base.extension() == ".tar";
}

bool isKnownBeforeSolve(const std::filesystem::path& path) {
  auto extension = path.extension();
  return extension == ".lp" || extension == ".mps";
}

std::filesystem::path removeExtension(const std::filesystem::path& path) {
  auto extension = path.extension();
  auto extensionless = path.parent_path() / path.stem();
  if (extension == ".gz") {
    return removeExtension(extensionless);
  }
  return extensionless;
}

// For checking constraint validity, we use the feasibility tolerance used by
// the solver and report a warning if the final solution violated those
// tolerances. However, in extreme cases where the constraint is violated by a
// value bigger than the following, we will reject that solution and throw and
// exception. This is done so that we still surface some extreme numeric issues.
constexpr double kMaxPossibleFeasibilityTolerance = 1e-2;

/** checks and print the broken constraints starting at @param node,
 * returns {true, true} if no broken constraints. Otherwise, depending on the
 * extent of violation, it returns {false, false} or {false, true}
 *  NOTE: the constraint feasibility (FEASTOL) tolerance is a "guideline" for
 * Gurobi and it is possible that Gurobi returns a solution that violates a
 * constraint more than the tolerance. Therefore, we will check the constraint
 * validity using maximum possible value of feasibility tolerance.
 * https://docs.gurobi.com/projects/optimizer/en/current/reference/parameters.html#feasibilitytol
 */
std::tuple<bool, bool> checkValidityAndprintBrokenConstraints(
    const ExprPtr& node,
    const Problem& p,
    std::vector<std::string>& brokenExprIds,
    double feasibilityTolerance) {
  bool valid = true;
  bool exceedsMaxViolationTol = false;
  const auto& precision = node->getPrecision();
  if (node->isAnyPositive()) {
    for (const auto& ctr : node->children()) {
      const auto [validCtr, ctrExceedsMaxViolationTol] =
          checkValidityAndprintBrokenConstraints(
              ctr, p, brokenExprIds, feasibilityTolerance);
      valid = valid && validCtr;
      exceedsMaxViolationTol =
          exceedsMaxViolationTol || ctrExceedsMaxViolationTol;
    }
  } else if (precision.isstrictlyGreater(node->value, feasibilityTolerance)) {
    brokenExprIds.emplace_back(std::to_string(node->getId()));
    valid = false;
    XLOGF(
        INFO,
        "Broken constraint: ExprId={}{}; constraint value={}, tolerance used={}",
        node->getId(),
        node->description.empty()
            ? ""
            : fmt::format(" (desc: {})", node->description),
        node->value,
        feasibilityTolerance);

    // check against maximum possible feasibility tolerance
    if (precision.isstrictlyGreater(
            node->value, kMaxPossibleFeasibilityTolerance)) {
      exceedsMaxViolationTol = true;
    }

    XLOG(INFO) << fmt::format("Constraint: {}, ", node->digest(p));
  }
  return {valid, exceedsMaxViolationTol};
}
} // namespace

static void printBanner(
    const Problem& p,
    const interface::OptimalSolverSpec& configs,
    const std::string& message) {
  if (*configs.suppressLogs()) {
    return;
  }
  printf(
      "========%s:%s========\n",
      p.configs.runId.run_id.c_str(),
      message.c_str());
}

static bool isWarmStartSuccessful(algopt::lp::thrift::WarmStartStatus status) {
  switch (status) {
    case algopt::lp::thrift::WarmStartStatus::PROCESSING_ERROR:
    case algopt::lp::thrift::WarmStartStatus::FEASIBLE_SOLUTION_NOT_FOUND:
      return false;
    case algopt::lp::thrift::WarmStartStatus::FEASIBLE_SOLUTION_PROVIDED:
    case algopt::lp::thrift::WarmStartStatus::FEASIBLE_SOLUTION_OBTAINED:
    case algopt::lp::thrift::WarmStartStatus::SOLUTION_IS_DROPPED:
      return true;
    default:
      throw std::runtime_error(
          fmt::format(
              "Unexpected warm start status: {}", fmt::underlying(status)));
  }
}

static interface::EndReason getEndReason(
    algopt::lp::thrift::ProblemStatus status) {
  interface::EndReason endReason;
  switch (status) {
    case algopt::lp::thrift::ProblemStatus::OPTIMAL_FOUND:
      endReason = interface::EndReason::OPTIMAL;
      break;
    default:
      endReason = interface::EndReason::HIT_TIME_LIMIT;
      break;
  }
  return endReason;
}

OptimalSolver::OptimalSolver(const interface::OptimalSolverSpec& inputconfigs) {
  configs = inputconfigs;
}

bool OptimalSolver::solve(Problem& p, Profile profile) {
  const algopt::treeprof::EventRecorder optimalSolve("OptimalSolver::solve");
  PackerSet<entities::ContainerId> dynamicContainers;
  for (auto containerId : p.containers) {
    if (!p.getFixedContainers().contains(containerId)) {
      dynamicContainers.insert(containerId);
    }
  }

  solve(p, std::move(dynamicContainers), profile);
  return true;
}

size_t OptimalSolver::getNumDynamicEquivalenceSets() const {
  return num_dynamic_equivalence_sets;
}

void OptimalSolver::addObjectInContainerConstraints(
    Problem& p,
    LpContext& context) {
  XLOG(DBG1) << "Setting up basic object in container constraints";
  // object assigned to 1 container
  const auto buildRequest = [&](auto it) {
    auto equivSet = *it;
    auto expr = p.lp_store.expression();
    auto totalSize = p.getEquivalenceSets().getSet(equivSet).size();
    for (auto cont : p.containers) {
      if (auto fixedValue =
              p.get_maybe_fixed_assignment_value(context, equivSet, cont)) {
        totalSize -= fixedValue.value();
      } else {
        expr += p.lp_assignment_var(context, equivSet, cont);
      }
    }
    for (auto container : context.dynamicContainers()) {
      p.assignment_var(equivSet, container).setLB(0);
      p.assignment_var(equivSet, container).setUB(totalSize);
    }
    return std::make_pair(expr == totalSize, std::string{"basic"});
  };
  if (p.configs.enableParallelizedLpBuilding ||
      !p.lp_store.getLpProblem().supportsConstraintBatching()) {
    folly::coro::blockingWait(
        CoroUtils::runEachFunc(
            context.dynamicEquivSets().begin(),
            context.dynamicEquivSets().end(),
            [&](auto it) {
              const auto request = buildRequest(it);
              p.lp_store.addConstraint(request.first, request.second);
            },
            p.getExecutorForLpBuilding()));
    return;
  }

  algopt::lp::Problem::ConstraintRequests requests;
  constexpr size_t kConstraintBatchSize = 128;
  requests.reserve(kConstraintBatchSize);
  for (auto it = context.dynamicEquivSets().begin();
       it != context.dynamicEquivSets().end();
       ++it) {
    requests.push_back(buildRequest(it));
    if (requests.size() == kConstraintBatchSize) {
      p.lp_store.addConstraints(requests);
      requests.clear();
    }
  }
  if (!requests.empty()) {
    p.lp_store.addConstraints(requests);
  }
}

static void collectConstraintExprs(
    ExprPtr node,
    Problem& p,
    LpContext& context,
    PackerMap<ExprPtr, std::string>& constraintExprToName,
    const std::string& suffix) {
  if (node->lowerAndUpperBounds(context).upper_bound <= 0) {
    return;
  }

  std::string suffixWithAnnotation = suffix;
  if (auto annotation = node->getSpecAnnotation()) {
    suffixWithAnnotation = fmt::format("{}_{}", suffix, *annotation);
  }
  if (node->isAnyPositive()) {
    for (const auto& constraint : node->children()) {
      collectConstraintExprs(
          constraint, p, context, constraintExprToName, suffixWithAnnotation);
    }
    return;
  }
  auto nameStr = fmt::format("{}_{}", node->getId(), suffixWithAnnotation);
  constraintExprToName.emplace(node, std::move(nameStr));
}

const PackerMap<ExprPtr, std::string> OptimalSolver::getConstraintExprs(
    Problem& problem,
    LpContext& context) {
  PackerMap<ExprPtr, std::string> constraintExprToName;
  collectConstraintExprs(
      problem.constraint, problem, context, constraintExprToName, "root");
  return constraintExprToName;
}

void OptimalSolver::setUpObjectivesAndConstraints(
    Problem& problem,
    LpContext& context) const {
  auto objectiveNodes = getObjectiveExprs(problem);
  auto constraintNodesToNames = getConstraintExprs(problem, context);
  XLOGF(
      DBG1,
      "Setting up {} objective(s) and {} constraints",
      objectiveNodes.size(),
      constraintNodesToNames.size());

  problem.orchestrator_.buildLp(
      objectiveNodes, constraintNodesToNames, problem, context, configs);

  // NOTE: user-defined constraints are added in orchestrator_.buildLp()
  // whenever the lp::Expression of the corresponding node is computed. The main
  // reason is, there are potentially several thousand constraint nodes and
  // adding constraints to the lp model takes considerable time when done
  // serially. Therefore, addition of constraints is moved there so that, when
  // orchestrator uses multiple threads, constraint addition to the model can be
  // parallelized.

  addObjectivesToModel(problem, context, objectiveNodes);

  addObjectInContainerConstraints(problem, context);
}

void OptimalSolver::addObjectivesToModel(
    Problem& problem,
    LpContext& context,
    const std::vector<ExprPtr>& objectiveNodes) const {
  if (objectiveNodes.size() == 1) {
    auto& objectiveLP = context.lpMin().at(objectiveNodes.at(0)->getId());
    return problem.lp_store.setObjective({objectiveLP});
  }

  // multi-objective scenario
  auto& multObjSettings = *configs.multiObjSolveSettings();
  const bool useBlendedObjectives = multObjSettings.solveType() ==
      interface::MultiObjectiveSolveType::BLENDED;
  XLOGF(
      DBG1,
      "Setting up {} {} objectives",
      objectiveNodes.size(),
      useBlendedObjectives ? "blended" : "hierarchical");

  std::vector<algopt::lp::Expression> objectiveLpExprTuple;
  algopt::lp::Expression blendedLpExpr;
  for (auto& objective : objectiveNodes) {
    auto& objLpExpr = context.lpMin().at(objective->getId());
    if (useBlendedObjectives) {
      blendedLpExpr += objLpExpr;
    } else {
      objectiveLpExprTuple.push_back(objLpExpr);
    }
  }

  if (useBlendedObjectives) {
    return problem.lp_store.setObjective({std::move(blendedLpExpr)});
  }

  algopt::lp::MultiObjConfig multiObjConfig;
  multiObjConfig.higherPriObjConfig =
      multObjSettings.higherPriorityObjConfig().to_optional();

  if (multObjSettings.paramNamesToValues().has_value()) {
    for (const auto& [paramName, value] :
         *multObjSettings.paramNamesToValues()) {
      multiObjConfig.paramNamesToValues[paramName] = value;
    }
  }

  problem.lp_store.setMultiObjConfig(std::move(multiObjConfig));

  return problem.lp_store.setObjective(objectiveLpExprTuple);
}

const std::vector<ExprPtr> OptimalSolver::getObjectiveExprs(Problem& p) const {
  std::vector<ExprPtr> objectiveTuple;
  if (p.objective.size() == 1) {
    objectiveTuple.push_back(p.objective.getOnlyObjective());
    return objectiveTuple;
  }

  // Multi-objective case, based on config settings, collect appropriate
  // objectives for the MIP solver
  auto& multObjSettings = *configs.multiObjSolveSettings();
  // solve for objectives in range [start, end)
  const int start = multObjSettings.firstObjectiveIdx()
      ? *multObjSettings.firstObjectiveIdx()
      : 0;
  const int end = multObjSettings.lastObjectiveIdx()
      ? *multObjSettings.lastObjectiveIdx() + 1
      : p.objective.size();

  auto filteredObjectives = p.objective.getView(start, end);
  for (const auto& objective : filteredObjectives) {
    objectiveTuple.push_back(objective);
  }

  return objectiveTuple;
}

interface::EndReason OptimalSolver::buildAndSolveMIPModel(
    Problem& p,
    LpContext& context,
    Profile profile) {
  interface::OptimalSolverProfile solverProfile;
  algopt::Timer profileTimer(true);
  const bool useSimplifier = configs.simplifyLpProblem().value();

  algopt::treeprof::EventRecorder mipModelBuilding("Building MIP model");
  p.lp_store.reset(
      configs.solverPackage().value(),
      useSimplifier,
      context,
      p.getExecutorForLpBuilding());

  num_dynamic_equivalence_sets = context.dynamicEquivSets().size();
  initialize(p);

  XLOGF(
      INFO,
      "Modeling {} dynamic containers and {} dynamic object equivalence sets",
      context.dynamicContainers().size(),
      context.dynamicEquivSets().size());

  solverProfile.dynamicContainers() = context.dynamicContainers().size();
  solverProfile.dynamicEquivalenceSets() = context.dynamicEquivSets().size();

  setUpObjectivesAndConstraints(p, context);

  solverProfile.xpressBuildSec() = profileTimer.resetSeconds();

  // If requested, set start values for all variables
  const bool useInitialHint = !*configs.skipInitialAssignmentHint();
  if (useInitialHint) {
    XLOG(DBG1) << "Creating variable start values";
    addVariableStartValues(p, context);
  }

  XLOG(DBG1) << "Completed setup";
  mipModelBuilding.stop();

  if (*configs.skipMipSolveForTesting()) {
    if (profile) {
      solverProfile.xpressMipOptimizeSec()->push_back(0);
      profile->get().optimalSolverProfile() = std::move(solverProfile);
    }
    return interface::EndReason::HIT_TIME_LIMIT;
  }

  XLOG(DBG1) << "Starting assignment solve";
  printBanner(p, configs, "Starting assignment solve");

  auto resultPerObjective = mipOptimize(p, context);
  auto problemStatus = p.lp_store.getLpProblem().getStatus();

  if (profile) {
    updateSolverProfile(resultPerObjective, solverProfile, problemStatus);
    solverProfile.xpressMipOptimizeSec()->push_back(
        profileTimer.resetSeconds());
    profile->get().optimalSolverProfile() = std::move(solverProfile);
  }
  return getEndReason(problemStatus);
}

void OptimalSolver::solve(
    Problem& p,
    PackerSet<entities::ContainerId> dynamicContainers,
    Profile profile) {
  if (p.initial_assignment.getObjects().size() == 0) {
    XLOG(DBG1) << "No objects; nothing to solve";
    return;
  }
  const facebook::algopt::Timer timer(true);
  auto dynamicEquivalenceSets = p.getDynamicEquivalentSets(dynamicContainers);
  auto dynamicChildren = p.orchestrator_.getDynamicChildren(dynamicContainers);
  LpContext context(
      std::move(dynamicContainers),
      std::move(dynamicEquivalenceSets),
      std::move(dynamicChildren));

  auto endReason = buildAndSolveMIPModel(p, context, profile);

  if (*configs.skipMipSolveForTesting()) {
    XLOG(INFO) << "Skipped MIP solve, no changes to apply";
    return;
  }

  const algopt::treeprof::EventRecorder applySolution(
      "Applying final solution");

  XLOG(DBG1) << "Starting constructing changes from solution";
  auto changes = createChangesFromSolution(
      p, context.dynamicContainers(), context.dynamicEquivSets());
  XLOG(DBG1) << "Finished constructing changes from solution";
  if (profile) {
    if (auto optimalProfile = profile->get().optimalSolverProfile()) {
      optimalProfile->postProcessingSec() = timer.getSeconds();
    }
  }

  applyChanges(p, changes);

  auto solverSummary = SolverSummary{
      .solverType = SolverType::OPTIMAL,
      .solverPackage = std::string(p.lp_store.getLpProblem().backendName()),
      .endReason = endReason,
      .auxInfo = std::to_string(timer.getSeconds()),
  };
  p.configs.logger->log(std::move(solverSummary));
}

ChangeSet OptimalSolver::findOptimalChangeSet(
    Problem& p,
    PackerSet<entities::ContainerId> dynamicContainers,
    Profile profile) {
  auto dynamicEquivalenceSets = p.getDynamicEquivalentSets(dynamicContainers);
  auto dynamicChildren = p.orchestrator_.getDynamicChildren(dynamicContainers);
  LpContext context(
      std::move(dynamicContainers),
      std::move(dynamicEquivalenceSets),
      std::move(dynamicChildren));

  buildAndSolveMIPModel(p, context, profile);
  // extract changes from MIP model solution, the changes may be applied later
  // by the calling function
  // TODO (@nks): the current way of extracting moves may be quite slow as it
  // iterates on all the objects and containers. We will need to speed it up by
  // only looking at relevant objects in the subset
  XLOG(DBG1) << "Constructing changes from the solution";
  auto changes = createChangesFromSolution(
      p, context.dynamicContainers(), context.dynamicEquivSets());
  return changes;
}

void OptimalSolver::updateSolverProfile(
    std::vector<algopt::lp::thrift::ProblemResult>& resultPerObjective,
    interface::OptimalSolverProfile& solverProfile,
    algopt::lp::thrift::ProblemStatus problemStatus) {
  if (resultPerObjective.size() == 0) {
    throw std::runtime_error("expected at least one problemResult");
  }

  // save some stats w.r.t. to the first objective---i.e., the most important
  // objective (e.g., warmStartResult )
  auto& resultForFirstObjective = resultPerObjective.at(0);
  solverProfile.gap()->absolute() = *resultForFirstObjective.absoluteGap();
  solverProfile.gap()->relative() = *resultForFirstObjective.relativeGap();

  if (resultForFirstObjective.warmStartResult().has_value()) {
    const algopt::lp::thrift::WarmStartResult& warmStartResult =
        resultForFirstObjective.warmStartResult().value();
    solverProfile.isWarmStartSuccessful() =
        isWarmStartSuccessful(*warmStartResult.status());
    solverProfile.warmStartProcessingTimeSec() =
        *warmStartResult.processingTimeInSecs();
  }

  if (problemStatus == algopt::lp::thrift::ProblemStatus::SOLUTION_NOT_FOUND) {
    solverProfile.noFeasibleSolution() = true;
  }

  // save solverStats w.r.t. every objective
  std::vector<interface::SolverStatsForObjective> solverStatsForObjectives;
  for (auto& result : resultPerObjective) {
    interface::SolverStatsForObjective objectiveStats;
    objectiveStats.gap()->absolute() = *result.absoluteGap();
    objectiveStats.gap()->relative() = *result.relativeGap();
    objectiveStats.solverFoundSolution() =
        result.status() == algopt::lp::thrift::ProblemStatus::SOLUTION_NOT_FOUND
        ? false
        : true;
    solverStatsForObjectives.push_back(std::move(objectiveStats));
  }

  solverProfile.solverStatsForObjectives() =
      std::move(solverStatsForObjectives);
}

void OptimalSolver::addVariableStartValues(Problem& p, LpContext& context) {
  for (const entities::EquivalenceSetId equiv_set :
       context.dynamicEquivSets()) {
    const auto& equivalentSetsObjects =
        p.getEquivalenceSets().getSet(equiv_set);

    for (auto cont : context.dynamicContainers()) {
      const auto& containerObjects = p.assignment.getObjects(cont);

      size_t intersection = 0;
      if (containerObjects.size() < equivalentSetsObjects.size()) {
        for (auto obj : containerObjects) {
          intersection += equivalentSetsObjects.contains(obj) ? 1 : 0;
        }
      } else {
        for (auto obj : equivalentSetsObjects) {
          intersection += containerObjects.contains(obj) ? 1 : 0;
        }
      }

      p.lp_store.addVarStartValue(
          p.assignment_var(equiv_set, cont), intersection);
    }
  }
}

void OptimalSolver::initialize(Problem& p) {
  // Tolerance adjustment
  algopt::lp::Tolerances tols;
  // The default values for mipValuehave a 5:1 ratio here, so this
  // preserves that ratio.
  tols.constraint = *configs.xpressTolerance() / 5;
  tols.integer = *configs.xpressTolerance();
  if (*configs.suppressLogs()) {
    p.lp_store.disableLogs();
  }
  p.lp_store.setTolerances(tols);
  for (auto& part : *configs.xpressArgs()) {
    p.lp_store.updateParam(part.first, part.second);
  }
}

std::vector<algopt::lp::thrift::ProblemResult> OptimalSolver::mipOptimize(
    Problem& p,
    [[maybe_unused]] LpContext& context) {
  const algopt::treeprof::EventRecorder mipModelOptimize(
      "MIP model optimization");
  if (*configs.printFullLp()) {
    p.lp_store.print();
  }

  if (configs.solveTime()) {
    if (*configs.solveTime() < 0) {
      throw std::runtime_error("Negative solve times not supported.");
    } else {
      // For some tests and benchmarks, we might just want to setup MIP but
      // not actually run the solver. So we may have *configs.solveTime_ref() =
      // 0. In such cases, we approximate solveTime by 1 second. This may change
      // if we need special handling for solveTime = 0 case.
      const double solveTime = std::max(double(*configs.solveTime()), 1.0);
      p.lp_store.setMaxSolveTime(solveTime);
    }
  }

  if (*configs.xpressLogFile() != "") {
    p.lp_store.setLogfile(*configs.xpressLogFile());
  }

  saveDebugDataPreSolve(p);

  p.lp_store.solve();

  saveDebugDataPostSolve(p);

  p.lp_store.checkInfeasible(p);
  return p.lp_store.getLpProblem().getResultPerObjective();
}

ChangeSet OptimalSolver::createChangesFromSolution(
    Problem& p,
    const PackerSet<entities::ContainerId>& dynamic_containers,
    const PackerSet<entities::EquivalenceSetId>& dynamic_equiv_sets) {
  if (p.lp_store.getLpProblem().getStatus() ==
      algopt::lp::thrift::ProblemStatus::SOLUTION_NOT_FOUND) {
    XLOG(DBG1) << "The solver was unable to find a solution. "
               << "Falling back to the initial assignment as final solution.";
    return {};
  }

  PackerMap<entities::EquivalenceSetId, PackerMap<entities::ContainerId, int>>
      new_equiv_set_to_cont_map;
  for (auto cont : p.initial_assignment.getContainers()) {
    for (auto obj : p.initial_assignment.getObjects(cont)) {
      const auto equiv_set = p.getEquivalenceSets().at(obj);
      new_equiv_set_to_cont_map[equiv_set][cont] = 0;
    }
  }

  for (auto cont : p.assignment.getContainers()) {
    for (auto obj : p.assignment.getObjects(cont)) {
      const auto equiv_set = p.getEquivalenceSets().at(obj);
      if (!dynamic_containers.contains(cont) ||
          !dynamic_equiv_sets.contains(equiv_set)) {
        new_equiv_set_to_cont_map[equiv_set][cont] += 1;
      }
    }
  }
  for (const auto& [equiv_set, container_to_variable] :
       p.lp_store.getLpVars()) {
    for (const auto& [cont, var] : container_to_variable) {
      new_equiv_set_to_cont_map[equiv_set][cont] = round(var.getValue());
    }
  }

  auto new_assignment = p.getStableObjectToContainerAssignment(
      new_equiv_set_to_cont_map, p.initial_assignment, p.configs.runId.run_id);

  ChangeSet changes;
  for (auto cont : p.assignment.getContainers()) {
    for (auto obj : p.assignment.getObjects(cont)) {
      auto fromContainer = p.assignment.getContainer(obj);
      auto toContainer = new_assignment.at(obj);
      if (fromContainer != toContainer) {
        changes.insert(Change(obj, fromContainer, -1));
        changes.insert(Change(obj, toContainer, 1));
      }
    }
  }
  XLOG(DBG1) << "Changes size " << changes.size();
  return changes;
}

void OptimalSolver::applyChanges(Problem& p, const ChangeSet& changes) {
  auto feasibilityTolerance = getFeasibilityTolerance(p);
  const bool initiallyValid = p.constraint->value <= feasibilityTolerance;
  // At the moment, algopt::kEpsilon is hardcoded in
  // rebalancer expressions. For example, ANY_POSITIVE expressions are
  // considered violated if the value is higher than
  // algopt::kEpsilon. But Optimal solver internally uses
  // user provided feasibility tolerance. This mismatch may make the solution
  // invalid even when things are working as expected.
  // Therefore, Optimal solver will use its own function
  // 'checkValidityAndprintBrokenConstraint' to verify if the result is invalid
  // because of broken constraints.
  const auto [oldValue, newValue] = p.apply(changes);
  bool appliedChanges = !changes.empty();
  const auto& precision = p.getUniverse().getPrecision();

  // LP constraint names use exprIds as substring, we will collect all the
  // broken exprIds so that we can filter relevant constraints from the LP model
  std::vector<std::string> brokenExprIds;
  const auto [valid, violationExceedsMaxTolerance] =
      checkValidityAndprintBrokenConstraints(
          p.constraint, p, brokenExprIds, feasibilityTolerance);
  if (!valid) {
    // print the constraints that rebalancer thinks are broken
    XLOG(WARNING)
        << "Applying LP solution resulted in some broken constraints.";
    XLOG(WARNING) << "Broken constraints in underlying LP model:";
    p.lp_store.print(brokenExprIds);
    if (violationExceedsMaxTolerance) {
      // still throw exception if the violation is higher than max tolerance to
      // uncover obvious violations
      throw std::runtime_error(
          fmt::format(
              "Optimal solver found an invalid solution that violated max feasibility tolerance of {}",
              kMaxPossibleFeasibilityTolerance));
    }
  }

  // Revert the moves if the constraint is initially valid and the solution
  // found is worse or neutral.
  if (initiallyValid && appliedChanges) {
    const auto improved =
        GlobalObjectiveValue::isStrictlyBetter(newValue, oldValue, precision);
    const auto worsened =
        GlobalObjectiveValue::isStrictlyBetter(oldValue, newValue, precision);
    if (!improved && !worsened) {
      XLOG(INFO)
          << "Reverting " << changes.size()
          << " useless changes, that optimal produced due to precision differences.";
      p.apply(reverseChanges(changes));
      appliedChanges = false;
    } else if (worsened) {
      auto isWorse = true;
      auto higherPriorityObjConfigOpt =
          configs.multiObjSolveSettings()->higherPriorityObjConfig();
      // An uninitialized value represents +inf. It is always worse than an
      // initialized value, but there are no tuple positions on which to apply
      // an allowed-worsening override.
      if (higherPriorityObjConfigOpt.has_value() &&
          newValue.size() == oldValue.size()) {
        const auto& higherPriorityObjConfig =
            higherPriorityObjConfigOpt.value();
        const auto& tuplePosToAllowedWorsening =
            *higherPriorityObjConfig.tuplePosToAllowedWorsening();
        for (const auto i : folly::irange(static_cast<int>(oldValue.size()))) {
          const auto newI = newValue.get(i);
          const auto oldI = oldValue.get(i);
          const auto allowedWorsenPtr =
              folly::get_ptr(tuplePosToAllowedWorsening, i);
          // if tuple is not allowed to worsen, then the check if new > old
          const auto cond1 = allowedWorsenPtr == nullptr &&
              precision.isstrictlyGreater(newI, oldI);
          // if tuple is allowed to worsen, then the check if new >
          // allowedWorsenUntilValue
          const auto cond2 = allowedWorsenPtr != nullptr &&
              precision.isstrictlyGreater(
                  newI,
                  algopt::common::thriftUtils::getAllowedWorsenUntilValue(
                      oldI, *allowedWorsenPtr));
          if (cond1 || cond2) {
            break; // implies that solution is worse and isWorse remains true
          }
        }
        // if we reach here, then all the values are within the allowedWorsen
        // value
        isWorse = false;
      }

      if (isWorse) {
        XLOGF(
            WARNING,
            "Reverting {} changes, optimal solver found solution {}, which is worse than initial {}",
            changes.size(),
            newValue.toString(),
            oldValue.toString());
        p.apply(reverseChanges(changes));
        appliedChanges = false;
      }
    }
  }

  if (appliedChanges) {
    p.configs.logger->log(
        MovesSummaryHelper::makeMovesSummary(
            p,
            MoveSet::fromChangeSet(changes),
            /*objectiveDeltas=*/nullptr,
            MoveStatsAggregator(precision)));
  }
}

ChangeSet OptimalSolver::reverseChanges(const ChangeSet& changes) {
  ChangeSet reverse;

  for (int i = changes.size() - 1; i >= 0; --i) {
    auto& change = changes.at(i);
    reverse.insert(
        Change(change.getObject(), change.getContainer(), -change.getValue()));
  }
  return reverse;
}

double OptimalSolver::getFeasibilityTolerance(Problem& p) {
  auto tols = p.lp_store.getTolerances();
  return tols.constraint;
}

void OptimalSolver::saveDebugDataPreSolve(Problem& p) const {
  const auto& filename = *configs.solverOutputFiles()->solverProblemFile();
  if (filename.empty()) {
    return;
  }

  const std::filesystem::path path(filename);
  auto extension = path.extension();
  auto extensionless = removeExtension(path);
  if (isTar(path)) {
    // If we are creating a bundle, then create a temporary directory with the
    // same name where the solver will save all debug files.
    std::filesystem::create_directory(extensionless);
    p.lp_store.getLpProblem().setDebugPath(extensionless.string());
  } else if (isKnownBeforeSolve(path)) {
    // If the data is known before solving, then we save it now so we can
    // obtain the fail even if the solver crashes.
    p.lp_store.getLpProblem().saveToFile(filename);
  }
}

void OptimalSolver::saveDebugDataPostSolve(Problem& p) const {
  const auto& filename = *configs.solverOutputFiles()->solverProblemFile();
  if (filename.empty()) {
    return;
  }

  const std::filesystem::path path(filename);
  auto extension = path.extension();
  auto extensionless = removeExtension(path);
  if (isTar(path)) {
#ifdef _WIN32
    throw std::runtime_error("Tar bundle saving is not supported on Windows");
#else
    // Compress the files in the temporary directory into a bundle.
    const auto exitStatus = folly::Subprocess(
                                "tar -C {} -c{}f {} $(ls {})"_shellify(
                                    extensionless.string(),
                                    extension == ".gz" ? "z" : "",
                                    filename,
                                    extensionless.string()))
                                .wait()
                                .exitStatus();
    if (exitStatus) {
      throw std::runtime_error(
          fmt::format("Failed to compress tar file, errorcode={}", exitStatus));
    }
    // Remove the temporary directory.
    std::filesystem::remove_all(extensionless);
#endif
  } else if (!isKnownBeforeSolve(path)) {
    // If the file was not saved before solving, then save it now.
    p.lp_store.getLpProblem().saveToFile(filename);
  }
}

} // namespace facebook::rebalancer
