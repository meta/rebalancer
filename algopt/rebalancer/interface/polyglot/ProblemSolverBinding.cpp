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

#include "algopt/rebalancer/interface/polyglot/ProblemSolverBinding.h"

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"

#include <thrift/lib/cpp2/op/Get.h>

#include <optional>
#include <unordered_map>

namespace facebook::rebalancer::binding {

namespace {
template <typename T>
void validateSpecName(const T& spec) {
  if (spec.name().value().empty()) {
    throw std::runtime_error(
        fmt::format(
            "Expected name field in '{}' to be non-empty",
            apache::thrift::op::get_class_name_v<T>));
  }
}
} // namespace

ProblemSolverBinding::ProblemSolverBinding(
    std::string serviceName,
    std::string serviceScope,
    bool canExecuteAsync)
    : solver_(
          interface::ProblemSolverFactory::makeProblemSolver(
              std::move(serviceName),
              std::move(serviceScope),
              /* canExecuteAsync= */ canExecuteAsync)) {}

int32_t ProblemSolverBinding::ping() noexcept {
  return 4;
}

// Problem building

void ProblemSolverBinding::addConstraint(
    interface::ConstraintSpecs spec,
    std::optional<interface::ConstraintPolicy> policy,
    std::optional<float> invalidCost,
    std::optional<float> invalidState,
    std::optional<int32_t> tuplePosIfBroken) {
  algopt::utils::throwIfUnionIsEmpty(spec);

  apache::thrift::op::visit_union_with_tag(
      spec,
      [this, policy, invalidCost, invalidState, tuplePosIfBroken](
          auto&&, auto&& specType) {
        validateSpecName(specType);
        // special constraints that only accept a singleParameter in
        // addConstraint()
        if constexpr (rebalancer::interface::isSingleParameterConstraint<
                          decltype(specType)>) {
          solver_->addConstraint(std::move(specType));
        } else {
          solver_->addConstraint(
              std::move(specType),
              policy,
              invalidCost,
              invalidState,
              tuplePosIfBroken);
        }
      },
      [] {});
}

void ProblemSolverBinding::addContainerDimension(
    const std::string& dimensionName,
    const std::unordered_map<std::string, double>& containerToValue,
    double defaultValue) {
  solver_->addContainerDimension(dimensionName, containerToValue, defaultValue);
}

void ProblemSolverBinding::addGoal(
    interface::GoalSpecs spec,
    float weight,
    std::optional<int32_t> tuplePosOpt) {
  algopt::utils::throwIfUnionIsEmpty(spec);

  apache::thrift::op::visit_union_with_tag(
      spec,
      [this, weight, tuplePosOpt](auto&&, auto&& specType) {
        validateSpecName(specType);
        solver_->addGoal(std::move(specType), weight, tuplePosOpt);
      },
      [] {});
}

void ProblemSolverBinding::addGoalBoundary() {
  solver_->addGoalBoundary();
}

void ProblemSolverBinding::addObjectDimension(
    const std::string& dimensionName,
    std::unordered_map<std::string, double> objectToValue,
    double defaultValue) {
  solver_->addObjectDimension(
      dimensionName, std::move(objectToValue), defaultValue);
}

void ProblemSolverBinding::addObjectDimensionByPartition(
    const std::string& dimensionName,
    const std::string& partitionName,
    std::unordered_map<std::string, double> groupToValue,
    double defaultValue) {
  solver_->addObjectDimension(
      dimensionName, partitionName, std::move(groupToValue), defaultValue);
}

void ProblemSolverBinding::addObjectDimensionVector(
    const std::string& dimensionName,
    std::unordered_map<std::string, std::vector<double>> objectToValues,
    double defaultValue) {
  solver_->addObjectDimension(
      dimensionName, std::move(objectToValues), defaultValue);
}

void ProblemSolverBinding::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scope,
    folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
        scopeItemToObjectToValue,
    double defaultValue) {
  solver_->addDynamicObjectDimension(
      dimensionName, scope, std::move(scopeItemToObjectToValue), defaultValue);
}

