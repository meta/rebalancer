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

#include "algopt/rebalancer/solver/solvers/CoreLocalSearchSolve.h"

#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"
#include "algopt/rebalancer/solver/solvers/HotContainerSelector.h"
#include "algopt/rebalancer/solver/utils/FinalEvaluationSummaryHelper.h"
#include "algopt/rebalancer/solver/utils/MovesSummaryHelper.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include <fmt/format.h>
#include <folly/container/irange.h>

namespace facebook::rebalancer {

namespace {

constexpr std::string_view kNoMoreMoves = "unable to find more moves";
constexpr std::string_view kReachedLocalOptimum =
    "(stage) objective reached its estimated local optimum";
constexpr std::string_view kReachedGlobalOptimum =
    "(stage) objective reached its estimated global optimum";
const static std::string kCycle = "cycle";

bool isObjectiveWithinBounds(
    const GlobalObjectiveValue& objectiveValue,
    const GlobalObjectiveValue& computedBound,
    const std::string& boundType,
    const Precision& precision) {
  auto compare = GlobalObjectiveValue::precisionCompare(
      objectiveValue, computedBound, precision);
  if (compare < 0) {
    XLOGF_EVERY_MS(
        WARNING,
        10000,
        "Objective value {} is less than estimated {} lower bound {}. This is likely to due to numerical issues.",
        objectiveValue.toString(),
        boundType,
        computedBound.toString());
    return true;
  }

  return (compare == 0);
}

bool isObjectiveWithinConstrainedBounds(
    const GlobalObjective& objective,
    const PackerSet<entities::ContainerId>& skipContainers,
    const Precision& precision) {
  auto objectiveValue = objective.getValue();
  auto bc = BoundConstraints::Builder{}
                .setNotGivingContainers(skipContainers)
                .build();
  auto constrainedLowerBound = objective.lowerBound(bc);
  XLOG_EVERY_MS(DBG1, 30000) << fmt::format(
      "Pruning attempt: objective value: {}, local optimum: {}, "
      "num could not improve containers: {}",
      objectiveValue.toString(),
      constrainedLowerBound.toString(),
      skipContainers.size());

  return isObjectiveWithinBounds(
      objectiveValue, constrainedLowerBound, "constrained", precision);
}

std::string formatAppliedMoveCounts(
    const PackerMap<size_t, MoveApplyInfo>& moveApplyInfo,
    const std::vector<std::shared_ptr<MoveType>>& allowedMoves) {
  std::vector<std::string> result;
  result.reserve(moveApplyInfo.size());
  for (auto& [moveIndex, moveInfo] : moveApplyInfo) {
    result.emplace_back(
        fmt::format(
            "{} applied {} times => {} moves",
            allowedMoves.at(moveIndex)->name(),
            moveInfo.applyCount,
            moveInfo.moveCount));
  }

  if (result.empty()) {
    return "zero moves performed";
  }

  return folly::join(", ", result);
}

std::string formatSolveStartMessage(const SolveParams& solveParams) {
  auto stopAfterMoves = solveParams.stopAfterMoves;

  auto timeLimitDesc =
      (solveParams.solveTime != std::numeric_limits<double>::infinity())
      ? std::to_string(solveParams.solveTime)
      : "UNBOUNDED";

  auto moveLimitDesc =
      (stopAfterMoves ? std::to_string(*stopAfterMoves) : "UNBOUNDED");

  return fmt::format(
      "Running local search until {} seconds or {} moves",
      timeLimitDesc,
      moveLimitDesc);
}

MoveStatsAggregator createMoveStats(const Problem& problem) {
  auto msa = MoveStatsAggregator(
      problem.moveStatsAggregatorConfig, problem.getUniverse().getPrecision());
  msa.setObjectNameProvider(
      [&](entities::ObjectId object) { return problem.objectName(object); });
  msa.setContainerNameProvider([&](entities::ContainerId container) {
    return problem.containerName(container);
  });
  return msa;
}
} // namespace

/* static */
interface::SolverMoveStats CoreLocalSearchSolve::getMovesHistogram(
    const std::vector<std::pair<double, int>>& moveTimes,
    const double duration,
    const double timeToApplyMoves,
    const std::vector<std::shared_ptr<MoveType>>& moveTypes) {
  static constexpr double smallBucketSize = 10;
  static constexpr double bigBucketSize = 60;
  static constexpr int expectedNumBuckets = 120;

  const auto bucketSize = (duration / smallBucketSize > expectedNumBuckets)
      ? bigBucketSize
      : smallBucketSize;
  int totalNumMoves = 0;
  folly::Histogram<double> hist(bucketSize, 0, duration);
  for (auto [moveTime, numMoves] : moveTimes) {
    totalNumMoves += numMoves;
    for ([[maybe_unused]] const auto _ : folly::irange(numMoves)) {
      hist.addValue(moveTime);
    }
  }

  interface::SolverMoveStats moveStats;
  *moveStats.durationSecs() = duration;
  *moveStats.numMoves() = totalNumMoves;
  *moveStats.applyDurationSecs() = timeToApplyMoves;
  for (const auto& moveType : moveTypes) {
    moveStats.moveTypes()->push_back(moveType->name());
  }

  // first bucket (index 0) has all values less than min (0 here) and the last
  // bucket (index hist.getNumBuckets()-1) has all values greater than max
  // ('duration' here). Since all the moves happen within [0, duration], we only
  // look at buckets between index 1 and getNumBuckets()-2
  for (const auto n : folly::irange(1, hist.getNumBuckets() - 2 + 1)) {
    moveStats.movesHistogram()->push_back(hist.getBucketByIndex(n).count);
  }

  return moveStats;
}

bool CoreLocalSearchSolve::shouldRecomputeHottestOrderingAfterMove(
    int nContainersToOrder) const {
  // if hottest ordering is based on objectPotentials, then we must recompute
  // the ordering after a move because several of the iterators might have
  // become invalid (e.g. see how ObjectLookup 'objectPotentials_' are updated
  // inside partialApply)
  if (solveParams_.enableObjectPotentialSorting) {
    return true;
  }

  // After an apply, re-set descending containers traversal if
  // 'recomputeContainerOrderingAfterEveryMove' is set to 'true' and
  // there are at least two containers that are NOT skipped
  return (
      *spec_.recomputeContainerOrderingAfterEveryMove() &&
      nContainersToOrder >= 2);
}

bool CoreLocalSearchSolve::shouldTerminate(
    double curTime,
    double lastImprovedTime) {
  if (curTime >= solveParams_.solveTime) {
    stopInfo_.endReason = interface::EndReason::HIT_TIME_LIMIT;
    stopInfo_.auxInfo =
        fmt::format("hit time limit of {}s", solveParams_.solveTime);
    return true;
  }

  if (solveParams_.stopAfterMoves &&
      totalMoves_ >= *solveParams_.stopAfterMoves) {
    stopInfo_.endReason = interface::EndReason::HIT_MOVE_LIMIT;
    stopInfo_.auxInfo =
        fmt::format("hit move limit of {}", *solveParams_.stopAfterMoves);
    return true;
  }

  if (spec_.allowedPlateauTime()) {
    const int plateauTime = *spec_.allowedPlateauTime();
    if (curTime - lastImprovedTime > plateauTime) {
      stopInfo_.endReason = interface::EndReason::HIT_PLATEAU_TIME;
      stopInfo_.auxInfo = fmt::format("hit plateau time of {}s", plateauTime);
      return true;
    }
  }

  return false;
}

// Returns true if any objective position improved beyond the configured
// minCycleObjectiveImprovement threshold during the current cycle.
bool CoreLocalSearchSolve::hasSufficientObjectiveImprovement() {
  if (!spec_.minCycleObjectiveImprovement().has_value()) {
    return true;
  }

  const auto& threshold =
      *spec_.minCycleObjectiveImprovement()->defaultThreshold();
  const auto& objectiveAtCycleStart = solveState_.objectiveAtCycleStart;
  const auto tupleSize = std::min(
      static_cast<int>(objectiveAtCycleStart.size()),
      problem_.objective.size());

  for (const auto i : folly::irange(tupleSize)) {
    if (algopt::common::thriftUtils::isSignificantDecrease(
            threshold,
            objectiveAtCycleStart.get(i),
            problem_.objective.getValueAt(i))) {
      return true;
    }
  }

  stopInfo_.endReason =
      interface::EndReason::HIT_MIN_CYCLE_OBJECTIVE_IMPROVEMENT;
  stopInfo_.auxInfo = "cycle objective improvement below minimum threshold";
  return false;
}

bool CoreLocalSearchSolve::shouldStartNextCycle() {
  return solveState_.movesInCycle > 0 && hasSufficientObjectiveImprovement();
}

CoreLocalSearchSolve::CoreLocalSearchSolve(
    interface::LocalSearchSolverSpec spec,
    std::vector<std::shared_ptr<MoveType>> allowedMoves,
    Problem& problem,
    const SolveParams& solveParams,
    std::shared_ptr<SolverOutputLogger> logger)
    : spec_(std::move(spec)),
      problem_(problem),
      solveParams_(solveParams),
      logger_(std::move(logger)),
      allowedMoves_(std::move(allowedMoves)),
      solveState_{SolveState(problem_, solveParams_, spec_)},
      stopInfo_{
          .endReason = interface::EndReason::UNABLE_TO_FIND_MORE_MOVES,
          .auxInfo = kNoMoreMoves.data()},
      finalEvaluationStats_{createMoveStats(problem)},
      perMoveStatsAggregator_{createMoveStats(problem)} {
  solved_ = solve();
}

bool CoreLocalSearchSolve::finalizeAndReturn(bool solved) {
  timer_.stop();
  boundsCheckTimer_.stop();

  assert(!applyTimer_.is_running());
  assert(!evalTimer_.is_running());
  assert(!pruneTimer_.is_running());

  auto findTime = getFindTime();
  const auto totalDuration = timer_.getSeconds();

  moveStats_ = CoreLocalSearchSolve::getMovesHistogram(
      moveTimes_, totalDuration, applyTimer_.getSeconds(), allowedMoves_);
  evalStats_ = solverEvalSummary_.getSolverEvalStats(
      /*evalDuration=*/evalTimer_.getSeconds(),
      /*timeToFindWorstContainers=*/findTime,
      solveState_.cyclesStarted,
      problem_.configs.moveStatsSpec.evalReasonBreakdownSpec().to_optional());

  auto stopMessage = getStopLog(solveParams_.stopEventName);
  REBALANCER_LOG_SOLVER_OUTPUT(INFO, stopMessage);

  auto endStageMessage = fmt::format(
      "\n**** {} summary ****{}\n{}\nend reason: {} ({})",
      solveParams_.solveEventName,
      summarizeProgress(),
      summarizeEvalStatsAndObjective(),
      apache::thrift::util::enumNameSafe(stopInfo_.endReason),
      stopInfo_.auxInfo);
  REBALANCER_LOG_SOLVER_OUTPUT(INFO, endStageMessage);

  return solved;
}

double CoreLocalSearchSolve::getTimePerMove() {
  const auto remainingStageSolveTime =
      solveParams_.solveTime - timer_.getSeconds();
  const auto timePerMove = spec_.timePerMove()
      ? *spec_.timePerMove()
      : std::numeric_limits<double>::infinity();
  const auto timeLimit = std::min(timePerMove, remainingStageSolveTime);
  return timeLimit;
}

MoveResult CoreLocalSearchSolve::findBestMove(
    entities::ContainerId hotContainer,
    const std::shared_ptr<MoveType>& allowedMove) {
  ++localSearchIterations_;
  const double evalTimeBeforeCall = evalTimer_.getSeconds();
  evalTimer_.start();
  // make sure to clear the stats before we start evaluating new moves
  perMoveStatsAggregator_.clear();
  const auto& precision = problem_.getUniverse().getPrecision();
  MoveResult bestResult = allowedMove->findBestMove(
      solveState_.evaluator,
      hotContainer,
      perMoveStatsAggregator_,
      solveState_.hints,
      getTimePerMove());
  finalEvaluationStats_.aggregate(perMoveStatsAggregator_);
  evalTimer_.stop();

  const auto& globalStats = perMoveStatsAggregator_.getGlobalStats();
  totalEvals_ += globalStats.getTotalMoves();
  solverEvalSummary_.aggregate(globalStats);
  globalMoveStats_.aggregate(globalStats);

  const double callDuration = evalTimer_.getSeconds() - evalTimeBeforeCall;

  XLOG_EVERY_MS(INFO, 30000) << summarizeProgress();
  XLOG_EVERY_MS(DBG1, 30000)
      << summarizeEvalStatsAndObjective() << solverEvalSummary_.reasons(5);
  XLOGF_EVERY_MS(
      DBG1,
      30000,
      "findBestMove stats - evals this call: {}, time this call: {}s, time per eval: {}us",
      globalStats.getTotalMoves(),
      callDuration,
      (globalStats.getTotalMoves() > 0
           ? (callDuration * 1e6 / globalStats.getTotalMoves())
           : 0.0));
  XLOG(DBG2) << fmt::format(
      "Attempted move with hot container {} and move type {}, is better?  {} ",
      problem_.containerName(hotContainer),
      allowedMove->name(),
      bestResult.isBetter(precision));

  return bestResult;
}

bool CoreLocalSearchSolve::tryApplyMove(
    size_t moveIndex,
    const MoveResult& moveResult) {
  const auto acceptMove = [this, moveIndex, &moveResult] {
    lastApplyRejectionReason_.reset();
    solveState_.hints.update(problem_, solveState_.evaluator, moveResult);
    const auto numMoves = moveResult.getMoveSet().size();
    solveState_.movesInCycle += numMoves;
    solveState_.moveCounts[moveIndex].recordApply(numMoves);
    return true;
  };
  const auto rejectMove = [this, moveIndex, &moveResult](
                              std::string_view reason) {
    lastApplyRejectionReason_.emplace(reason);
    XLOGF(
        DBG2,
        "Post-apply validation rejected a candidate from move type {}.\nReason: {}\nCandidate move: {}",
        allowedMoves_[moveIndex]->name(),
        reason,
        moveResult.getMoveSet().toString(problem_.getUniverse()));
    return false;
  };

  switch (solveState_.evaluator.tryApply(moveResult)) {
    case ApplyStatus::APPLIED:
      return acceptMove();
    case ApplyStatus::VIOLATES_CONSTRAINT:
      return rejectMove("candidate move violates a constraint");
    case ApplyStatus::WORSENS_HIGHER_PRIORITY_OBJECTIVE:
      return rejectMove("candidate move worsens a higher-priority objective");
    case ApplyStatus::DOES_NOT_IMPROVE_OBJECTIVE:
      return rejectMove("candidate move does not improve the objective");
  }
}

bool CoreLocalSearchSolve::improveHotContainer(
    entities::ContainerId hotContainer,
    double& lastImprovedTime) {
  const auto& precision = problem_.getUniverse().getPrecision();
  for (const auto moveIndex : folly::irange(allowedMoves_.size())) {
    const auto& allowedMove = allowedMoves_[moveIndex];
    auto moveProfiler =
        MoveTypeProfiler(solveParams_.profiler, allowedMove->name());
    auto moveResult = findBestMove(hotContainer, allowedMove);

    if (!moveResult.isBetter(precision)) {
      continue;
    }

    applyTimer_.start();
    if (!tryApplyMove(moveIndex, moveResult)) {
      applyTimer_.stop();
      continue;
    }

    const auto numMoves = moveResult.getMoveSet().size();
    auto movesSummary = MovesSummaryHelper::makeMovesSummary(
        problem_,
        moveResult.getMoveSet(),
        moveResult.hasObjectiveDeltas() ? &moveResult.getObjectiveDeltas()
                                        : nullptr,
        perMoveStatsAggregator_,
        solveParams_.stageId,
        solveState_.cyclesStarted);
    problem_.configs.logger->log(std::move(movesSummary));
    moveTimes_.emplace_back(timer_.getSeconds(), numMoves);

    lastImprovedTime = timer_.getSeconds();

    moveProfiler.updateValue(problem_.objective.getValue().toVector());

    // Only track stats after the last apply.
    finalEvaluationStats_.clear();
    applyTimer_.stop();

    auto nContainersToOrder =
        problem_.containers.size() - solveState_.skipContainers.size();
    if (shouldRecomputeHottestOrderingAfterMove(nContainersToOrder)) {
      solveState_.hotContainerSelector_.reset();
    }

    totalMoves_ += numMoves;
    return true; // found a move
  }
  return false;
}

std::string CoreLocalSearchSolve::summarizeProgress() {
  return fmt::format(
      "\nmoves applied: {} ({}), moves this cycle: {}, total: {}; containers explored: {}, cycles started: {}\n"
      "time: {:.3f}s, find worst: {:.3f}s, eval: {:.3f}s, apply: {:.3f}s, prune: {:.3f}s",
      totalMoves_,
      formatAppliedMoveCounts(solveState_.moveCounts, allowedMoves_),
      solveState_.movesInCycle,
      totalMoves_,
      solveState_.skipContainers.size(),
      solveState_.cyclesStarted,
      getTotalDuration(),
      getFindTime(),
      getEvalTime(),
      getApplyTime(),
      getPruneTime());
}

std::string CoreLocalSearchSolve::summarizeEvalStatsAndObjective() {
  return fmt::format(
      "objective: {} | estimated lower bound: {}\n{}",
      problem_.objective.getValue().toString(),
      problem_.objective.lowerBound().toString(),
      solverEvalSummary_.distribution(
          EvalMetrics{
              .evalDuration = evalTimer_.getSeconds(),
              .localSearchIterations = localSearchIterations_,
              .totalEvals = totalEvals_}));
}

std::string CoreLocalSearchSolve::getStopLog(const std::string& stopEventName) {
  auto reason = stopInfo_.auxInfo.empty()
      ? "..."
      : fmt::format(" because {}", stopInfo_.auxInfo);
  return fmt::format("\nStopping {}{}", stopEventName, std::move(reason));
}

bool CoreLocalSearchSolve::reachedGlobalOptimum() {
  pruneTimer_.start();
  const auto objective = problem_.objective.getRange(
      solveParams_.objectiveTupleStart, solveParams_.objectiveTupleEnd);
  const auto& precision = problem_.getUniverse().getPrecision();
  if (isObjectiveWithinBounds(
          objective.getValue(),
          objective.lowerBound(),
          "unconstrained",
          precision)) {
    pruneTimer_.stop();
    stopInfo_.endReason = interface::EndReason::OPTIMAL;
    stopInfo_.auxInfo = kReachedGlobalOptimum;
    return true;
  }
  pruneTimer_.stop();
  return false;
}

bool CoreLocalSearchSolve::reachedLocalOptimum() {
  pruneTimer_.start();
  const auto& objective = solveState_.userObjective;
  if (!(*spec_.enableConstrainedBoundsOptimization())) {
    pruneTimer_.stop();
    return false;
  }
  if (boundsCheckTimer_.getMilliseconds() < *spec_.constrainedBoundsCheckMs()) {
    pruneTimer_.stop();
    return false;
  }
  boundsCheckTimer_.reset();

  const auto& precision = problem_.getUniverse().getPrecision();
  if (isObjectiveWithinConstrainedBounds(
          objective, solveState_.skipContainers, precision)) {
    stopInfo_.auxInfo = kReachedLocalOptimum;
    pruneTimer_.stop();
    return true;
  }
  pruneTimer_.stop();
  return false;
}

bool CoreLocalSearchSolve::solve() {
  const algopt::treeprof::EventRecorder solveEvent(solveParams_.solveEventName);
  timer_.start();
  boundsCheckTimer_.start();
  XLOG(INFO) << formatSolveStartMessage(solveParams_);

  // exit early and report if we have reached the global optimum
  if (reachedGlobalOptimum()) {
    return finalizeAndReturn(true);
  }

  // exit early when there is no time or move budget left.
  auto lastImprovedTime = timer_.getSeconds();
  if (shouldTerminate(timer_.getSeconds(), lastImprovedTime)) {
    return finalizeAndReturn(false);
  }

  if (spec_.customEquivalenceSetConfig()) {
    problem_.getEquivalenceSetsStore().initialize(
        *spec_.customEquivalenceSetConfig());
  }
  // Move types read the equivalence indexes, some from worker threads, so they
  // are built before they run.
  problem_.assignment.buildAllIndexesByEquivalentSets(
      problem_.getUniverse(), problem_.getEquivalenceSets());

  // Cycle through checking improvements on all containers,
  // until a cycle's improvement is insufficient to justify another cycle.
  do {
    solveState_.startNewCycle();
    while (auto hotContainer = solveState_.hotContainerSelector_.next(
               solveState_.skipContainers)) {
      if (!improveHotContainer(*hotContainer, lastImprovedTime)) {
        solveState_.skipContainers.insert(*hotContainer);
        solveState_.unfixableContainers.insert(*hotContainer);
      } else {
        // After an apply, the list of unfixable containers becomes stale.
        solveState_.unfixableContainers.clear();
      }

      if (reachedGlobalOptimum()) {
        return finalizeAndReturn(true);
      } else if (shouldTerminate(timer_.getSeconds(), lastImprovedTime)) {
        return finalizeAndReturn(false);
      } else if (reachedLocalOptimum()) {
        break;
      }
    }

    auto msg = fmt::format(
        "{}{}\n{}",
        getStopLog(kCycle),
        summarizeProgress(),
        summarizeEvalStatsAndObjective());
    REBALANCER_LOG_SOLVER_OUTPUT(DBG1, msg);

  } while (shouldStartNextCycle());
  if (lastApplyRejectionReason_.has_value() &&
      stopInfo_.endReason == interface::EndReason::UNABLE_TO_FIND_MORE_MOVES) {
    stopInfo_.endReason = interface::EndReason::UNABLE_TO_FIND_IMPROVING_MOVES;
    stopInfo_.auxInfo = *lastApplyRejectionReason_;
  }
  return finalizeAndReturn(true);
}

void CoreLocalSearchSolve::writeToOutputLoggerMaybe(
    const std::string& logLine) const {
  if (logger_ != nullptr) {
    logger_->appendToLog(logLine);
  }
}

bool CoreLocalSearchSolve::getSolved() const {
  return solved_;
}
double CoreLocalSearchSolve::getEvalTime() const {
  return evalTimer_.getSeconds();
}
double CoreLocalSearchSolve::getApplyTime() const {
  return applyTimer_.getSeconds();
}

double CoreLocalSearchSolve::getFindTime() const {
  return solveState_.hotContainerSelector_.getFindTime();
}
double CoreLocalSearchSolve::getPruneTime() const {
  return pruneTimer_.getSeconds();
}
double CoreLocalSearchSolve::getTotalDuration() const {
  return timer_.getSeconds();
}
StopInfo CoreLocalSearchSolve::getStopInfo() const {
  return stopInfo_;
}
std::vector<std::pair<double, int>> CoreLocalSearchSolve::getMoveTimes() const {
  return moveTimes_;
}
interface::SolverMoveStats CoreLocalSearchSolve::getMoveStats() const {
  return moveStats_;
}
interface::FinalEvaluationSummary
CoreLocalSearchSolve::makeFinalEvaluationSummary() const {
  return FinalEvaluationSummaryHelper::makeSummary(
      problem_, finalEvaluationStats_);
}
std::optional<interface::SolverEvalStats> CoreLocalSearchSolve::getEvalStats()
    const {
  return evalStats_;
}
size_t CoreLocalSearchSolve::getTotalEvals() const {
  return totalEvals_;
}
int CoreLocalSearchSolve::getTotalMoves() const {
  return totalMoves_;
}
const MoveStats& CoreLocalSearchSolve::getGlobalMoveStats() const {
  return globalMoveStats_;
}

} // namespace facebook::rebalancer
