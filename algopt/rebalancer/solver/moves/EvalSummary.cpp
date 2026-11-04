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

#include "algopt/rebalancer/solver/moves/EvalSummary.h"

#include "algopt/rebalancer/common/Tabulator.h"

#include <fmt/format.h>
#include <folly/json/dynamic.h>

#include <iterator>

using folly::dynamic;

namespace facebook::rebalancer {
EvalSummary::EvalSummary() = default;

void EvalSummary::aggregate(const MoveStats& other) {
  stats_.aggregate(other);
}

std::string EvalSummary::distribution(const EvalMetrics& params) const {
  auto totalMoves = stats_.getTotalMoves();
  if (totalMoves == 0) {
    return "no evals";
  }

  double avgEvalsPerIteration = params.localSearchIterations > 0
      ? static_cast<double>(params.totalEvals) / params.localSearchIterations
      : 0.0;

  return fmt::format(
      ("evals per second: {:.3f}, total: {}, pruned: {}, invalid: {:.3f}%, worse: {:.3f}%, "
       "neutral: {:.3f}%, better: {:.3f}%, search iterations: {}, avg evals/iteration: {:.1f}"),
      totalMoves / params.evalDuration,
      totalMoves,
      stats_.getNumPruned(),
      stats_.getInvalidMoves() * 100.0 / totalMoves,
      stats_.getWorseMoves() * 100.0 / totalMoves,
      stats_.getNeutralMoves() * 100.0 / totalMoves,
      stats_.getBetterMoves() * 100.0 / totalMoves,
      params.localSearchIterations,
      avgEvalsPerIteration);
}

std::string EvalSummary::reasons(std::optional<size_t> truncate) const {
  using reason = std::pair<LabeledExpressionPtr, int64_t>;
  auto compare = [](const reason& x, const reason& y) {
    if (x.second == y.second) {
      return x.first < y.first;
    }
    return x.second > y.second;
  };

  fmt::memory_buffer buf;

  Tabulator invalidMovesTable({});
  invalidMovesTable.setHeader({"Freq", "Invalid Expr"});
  if (auto numInvalidMoves = stats_.getInvalidMoves(); numInvalidMoves > 0) {
    size_t size = 0;
    const std::set<reason, decltype(compare)> invalidMoveReasons(
        stats_.getInvalidMoveReasons().begin(),
        stats_.getInvalidMoveReasons().end(),
        compare);
    for (auto& [expr, count] : invalidMoveReasons) {
      invalidMovesTable.addRow(
          std::initializer_list<std::string>{
              fmt::format("{:.2f}", count * 100.0 / numInvalidMoves),
              expr->name});
      ++size;
      if (truncate && size > *truncate) {
        break;
      }
    }
  }
  invalidMovesTable.tabulate(buf);

  Tabulator worseMovesTable({});
  worseMovesTable.setHeader({"Freq", "Worse Expr"});
  if (auto numWorseMoves = stats_.getWorseMoves(); numWorseMoves > 0) {
    size_t size = 0;
    const std::set<reason, decltype(compare)> worseMoveReasons(
        stats_.getWorseMoveReasons().begin(),
        stats_.getWorseMoveReasons().end(),
        compare);
    for (auto& [expr, count] : worseMoveReasons) {
      worseMovesTable.addRow(
          std::initializer_list<std::string>{
              fmt::format("{:.2f}", count * 100.0 / numWorseMoves),
              expr->name});
      ++size;
      if (truncate && size > *truncate) {
        break;
      }
    }
  }
  worseMovesTable.tabulate(buf);

  return fmt::to_string(buf);
}

std::optional<interface::SolverEvalStats> EvalSummary::getSolverEvalStats(
    double evalDuration,
    double timeToFindWorstContainers,
    int32_t numCycles) const {
  auto totalMoves = stats_.getTotalMoves();
  if (totalMoves == 0) {
    return std::nullopt;
  }

  interface::SolverEvalStats evalStats;
  *evalStats.evalDurationSecs() = evalDuration;
  *evalStats.findWorstDurationSecs() = timeToFindWorstContainers;
  *evalStats.avgEvalSpeed() = totalMoves / evalDuration;
  *evalStats.numEvals() = totalMoves;
  *evalStats.invalidEvalsPct() = stats_.getInvalidMoves() * 100.0 / totalMoves;
  *evalStats.worseEvalsPct() = stats_.getWorseMoves() * 100.0 / totalMoves;
  *evalStats.neutralEvalsPct() = stats_.getNeutralMoves() * 100.0 / totalMoves;
  *evalStats.betterEvalsPct() = stats_.getBetterMoves() * 100.0 / totalMoves;
  *evalStats.numEvalTimeouts() = static_cast<int32_t>(stats_.getNumTimeouts());
  *evalStats.numCycles() = numCycles;
  *evalStats.numPrunedEvals() = stats_.getNumPruned();
  return evalStats;
}

/* static */
void EvalSummary::populateSummary(
    fmt::memory_buffer& buf,
    const MoveResult& moveResult,
    const std::array<std::string, 4>& auxiliaryData,
    const Precision& precision) {
  auto invalidConstraint = moveResult.getFirstInvalidConstraint();
  if (!invalidConstraint) {
    const auto& oldValue = moveResult.getOldValue();
    const auto& newValue = moveResult.getValue();
    fmt::format_to(
        std::back_inserter(buf),
        ("current objective:\t{}\n"
         "evaluated objective:\t{}\n"),
        oldValue.toString(),
        newValue.toString());
  }

  if (moveResult.hasObjectiveDeltas()) {
    auto tb = Tabulator({}).setHeader(
        {"current value",
         "decreased by",
         "description",
         "tracking",
         "object",
         "src container",
         "dst container"});
    for (const auto& delta :
         moveResult.getFirstChangedObjectiveDelta(precision)) {
      auto valueChange = delta.oldValue - delta.newValue;
      if (valueChange == 0) {
        continue;
      }
      tb.addRow(
          dynamic::array(
              delta.newValue,
              valueChange,
              delta.objective->name,
              auxiliaryData.at(0), // tracking info
              auxiliaryData.at(1), // object data
              auxiliaryData.at(2), // src container
              auxiliaryData.at(3) // dst container
              ));
    }
    tb.tabulate(buf);
  }

  if (invalidConstraint) {
    fmt::format_to(
        std::back_inserter(buf),
        "Breaks constraint: {}\n",
        invalidConstraint->name);
  }
}

} // namespace facebook::rebalancer
