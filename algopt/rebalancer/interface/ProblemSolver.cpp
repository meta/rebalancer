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

#include "algopt/rebalancer/interface/ProblemSolver.h"

#include "algopt/rebalancer/common/UuidGenerator.h"
#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/CoreSolver.h"
#include "algopt/rebalancer/interface/serialization/Serializer.h"
#include "algopt/rebalancer/interface/UniverseProblemBuilder.h"
#include "algopt/rebalancer/treeprof/Profiler.h"

#include <fmt/core.h>
#include <folly/CancellationToken.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/FileUtil.h>
#include <folly/futures/Future.h>
#include <folly/logging/Init.h>
#include <folly/logging/xlog.h>
#include <folly/system/HardwareConcurrency.h>

#include <memory>
#include <stdexcept>
#include <string_view>

#ifndef REBALANCER_OSS_BUILD
#include "algopt/rebalancer/common/log/fb/ScubaLog.h"
#include "algopt/rebalancer/interface/fb/Manifold.h"
#include "algopt/rebalancer/interface/fb/RolloutConfigResolver.h"
#else
#include "algopt/rebalancer/common/log/StreamLog.h"
#endif

namespace facebook::rebalancer::interface {
namespace {
constexpr std::string_view kStandaloneSolverCmd =
    "buck2 run @//mode/opt //algopt/rebalancer/interface/standalone:standalone_solver";
constexpr std::string_view kRebalancerExplorerUrl =
    "https://www.internalfb.com/rebalancer-explorer/evaluation?runId=";
constexpr std::string_view kDefaultManifoldThreadPoolName = "ManifoldUpload";
// unless user provides an executor, we just use 1 thread for manifold uploads
constexpr size_t kDefaultManifoldExecutorThreads = 1;

Bundle getBundleImpl(
    AssignmentProblem problem,
    const entities::Universe* universe,
    std::optional<AssignmentSolution> solution) {
  Bundle bundle;
  bundle.problem() = std::move(problem);
  if (universe != nullptr) {
    bundle.problem()->universe() = universe->toThrift();
  }
  if (solution) {
    bundle.solution() = std::move(*solution);
  }

  return bundle;
}
} // namespace

/*static*/
std::shared_ptr<folly::ThreadPoolExecutor>
ProblemSolver::makeCPUThreadPoolExecutor(
    std::string_view threadPoolNamePrefix,
    size_t numThreads) {
  return std::make_shared<folly::CPUThreadPoolExecutor>(
      numThreads,
      std::make_shared<folly::NamedThreadFactory>(threadPoolNamePrefix));
}

AsyncManifoldUploadHandle::AsyncManifoldUploadHandle()
    : executor_(
          ProblemSolver::makeCPUThreadPoolExecutor(
              kDefaultManifoldThreadPoolName,
              kDefaultManifoldExecutorThreads)) {}

AsyncManifoldUploadHandle::AsyncManifoldUploadHandle(
    std::shared_ptr<folly::ThreadPoolExecutor> executor)
    : executor_(std::move(executor)) {}

AsyncManifoldUploadHandle::~AsyncManifoldUploadHandle() {
  if (!joined_) {
    XLOGF(
        WARNING,
        "User is expected to wait() when using AsyncManifoldUploadHandle. Otherwise, ~AsyncManifoldUploadHandle() waits until upload is complete");
    folly::coro::blockingWait(scope_.joinAsync());
  }
}

void AsyncManifoldUploadHandle::wait() {
  if (joined_) [[unlikely]] {
    throw std::runtime_error(
        "Unexpected call to wait() w.r.t. a previously joined AsyncManifoldUploadHandle");
  }

  folly::coro::blockingWait(scope_.joinAsync());
  joined_ = true;
}

void AsyncManifoldUploadHandle::add(folly::coro::Task<void> task) {
  if (joined_) [[unlikely]] {
    throw std::runtime_error(
        "Unexpected call to add() w.r.t. a previously joined AsyncManifoldUploadHandle");
  }
  scope_.add(folly::coro::co_withExecutor(executor_.get(), std::move(task)));
}

#ifndef REBALANCER_OSS_BUILD
static folly::coro::Task<void> uploadToManifoldCoro(
    AssignmentProblem problem,
    std::shared_ptr<const entities::Universe> universe,
    std::optional<AssignmentSolution> solution,
    std::shared_ptr<RebalancerLog> logger) {
  const algopt::Timer timer(true);
  const auto runId = folly::copy(*problem.runId());
  const auto bundle =
      getBundleImpl(std::move(problem), universe.get(), std::move(solution));

  try {
    const auto expirationTime = co_await Manifold::coUpload(bundle, runId);
    ProblemSolver::printReRunManifoldCommand(runId);
    if (logger) {
      logger->log(ManifoldInfo{.manifoldExpirationTime = expirationTime});
      logger->log(
          GenericInfo{
              .key = "create manifold bundle and upload time sec",
              .value = timer.getSeconds(),
              .additionalInfo = ""});
    }
  } catch (...) {
    XLOGF(
        ERR,
        "Manifold upload failed for runId: {}. Error: {}",
        runId,
        folly::exceptionStr(std::current_exception()));
  }
}
#endif

bool shouldBackup(
    const ManifoldBackupParams& manifoldBackupParams,
    const ProblemProfile& profile) {
  // As long as the ManifoldUploadPolicy is not NEVER and time taken to solve is
  // greater than kOverrideUploadPolicyMinSolveTimeSecs, we override the policy
  // set with a small probabilty (kOverrideUploadPolicyProbability) to ALWAYS.
  // This is done to ensure that we have a reasonable set of instances that are
  // always available in the rebalancer manifold bucket for testing purposes
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0.0, 1.0);
  if (*manifoldBackupParams.uploadPolicy() != ManifoldUploadPolicy::NEVER &&
      *profile.solveSec() >= kOverrideUploadPolicyMinSolveTimeSecs &&
      dis(gen) <= kOverrideUploadPolicyProbability) {
    XLOG(INFO) << "Overriding ManifoldUploadPolicy and uploading to manifold.";
    return true;
  }

