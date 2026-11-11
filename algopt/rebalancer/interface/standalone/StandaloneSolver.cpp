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

#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/common/replayer/RebalancerReplayer.h"
#include "algopt/rebalancer/common/UuidGenerator.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/CoreSolver.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/serialization/Serializer.h"
#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"
#include "algopt/rebalancer/solver/moves/MoveTypeFactory.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/Profiler.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/FileUtil.h>
#include <folly/gen/File.h>
#include <folly/init/Init.h>
#include <folly/logging/Init.h>
#include <folly/system/HardwareConcurrency.h>
#include <folly/testing/TestUtil.h>
#include <gflags/gflags.h>

#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/common/log/fb/ScubaLog.h"
#include "algopt/rebalancer/fb/rebalancer_net/LearnedFilterHook.h"
#include "algopt/rebalancer/interface/fb/Manifold.h"
#include "algopt/rebalancer/interface/standalone/fb/perfmon/InstanceSelector.h"
#else
#include "algopt/rebalancer/common/log/StreamLog.h"
#endif

using namespace facebook::rebalancer;
using namespace facebook::rebalancer::interface;

namespace algopt = facebook::algopt;
namespace treeprof = facebook::algopt::treeprof;

namespace {
int getCoreCount() {
  return folly::available_concurrency();
}
} // namespace

DEFINE_int32(num_threads, getCoreCount(), "");
DEFINE_string(xlog_config, "dbg1", "logging level");
DEFINE_string(run_id, "", "Rebalancer run id");
DEFINE_bool(
    enable_new_parallelized_materializer,
    true,
    "Enables parallelized version of new materializer");
DEFINE_int32(
    update_manifold_expiration_days,
    kManifoldExtensionDays,
    "update manifold expiration by given number of days; use 0 for 'never expires', -1 to avoid updating");
DEFINE_string(run_ids_file, "", "");
DEFINE_string(
    preselected_run_id_set,
    "",
    "if set, standalone solver will run instances from preselected set sequentially");
DEFINE_bool(
    list_run_ids,
    false,
    "print run IDs for the given preselected_run_id_set and exit");
DEFINE_bool(
    skip_solving,
    false,
    "set solve time to 0; problem will only be materialized");
DEFINE_bool(
    enable_simplifier,
    false,
    "use simplifier to simplify LP expressions");
DEFINE_string(
    logging_label,
    "",
    "add a label to the rebalancer_runs table under the column 'logging_label'");
DEFINE_int32(
    num_repeat,
    1,
    "repeat each instance (i.e., runId) 'num_repeat' times. Useful to test variance");
DEFINE_double(solve_time, -1, "Maximum solve time override");
DEFINE_double(move_limit, -1, "Maximum move limit for the entire solve");
DEFINE_bool(enable_new_materializer, false, "Unused");
DEFINE_string(
    persist_to_manifold_with_new_run_id,
    "",
    "Persist the solve to manifold using the run_id given");
DEFINE_bool(override_backup, false, "Upload backup even if it exists");
DEFINE_string(
    override_move_type,
    "",
    "Override move type for LocalSearchSolver only (not for staged solves). Options: single, single_fast, single_greedy, swap. If empty, no override is applied");
DEFINE_string(
    force_optimal_solver,
    "",
    "Force the use of OptimalSolver with specified MIP solver by completely replacing the solver strategy. Options: xpress, gurobi. If empty, no override is applied. This overrides all existing solver configurations");

DEFINE_string(
    equivalence_sets_output_file,
    "",
    "if non-empty, write equivalence sets to this file in csv format");

DEFINE_string(
    parallel_execution_strategy,
    "",
    "Override parallel execution strategy for local search. "
    "Options: sliding_window, batching, auto. If empty, uses problem config.");

DEFINE_int32(
    batch_size,
    0,
    "Batch size for batching execution strategy. "
    "Only used when --parallel_execution_strategy=batching or auto. "
    "If 0, uses default (32).");

