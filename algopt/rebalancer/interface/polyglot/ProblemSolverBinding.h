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

#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"

#include <folly/container/F14Map.h>

#include <string>
#include <vector>

namespace facebook {
namespace rebalancer {
namespace binding {

class ProblemSolverBinding {
 public:
  ProblemSolverBinding(
      std::string serviceName,
      std::string serviceScope,
      bool canExecuteAsync = false);

  static int32_t ping() noexcept;

  void addConstraint(
      interface::ConstraintSpecs spec,
      std::optional<interface::ConstraintPolicy> policy,
      std::optional<float> invalidCost,
      std::optional<float> invalidState,
      std::optional<int32_t> tuplePosIfBroken);

  void addContainerDimension(
      const std::string& dimensionName,
      const std::unordered_map<std::string, double>& containerToValue,
      double defaultValue);

  void addGoal(
      interface::GoalSpecs spec,
      float weight,
      std::optional<int32_t> tuplePosOpt);

  void addGoalBoundary();

  void addObjectDimension(
      const std::string& dimensionName,
      std::unordered_map<std::string, double> objectToValue,
      double defaultValue);

  void addObjectDimensionByPartition(
      const std::string& dimensionName,
      const std::string& partitionName,
      std::unordered_map<std::string, double> groupToValue,
      double defaultValue);

  void addObjectDimensionVector(
      const std::string& dimensionName,
      std::unordered_map<std::string, std::vector<double>> objectToValues,
      double defaultValue);

  void addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
          scopeItemToObjectToValue,
      double defaultValue);

  void addDynamicObjectDimensionByPartition(
      const std::string& dimensionName,
      const std::string& scope,
      const std::string& partitionName,
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
          scopeItemToGroupToValue,
      double defaultValue);

  void addObjectPartitionRoutingDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      double defaultValue = 0);

  void addObjectPartitionRoutingDimensionWithStaticValues(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      const std::unordered_map<std::string, double>& groupToStaticValue,
      double defaultValue = 0, // default value for groupToValue
      double defaultStaticValue = 0); // default value for groupToStaticValue

  void addPartition(
      const std::string& partitionName,
      std::unordered_map<std::string, std::vector<std::string>> groupToObjects);

  void addScope(
      const std::string& scopeName,
      const std::unordered_map<std::string, std::string>& containerToScopeItem);

  void addScopeDimension(
      const std::string& dimensionName,
      const std::string& scopeName,
      const std::unordered_map<std::string, double>& scopeItemToValue);

  void addSimilarContainers(
      const std::vector<std::vector<std::string>>& similarContainerClasses);

  void addRoutingConfig(
      const std::string& configName,
      const std::string& scopeName,
      const std::string& partitionName,
      const std::unordered_map<std::string, interface::GroupRoutingRings>&
          groupToRoutingRings,
      const std::unordered_map<
          std::string,
          std::unordered_map<std::string, double>>& originToDestinationLatency,
      const std::optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>&
          defaultOriginToDestinationScopeItemSets = std::nullopt);

  void addSolver(interface::SolverSpecs spec);

  void disableSolutionSummary();

  void disableScubaLogging();

  void enableMoveStats(const interface::MoveStatsSpec& spec);

  void enableProfiler();

  void enableRestrictMovingObjectOnlyOnce();

  void enableStableAsMuchAsPossible();

  int32_t getCurrentGoalIndex() const;

  std::string getRunId() const;

  void printProblemSetup() const;
  std::string getProblemSummary() const;

  void persistToManifold(
      std::optional<std::shared_ptr<interface::AsyncManifoldUploadHandle>>
          manifoldUploadHandle = std::nullopt);

  void saveBundle(const std::string& path);

  void publishMetrics();

  void setAssignment(
      const std::unordered_map<std::string, std::vector<std::string>>&
          containerToObjects);

  void setConstraintPolicy(const interface::ConstraintPolicy& policy);

  void setDefaultConstraintParams(const interface::ConstraintParams& params);

  void setContainerName(const std::string& containerName);

  void setFeasibilityTolerance(double feasibilityTolerance);

  void setLogLevel(const std::string& level);

  void setManifoldBackupParams(
      const interface::ManifoldBackupParams& params,
      std::optional<std::shared_ptr<interface::AsyncManifoldUploadHandle>>
          manifoldUploadHandle = std::nullopt);

  void setLoggingLabel(const std::string& loggingLabel);

  void setObjectName(const std::string& objectName);

  void setRunId(std::string runId);

  interface::AssignmentSolution solve();

  void useParallelizedNewMaterializer();

  void enableParallelBoundsComputation(bool enable);

  void shouldUseDynamicObjectOrdering(bool useDynamicObjectOrdering);

  void validateAppliedMoves(bool enable);

  void setPrecision(
      const algopt::common::thrift::PrecisionTolerances& precisionTolerances);

 private:
  std::unique_ptr<interface::ProblemSolver> solver_;
};

} // namespace binding
} // namespace rebalancer
} // namespace facebook