  switch (*manifoldBackupParams.uploadPolicy()) {
    case ManifoldUploadPolicy::ALWAYS:
      XLOG(INFO) << "ManifoldUploadPolicy = ALWAYS. Uploading to Manifold.";
      return true;
    case ManifoldUploadPolicy::NEVER:
      XLOG(INFO) << "ManifoldUploadPolicy = NEVER. Not uploading to Manifold.";
      return false;
    case ManifoldUploadPolicy::ON_FAILURE:
      XLOG(INFO)
          << "ManifoldUploadPolicy = ON_FAILURE. Not uploading to Manifold.";
      return false;
    case ManifoldUploadPolicy::OUTLIER:
      if (manifoldBackupParams.expectedRuntime()) {
        const bool shouldUpload =
            *profile.solveSec() >= *manifoldBackupParams.expectedRuntime();
        if (shouldUpload) {
          XLOG(INFO) << fmt::format(
              "ManifoldUploadPolicy = OUTLIER. Uploading to manifold since solve time {}s >= expected solve time {}s.",
              *profile.solveSec(),
              *manifoldBackupParams.expectedRuntime());
        } else {
          XLOG(INFO) << fmt::format(
              "ManifoldUploadPolicy = OUTLIER. Not uploading to manifold since solve time {}s < expected solve time {}s.",
              *profile.solveSec(),
              *manifoldBackupParams.expectedRuntime());
        }
        return shouldUpload;
      } else {
        XLOG(INFO)
            << "ManifoldUploadPolicy = OUTLIER. Not uploading since expected solve time param is not set.";
      }
      return false;
    default:
      throw std::runtime_error("Unhandled ManifoldUploadPolicy");
  }
}