DEFINE_int32(
    batching_max_concurrency,
    -1,
    "Maximum concurrent batch consumers for the batching execution strategy. "
    "Only used when --parallel_execution_strategy=batching or auto. "
    "-1 uses the configured default, 0 uses all executor threads, and positive "
    "values set an explicit limit.");

DEFINE_bool(
    enable_invalid_move_filter,
    true,
    "Override rolloutConfig.enableInvalidMoveFilter: materialize "
    "constraint-derived invalid move filters for local search.");

DEFINE_bool(
    validate_applied_moves,
    false,
    "Validate local search moves after applying them.");

DEFINE_bool(
    use_separated_local_search_penalty_objective,
    true,
    "Use a separate tie-breaking penalty objective for local search.");

DEFINE_bool(
    move_stats_track_containers,
    false,
    "Override MoveStatsSpec.trackContainers, i.e. per-container move stats, "
    "like ProblemSolver::enableMoveStats does. If not set, the instance's "
    "saved value is kept.");

DEFINE_double(
    precision_tolerance_absolute,
    -1,
    "Override absolute precision tolerance. If >= 0, sets universe.precisionTolerances.absolute to this value.");
DEFINE_double(
    precision_tolerance_relative,
    -1,
    "Override relative precision tolerance. If >= 0, sets universe.precisionTolerances.relative to this value.");

DEFINE_string(
    solver_problem_file,
    "",
    "Override OptimalSolverSpec.solverOutputFiles.solverProblemFile, e.g. "
    "/tmp/model.mps, to write the MIP model to a file.");

DEFINE_string(
    learned_model_path,
    "",
    "If set, enable the learned move-legality heuristic (rebalancer-net) using "
    "this model: a manifold://<bucket>/<key> URI or a local file path.");
DEFINE_double(
    learned_prune_threshold,
    0.5,
    "Prune (object, container) moves whose predicted probability is below this "
    "threshold. Only used when --learned_model_path is set.");
DEFINE_string(
    learned_metrics_json,
    "",
    "If set, append one JSON line of learned-filter inference metrics (timings "
    "+ prune counts) per solve to this file, for offline plotting/analysis. "
    "Passed to the solver via LearnedMoveFilterSpec, not a core global flag.");
DEFINE_bool(
    learned_auto_resolve,
    false,
    "Enable the learned move filter via auto-resolve: leaves modelPath empty so "
    "the solver picks the latest published model for the problem's (service, "
    "fingerprint) from Manifold, instead of an explicit --learned_model_path.");
DEFINE_string(
    learned_model_service,
    "",
    "Override the service used for learned-model auto-resolve (defaults to the "
    "problem's own service).");

static interface::LearnedMoveFilterSpec makeLearnedMoveFilterSpecFromFlags() {
  interface::LearnedMoveFilterSpec spec;
  spec.modelPath() = FLAGS_learned_model_path;
  spec.pruneThreshold() = FLAGS_learned_prune_threshold;
  if (!FLAGS_learned_metrics_json.empty()) {
    spec.learnedMetricsJson() = FLAGS_learned_metrics_json;
  }
  return spec;
}

static std::optional<interface::ParallelExecutionConfig>
makeParallelExecutionConfigFromFlags() {
  if (FLAGS_parallel_execution_strategy.empty()) {
    return std::nullopt;
  }

  interface::ParallelExecutionConfig spec;

  if (FLAGS_parallel_execution_strategy == "sliding_window") {
    spec.set_slidingWindow(interface::SlidingWindowExecutionConfig{});
    XLOGF(
        INFO,
        "Overriding parallel execution: strategy={}",
        FLAGS_parallel_execution_strategy);
  } else if (
      FLAGS_parallel_execution_strategy == "batching" ||
      FLAGS_parallel_execution_strategy == "auto") {
    if (FLAGS_batching_max_concurrency < -1) {
      throw std::runtime_error(
          fmt::format(
              "Invalid batching_max_concurrency '{}'. Expected -1 or greater",
              FLAGS_batching_max_concurrency));
    }
    interface::BatchingExecutionConfig batchingConfig;
    if (FLAGS_batch_size > 0) {
      batchingConfig.batchSize() = FLAGS_batch_size;
    }
    if (FLAGS_batching_max_concurrency >= 0) {
      batchingConfig.maxConcurrency() = FLAGS_batching_max_concurrency;
    }
    const auto batchSize = *batchingConfig.batchSize();
    const auto maxConcurrency = *batchingConfig.maxConcurrency();
    if (FLAGS_parallel_execution_strategy == "batching") {
      spec.set_batching(std::move(batchingConfig));
    } else {
      interface::AutoExecutionConfig autoConfig;
      autoConfig.batching() = std::move(batchingConfig);
      spec.set_autoExecution(std::move(autoConfig));
    }
    XLOGF(
        INFO,
        "Overriding parallel execution: strategy={}, batchSize={}, "
        "maxConcurrency={}",
        FLAGS_parallel_execution_strategy,
        batchSize,
        maxConcurrency);
  } else {
    throw std::runtime_error(
        fmt::format(
            "Invalid parallel_execution_strategy '{}'. "
            "Valid options: sliding_window, batching, auto",
            FLAGS_parallel_execution_strategy));
  }

  return spec;
}