void ProblemSolverBinding::addDynamicObjectDimensionByPartition(
    const std::string& dimensionName,
    const std::string& scope,
    const std::string& partitionName,
    folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
        scopeItemToGroupToValue,
    double defaultValue) {
  solver_->addDynamicObjectDimension(
      dimensionName,
      scope,
      partitionName,
      std::move(scopeItemToGroupToValue),
      defaultValue);
}

void ProblemSolverBinding::addObjectPartitionRoutingDimension(
    const std::string& dimensionName,
    const std::string& partitionName,
    const std::string& routingConfigName,
    const std::unordered_map<std::string, double>& groupToValue,
    double defaultValue) {
  solver_->addObjectPartitionRoutingDimension(
      dimensionName,
      partitionName,
      routingConfigName,
      groupToValue,
      defaultValue);
}

void ProblemSolverBinding::addObjectPartitionRoutingDimensionWithStaticValues(
    const std::string& dimensionName,
    const std::string& partitionName,
    const std::string& routingConfigName,
    const std::unordered_map<std::string, double>& groupToValue,
    const std::unordered_map<std::string, double>& groupToStaticValue,
    double defaultValue,
    double defaultStaticValue) {
  solver_->addObjectPartitionRoutingDimension(
      dimensionName,
      partitionName,
      routingConfigName,
      groupToValue,
      groupToStaticValue,
      defaultValue,
      defaultStaticValue);
}

void ProblemSolverBinding::addPartition(
    const std::string& partitionName,
    std::unordered_map<std::string, std::vector<std::string>> groupToObjects) {
  solver_->addPartition(partitionName, std::move(groupToObjects));
}

void ProblemSolverBinding::addScope(
    const std::string& scopeName,
    const std::unordered_map<std::string, std::string>& containerToScopeItem) {
  solver_->addScope(scopeName, containerToScopeItem);
}

void ProblemSolverBinding::addScopeDimension(
    const std::string& dimensionName,
    const std::string& scopeName,
    const std::unordered_map<std::string, double>& scopeItemToValue) {
  solver_->addScopeDimension(dimensionName, scopeName, scopeItemToValue);
}

void ProblemSolverBinding::addSimilarContainers(
    const std::vector<std::vector<std::string>>& similarContainerClasses) {
  solver_->addSimilarContainers(similarContainerClasses);
}

void ProblemSolverBinding::addRoutingConfig(
    const std::string& configName,
    const std::string& scopeName,
    const std::string& partitionName,
    const std::unordered_map<std::string, interface::GroupRoutingRings>&
        groupToRoutingRings,
    const std::unordered_map<
        std::string,
        std::unordered_map<std::string, double>>& originToDestinationLatency,
    const std::optional<
        std::unordered_map<std::string, std::vector<std::vector<std::string>>>>&
        defaultOriginToDestinationScopeItemSets) {
  solver_->addRoutingConfig(
      configName,
      scopeName,
      partitionName,
      groupToRoutingRings,
      originToDestinationLatency,
      defaultOriginToDestinationScopeItemSets);
}

void ProblemSolverBinding::addSolver(interface::SolverSpecs spec) {
  algopt::utils::throwIfUnionIsEmpty(spec);
  apache::thrift::op::visit_union_with_tag(
      spec,
      [this](auto&&, auto&& specType) {
        solver_->addSolver(std::move(specType));
      },
      [] {});
}

// Solver Options

void ProblemSolverBinding::disableSolutionSummary() {
  solver_->disableSolutionSummary();
}

void ProblemSolverBinding::enableMoveStats(
    const interface::MoveStatsSpec& spec) {
  solver_->enableMoveStats(spec);
}

void ProblemSolverBinding::enableProfiler() {
  solver_->enableProfiler();
}

void ProblemSolverBinding::enableRestrictMovingObjectOnlyOnce() {
  solver_->enableRestrictMovingObjectOnlyOnce();
}