ProblemSolver::ProblemSolver(
    std::shared_ptr<folly::ThreadPoolExecutor> executor,
    std::string serviceName,
    std::string serviceScope,
    bool prepareProblemOnly,
    bool canExecuteAsync)
    : executor(std::move(executor)),
      universeProblemBuilder_(
          canExecuteAsync ? std::make_unique<AsyncConfig>(this->executor)
                          : nullptr),
      service(std::move(serviceName)),
      scope(std::move(serviceScope)),
      prepareProblemOnly(prepareProblemOnly),
      canExecuteAsync_(canExecuteAsync) {
  this->runId = UuidGenerator::genString();
#ifndef REBALANCER_OSS_BUILD
  RolloutConfigResolver::resolveProblemSolverConfigs(
      rolloutConfig_, service, scope);
#endif
  universeProblemBuilder_.setGroupBackedDynamicDimensions(
      *rolloutConfig_.groupBackedDynamicDimensions());
}

ProblemSolver::~ProblemSolver() = default;

ProblemSolver& ProblemSolver::enableRestrictMovingObjectOnlyOnce() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getProblemBuilder().enableRestrictMovingObjectOnlyOnce();
  return *this;
}

ProblemSolver& ProblemSolver::enableStableAsMuchAsPossible() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getProblemBuilder().enableStableAsMuchAsPossible();
  return *this;
}

ProblemSolver& ProblemSolver::enableMoveStats() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  MoveStatsSpec spec;
  *spec.trackContainers() = true;
  *spec.trackObjects() = false;
  enableMoveStatsImpl(std::move(spec));
  return *this;
}

ProblemSolver& ProblemSolver::enableMoveStats(MoveStatsSpec spec) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  enableMoveStatsImpl(std::move(spec));
  return *this;
}

void ProblemSolver::enableMoveStatsImpl(MoveStatsSpec spec) {
  checker.enableMoveStats(spec);
  moveStatsSpec = std::move(spec);
}

ProblemSolver& ProblemSolver::enableProfiler() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  profilerEnabled = true;
  return *this;
}

ProblemSolver& ProblemSolver::setFeasibilityTolerance(
    double feasibilityTolerance) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getProblemBuilder().setFeasibilityTolerance(feasibilityTolerance);
  return *this;
}

ProblemSolver& ProblemSolver::setManifoldBackupParams(
    const ManifoldBackupParams& params,
    std::shared_ptr<AsyncManifoldUploadHandle> manifoldUploadHandle) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  manifoldBackupParams = params;
  manifoldUploadHandle_ = std::move(manifoldUploadHandle);
  return *this;
}

ProblemSolver& ProblemSolver::setLoggingLabel(const std::string& loggingLabel) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  loggingLabel_ = loggingLabel;
  return *this;
}

ProblemSolver& ProblemSolver::disableLogging() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  loggingEnabled = false;
  return *this;
}

ProblemSolver& ProblemSolver::setRunId(std::string runIdStr) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  if (runIdStr.empty()) {
    throw std::runtime_error("cannot set an empty 'runId'");
  }

  this->runId = std::move(runIdStr);
  return *this;
}

std::string ProblemSolver::getService() const {
  return service;
}

std::string ProblemSolver::getScope() const {
  return scope;
}

std::string ProblemSolver::getRunId() const {
  return runId;
}

MoveStatsSpec ProblemSolver::getMoveStatsSpec() const {
  return moveStatsSpec;
}

bool ProblemSolver::isProfilerEnabled() const {
  return profilerEnabled;
}

bool ProblemSolver::isLoggingEnabled() const {
  return loggingEnabled;
}

ProblemSolver& ProblemSolver::setConstraintPolicy(ConstraintPolicy policy) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  constraintPolicy = policy;
  getProblemBuilder().setConstraintPolicy(policy);
  return *this;
}

ProblemSolver& ProblemSolver::setDefaultConstraintParams(
    const ConstraintParams& params) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkConstraintParams(params);
  constraintParams = params;
  getProblemBuilder().setDefaultConstraintParams(params);
  return *this;
}