static std::shared_ptr<folly::CPUThreadPoolExecutor> make_executor(
    int num_threads) {
  return std::make_shared<folly::CPUThreadPoolExecutor>(
      num_threads,
      std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
}

static void validateInputParameters() {
  int countInputParamsSet = 0;
  const std::vector<std::string> stringParams = {
      FLAGS_run_id, FLAGS_run_ids_file, FLAGS_preselected_run_id_set};
  for (const auto& param : stringParams) {
    if (!param.empty()) {
      countInputParamsSet++;
    }
  }

  if (countInputParamsSet != 1) {
    throw std::runtime_error(
        "Expect exactly one of 'run_id' 'runIds_file' and 'preselected_run_id_set' params to be set");
  }

  // Validate force_optimal_solver options
  if (!FLAGS_force_optimal_solver.empty()) {
    if (FLAGS_force_optimal_solver != "xpress" &&
        FLAGS_force_optimal_solver != "gurobi") {
      throw std::runtime_error(
          fmt::format(
              "Invalid force_optimal_solver '{}'. Valid options: xpress, gurobi",
              FLAGS_force_optimal_solver));
    }

    // Check for conflicting flags
    if (!FLAGS_override_move_type.empty()) {
      throw std::runtime_error(
          "Cannot specify both --force_optimal_solver and --override_move_type. "
          "OptimalSolver does not use move types. Use only --force_optimal_solver to force optimal solver.");
    }
  }
}

static void logSolveTimeZero() {
  XLOG(INFO) << "Setting solve time to zero; problem will only be materialized";
}

static void possiblyModifyOptimalSolverSpec(
    interface::OptimalSolverSpec& optimalSolverSpec) {
  if (FLAGS_skip_solving) {
    logSolveTimeZero();
    optimalSolverSpec.skipMipSolveForTesting() = true;
  }
  if (FLAGS_enable_simplifier) {
    XLOG(INFO) << "Using problem simplifier to simplify LP expressions";
    optimalSolverSpec.simplifyLpProblem() = true;
  }
  if (FLAGS_solve_time >= 0) {
    optimalSolverSpec.solveTime() = FLAGS_solve_time;
  }
  const auto& logFile = *optimalSolverSpec.xpressLogFile();
  const auto logDir = std::filesystem::path(logFile).parent_path();
  if (!logDir.empty() && !std::filesystem::is_directory(logDir)) {
    XLOGF(WARN, "Ignoring saved MIP log file with missing dir: {}", logFile);
    optimalSolverSpec.xpressLogFile() = "";
  }
  if (!FLAGS_solver_problem_file.empty()) {
    optimalSolverSpec.solverOutputFiles()->solverProblemFile() =
        FLAGS_solver_problem_file;
  }
  if (FLAGS_move_limit >= 0) {
    throw std::runtime_error(
        "Setting a move limit is not supported when using optimalSolver");
  }
}

static void possiblyModifyLocalSearchSolverSpec(
    interface::LocalSearchSolverSpec& localSearchSolverSpec) {
  MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(
      localSearchSolverSpec);
  if (FLAGS_skip_solving) {
    logSolveTimeZero();
    localSearchSolverSpec.solveTime() = 0;
    localSearchSolverSpec.stopAfterMoves() = 0;
  }
  if (FLAGS_solve_time >= 0) {
    localSearchSolverSpec.solveTime() = FLAGS_solve_time;
  }
  if (FLAGS_move_limit >= 0) {
    localSearchSolverSpec.stopAfterMoves() = FLAGS_move_limit;
  }

  // Apply parallel execution strategy override if specified
  auto execSpec = makeParallelExecutionConfigFromFlags();
  if (execSpec) {
    localSearchSolverSpec.parallelExecutionConfig() = std::move(*execSpec);
  }

  // Apply move type override if specified
  if (!FLAGS_override_move_type.empty()) {
    std::vector<interface::MoveTypeSpec> moveTypeList;

    if (FLAGS_override_move_type == "single") {
      moveTypeList.push_back(
          interface::ProblemSolver::makeMoveTypeSpec(
              interface::SingleMoveTypeSpec{}));
    } else if (FLAGS_override_move_type == "single_fast") {
      moveTypeList.push_back(
          interface::ProblemSolver::makeMoveTypeSpec(
              interface::SingleFastMoveTypeSpec{}));
    } else if (FLAGS_override_move_type == "single_greedy") {
      moveTypeList.push_back(
          interface::ProblemSolver::makeMoveTypeSpec(
              interface::SingleGreedyMoveTypeSpec{}));
    } else if (FLAGS_override_move_type == "swap") {
      moveTypeList.push_back(
          interface::ProblemSolver::makeMoveTypeSpec(
              interface::SwapMoveTypeSpec{}));
    } else {
      throw std::runtime_error(
          fmt::format(
              "Invalid override_move_type '{}'. Valid options: single, single_fast, single_greedy, swap",
              FLAGS_override_move_type));
    }

    localSearchSolverSpec.moveTypeList() = std::move(moveTypeList);
    XLOGF(INFO, "Applied move type override: {}", FLAGS_override_move_type);
  }
}

static void possiblyModifyLocalSearchStageSolverSpec(
    interface::LocalSearchStageSolverSpec& localSearchStageSolverSpec) {
  for (auto& stageSpec : *localSearchStageSolverSpec.stageSpecs()) {
    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(
        *stageSpec.solverSpec());
  }
  if (FLAGS_skip_solving) {
    logSolveTimeZero();
    localSearchStageSolverSpec.solveTime() = 0;
    localSearchStageSolverSpec.stopAfterMoves() = 0;
  }
  if (FLAGS_solve_time >= 0) {
    localSearchStageSolverSpec.solveTime() = FLAGS_solve_time;
  }
  if (FLAGS_move_limit >= 0) {
    localSearchStageSolverSpec.stopAfterMoves() = FLAGS_move_limit;
  }

  // Apply parallel execution strategy override if specified
  auto execSpec = makeParallelExecutionConfigFromFlags();
  if (execSpec) {
    localSearchStageSolverSpec.parallelExecutionConfig() = std::move(*execSpec);
  }
}

static void possiblyModifyRasHybridSolverSpec(
    interface::RasHybridSolverSpec& rasHybridSolverSpec) {
  if (FLAGS_solve_time >= 0) {
    rasHybridSolverSpec.localSearchSolveTime() = FLAGS_solve_time;
  }
  if (FLAGS_move_limit >= 0) {
    rasHybridSolverSpec.stopAfterMoves() = FLAGS_move_limit;
  }
}

static void possiblyModifySolverSpec(AssignmentProblem& problem) {
  for (auto& solver : *problem.strategy()->solvers()) {
    switch (solver.getType()) {
      case SolverT::Type::optimalSolverSpec: {
        auto& optimalSolverSpec = solver.mutable_optimalSolverSpec();
        possiblyModifyOptimalSolverSpec(optimalSolverSpec);
        break;
      }
      case SolverT::Type::optimalSubsetSolverSpec: {
        auto& optimalSubsetSolverSpec =
            solver.mutable_optimalSubsetSolverSpec();
        if (FLAGS_skip_solving) {
          // set to 1 since 0 is treated as 'unlimited'
          optimalSubsetSolverSpec.maxSubsetRuns() = 1;
        }
        possiblyModifyOptimalSolverSpec(
            *optimalSubsetSolverSpec.optimalConfig());
        break;
      }
      case SolverT::Type::localSearchStageSolverSpec: {
        auto& stageSolverSpec = solver.mutable_localSearchStageSolverSpec();
        possiblyModifyLocalSearchStageSolverSpec(stageSolverSpec);
        break;
      }
      case SolverT::Type::localSearchSolverSpec: {
        auto& localSearchSolverSpec = solver.mutable_localSearchSolverSpec();
        possiblyModifyLocalSearchSolverSpec(localSearchSolverSpec);
        break;
      }
      case SolverT::Type::rasHybridSolverSpec: {
        auto& rasHybridSolverSpec = solver.mutable_rasHybridSolverSpec();
        possiblyModifyRasHybridSolverSpec(rasHybridSolverSpec);
        break;
      }
      default:
        if (FLAGS_skip_solving || FLAGS_enable_simplifier) {
          throw std::runtime_error(
              fmt::format(
                  "unknown solver type = {}; cannot set solve time to zero or enable simplifier",
                  apache::thrift::util::enumNameSafe(solver.getType())));
        }
    }
  }
  RebalancerReplayer::clearSolverLogFile(problem);
}

static void possiblyModifyProblem(AssignmentProblem& problem) {
  // if new instance and run_id is not given, then generate one
  if (problem.runId()->empty()) {
    problem.runId() = UuidGenerator::genString();
  }

  // Only override the instance's saved setting when the flag was passed
  // explicitly, so replays preserve the value the problem was solved with.
  if (!gflags::GetCommandLineFlagInfoOrDie("enable_invalid_move_filter")
           .is_default) {
    problem.rolloutConfig()->enableInvalidMoveFilter() =
        FLAGS_enable_invalid_move_filter;
  }
  if (!gflags::GetCommandLineFlagInfoOrDie("validate_applied_moves")
           .is_default) {
    problem.validateAppliedMoves() = FLAGS_validate_applied_moves;
  }
  if (!gflags::GetCommandLineFlagInfoOrDie(
           "use_separated_local_search_penalty_objective")
           .is_default) {
    problem.rolloutConfig()->useSeparatedLocalSearchPenaltyObjective() =
        FLAGS_use_separated_local_search_penalty_objective;
  }
  if (!gflags::GetCommandLineFlagInfoOrDie("move_stats_track_containers")
           .is_default) {
    problem.moveStatsSpec()->trackContainers() =
        FLAGS_move_stats_track_containers;
  }

  // Enable the learned move-legality heuristic (rebalancer-net): an explicit
  // --learned_model_path uses that model; --learned_auto_resolve leaves
  // modelPath empty so the solver auto-resolves by (service, fingerprint).
  if (!FLAGS_learned_model_path.empty() || FLAGS_learned_auto_resolve) {
    problem.learnedMoveFilterSpec() = makeLearnedMoveFilterSpecFromFlags();
    if (!FLAGS_learned_model_service.empty()) {
      problem.service_() = FLAGS_learned_model_service;
    }
    XLOGF(
        INFO,
        "Enabled learned move filter: model={}, pruneThreshold={}",
        FLAGS_learned_model_path.empty() ? "<auto-resolve>"
                                         : FLAGS_learned_model_path,
        FLAGS_learned_prune_threshold);
  }

  if (!FLAGS_logging_label.empty()) {
    problem.scubaLoggingLabel() = FLAGS_logging_label;
    // make sure scubaLogger is enabled when logging label is given
    problem.enableScubaLogger() = true;
  }

  // Force OptimalSolver if requested (complete strategy replacement)
  if (!FLAGS_force_optimal_solver.empty()) {
    SolverT solverT;
    interface::OptimalSolverSpec optimalSolverSpec;

    // Set solver package based on the flag
    if (FLAGS_force_optimal_solver == "xpress") {
      optimalSolverSpec.solverPackage() =
          interface::OptimalSolverPackage::XPRESS;
      XLOGF(
          INFO,
          "Forced OptimalSolver with XPRESS: completely replaced solver strategy");
    } else if (FLAGS_force_optimal_solver == "gurobi") {
      optimalSolverSpec.solverPackage() =
          interface::OptimalSolverPackage::GUROBI;
      XLOGF(
          INFO,
          "Forced OptimalSolver with GUROBI: completely replaced solver strategy");
    }

    solverT.optimalSolverSpec() = std::move(optimalSolverSpec);
    problem.strategy()->solvers() = {solverT};
  }

  possiblyModifySolverSpec(problem);
}

static void possiblyModifyPrecisionTolerances(
    entities::thrift::Universe& universeThrift) {
  if (FLAGS_precision_tolerance_absolute >= 0 ||
      FLAGS_precision_tolerance_relative >= 0) {
    if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            universeThrift.precisionTolerances())) {
      universeThrift.precisionTolerances() =
          facebook::algopt::common::thrift::PrecisionTolerances{};
    }
    auto& tolerances = *universeThrift.precisionTolerances();
    if (FLAGS_precision_tolerance_absolute >= 0) {
      tolerances.absolute() = FLAGS_precision_tolerance_absolute;
      XLOGF(
          INFO,
          "Overriding precision tolerance absolute: {}",
          FLAGS_precision_tolerance_absolute);
    }
    if (FLAGS_precision_tolerance_relative >= 0) {
      tolerances.relative() = FLAGS_precision_tolerance_relative;
      XLOGF(
          INFO,
          "Overriding precision tolerance relative: {}",
          FLAGS_precision_tolerance_relative);
    }
  }
}

