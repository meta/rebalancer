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

#include "algopt/rebalancer/interface/CoreSolver.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/common/log/fb/ScubaLog.h"
#endif
#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/common/log/StreamLog.h"
#include "algopt/rebalancer/common/RebalancerExcep.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/materializer/Materializer.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/solvers/SolverFactory.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "algopt/rebalancer/treeprof/visualizer/EventTreeVisualizer.h"

#include <folly/debugging/exception_tracer/SmartExceptionTracer.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/folly_dynamic/folly_dynamic.h>

#include <memory>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

using namespace facebook;
using namespace facebook::rebalancer;
using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;
using namespace facebook::rebalancer::materializer;

std::shared_ptr<folly::CPUThreadPoolExecutor> getSerialExecutor() {
  return std::make_shared<folly::CPUThreadPoolExecutor>(1);
}

void checkSummaryCount(size_t count, std::string_view name) {
  if (count != 2) {
    throw std::runtime_error(
        fmt::format("expected 2 '{}' summaries but found {}", name, count));
  }
}

void validateDataForSolution(const LogCollector::Data& data) {
  checkSummaryCount(data.objectiveSummaries.size(), "objective");
  checkSummaryCount(data.constraintSummaries.size(), "constraint");
  if (!data.metrics.empty()) {
    checkSummaryCount(data.metrics.size(), "metric");
  }
}

void logIfThereAreNegativeDimensionValues(
    RebalancerLog& logger,
    const Universe& universe) {
  logger.log(
      GenericInfo{
          .key = "object_dimension_with_negative_values",
          .value = (double)universe.getObjects().hasNegativeDimensions()});
}

void printAndLogProblemDefinition(
    std::shared_ptr<const Universe> universe,
    LogCollector& logCollector) {
  ProblemDefinition problemDefinition;
  const auto& objects = universe->getObjects();
  problemDefinition.objectName = universe->getObjectTypeName();
  problemDefinition.containerName = universe->getContainerTypeName();
  problemDefinition.objectCount = objects.getNumObjects();
  problemDefinition.containerCount =
      universe->getContainers().getContainerIds().size();
  problemDefinition.scopeCount = universe->getScopeIds().size();
  problemDefinition.partitionCount = universe->getPartitionIds().size();
  problemDefinition.dimensionCount = objects.getDimensionCount();
  problemDefinition.goalCount = universe->getGoals().getGoalIds().size();
  problemDefinition.constraintCount =
      universe->getConstraints().getConstraintIds().size();

  XLOG(INFO) << "Objects: " << problemDefinition.objectCount;
  XLOG(INFO) << "Containers: " << problemDefinition.containerCount;
  XLOG(INFO) << "Scopes: " << problemDefinition.scopeCount;
  XLOG(INFO) << "Partitions: " << problemDefinition.partitionCount;
  XLOG(INFO) << "Goals: " << problemDefinition.goalCount;
  XLOG(INFO) << "Constraints: " << problemDefinition.constraintCount;

  logCollector.log(problemDefinition);
}

SolutionStats makeSolutionStats(
    const ProblemProfile& problemProfile,
    size_t equivalenceSetCount,
    size_t totalMoveCount) {
  SolutionStats solutionStats;
  solutionStats.equivalentSetCount = static_cast<int>(equivalenceSetCount);
  solutionStats.totalMoves = static_cast<int>(totalMoveCount);

  if (auto optimalSolverProfile = problemProfile.optimalSolverProfile()) {
    solutionStats.relativeObjectiveGap =
        *optimalSolverProfile->gap()->relative();
  }

  return solutionStats;
}

void logSummary(
    const MaterializedProblem& materialized,
    LogCollector& logger,
    const entities::Universe& universe,
    const Assignment& assignment,
    bool solved) {
  // Objective summary.
  interface::GlobalObjectiveSummary finalGlobalObjectiveSummary;
  for (auto& singleObjective : materialized.labeledObjectives) {
    finalGlobalObjectiveSummary.goals()->push_back(
        singleObjective.getSummary());
  }
  finalGlobalObjectiveSummary.solved() = solved;
  logger.log(std::move(finalGlobalObjectiveSummary));

  // Constraint summary.
  auto finalConstraintSummary =
      materialized.labeledUserConstraints.getSummary();
  finalConstraintSummary.solved() = solved;
  logger.log(std::move(finalConstraintSummary));

  // log metrics if they were collected
  if (materialized.metrics) {
    const algopt::treeprof::EventRecorder coreSolveEvent("log metrics");
    logger.log(materialized.metrics->getSummary(universe, assignment));
  }
}

} // namespace