ProblemSolver& ProblemSolver::overrideContainerHotnessRanking(
    const std::vector<std::string>& descendingHotnessContainers) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkDescendingHotnessContainers(descendingHotnessContainers);
  descendingHotnessContainersOverride = descendingHotnessContainers;
  getProblemBuilder().overrideContainerHotnessRanking(
      descendingHotnessContainers);
  return *this;
}

ConstraintParams ProblemSolver::getDefaultConstraintParams() const {
  return constraintParams;
}

ConstraintPolicy ProblemSolver::getConstraintPolicy() const {
  return constraintPolicy;
}

ProblemSolver& ProblemSolver::setObjectName(const std::string& objectName) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.setObjectName(objectName);
  getProblemBuilder().setObjectName(objectName);
  return *this;
}

ProblemSolver& ProblemSolver::setContainerName(
    const std::string& containerName) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.setContainerName(containerName);
  getProblemBuilder().setContainerName(containerName);
  return *this;
}

ProblemSolver& ProblemSolver::addObjectPartitionRoutingDimension(
    const std::string& dimensionName,
    const std::string& partitionName,
    const std::string& routingConfigName,
    const std::unordered_map<std::string, double>& groupToValue,
    double defaultValue) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addObjectPartitionRoutingDimension(
      dimensionName, partitionName, routingConfigName, groupToValue);
  getProblemBuilder().addObjectPartitionRoutingDimension(
      dimensionName,
      partitionName,
      routingConfigName,
      groupToValue,
      defaultValue);
  return *this;
}

ProblemSolver& ProblemSolver::addObjectPartitionRoutingDimension(
    const std::string& dimensionName,
    const std::string& partitionName,
    const std::string& routingConfigName,
    const std::unordered_map<std::string, double>& groupToValue,
    const std::unordered_map<std::string, double>& groupToStaticValue,
    double defaultValue,
    double defaultStaticValue) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addObjectPartitionRoutingDimension(
      dimensionName,
      partitionName,
      routingConfigName,
      groupToValue,
      groupToStaticValue);
  getProblemBuilder().addObjectPartitionRoutingDimension(
      dimensionName,
      partitionName,
      routingConfigName,
      groupToValue,
      defaultValue,
      groupToStaticValue,
      defaultStaticValue);
  return *this;
}

ProblemSolver& ProblemSolver::addDestinationsToExploreOptions(
    const std::string& name,
    interface::DestinationsToExploreOptions destinationsToExploreOptions) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  XLOG(CRITICAL) << "addDestinationsToExploreOptions is not supported in v2: "
                 << name;
  checker.addDestinationsToExploreOptions(name, destinationsToExploreOptions);
  getProblemBuilder().addDestinationsToExploreOptions(
      name, std::move(destinationsToExploreOptions));
  return *this;
}

void ProblemSolver::setDecompositionScope(const std::string& scopeName) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkScopeExists(scopeName);
  decompositionScopeName_ = scopeName;
}

ProblemSolver& ProblemSolver::addSimilarContainers(
    const std::vector<std::vector<std::string>>& similarContainerClasses) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getProblemBuilder().addSimilarContainers(similarContainerClasses);
  return *this;
}

ProblemSolver& ProblemSolver::addRoutingConfig(
    const std::string& configName,
    const std::string& scopeName,
    const std::string& partitionName,
    const std::unordered_map<std::string, GroupRoutingRings>&
        groupToRoutingRings,
    const std::unordered_map<
        std::string,
        std::unordered_map<std::string, double>>& originToDestinationLatency,
    const std::optional<
        std::unordered_map<std::string, std::vector<std::vector<std::string>>>>&
        defaultOriginToDestinationScopeItemSets) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.addRoutingConfig(
      configName,
      scopeName,
      partitionName,
      groupToRoutingRings,
      originToDestinationLatency,
      defaultOriginToDestinationScopeItemSets);
  getProblemBuilder().addRoutingConfig(
      configName,
      scopeName,
      partitionName,
      groupToRoutingRings,
      originToDestinationLatency,
      defaultOriginToDestinationScopeItemSets);
  return *this;
}