template <class T>
static void writeToFile(const T& thriftStruct, const std::string& filename) {
  const auto serialized = Serializer::serialize(thriftStruct);
  if (!folly::writeFile(serialized, filename.c_str())) {
    throw std::runtime_error(fmt::format("error writing file {}", filename));
  }
}

// Note that equivalence set is a partition of the object set,
// This function writes object to equivalence set mapping (along with
// other object partitions) to a csv file.
static void writeEquivalenceSetsData(
    const std::string& filename,
    const AssignmentSolution& solution,
    const entities::Universe& universe) {
  auto& equivalenceSets = *solution.equivalenceSetInfo()->equivalenceSets();
  // columns: objects, equivalence set, partition 1, partition 2, ...
  auto header = fmt::format("{},equivalence_set", universe.getObjectTypeName());
  std::vector<entities::PartitionId> disjointPartitions;
  for (auto& partitionId : universe.getPartitionIds()) {
    auto& partition = universe.getPartition(partitionId);
    if (partition.isDisjoint()) {
      disjointPartitions.push_back(partitionId);
      header += fmt::format(",{}", universe.getEntityName(partitionId));
    }
  }
  header += fmt::format(",{}", universe.getContainerTypeName());
  std::string data = header + "\n";
  for (auto& equivSet : equivalenceSets) {
    for (auto& object : *equivSet.objectNames()) {
      data += fmt::format("{},{}", object, *equivSet.name());
      auto objectId = universe.getObjectId(object);
      for (auto& partitionId : disjointPartitions) {
        auto& partition = universe.getPartition(partitionId);
        auto groupId = partition.getObjectIdToGroupIds().at(objectId).at(0);
        data += fmt::format(",{}", universe.getEntityName(groupId));
      }
      data += fmt::format(",{}", solution.assignment()->at(object));
      data += "\n";
    }
  }
  if (!folly::writeFile(data, filename.c_str())) {
    throw std::runtime_error(fmt::format("error writing file {}", filename));
  }
  XLOG(INFO) << "Wrote equivalence sets to " << filename;
}