namespace facebook {
namespace rebalancer {
namespace interface {

using Timer = facebook::algopt::Timer;

void CoreSolver::printAndLogHierarchicalProfile(
    std::shared_ptr<const algopt::treeprof::Event> hierarchyTreeRoot,
    ProblemProfile& problemProfile,
    std::shared_ptr<RebalancerLog> logger) {
  // TODO: also log tree to scuba so that it can be visualized using icicle
  // format for now just print to stdout
  std::shared_ptr<algopt::treeprof::VisualizationFilter> filter = nullptr;
  if (!XLOG_IS_ON(DBG2)) {
    // This means we have logLevel = INFO or DBG1
    // In this case, only show nodes that have runtime least 5% of total OR
    // memory at least 5%
    double runTimeThreshold = 0.05 * hierarchyTreeRoot->duration();
    double memoryThreshold = 0.05 * hierarchyTreeRoot->getMemoryPeak() /
        algopt::treeprof::NUM_BYTES_IN_ONE_MB;
    // lower bound by small value to only report non-zero values
    memoryThreshold = std::max(memoryThreshold, 1e-6);
    filter = std::make_shared<algopt::treeprof::Or>(
        std::make_shared<algopt::treeprof::RuntimeAtLeastX>(runTimeThreshold),
        std::make_shared<algopt::treeprof::PeakMemoryAtLeastXMBytes>(
            memoryThreshold));

    XLOG(INFO) << "Visualizing Hierarchical profile with :";
    XLOG(INFO) << fmt::format(
        " - Nodes with runtime >= 5% of total = {:.2f}s,  OR",
        runTimeThreshold);
    XLOG(INFO) << fmt::format(
        " - Nodes with nonzero memory >= 5% of total = {:.2f} MBs\n",
        memoryThreshold);
  }
  algopt::treeprof::EventTreeVisualizer visualizer(hierarchyTreeRoot, filter);

  XLOG(INFO) << "Rebalancer Hierarchical Time Profile: \n"
             << visualizer.digest(
                    algopt::treeprof::EventTreeVisualizer::MetricType::RUNTIME);
  XLOG(INFO) << "Rebalancer Peak Memory Contribution Profile: \n"
             << visualizer.digest(
                    algopt::treeprof::EventTreeVisualizer::MetricType::MEMORY);

#ifndef REBALANCER_OSS_BUILD
  problemProfile.hierarchicalProfileRoot() =
      ScubaLog::serializeEventHierarchyTree(
          hierarchyTreeRoot, visualizer.getEventVisibility());
  if (logger) {
    const auto& root = *problemProfile.hierarchicalProfileRoot();
    logger->log(root);
    logger->log(
        PerformanceInfo{
            .totalSolveTime = *root.duration(),
            .peakMemoryUsed = *root.maxInclusiveMemory()});
  }
#endif
}

AssignmentSolution CoreSolver::solve(
    const AssignmentProblem& problemSpec,
    std::shared_ptr<folly::ThreadPoolExecutor> executor,
    bool enableParallelizedNewMaterializer,
    std::shared_ptr<const entities::Universe> universe,
    std::shared_ptr<RebalancerLog> logger,
    std::shared_ptr<const InvalidMoveFilter> learnedInvalidMoveFilter) {
  if (problemSpec.runId()->empty()) {
    throw std::runtime_error(
        "Expect run_id to be set before calling CoreSolver::solve()");
  }
  XLOG(INFO) << "Problem run_uuid: " << *problemSpec.runId();
  if (logger) {
    logger->log(
        GenericInfo{
            .key = "rollout_config",
            .value = ::facebook::thrift::to_dynamic(
                *problemSpec.rolloutConfig(),
                ::facebook::thrift::dynamic_format::JSON_1)});
    logger->log(
        GenericInfo{
            .key = "using_parallelized_new_materializer",
            .value = (double)enableParallelizedNewMaterializer});
    logger->log(
        GenericInfo{
            .key = "using_parallel_bounds_computation",
            .value =
                *problemSpec.enableParallelBoundsComputation() ? 1.0 : 0.0});
    logger->log(
        GenericInfo{
            .key = "using_dynamic_object_ordering",
            .value = *problemSpec.useDynamicObjectOrdering() ? 1.0 : 0.0});
  }
  try {
    const algopt::treeprof::EventRecorder coreSolveEvent("CoreSolver::Solve");
    AssignmentSolution solution = materializeAndSolve(
        problemSpec,
        executor,
        logger,
        enableParallelizedNewMaterializer,
        universe,
        std::move(learnedInvalidMoveFilter));
    if (logger) {
      logger->log(ExitStatusInfo{.hasException = false, .exceptionMsg = ""});
    }
    return solution;
  } catch (const std::exception& e) {
    const bool userError =
        dynamic_cast<const RebalancerUserError*>(&e) != nullptr;
    std::stringstream exceptionInfo;
#ifdef FOLLY_HAVE_SMART_EXCEPTION_TRACER
    exceptionInfo << folly::exception_tracer::getTrace(e);
#else
    exceptionInfo << "(Detailed stacktrace is missing on this platform)";
#endif
    const auto details =
        fmt::format("Exception: {} {}", e.what(), exceptionInfo.str());
    XLOG(ERR) << details;
    if (logger) {
      logger->log(
          ExitStatusInfo{
              .hasException = true,
              .exceptionMsg = details,
              .isUserError = userError});
    }
    throw;
  }
}

AssignmentSolution CoreSolver::materializeAndSolve(
    const AssignmentProblem& problemSpec,
    std::shared_ptr<folly::ThreadPoolExecutor> executor,
    std::shared_ptr<RebalancerLog> logger,
    bool enableParallelizedNewMaterializer,
    std::shared_ptr<const entities::Universe> universe,
    std::shared_ptr<const InvalidMoveFilter> learnedInvalidMoveFilter) {
  Timer timer(true);
  std::vector<std::shared_ptr<RebalancerLog>> loggers = {
      std::make_shared<StreamLog>()};
  if (logger) {
    loggers.push_back(logger);
  }
  const auto logCollector = std::make_shared<LogCollector>(std::move(loggers));

  algopt::treeprof::EventRecorder materializationEvent("Materialization");

  printAndLogProblemDefinition(universe, *logCollector);

  // TODO (T180294479): remove this once we add a check in ProblemChecker to
  // prevent object dimensions with negative values
  if (logger) {
    logIfThereAreNegativeDimensionValues(*logger, *universe);
  }

  XLOG(INFO) << "Materializing problem...";
  auto solver = SolverFactory::createSolver(*problemSpec.strategy());

  algopt::treeprof::EventRecorder materialize("Calling materializer");
  auto materializerExecutor =
      enableParallelizedNewMaterializer ? executor : getSerialExecutor();

  XLOGF(
      INFO,
      "Using parallelized materializer = {}; number of threads = {}",
      enableParallelizedNewMaterializer,
      materializerExecutor->numThreads());

  auto wrappedExecutor =
      std::make_shared<algopt::treeprof::ExecutorWrapper>(materializerExecutor);

  auto materialized = materializer::Materializer::materialize(
      wrappedExecutor,
      universe,
      solver->needs_continuous_expressions(),
      logCollector,
      *problemSpec.publishMetrics(),
      *problemSpec.rolloutConfig()->enableInvalidMoveFilter() &&
          solver->needsInvalidMoveFilter(),
      *problemSpec.rolloutConfig()->useSeparatedLocalSearchPenaltyObjective() &&
          solver->needs_continuous_expressions(),
      enableParallelizedNewMaterializer);
  materialize.stop();

  // TODO: simplify the logic below this line once the old materializer is gone
  XLOG(INFO) << "Setting up the solver...";

  // TODO: remove the concept of ProblemConfigs
  auto config = makeProblemConfig(
      problemSpec, logCollector, executor, enableParallelizedNewMaterializer);
  config.learnedInvalidMoveFilter = std::move(learnedInvalidMoveFilter);

  algopt::treeprof::EventRecorder initProblemEvent("Initialize Problem");

  Problem problem(universe, materialized, config, logCollector);
  initProblemEvent.stop();

  if (auto decompositionScopeName = problemSpec.decompositionScopeName()) {
    initSubproblemDecomposition(problem, *universe, *decompositionScopeName);
  }

  // Initial summary.
  logSummary(
      *materialized,
      *logCollector,
      *universe,
      problem.initial_assignment,
      false);

  // Solve.

  AssignmentSolution solution;

  // All processing done before solver invocation is considered materialization.
  solution.problemProfile()->materializationSec() = timer.resetSeconds();
  materializationEvent.stop();

  solver->solve(problem, *solution.problemProfile());
  solution.problemProfile()->solveSec() = timer.resetSeconds();

  // Update user constraints with final assignment.
  algopt::treeprof::EventRecorder updateUserConstraintsEvent(
      "Update user constraints with final assignment");
  problem.fullApplyReusingOrchestratorValues(*materialized->userConstraintSum);
  updateUserConstraintsEvent.stop();

  // Final summary.
  const algopt::treeprof::EventRecorder logEvent("log solution summary");
  logSummary(*materialized, *logCollector, *universe, problem.assignment, true);

  solution.runId() = *problemSpec.runId();

  auto insertContainerNameForEachObject =
      [&universe](auto& map, const auto& objectIds, const auto& containerName) {
        for (auto objectId : objectIds) {
          map.emplace(universe->getEntityName(objectId), containerName);
        }
      };
  auto objectCount = universe->getNumObjects();
  solution.initialAssignment()->reserve(objectCount);
  solution.assignment()->reserve(objectCount);
  auto& containers = universe->getContainers();
  for (auto containerId : containers.getContainerIds()) {
    auto& initialObjectIds = containers.getInitialObjectIds(containerId);
    auto& finalObjectIds = problem.assignment.getObjects(containerId);
    auto& containerName = universe->getEntityName(containerId);

    insertContainerNameForEachObject(
        *solution.initialAssignment(), initialObjectIds, containerName);
    insertContainerNameForEachObject(
        *solution.assignment(), finalObjectIds, containerName);
  }

  if (*problemSpec.publishEquivalenceSetsInfo()) {
    solution.equivalenceSetInfo() = problem.makeEquivalenceSetInfo();
  }

  solution.numContainers() = problem.containers.size();

  logCollector->log(makeSolutionStats(
      *solution.problemProfile(),
      problem.getEquivalenceSets().size(),
      logCollector->getTotalMoveCount()));

  populateSolutionFromLogs(solution, *logCollector);

  return solution;
}

ProblemConfigs CoreSolver::makeProblemConfig(
    const AssignmentProblem& problemSpec,
    std::shared_ptr<LogCollector> logger,
    std::shared_ptr<folly::ThreadPoolExecutor> executor,
    bool enableParallelMaterializer) {
  ProblemConfigs problemConfig{
      .threadPool = std::move(executor),
      .logger = std::move(logger),
      .runId = {}};
  problemConfig.moveStatsSpec = *problemSpec.moveStatsSpec();
  problemConfig.runId.scope = *problemSpec.scope();
  problemConfig.runId.service = *problemSpec.service_();
  problemConfig.runId.run_id = *problemSpec.runId();
  problemConfig.useDynamicObjectOrdering =
      *problemSpec.useDynamicObjectOrdering();
  problemConfig.enableParallelizedBoundsComputing =
      *problemSpec.enableParallelBoundsComputation();
  problemConfig.useIncrementalPriorityQueueV2 =
      *problemSpec.rolloutConfig()->useIncrementalPriorityQueueV2();
  problemConfig.validateAppliedMoves = *problemSpec.validateAppliedMoves();
  problemConfig.rolloutPruneOptimalSubgraphs =
      *problemSpec.rolloutConfig()->pruneOptimalSubgraphs();
  problemConfig.useAutoParallelExecution =
      *problemSpec.rolloutConfig()->useAutoParallelExecution();

  if (auto decompositionScopeName = problemSpec.decompositionScopeName()) {
    problemConfig.decompositionScopeName = *decompositionScopeName;
  }

  // Currently LP building parallelization can only be used when going through
  // GenericProblemImpl (and not when directly building the Xpress or Gurobi
  // Problem) and when enableParallelMaterializer is set to 'true'
  if (enableParallelMaterializer) {
    auto checkIfLpCanBeParallelized = [&](const auto& optimalSolverSpec) {
      if (*optimalSolverSpec.simplifyLpProblem()) {
        problemConfig.enableParallelizedLpBuilding = true;
      }
    };

    const auto& solverType = problemSpec.strategy()->solvers()->at(0).getType();
    if (solverType == SolverT::Type::optimalSolverSpec) {
      const auto& optimalSolverSpec =
          problemSpec.strategy()->solvers()->at(0).optimalSolverSpec().value();
      checkIfLpCanBeParallelized(optimalSolverSpec);
    } else if (solverType == SolverT::Type::optimalSubsetSolverSpec) {
      const auto& optimalSolverSpec = *problemSpec.strategy()
                                           ->solvers()
                                           ->at(0)
                                           .optimalSubsetSolverSpec()
                                           ->optimalConfig();
      checkIfLpCanBeParallelized(optimalSolverSpec);
    }
  }

  return problemConfig;
}

void CoreSolver::initSubproblemDecomposition(
    Problem& problem,
    const entities::Universe& universe,
    const std::string& decompositionScopeName) {
  // use scopeItemId of decompositionScope to map containers to subproblems
  // and store it in problem.containerToSubproblemId
  XLOG(INFO) << fmt::format(
      "Using scope {} for subproblem decomposition", decompositionScopeName);
  auto& decompositionScope =
      universe.getScope(universe.getScopeId(decompositionScopeName));
  int nextAvailableSubproblemId = SPECIAL_SUBPROBLEM_ID + 1;
  PackerMap<entities::ContainerId, int> containerToSubproblemId;
  for (auto scopeItemId : decompositionScope.getScopeItemIds()) {
    problem.subproblemIdToName[nextAvailableSubproblemId] =
        universe.getEntityName(scopeItemId);
    for (auto containerId : decompositionScope.getContainerIds(scopeItemId)) {
      containerToSubproblemId.emplace(containerId, nextAvailableSubproblemId);
      problem.subproblemToContainerIds[nextAvailableSubproblemId].insert(
          containerId);
    }
    nextAvailableSubproblemId++;
  }
  XLOG(INFO) << fmt::format(
      "{} containers mapped to {} subproblems",
      containerToSubproblemId.size(),
      nextAvailableSubproblemId);
  problem.containerToSubproblemId = std::move(containerToSubproblemId);
}

void CoreSolver::populateSolutionFromLogs(
    AssignmentSolution& solution,
    LogCollector& logCollector) {
  auto data = logCollector.takeLoggedData();
  validateDataForSolution(data);
  solution.initialGlobalObjective() = std::move(data.objectiveSummaries.at(0));
  solution.initialObjective() =
      solution.initialGlobalObjective()->goals()->at(0);
  solution.initialConstraint() = std::move(data.constraintSummaries.at(0));
  solution.finalGlobalObjective() = std::move(data.objectiveSummaries.at(1));
  solution.finalObjective() = solution.finalGlobalObjective()->goals()->at(0);
  solution.finalConstraint() = std::move(data.constraintSummaries.at(1));
  solution.initialMetrics() =
      data.metrics.empty() ? thrift::Metrics() : std::move(data.metrics.at(0));
  solution.finalMetrics() =
      data.metrics.empty() ? thrift::Metrics() : std::move(data.metrics.at(1));
  solution.movesSummary() = std::move(data.moves);
  for (auto& summary : data.solverSummaries) {
    SolverReport solverReport;
    solverReport.endReason() = summary.endReason;
    if (summary.evalStats) {
      solverReport.evalStats() = std::move(*summary.evalStats);
    }
    if (summary.moveStats) {
      solverReport.moveStats() = std::move(*summary.moveStats);
    }
    if (summary.stagesSummaries) {
      solverReport.stagesSummaries() = std::move(*summary.stagesSummaries);
    }
    solution.solverSummaries()->push_back(std::move(solverReport));
  }
  solution.finalEvaluationSummary().from_optional(
      std::move(data.finalEvaluationSummary));
  solution.problemProfile()->localSearchProfiles() =
      std::move(data.localSearchProfiles);
  for (auto& metadata : data.specMetadata) {
    auto specName = *metadata.specName();
    solution.specNameToMetadata()->emplace(
        std::move(specName), std::move(metadata));
  }
}

std::optional<std::string> CoreSolver::getObjectOrderingDimensionName(
    const StrategyT& strategy) {
  std::optional<std::string> objectOrderingDimension = std::nullopt;
  int numLocalSearchSolvers = 0;
  for (auto& solverSpec : *strategy.solvers()) {
    if (solverSpec.getType() == SolverT::Type::localSearchSolverSpec) {
      if (auto dim =
              solverSpec.localSearchSolverSpec()->objectOrderingDimension()) {
        objectOrderingDimension = *dim;
      }
      ++numLocalSearchSolvers;
    }
    if (solverSpec.getType() == SolverT::Type::localSearchStageSolverSpec) {
      for (const auto& stageSpec :
           *solverSpec.localSearchStageSolverSpec()->stageSpecs()) {
        if (auto dim = stageSpec.solverSpec()->objectOrderingDimension()) {
          if (objectOrderingDimension && *objectOrderingDimension != *dim) {
            throw std::runtime_error(
                "inconsistent object_ordering_dimension across stages");
          }
          objectOrderingDimension = *dim;
        }
      }
      ++numLocalSearchSolvers;
    }
  }

  if (numLocalSearchSolvers > 1 && objectOrderingDimension) {
    throw std::runtime_error(
        "when object_ordering_dimension is set exactly one localsearch solver can be used");
  }

  return objectOrderingDimension;
}

} // namespace interface
} // namespace rebalancer
} // namespace facebook