void ProblemSolverBinding::disableScubaLogging() {
  solver_->disableLogging();
}

void ProblemSolverBinding::enableStableAsMuchAsPossible() {
  solver_->enableStableAsMuchAsPossible();
}

// Reading properties of solver

int32_t ProblemSolverBinding::getCurrentGoalIndex() const {
  return solver_->getCurrentGoalIndex();
}

std::string ProblemSolverBinding::getRunId() const {
  return solver_->getRunId();
}

void ProblemSolverBinding::printProblemSetup() const {
  solver_->printProblemSetup();
}

std::string ProblemSolverBinding::getProblemSummary() const {
  return solver_->getProblemSummary();
}

// Admin

void ProblemSolverBinding::persistToManifold(
    std::optional<std::shared_ptr<interface::AsyncManifoldUploadHandle>>
        manifoldUploadHandle) {
  if (manifoldUploadHandle.has_value()) {
    solver_->persistToManifold(std::move(manifoldUploadHandle.value()));
  } else {
    solver_->persistToManifold();
  }
}

void ProblemSolverBinding::saveBundle(const std::string& path) {
  solver_->saveBundle(path);
}

// More problem building

void ProblemSolverBinding::setAssignment(
    const std::unordered_map<std::string, std::vector<std::string>>&
        containerToObjects) {
  solver_->setAssignment(containerToObjects);
}

void ProblemSolverBinding::setConstraintPolicy(
    const interface::ConstraintPolicy& policy) {
  solver_->setConstraintPolicy(policy);
}

void ProblemSolverBinding::setDefaultConstraintParams(
    const interface::ConstraintParams& params) {
  solver_->setDefaultConstraintParams(params);
}

void ProblemSolverBinding::setContainerName(const std::string& containerName) {
  solver_->setContainerName(containerName);
}

void ProblemSolverBinding::setFeasibilityTolerance(
    double feasibilityTolerance) {
  solver_->setFeasibilityTolerance(feasibilityTolerance);
}

void ProblemSolverBinding::setLogLevel(const std::string& level) {
  solver_->setLogLevel(level);
}

void ProblemSolverBinding::setManifoldBackupParams(
    const interface::ManifoldBackupParams& params,
    std::optional<std::shared_ptr<interface::AsyncManifoldUploadHandle>>
        manifoldUploadHandle) {
  if (manifoldUploadHandle.has_value()) {
    solver_->setManifoldBackupParams(
        params, std::move(manifoldUploadHandle.value()));
  } else {
    solver_->setManifoldBackupParams(params);
  }
}

void ProblemSolverBinding::setLoggingLabel(const std::string& loggingLabel) {
  solver_->setLoggingLabel(loggingLabel);
}

void ProblemSolverBinding::setObjectName(const std::string& objectName) {
  solver_->setObjectName(objectName);
}

void ProblemSolverBinding::setRunId(std::string runId) {
  solver_->setRunId(std::move(runId));
}

interface::AssignmentSolution ProblemSolverBinding::solve() {
  return solver_->solve();
}

void ProblemSolverBinding::useParallelizedNewMaterializer() {
  solver_->useParallelizedNewMaterializer();
}

void ProblemSolverBinding::enableParallelBoundsComputation(bool enable) {
  solver_->enableParallelBoundsComputation(enable);
}

void ProblemSolverBinding::publishMetrics() {
  solver_->publishMetrics();
}

void ProblemSolverBinding::shouldUseDynamicObjectOrdering(
    bool useDynamicObjectOrdering) {
  solver_->shouldUseDynamicObjectOrdering(useDynamicObjectOrdering);
}

void ProblemSolverBinding::validateAppliedMoves(bool enable) {
  solver_->validateAppliedMoves(enable);
}

void ProblemSolverBinding::setPrecision(
    const algopt::common::thrift::PrecisionTolerances& precisionTolerances) {
  solver_->setPrecision(precisionTolerances);
}

} // namespace facebook::rebalancer::binding
