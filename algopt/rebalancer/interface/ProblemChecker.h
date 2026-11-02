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

#include "algopt/rebalancer/algopt_common/alias.h"
#include "algopt/rebalancer/algopt_common/Concepts.h"
#include "algopt/rebalancer/common/ValueRequirement.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>

#include <string>
#include <unordered_map>

namespace facebook::rebalancer::interface {

class ProblemChecker {
 public:
  void setObjectName(const std::string& objectName);
  void setContainerName(const std::string& containerName);
  static void checkIfReservedPartitionName(const std::string& partitionName);

  void checkObjectIntegrity();

  void addObject(const std::string& object);
  void addContainer(const std::string& container);
  void resetAssignment();
  void addScope(const std::string& scope);
  void addScopeItem(const std::string& scope, const std::string& scopeItem);
  void addScopeContainer(
      const std::string& scope,
      const std::string& container);

  template <class T>
  void addObjectDimension(
      const std::string& dimensionName,
      const T& objectToValue);
  template <class T>
  void addContainerDimension(
      const std::string& dimensionName,
      const T& containerToValue);
  template <class T>
  void addScopeDimension(
      const std::string& dimensionName,
      const std::string& scopeName,
      const T& scopeItemToValue);

  template <typename ScopeItemToObjectToValue>
  void addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      const ScopeItemToObjectToValue& scopeItemToObjectToValue)
    requires IsMapOfMap<
        ScopeItemToObjectToValue,
        std::string,
        std::string,
        double>;