static std::vector<std::string> extractRunIds() {
  std::vector<std::string> runIds;
  if (!FLAGS_run_ids_file.empty()) {
    for (const auto& runId : folly::gen::byLine(FLAGS_run_ids_file.c_str()) |
             folly::gen::eachTo<std::string>() |
             folly::gen::as<std::vector>()) {
      if (runId.empty()) {
        // ignore empty lines
        continue;
      }
      runIds.push_back(runId);
    }
  } else if (!FLAGS_run_id.empty()) {
    runIds.push_back(FLAGS_run_id);
  } else if (!FLAGS_preselected_run_id_set.empty()) {
#ifndef REBALANCER_OSS_BUILD
    return InstanceSelector::getRunIds(FLAGS_preselected_run_id_set);
#else
    throw std::runtime_error("This option is not available in open source!");
#endif
  }

  return runIds;
}

void saveAsJson(const Bundle& bundle, const std::string& directory) {
  std::filesystem::create_directories(directory);

  const auto& problem = *bundle.problem();
  const auto problemFile = fmt::format("{}/{}", directory, kProblemJson);
  folly::writeFile(Serializer::serialize(problem), problemFile.c_str());

  if (bundle.solution()) {
    const auto& solution = *bundle.solution();
    const auto solutionFile = fmt::format("{}/{}", directory, kSolutionJson);
    folly::writeFile(Serializer::serialize(solution), solutionFile.c_str());
  }
}

