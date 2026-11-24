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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"

#include <fmt/format.h>

#include <optional>

namespace facebook::rebalancer {

struct EvalMetrics {
  double evalDuration = 0.0;
  size_t localSearchIterations = 0;
  size_t totalEvals = 0;
};

class EvalSummary {
 public:
  EvalSummary();
  void aggregate(const MoveStats& other);
  std::string distribution(const EvalMetrics& params) const;
  std::string reasons(std::optional<size_t> truncate = std::nullopt) const;
  std::optional<interface::SolverEvalStats> getSolverEvalStats(
      double evalDuration,
      double timeToFindWorstContainers,
      int32_t numCycles,
      const std::optional<interface::EvalReasonBreakdownSpec>&
          reasonBreakdownSpec = std::nullopt) const;

  static void populateSummary(
      fmt::memory_buffer& buf,
      const MoveResult& moveResult,
      const std::array<std::string, 4>& auxiliaryData,
      const Precision& precision);

 private:
  MoveStats stats_;
};

} // namespace facebook::rebalancer
