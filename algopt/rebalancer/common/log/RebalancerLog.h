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

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"

#include <fmt/core.h>
#include <folly/json/dynamic.h>

#include <map>
#include <set>
#include <string>

namespace facebook {
namespace rebalancer {

struct ProblemDefinition {
  std::string objectName;
  std::string containerName;
  int objectCount;
  int containerCount;
  int scopeCount;
  int partitionCount;
  int dimensionCount;
  int goalCount;
  int constraintCount;
};

struct NodeSummary {
  size_t fixedNodeCount;
  size_t nodeCount;
};

struct ScopeDimensionSummary {
  double initialAfter;
  double after;
  double inVal;
  double outVal;
};

struct ObjectGroupSummary {
  std::string group_name;
  int count_in_item;
  int total_size;
  int limit_count;
};

struct ScopeItemSummary {
  std::string name;
  int objectCount;
  int fixedCount;
  /* key = dimension name */
  std::map<std::string, ScopeDimensionSummary> dimSummary;
  /* key = partition name */
  std::map<std::string, std::vector<ObjectGroupSummary>> partitionSummary;
};

struct ScopeSummary {
  std::string name;
  std::set<int> changedItems;
  std::map<std::string, double> balancedUtil;

  // this is a vector because order matters
  std::vector<ScopeItemSummary> itemSummaries;
};

struct SolutionStats {
  double value;
  int totalMoves;
  int equivalentSetCount;
  // optional since only this value is only available when using optimalSolver
  std::optional<double> relativeObjectiveGap;
};

struct SolverSummary {
  SolverType solverType;

  // Solver backend that actually ran, which differs from the requested one
  // when a commercial solver fails to load and falls back to HiGHS. Only set
  // by MIP solvers.
  std::optional<std::string> solverPackage = std::nullopt;

  // Parameters set on the solver library and the library's version. Only set
  // by MIP solvers; see algopt::lp::ProblemImpl::getSetParameters().
  std::optional<std::map<std::string, double>> solverParameters = std::nullopt;
  std::optional<std::string> solverVersion = std::nullopt;

  interface::EndReason endReason;
  /* some endReason has detailed numerical information,
   * such as move_limit, time_limit etc,
   * the extra information is in double auxInfo
   */
  std::optional<std::string> auxInfo;

  // captures additional stats during solving: like evals/s, num evals
  // histogram of moves, etc.
  std::optional<interface::SolverEvalStats> evalStats = std::nullopt;

  // captures the distribution of moves with time spent in solver
  std::optional<interface::SolverMoveStats> moveStats = std::nullopt;

  // captures solver summary at each stage (only populated by stage solvers)
  std::optional<std::vector<interface::StageSummary>> stagesSummaries =
      std::nullopt;
};

struct BenchmarkInfo {
  std::string key;
  double totalTime;
};

struct GenericInfo {
  std::string key;
  folly::dynamic value;
  std::string additionalInfo{};
};

struct ManifoldInfo {
  // information only logged in scuba if the problem instance is uploaded to
  // manifold
  std::optional<time_t> manifoldExpirationTime;
};

struct ExitStatusInfo {
  bool hasException;
  std::string exceptionMsg;
  bool isUserError = false;
};

struct PerformanceInfo {
  double totalSolveTime;
  double peakMemoryUsed;
};

struct SpecParameters {
  std::string name;
  std::optional<std::string> scope = std::nullopt;
  std::optional<std::string> partition = std::nullopt;
  std::optional<std::string> dimension = std::nullopt;
  std::optional<std::string> definition = std::nullopt;
  std::optional<std::string> boundType = std::nullopt;
  std::optional<std::string> limitType = std::nullopt;
  std::optional<std::string> formula = std::nullopt;
  std::optional<std::string> zeroAllowed = std::nullopt;
  std::optional<std::string> squares = std::nullopt;

  // defined for specs for which there is a natural notion of size. For example,
  // size is equal to number of affinities in AssignmentAffinitiesSpec and is
  // equal to number of assignments in AvoidAssignmentsSpec
  std::optional<int> size = std::nullopt;
  std::optional<int> filterAllowListSize = std::nullopt;
  std::optional<int> filterBlockListSize = std::nullopt;

  bool operator==(const SpecParameters& other) const = default;
};

struct SpecUsageInfo {
  std::string specType;
  SpecParameters specParameters;
  std::string usedAs;
  double materializationTime;
};

class RebalancerLog {
 public:
  /* high level APIs */
  virtual void log(const ProblemDefinition& /* info */) {}
  virtual void log(const interface::GlobalObjectiveSummary& /* info */) {}
  virtual void log(const interface::ConstraintSummary& /* info */) {}
  virtual void log(const SolutionStats& /* info */) {}
  virtual void log(const interface::MovesSummary& /* info */) {}
  virtual void log(const SolverSummary& /* info */) {}
  virtual void log(const BenchmarkInfo& /* info */) {}
  virtual void log(const ExitStatusInfo& /* info */) {}
  virtual void log(const interface::FinalEvaluationSummary& /* info */) {}
  virtual void log(const interface::LocalSearchProfile& /* info */) {}
  virtual void log(const interface::SpecMetadata& /* info */) {}
  virtual void log(const GenericInfo& /* info */) {}
  virtual void log(const PerformanceInfo& /*info */) {}
  virtual void log(const interface::HierarchicalProfileNode& /*info*/) {}
  virtual void log(const SpecUsageInfo& /*info*/) {}
  virtual void log(const ManifoldInfo& /*info*/) {}
  virtual void log(const interface::thrift::Metrics& /*metrics*/) {}
  virtual void log(const NodeSummary& /*info*/) {}
  virtual void log(const interface::LearnedMoveFilterInfo& /*info*/) {}

  virtual void setLoggingLabel(const std::string& /*label*/) {}

  virtual ~RebalancerLog() = default;

 protected:
};

class Benchmark {
 public:
  explicit Benchmark(std::string&& key, std::shared_ptr<RebalancerLog> logger)
      : key_(std::move(key)), logger_(logger) {
    timer_.start();
  }

  ~Benchmark() {
    timer_.stop();
    logger_->log(BenchmarkInfo{.key = key_, .totalTime = timer_.getSeconds()});
  }
  Benchmark(const Benchmark&) = delete;
  Benchmark(Benchmark&&) = delete;
  Benchmark& operator=(const Benchmark&) = delete;
  Benchmark& operator=(Benchmark&&) = delete;

 private:
  std::string key_;
  std::shared_ptr<RebalancerLog> logger_;
  facebook::algopt::Timer timer_;
};

} // namespace rebalancer
} // namespace facebook