static Bundle getBundle(const std::string& runId) {
#ifndef REBALANCER_OSS_BUILD
  return Manifold::download(runId);
#else
  throw std::runtime_error("Manifold download is not supported in OSS build");
#endif
}

static void checkIfRunIdsExistAndUpdateExpirations(
    const std::vector<std::string>& runIds) {
#ifndef REBALANCER_OSS_BUILD
  for (const auto& runId : runIds) {
    // checks if the runId exists in the rebalancer manifold bucket; will throw
    // if not
    Manifold::checkIfExists(runId);

    // if given, updates the manifold expiration time
    if (FLAGS_update_manifold_expiration_days != -1) {
      Manifold::extendExpiration(
          runId, std::chrono::days(FLAGS_update_manifold_expiration_days));
    }
  }
#else
  throw std::runtime_error(
      "Manifold operations are not supported in OSS build");
#endif
}

static std::vector<Bundle> getBundles() {
  std::vector<Bundle> bundles;
  if (!FLAGS_run_id.empty() || !FLAGS_run_ids_file.empty() ||
      !FLAGS_preselected_run_id_set.empty()) {
    const auto runIds = extractRunIds();

    // check all runIds exist before downloading any to avoid unnecessary
    // downloads
    checkIfRunIdsExistAndUpdateExpirations(runIds);

    for (auto& runId : runIds) {
      bundles.emplace_back(getBundle(runId));
    }
  } else {
    throw std::runtime_error(
        "Either run_id or run_ids_file or preselected_run_id_set must be specified");
  }

  return bundles;
}