static BalanceSpecBoundType toV1(BalanceV2SpecBoundType boundType) {
  switch (boundType) {
    case BalanceV2SpecBoundType::ABSOLUTE:
      return BalanceSpecBoundType::ABSOLUTE;
    case BalanceV2SpecBoundType::RELATIVE:
      return BalanceSpecBoundType::RELATIVE;
    default:
      throw std::runtime_error("Unhandled BalanceV2SpecBoundType");
  }
}

static BalanceSpecDefinition toV1(BalanceV2SpecDefinition definition) {
  switch (definition) {
    case BalanceV2SpecDefinition::AFTER:
      return BalanceSpecDefinition::AFTER;
    case BalanceV2SpecDefinition::DURING:
      return BalanceSpecDefinition::DURING;
    case BalanceV2SpecDefinition::NEW:
      return BalanceSpecDefinition::NEW;
    default:
      throw std::runtime_error("Unhandled BalanceV2SpecDefinition");
  }
}

static BalanceSpecFormula toV1(BalanceV2SpecFormula formula) {
  switch (formula) {
    case BalanceV2SpecFormula::LINEAR:
      return BalanceSpecFormula::LINEAR;
    case BalanceV2SpecFormula::SQUARES:
      return BalanceSpecFormula::SQUARES;
    case BalanceV2SpecFormula::MAX:
      return BalanceSpecFormula::MAX;
    case BalanceV2SpecFormula::IDEAL:
      return BalanceSpecFormula::IDEAL;
    case BalanceV2SpecFormula::LEGACY:
      return BalanceSpecFormula::LEGACY;
    default:
      throw std::runtime_error("Unhandled BalanceV2SpecFormula");
  }
}

ProblemSolver& ProblemSolver::addGoal(
    const BalanceV2Spec& spec,
    double weight,
    std::optional<int> tuplePos) {
  BalanceSpec v1;
  v1.name() = *spec.name();
  v1.scope() = *spec.scope();
  v1.dimension() = *spec.dimension();
  auto& upperBounds = v1.upperBounds().ensure();
  upperBounds.type() = LimitType::ABSOLUTE;
  upperBounds.globalLimit() = *spec.upperBound();
  if (spec.softUpperBound()) {
    v1.softUpperBound() = *spec.softUpperBound();
  }
  v1.boundType() = toV1(*spec.boundType());
  v1.formula() = toV1(*spec.formula());
  v1.filter() = *spec.filter();
  v1.definition() = toV1(*spec.definition());
  v1.fixAverageToInitial() = *spec.fixAverageToInitial();
  v1.includeInInitialAverage() = *spec.includeInInitialAverage();

  return addGoal(std::move(v1), weight, tuplePos);
}

ProblemSolver& ProblemSolver::addGoalBoundary() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  ++currentGoalTupleIndex_;
  return *this;
}

int ProblemSolver::getCurrentGoalIndex() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  return currentGoalTupleIndex_;
}

ProblemSolver& ProblemSolver::addSolver(const LocalSearchSolverSpec& spec) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkSolverSpec(spec);
  getStrategyBuilder().addSolver(spec);
  return *this;
}

ProblemSolver& ProblemSolver::addSolver(
    const LocalSearchStageSolverSpec& spec) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkSolverSpec(spec);
  getStrategyBuilder().addSolver(spec);
  return *this;
}

ProblemSolver& ProblemSolver::addSolver(const OptimalSolverSpec& spec) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkSolverSpec(spec);
  getStrategyBuilder().addSolver(spec);
  return *this;
}

ProblemSolver& ProblemSolver::addSolver(const OptimalSubsetSolverSpec& spec) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getStrategyBuilder().addSolver(spec);
  return *this;
}

ProblemSolver& ProblemSolver::addSolver(const RasHybridSolverSpec& spec) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getStrategyBuilder().addSolver(spec);
  return *this;
}

void ProblemSolver::setLogLevel(const std::string& level) {
  folly::initLoggingOrDie(level);
}