  template <typename ScopeItemToGroupToValue>
  void addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      const std::string& partitionName,
      const ScopeItemToGroupToValue& scopeItemToGroupToValue)
    requires IsMapOfMap<
        ScopeItemToGroupToValue,
        std::string,
        std::string,
        double>;

  template <typename GroupToValue>
  void addObjectDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const GroupToValue& groupToValue)
    requires IsIterableOverPairs<GroupToValue, std::string, double>;

  template <typename ObjectToGroup>
  void addPartition(
      const std::string& partitionName,
      const ObjectToGroup& objectToGroup)
    requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>;

  template <typename GroupToObjects>
  void addPartition(
      const std::string& partitionName,
      const GroupToObjects& groupToObjects)
    requires IsIterableOverPairs<
        GroupToObjects,
        std::string,
        std::vector<std::string>>;

  void addObjectPartitionRoutingDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      const std::unordered_map<std::string, double>& groupToStaticValue = {});

  void checkContainerExists(const std::string& container) const;
  void checkScopeExists(const std::string& scope) const;
  void checkScopeItemExists(
      const std::string& scope,
      const std::string& scopeItem) const;
  void checkDescendingHotnessContainers(
      const std::vector<std::string>& containers);
  static void checkConstraintParams(const ConstraintParams& constraintParams);
  static void checkTuplePos(std::optional<int> pos);
  void checkRoutingConfigExists(const std::string& routingConfigName) const;
  void checkDestinationsToExploreOptionExists(const std::string& name) const;
  void checkCustomEquivalenceSetsConfig(
      const CustomEquivalenceSetConfig& config) const;

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
          defaultOriginToDestinationScopeItemSets);
  void checkDestinationScopeItemSets(
      const std::vector<std::vector<std::string>>& destinationScopeItemSets,
      const std::string& scopeNames) const;

  void addDestinationsToExploreOptions(
      const std::string& name,
      const interface::DestinationsToExploreOptions&
          destinationsToExploreOptions);

  void addSpec(const GroupCountSpec& spec);
  void addSpec(const AggregatedGroupSpec& spec);
  void addSpec(const BalanceSpec& spec);
  void addSpec(const SumOfMaxSpec& spec);
  void addSpec(const SRBufferCapacitySpec& spec);
  void addSpec(const CapacitySpec& spec);
  void addSpec(const CapacityRatioSpec& spec);
  void addSpec(const FlowSpec& spec);
  void addSpec(const DrainCapacitySpec& spec);
  void addSpec(const MinimizeContainersSpec& spec);
  void addSpec(const MinimizeMovementSpec& spec);
  void addSpec(const ThrottlingSpec& spec);
  void addSpec(const AvoidMovingSpec& spec);
  void addSpec(const ToFreeSpec& spec, bool isConstraint);
  void addSpec(const MovesInProgressSpec& spec);
  void addSpec(const MoveGroupSpec& spec);
  void addSpec(const GroupMoveLimitSpec& spec);
  void addSpec(const ExclusiveScopeItemsSpec& spec);
  void addSpec(const GroupIsolationLimitSpec& spec);
  void addSpec(const MaximizeAllocationSpec& spec);
  void addSpec(const MaximizeFreeCapacityUnitsSpec& spec);
  void addSpec(const WorkingSetSpec& spec);
  void addSpec(const AssignmentAffinitiesSpec& spec);
  void addSpec(const GroupAssignmentAffinitiesSpec& spec);
  void addSpec(const PairAffinitiesSpec& spec);
  void addSpec(const ExclusiveObjectsSpec& spec);
  void addSpec(const ObjectAffinitiesSpec& spec);
  void addSpec(const ScopeAffinitiesSpec& spec);
  void addSpec(const MinimizeSquaresSpec& spec);
  void addSpec(const ColocateGroupsSpec& spec);
  void addSpec(const UtilIncreaseCostSpec& spec);
  void addSpec(const ExclusiveSwapsSpec& spec);
  void addSpec(const BipartiteSwapsSpec& spec);
  void addSpec(const CapacityWithSupplyAndDrSpec& spec);
  void addSpec(const DisasterRecoveryCapacitySpec& spec);
  void addSpec(const MultipleOrCapacitySpec& spec);
  void addSpec(const LogicalOrSpec& spec);
  void addSpec(const LogicalAndSpec& spec);
  void addSpec(const GroupCapacitySpec& spec);
  void addSpec(const RasRebalancingMovementSpec& spec);
  void addSpec(const MinimizeNthLargestUtilizationSpec& spec);
  void addSpec(const AvoidAssignmentsSpec& spec);
  void addSpec(const ExclusiveGroupsSpec& spec);
  void addSpec(const NestedScopeLimitSpec& spec);
  void addSpec(const NonAcceptingSpec& spec);
  void addSpec(const ItemsAffinitySpec& spec);
  void addSpec(const LargeShardSpec& spec);
  void addSpec(const RoutingLatencySpec& spec);
  void addSpec(const GroupDiversitySpec& spec);
  void addSpec(const CapacityWithGroupPresenceSpec& spec);
  void addSpec(const DiversifyWithinScopeItemSpec& spec);

  // Records a constraint spec's name so checkConstraintNameExists can reject
  // goal names.
  template <typename Spec>
    requires FieldTypeExistsInThriftStructOrUnion<
        interface::ConstraintSpecs,
        Spec>
  void recordConstraintName(const Spec& spec) {
    if constexpr (requires { *spec.name(); }) {
      addConstraintName(*spec.name());
    }
  }

  void checkLogicalOrSpec(const LogicalOrSpec& spec);
  void checkLogicalAndSpec(const LogicalAndSpec& spec);
  void checkCapacitySpec(const CapacitySpec& spec);
  void checkGroupCountSpec(const GroupCountSpec& spec);
  void checkGroupCapacitySpec(const GroupCapacitySpec& spec);
  void checkGenericSpec(const GenericSpec& spec);

  static void checkSolverSpec(const interface::OptimalSolverSpec& solverSpec);
  void checkSolverSpec(const LocalSearchSolverSpec& spec) const;
  void checkSolverSpec(const LocalSearchStageSolverSpec& spec) const;
  static void checkExecutionConfig(const ParallelExecutionConfig& config);
  static void checkBatchingConfig(const BatchingExecutionConfig& config);
  static void checkMultiStageConfig(const MultiStageConfig& multiStageConfig);

  static void checkAbsoluteEpsilon(const double absoluteEpsilon);
  static void checkRelativeEpsilon(const double relativeEpsilon);
  static void checkPrecision(
      const algopt::common::thrift::PrecisionTolerances& precisionTolerances);

  void enableMoveStats(const MoveStatsSpec& spec) const;

 private:
  enum EntityType {
    OBJECT,
    SCOPE,
    PARTITION,
    DIMENSION,
  };

  enum ObjectBundleExpectation { DISALLOWED, REQUIRED };

  void checkObjectExists(const std::string& object) const;
  void checkObjectNameIsSet() const;
  void checkContainerNameIsSet() const;
  void checkPartitionExists(const std::string& partition) const;
  void checkGroupExists(const std::string& partition, const std::string& group)
      const;
  static void checkLimitType(
      const LimitType& type,
      const LimitType& expectedType);
  static void checkValue(
      double value,
      std::string_view attribute,
      ValueRequirement requirement);
  static void checkNonNegativeValue(double value, std::string_view attribute);
  static void checkLimitValues(
      const Limit& limit,
      ValueRequirement requirement);
  void checkLimitForScopeItems(
      const std::string& scope,
      const Limit& limit,
      ValueRequirement requirement = ValueRequirement::NONE) const;
  void checkLimitForGroups(
      const std::string& scope,
      const std::string& partition,
      const Limit& limit,
      std::optional<interface::LimitType> expectedType = std::nullopt,
      ValueRequirement requirement = ValueRequirement::NONE) const;
  void checkLimitForGroups(
      const std::string& partition,
      const Limit& limit,
      std::optional<LimitType> type = std::nullopt) const;
  static void checkScopeItemNoOverlap(
      std::vector<std::string> items1,
      std::vector<std::string> items2);

  void checkScopeItemFilterSpec(const Filter& spec, const std::string& scope)
      const;
  void checkGroupFilterSpec(const Filter& spec, const std::string& scope) const;

  // checks and throws if the given value is not positive integer
  static void checkIfPositiveInteger(
      const double value,
      const std::string& attributeName);

  // Check the given dimension is defined on any entity: because often users
  // rely on object or scope item default dimension values, a dimension may be
  // used in a spec without it being defined on both the object and the scope
  // item at the same time. However, a dimension not being defined on any
  // entities often indicates a bug.
  void checkDimensionExists(const std::string& dimensionName) const;
  void checkDimensionExistsOrEmpty(const std::string& dimensionName) const;

  // check if the given dimension is defined on the given entity type, where
  // entityType is objectsName, containerName, scopeName, etc.
  void checkIfDimensionExists(
      const std::string& dimensionName,
      const std::string& entityType) const;

  void addSpecName(const std::string& name);
  void addSpecName(const folly::Optional<std::string>& name);
  void addConstraintName(const std::string& name);
  void addGlobalName(const std::string& name, EntityType type);
  void addPartition(const std::string& partition);
  void addGroup(const std::string& partition, const std::string& group);
  // Registers an object dimension whose values are keyed by partition groups.
  void addObjectDimensionName(const std::string& dimensionName);

  template <class T>
  void addDimension(
      const std::string& entityType,
      const std::string& dimensionName,
      const algopt::SetImpl<std::string>& entityNames,
      const T& entityToValue);

  std::string getObjectCountDimensionName() const;

  static std::string toString(EntityType type);

  static void checkBalanceLegacy(const BalanceSpec& spec);
  static void checkBalanceIgnoreUpperBound(const BalanceSpec& spec);

  void checkMoveTypeSpecs(const interface::LocalSearchSolverSpec& spec) const;
  void checkMoveTypeSpec(
      const SingleRandomObjectStratifiedMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(
      const GroupMoveWithHintStrategiesMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(
      const interface::SingleRandomStratifiedMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(const interface::SwapMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(const interface::FixedDestMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(
      const interface::SingleFixedSourceMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(
      const interface::ColocateGroupsMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(const interface::SingleFastMoveTypeSpec& spec) const;
  void checkMoveTypeSpec(
      const interface::GreedyGroupToScopeItemMoveTypeSpec& spec) const;

  void checkDestinationsToExploreOptions(
      const interface::DestinationsToExploreOptions& spec) const;
  void checkExploreOption(
      const interface::MoveToCurrentScopeItemSpec& moveToCurrentScopeItem)
      const;
  void checkExploreOption(
      const interface::MoveToScopeItemsSpec& moveToScopeItems) const;
  void check(const interface::ScopeItemList& scopeItemList) const;
  void check(
      const interface::SampleSize& sampleSize,
      bool isObjectToSampleSizeSupported = true) const;
  void checkMoveStrategies(
      const std::string& secondaryPartition,
      const interface::MoveStrategies& moveStrategies) const;
  void checkSecondaryGroupReplacementConfig(
      const std::string& secondaryPartition,
      const interface::SecondaryGroupReplacementConfig&
          secondaryGroupReplacementConfig) const;
  void checkObjectsToExploreOptions(
      const interface::ObjectsToExploreOptions& objectsToExploreOptions,
      ObjectBundleExpectation objectBundleExpectation) const;
  void checkObjectsFromGroupsSpec(
      const interface::ObjectsFromGroupsSpec& spec,
      ObjectBundleExpectation objectBundleExpectation) const;
  void check(const interface::GroupList& groupList) const;
  static void check(
      const algopt::common::thrift::AllowedWorsening& allowedWorsening);
  static void check(const algopt::common::thrift::Threshold& threshold);
  static void check(
      const interface::MultiObjectiveSolveSettings& multiObjSettings);
  void check(
      const interface::GroupUtilizationBound& groupUtilizationBound,
      const std::string& scope) const;
  void check(
      const interface::GroupBundleUtilization& bundleUtilization,
      const interface::CapacitySpec& spec) const;
  static void check(
      const algopt::common::thrift::PerObjectiveValue& perObjectiveValue);

  void checkSpecNamesExists(const std::string& specName) const;
  void checkConstraintNameExists(const std::string& constraintName) const;

  // captures what exactly an object represents, eg: 'shard', 'task'
  // not to be confused with name of a specific object
  std::string objectName;
  // captures what exactly a container represents, eg: 'servers', 'hosts'
  // not to be confused with name of a specific container
  std::string containerName;
  algopt::SetImpl<std::string> objects;
  algopt::MapImpl<std::string, algopt::SetImpl<std::string>> scopes;
  algopt::MapImpl<std::string, algopt::SetImpl<std::string>> scopeContainers;
  algopt::MapImpl<std::string, algopt::SetImpl<std::string>> partitions;
  algopt::SetImpl<std::string> specNames;
  algopt::SetImpl<std::string> constraintNames_;
  algopt::MapImpl<std::string, algopt::SetImpl<std::string>>
      dimensionToEntityTypes;
  algopt::MapImpl<std::string, std::string> dynamicObjectDimensionToScope_;
  algopt::MapImpl<std::string, EntityType> globalNameToType;
  algopt::SetImpl<std::string> routingConfigNames_;
  algopt::SetImpl<std::string> destinationsToExploreOptionNames_;
};

// implementations
template <typename ScopeItemToObjectToValue>
void ProblemChecker::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scope,
    const ScopeItemToObjectToValue& scopeItemToObjectToValue)
  requires IsMapOfMap<
      ScopeItemToObjectToValue,
      std::string,
      std::string,
      double>
{
  checkScopeExists(scope);
  for (auto& [scopeItemName, objectToValue] : scopeItemToObjectToValue) {
    checkScopeItemExists(scope, scopeItemName);
    for (auto& [object, value] : objectToValue) {
      checkObjectExists(object);
    }
  }
  addObjectDimension(dimensionName, ScopeItemToObjectToValue());
  dynamicObjectDimensionToScope_.emplace(dimensionName, scope);
}

template <typename ScopeItemToGroupToValue>
void ProblemChecker::addDynamicObjectDimension(
    const std::string& dimensionName,
    const std::string& scope,
    const std::string& partitionName,
    const ScopeItemToGroupToValue& scopeItemToGroupToValue)
  requires IsMapOfMap<ScopeItemToGroupToValue, std::string, std::string, double>
{
  checkScopeExists(scope);
  checkPartitionExists(partitionName);
  for (auto& [scopeItemName, groupToValue] : scopeItemToGroupToValue) {
    checkScopeItemExists(scope, scopeItemName);
    for (auto& [group, value] : groupToValue) {
      checkGroupExists(partitionName, group);
    }
  }
  addObjectDimension(dimensionName, ScopeItemToGroupToValue());
  dynamicObjectDimensionToScope_.emplace(dimensionName, scope);
}

template <typename GroupToValue>
void ProblemChecker::addObjectDimension(
    const std::string& dimensionName,
    const std::string& partitionName,
    const GroupToValue& groupToValue)
  requires IsIterableOverPairs<GroupToValue, std::string, double>
{
  checkPartitionExists(partitionName);
  for (const auto& [group, value] : groupToValue) {
    checkGroupExists(partitionName, group);
  }
  checkObjectNameIsSet();
  addObjectDimensionName(dimensionName);
}

template <typename ObjectToGroup>
void ProblemChecker::addPartition(
    const std::string& partitionName,
    const ObjectToGroup& objectToGroup)
  requires IsIterableOverPairs<ObjectToGroup, std::string, std::string>
{
  addPartition(partitionName);
  for (auto& objectGroup : objectToGroup) {
    checkObjectExists(objectGroup.first);
    addGroup(partitionName, objectGroup.second);
  }
}

template <typename GroupToObjects>
void ProblemChecker::addPartition(
    const std::string& partitionName,
    const GroupToObjects& groupToObjects)
  requires IsIterableOverPairs<
      GroupToObjects,
      std::string,
      std::vector<std::string>>
{
  addPartition(partitionName);
  for (auto& [group, objectsInThisGroup] : groupToObjects) {
    addGroup(partitionName, group);
    for (auto& object : objectsInThisGroup) {
      checkObjectExists(object);
    }
  }
}

} // namespace facebook::rebalancer::interface

#include "algopt/rebalancer/interface/ProblemChecker-inl.h"