static void uploadToManifoldAndLog(
    AssignmentProblem&& problem,
    std::optional<AssignmentSolution>&& solution,
    const std::shared_ptr<RebalancerLog>& logger) {
#ifndef REBALANCER_OSS_BUILD
  Bundle bundle;
  bundle.problem() = std::move(problem);

  if (solution) {
    bundle.solution() = std::move(*solution);
  }

  std::optional<time_t> expirationTimeOpt;
  try {
    expirationTimeOpt = Manifold::upload(
        bundle, *bundle.problem()->runId(), FLAGS_override_backup);
  } catch (const std::exception& error) {
    XLOG(WARNING) << "Failed to upload to Manifold: " << error.what();
  }

  if (logger) {
    logger->log(ManifoldInfo{.manifoldExpirationTime = expirationTimeOpt});
  }
#else
  throw std::runtime_error("Manifold upload is not supported in OSS build");
#endif
}

static void runInstance(Bundle&& bundle) {
  algopt::Timer timer(true);
  auto& problem = *bundle.problem();
  possiblyModifyProblem(problem);

  const auto executor = make_executor(FLAGS_num_threads);

  if (!problem.universe()) {
    throw std::runtime_error("Universe is not set");
  }

  BackwardCompatabilityUtils::possiblyModify(problem);
  auto& universeThrift = *problem.universe();

  possiblyModifyPrecisionTolerances(universeThrift);

  treeprof::Profiler treeProfiler("Standalone::solve");

  treeprof::EventRecorder universeEvent("Build Universe");
  const auto universe = std::make_shared<entities::Universe>(universeThrift);
  universeEvent.stop();

  const bool shouldPersistWithNewRunId =
      !FLAGS_persist_to_manifold_with_new_run_id.empty();
  if (shouldPersistWithNewRunId) {
    problem.runId() = FLAGS_persist_to_manifold_with_new_run_id;
  }

  std::shared_ptr<RebalancerLog> logger = nullptr;
  if (*problem.enableScubaLogger()) {
#ifndef REBALANCER_OSS_BUILD
    logger = std::make_shared<ScubaLog>(problem);
#else
    logger = std::make_shared<StreamLog>();
#endif
  }

  XLOGF(
      DBG1,
      "downloaded and modified the problem in {:.2f}s",
      timer.getSeconds());

  // Build the learned move filter (no-op unless a model is configured); this is
  // the only place torch enters the binary, keeping CoreSolver torch-free.
  std::shared_ptr<const InvalidMoveFilter> learnedFilter;
#ifndef REBALANCER_OSS_BUILD
  learnedFilter =
      rebalancer_net::buildLearnedFilter(*universe, problem, logger.get());
#endif

  auto runCoreSolver = [&]() {
    auto solution = CoreSolver::solve(
        problem,
        executor,
        FLAGS_enable_new_parallelized_materializer,
        universe,
        logger,
        learnedFilter);

    treeProfiler.stop();
    CoreSolver::printAndLogHierarchicalProfile(
        treeProfiler.getRoot(), *solution.problemProfile(), logger);
    if (!FLAGS_equivalence_sets_output_file.empty()) {
      writeEquivalenceSetsData(
          FLAGS_equivalence_sets_output_file, solution, *universe);
    }
    return solution;
  };

  const bool oldInstance =
      (!FLAGS_run_id.empty() || !FLAGS_run_ids_file.empty());

  const bool shouldUploadToManifold =
      !oldInstance || shouldPersistWithNewRunId || FLAGS_override_backup;

  std::optional<AssignmentSolution> solution = std::nullopt;
  try {
    solution = runCoreSolver();
  } catch (std::exception& e) {
    if (shouldUploadToManifold) {
      uploadToManifoldAndLog(std::move(problem), std::move(solution), logger);
    }

    throw std::runtime_error(
        fmt ::format("Standalone solver failed with {}", e.what()));
  }

  if (shouldUploadToManifold) {
    uploadToManifoldAndLog(std::move(problem), std::move(solution), logger);
  }
}

static void runInstances() {
#ifndef REBALANCER_OSS_BUILD
  XLOGF_IF(
      INFO,
      !FLAGS_logging_label.empty(),
      "Running with logging label = {}. Data will be logged to '{}' table.",
      FLAGS_logging_label,
      rebalancerRunInfoTable);
#endif

  auto bundles = getBundles();
  if (FLAGS_num_repeat > 1) {
    XLOGF(INFO, "Each instance will be run {} times", FLAGS_num_repeat);
  }
  for (const auto _ : folly::irange(FLAGS_num_repeat)) {
    for (auto& bundle : bundles) {
      runInstance(std::move(bundle));
    }
  }
}

int main(int argc, char* argv[]) {
  const folly::Init init(&argc, &argv);
  folly::initLoggingOrDie(FLAGS_xlog_config);

  if (FLAGS_list_run_ids) {
#ifndef REBALANCER_OSS_BUILD
    const auto runIds =
        InstanceSelector::getRunIds(FLAGS_preselected_run_id_set);
    for (const auto& id : runIds) {
      fmt::print("{}\n", id);
    }
#else
    throw std::runtime_error("This option is not available in open source!");
#endif
    return 0;
  }

  validateInputParameters();

  runInstances();

  return 0;
}