void ProblemSolver::disableSolutionSummary() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  getSolutionSummary = false;
}

UniverseProblemBuilder& ProblemSolver::getProblemBuilder() {
  return universeProblemBuilder_;
}

StrategyBuilder& ProblemSolver::getStrategyBuilder() {
  return strategyBuilder;
}

AssignmentSolution ProblemSolver::solve() {
  algopt::treeprof::Profiler treeProfiler("ProblemSolver::Solve");
#ifndef REBALANCER_OSS_BUILD
  RolloutConfigResolver::resolveSolveConfigs(rolloutConfig_, service, scope);
#endif
  problem.emplace();
  problem->strategy() = strategyBuilder.build();
  problem->solutionSummaryEnabled() = getSolutionSummary;
  problem->moveStatsSpec() = getMoveStatsSpec();
  problem->profilerEnabled() = isProfilerEnabled();
  problem->service_() = getService();
  problem->scope() = getScope();
  problem->runId() =
      getRunId().empty() ? UuidGenerator::genString() : getRunId();
  problem->enableScubaLogger() = isLoggingEnabled();
  problem->constraintPolicy() = getConstraintPolicy();
  problem->defaultConstraintParams() = getDefaultConstraintParams();
  problem->descendingHotnessContainersOverride() =
      descendingHotnessContainersOverride;
  problem->useDynamicObjectOrdering() = useDynamicObjectOrdering_;
  problem->enableParallelBoundsComputation() = enableParallelBoundsComputation_;
  problem->validateAppliedMoves() = validateAppliedMoves_;
  problem->rolloutConfig() = rolloutConfig_;
  if (decompositionScopeName_) {
    problem->decompositionScopeName() = *decompositionScopeName_;
  }

  problem->publishMetrics() = publishMetrics_;
  problem->publishEquivalenceSetsInfo() = publishEquivalenceSetInfo_;

  // TODO: make object ordering dimension a global setting of the problem.
  auto objectOrderingDimensionName =
      CoreSolver::getObjectOrderingDimensionName(*problem->strategy());
  if (objectOrderingDimensionName) {
    universeProblemBuilder_.setObjectOrderingDimension(
        *objectOrderingDimensionName);
  }

  universe_ = universeProblemBuilder_.build();

  if (*problem->enableScubaLogger()) {
#ifndef REBALANCER_OSS_BUILD
    logger_ = std::make_shared<ScubaLog>(*problem);
#else
    logger_ = std::make_shared<StreamLog>();
#endif
    if (loggingLabel_) {
      logger_->setLoggingLabel(loggingLabel_.value());
    }
    logger_->log(
        GenericInfo{.key = "can_execute_async", .value = canExecuteAsync_});
  }

  if (prepareProblemOnly) {
    XLOG(INFO) << "prepareProblemOnly=true, skip solving";
    return AssignmentSolution{};
  }

  try {
    solution = CoreSolver::solve(
        problem.value(),
        executor,
        useParallelizedNewMaterializer_,
        universe_,
        logger_);

    if (shouldBackup(manifoldBackupParams, *solution->problemProfile())) {
      persistToManifold();
    }

    // stop profiling and store the hierarchy tree in solution object
    treeProfiler.stop();
    CoreSolver::printAndLogHierarchicalProfile(
        treeProfiler.getRoot(), *solution.value().problemProfile(), logger_);
    logProblemSetupTime();
    return solution.value();
  } catch (const std::exception&) {
    logProblemSetupTime();
#ifndef REBALANCER_OSS_BUILD
    persistToManifold();
#endif
    // rethrow the exception
    throw;
  }
}

void ProblemSolver::printReRunManifoldCommand(const std::string& runIdStr) {
  XLOGF(
      INFO,
      "Re-run using the following command: {} -- --run_id {}\n"
      "Link to rebalancer explorer: {}{}",
      kStandaloneSolverCmd,
      runIdStr,
      kRebalancerExplorerUrl,
      runIdStr);
}

void ProblemSolver::persistToManifold(
    std::shared_ptr<AsyncManifoldUploadHandle> manifoldUploadHandle) {
#ifndef REBALANCER_OSS_BUILD
  const algopt::Timer manifoldTimer(true);
  if (!problem.has_value()) {
    throw std::runtime_error("Must invoke solve() before persistToManifold()");
  }

  // if explicit handle is provided, use it
  if (manifoldUploadHandle) {
    manifoldUploadHandle_ = std::move(manifoldUploadHandle);
  }

  if (manifoldUploadHandle_) {
    XLOGF(
        INFO,
        "Launching background manifold upload for runId: {}",
        *problem->runId());
    persistToManifoldImpl(*manifoldUploadHandle_);
  } else {
    AsyncManifoldUploadHandle syncHandle(executor);
    persistToManifoldImpl(syncHandle);
    syncHandle.wait();
  }

  if (logger_) {
    logger_->log(
        GenericInfo{
            .key = "async manifold upload",
            .value = manifoldUploadHandle_ ? 1.0 : 0.0,
            .additionalInfo = ""});
    logger_->log(
        GenericInfo{
            .key = "persist to manifold time sec",
            .value = manifoldTimer.getSeconds(),
            .additionalInfo = ""});
  }
#else
  throw std::runtime_error("Manifold is not supported in OSS build");
#endif
}

void ProblemSolver::persistToManifoldImpl(
    AsyncManifoldUploadHandle& manifoldUploadHandle) {
#ifndef REBALANCER_OSS_BUILD
  if (*manifoldBackupParams.uploadPolicy() == ManifoldUploadPolicy::NEVER) {
    XLOG(WARNING)
        << "ManifoldUploadPolicy is set as NEVER. Not uploading to Manifold.";
    return;
  }

  manifoldUploadHandle.add(
      uploadToManifoldCoro(*problem, universe_, solution, logger_));

#else
  XLOG(WARNING) << "Manifold is not supported in OSS build";
#endif
}

void ProblemSolver::useParallelizedNewMaterializer() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  useParallelizedNewMaterializer_ = true;
}

void ProblemSolver::enableParallelBoundsComputation(bool enable) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  enableParallelBoundsComputation_ = enable;
}

void ProblemSolver::shouldUseDynamicObjectOrdering(
    bool useDynamicObjectOrdering) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  useDynamicObjectOrdering_ = useDynamicObjectOrdering;
}

void ProblemSolver::validateAppliedMoves(bool enable) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  validateAppliedMoves_ = enable;
}

void ProblemSolver::setPrecision(
    algopt::common::thrift::PrecisionTolerances precisionTolerances) {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  checker.checkPrecision(precisionTolerances);
  getProblemBuilder().setPrecision(precisionTolerances);
}

void ProblemSolver::publishMetrics() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  publishMetrics_ = true;
}

void ProblemSolver::publishEquivalenceSetInfo() {
  REBALANCER_PROBLEM_SETUP_TIMER_SCOPE();
  publishEquivalenceSetInfo_ = true;
}

void ProblemSolver::printProblemSetup() const {
  universeProblemBuilder_.printSummary();
}

std::string ProblemSolver::getProblemSummary() const {
  return universeProblemBuilder_.getSummary();
}

void ProblemSolver::logProblemSetupTime() const {
  if (logger_) {
    logger_->log(
        GenericInfo{
            .key = "problem_setup_time_sec",
            .value = problemSetupTime_.getSeconds(),
            .additionalInfo = ""});
  }
}

Bundle ProblemSolver::getBundle() const {
  if (!problem) {
    throw std::runtime_error("Must invoke solve() before getBundle().");
  }
  return getBundleImpl(*problem, universe_.get(), solution);
}

void ProblemSolver::saveBundle(const std::string& path) const {
  const auto serialized = Serializer::serializeBinaryZstd(getBundle());
  if (!folly::writeFile(serialized, path.c_str())) {
    throw std::runtime_error(fmt::format("Unable to write bundle to {}", path));
  }
}

} // namespace facebook::rebalancer::interface
