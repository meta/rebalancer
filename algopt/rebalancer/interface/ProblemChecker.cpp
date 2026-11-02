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

#include "algopt/rebalancer/interface/ProblemChecker.h"

#include "algopt/rebalancer/algopt_common/Precision.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/interface/Constants.h"

#include <fmt/core.h>
#include <folly/container/MapUtil.h>
#include <folly/Portability.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <cmath>
#include <stdexcept>

namespace facebook::rebalancer::interface {

namespace {

FOLLY_PUSH_WARNING
FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
bool hasNonDefaultDeprecatedUpperBound(const BalanceSpec& spec) {
  return apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
             spec.upperBound()) && // NOLINT(facebook-hte-Deprecated)
      *spec.upperBound() != 1; // NOLINT(facebook-hte-Deprecated)
}
FOLLY_POP_WARNING

} // namespace

void ProblemChecker::setObjectName(const std::string& name) {
  if (!this->objectName.empty()) {
    throw std::runtime_error(
        fmt::format("object name already set to {}", this->objectName));
  }
  addGlobalName(name, EntityType::OBJECT);
  this->objectName = name;
}

void ProblemChecker::setContainerName(const std::string& name) {
  if (!this->containerName.empty()) {
    throw std::runtime_error(
        fmt::format("container name already set to {}", this->containerName));
  }
  addScope(name);
  this->containerName = name;
}

void ProblemChecker::checkIfDimensionExists(
    const std::string& dimensionName,
    const std::string& entityType) const {
  auto entityTypesPtr = folly::get_ptr(dimensionToEntityTypes, dimensionName);
  if (entityTypesPtr && entityTypesPtr->contains(entityType)) {
    throw std::runtime_error(
        fmt::format(
            "dimension {} already defined on {}", dimensionName, entityType));
  }
}

void ProblemChecker::checkTuplePos(std::optional<int> pos) {
  if (pos.has_value() && pos.value() < 0) {
    throw std::runtime_error(
        "'tuplePos'/'tuplePosIfBroken' should be non-negative");
  }
}

void ProblemChecker::checkRoutingConfigExists(
    const std::string& configName) const {
  if (!routingConfigNames_.contains(configName)) {
    throw std::runtime_error(
        fmt::format(
            "Routing config '{}' not found. Add it using addRoutingConfig(...)",
            configName));
  }
}

void ProblemChecker::checkDestinationsToExploreOptionExists(
    const std::string& name) const {
  if (!destinationsToExploreOptionNames_.contains(name)) {
    throw std::runtime_error(
        fmt::format(
            "Destinations to explore option '{}' not found. Add it using addDestinationsToExploreOptions(...)",
            name));
  }
}

void ProblemChecker::addObject(const std::string& object) {
  checkObjectNameIsSet();
  if (!objects.insert(object).second) {
    throw std::runtime_error(fmt::format("duplicate object {}", object));
  }
}

void ProblemChecker::addContainer(const std::string& container) {
  checkContainerNameIsSet();
  addScopeItem(containerName, container);
}

void ProblemChecker::resetAssignment() {
  objects.clear();
  if (auto it = scopes.find(containerName); it != scopes.end()) {
    it->second.clear();
  }
}

void ProblemChecker::addScope(const std::string& scope) {
  if (!scopes.insert({scope, {}}).second) {
    throw std::runtime_error(fmt::format("scope {} added twice", scope));
  }
  addGlobalName(scope, EntityType::SCOPE);
}

void ProblemChecker::addScopeItem(
    const std::string& scope,
    const std::string& scopeItem) {
  auto it = scopes.find(scope);
  if (it == scopes.end()) {
    throw std::runtime_error(fmt::format("unknown scope {}", scope));
  }
  auto& scopeItems = it->second;
  scopeItems.insert(scopeItem);
}

void ProblemChecker::addScopeContainer(
    const std::string& scope,
    const std::string& container) {
  checkContainerExists(container);
  const bool inserted = scopeContainers[scope].insert(container).second;
  if (!inserted) {
    throw std::runtime_error(
        fmt::format(
            "{} '{}' appears as part of more than one scope item in scope '{}'",
            containerName,
            container,
            scope));
  }
}

void ProblemChecker::checkIfReservedPartitionName(
    const std::string& partition) {
  // this partition name is reserved for equivalence set partition generated
  // internally by Rebalancer.
  if (partition == kEquivSetPartition) {
    throw std::runtime_error(
        fmt::format("'{}' is a reserved partition name", kEquivSetPartition));
  } else if (partition.starts_with(kInternalPartitionPrefix)) {
    throw std::runtime_error(
        fmt::format(
            "partition names are not allowed to start with prefix '{}', but got '{}'",
            kInternalPartitionPrefix,
            partition));
  }
}

void ProblemChecker::addPartition(const std::string& partition) {
  checkIfReservedPartitionName(partition);
  if (!partitions.insert({partition, {}}).second) {
    throw std::runtime_error(
        fmt::format("partition {} added twice", partition));
  }
  addGlobalName(partition, EntityType::PARTITION);
}

void ProblemChecker::addGroup(
    const std::string& partition,
    const std::string& group) {
  auto it = partitions.find(partition);
  if (it == partitions.end()) {
    throw std::runtime_error(fmt::format("unknown partition {}", partition));
  }
  auto& groups = it->second;
  groups.insert(group);
}

void ProblemChecker::addObjectPartitionRoutingDimension(
    const std::string& dimensionName,
    const std::string& partitionName,
    const std::string& routingConfigName,
    const std::unordered_map<std::string, double>& groupToValue,
    const std::unordered_map<std::string, double>& groupToStaticValue) {
  checkPartitionExists(partitionName);
  checkRoutingConfigExists(routingConfigName);

  for (auto& [group, value] : groupToValue) {
    checkGroupExists(partitionName, group);
  }

  for (auto& [group, value] : groupToStaticValue) {
    checkGroupExists(partitionName, group);
  }

  addObjectDimensionName(dimensionName);
}

void ProblemChecker::addObjectDimensionName(const std::string& dimensionName) {
  // Unlike for containers, where everything is defined as a ScopeDimension,
  // there is no explicit concept of PartitionDimension for Objects. So use
  // ObjectName as the entityType
  checkIfDimensionExists(dimensionName, objectName);
  dimensionToEntityTypes[dimensionName].insert(objectName);

  addGlobalName(dimensionName, EntityType::DIMENSION);
}

void ProblemChecker::checkObjectNameIsSet() const {
  if (objectName.empty()) {
    throw std::runtime_error("object name not set");
  }
}

void ProblemChecker::checkContainerNameIsSet() const {
  if (containerName.empty()) {
    throw std::runtime_error("container name not set");
  }
}

void ProblemChecker::checkObjectExists(const std::string& object) const {
  checkObjectNameIsSet();
  if (!objects.contains(object)) {
    throw std::runtime_error(fmt::format("unknown {} {}", objectName, object));
  }
}

void ProblemChecker::checkContainerExists(const std::string& container) const {
  checkContainerNameIsSet();
  checkScopeItemExists(containerName, container);
}

void ProblemChecker::checkScopeExists(const std::string& scope) const {
  if (!scopes.contains(scope)) {
    throw std::runtime_error(fmt::format("unknown scope {}", scope));
  }
}

void ProblemChecker::checkScopeItemExists(
    const std::string& scope,
    const std::string& scopeItem) const {
  auto it = scopes.find(scope);
  if (it == scopes.end()) {
    throw std::runtime_error(fmt::format("unknown scope {}", scope));
  }
  auto& scopeItems = it->second;
  if (!scopeItems.contains(scopeItem)) {
    throw std::runtime_error(
        fmt::format("unknown item {} in scope {}", scopeItem, scope));
  }
}

void ProblemChecker::checkDimensionExists(
    const std::string& dimensionName) const {
  // Note: there is an implicitly defined dimension that represents object
  // counts.
  if (!dimensionToEntityTypes.contains(dimensionName) &&
      dimensionName != getObjectCountDimensionName()) {
    throw std::runtime_error(
        fmt::format("unknown dimension {}", dimensionName));
  }
}

void ProblemChecker::checkDimensionExistsOrEmpty(
    const std::string& dimensionName) const {
  if (!dimensionName.empty()) {
    checkDimensionExists(dimensionName);
  }
}

void ProblemChecker::checkPartitionExists(const std::string& partition) const {
  if (!partitions.contains(partition)) {
    throw std::runtime_error(fmt::format("unknown partition {}", partition));
  }
}

void ProblemChecker::checkGroupExists(
    const std::string& partition,
    const std::string& group) const {
  auto it = partitions.find(partition);
  if (it == partitions.end()) {
    throw std::runtime_error(fmt::format("unknown partition {}", partition));
  }
  auto& groups = it->second;
  if (!groups.contains(group)) {
    throw std::runtime_error(
        fmt::format("unknown group {} in partition {}", group, partition));
  }
}

void ProblemChecker::checkConstraintParams(
    const ConstraintParams& constraintParams) {
  if (*constraintParams.tuplePosIfBroken() < 0) {
    throw std::runtime_error("'tuplePosIfBroken' should be non-negative");
  }
}

void ProblemChecker::addSpec(const BalanceSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  if (hasNonDefaultDeprecatedUpperBound(spec) &&
      spec.upperBounds().has_value()) {
    throw std::runtime_error(
        "BalanceSpec cannot set both deprecated upperBound and upperBounds");
  }
  if (spec.upperBounds().has_value()) {
    checkLimitType(*spec.upperBounds()->type(), LimitType::ABSOLUTE);
    checkLimitForScopeItems(*spec.scope(), *spec.upperBounds());
    if (*spec.upperBounds()->isDefaultLimitUnbounded()) {
      throw std::runtime_error(
          "BalanceSpec upperBounds cannot set isDefaultLimitUnbounded to true");
    }
    if (*spec.formula() != BalanceSpecFormula::RELATIVE_UTIL_VARIANCE &&
        !spec.upperBounds()->scopeItemLimits()->empty()) {
      throw std::runtime_error(
          fmt::format(
              "BalanceSpec upperBounds scopeItemLimits are only supported with RELATIVE_UTIL_VARIANCE formula, got {}",
              apache::thrift::util::enumNameSafe(*spec.formula())));
    }
  }
  checkBalanceLegacy(spec);
  checkBalanceIgnoreUpperBound(spec);

  if (*spec.formula() == BalanceSpecFormula::IDEAL &&
      *spec.balanceMetric() == BalanceSpecMetric::CAPACITY_PER_ITEM) {
    throw std::runtime_error(
        "IDEAL formula is not supported with CAPACITY_PER_ITEM metric");
  }
  if (spec.capacityPerItemCountDimension().has_value()) {
    if (*spec.balanceMetric() != BalanceSpecMetric::CAPACITY_PER_ITEM) {
      throw std::runtime_error(
          fmt::format(
              "capacityPerItemCountDimension is only supported with CAPACITY_PER_ITEM metric, got {}",
              apache::thrift::util::enumNameSafe(*spec.balanceMetric())));
    }
    checkDimensionExists(*spec.capacityPerItemCountDimension());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const SumOfMaxSpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partitionName());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const SRBufferCapacitySpec& spec) {
  // SRBufferCapacitySpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  checkPartitionExists(*spec.partitionName());
  checkScopeExists(*spec.scope());
  for (const auto& pair : *spec.scopeItemPairs()) {
    auto& mainScopeItem = *pair.scopeItem1();
    auto& bufferScopeItem = *pair.scopeItem2();
    checkScopeItemExists(*spec.scope(), mainScopeItem);
    checkScopeItemExists(*spec.scope(), bufferScopeItem);
  }
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
  if (*spec.useHeuristics() && *spec.addUpperBound()) {
    throw std::runtime_error(
        "no support for upper bound AND heuristics for sr_buffer_constraint");
  }
}

void ProblemChecker::addSpec(const CapacitySpec& spec) {
  checkCapacitySpec(spec);
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const CapacityRatioSpec& spec) {
  checkScopeExists(*spec.scope());
  // CapacityRatioSpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  for (auto& [itemName1, ratios] : *spec.ratios()) {
    for (auto& [itemName2, _] : ratios) {
      checkScopeItemExists(*spec.scope(), itemName1);
      checkScopeItemExists(*spec.scope(), itemName2);
    }
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const FlowSpec& spec) {
  checkScopeExists(*spec.scope());
  // FlowSpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  checkScopeItemFilterSpec(*spec.sourceFilter(), *spec.scope());
  for (auto& [itemName, filter] : *spec.destinationFilter()) {
    checkScopeItemExists(*spec.scope(), itemName);
    checkScopeItemFilterSpec(filter, *spec.scope());
  }
  for (auto& pair : *spec.pairs()) {
    checkObjectExists(*pair.object1());
    checkObjectExists(*pair.object2());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const DrainCapacitySpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  for (auto& [srcItemName, distribution] : *spec.spillDistribution()) {
    checkScopeItemExists(*spec.scope(), srcItemName);
    for (auto& [dstItemName, _] : distribution) {
      checkScopeItemExists(*spec.scope(), dstItemName);
    }
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MultipleOrCapacitySpec& spec) {
  for (const auto& capacitySpec : *spec.capacitySpecs()) {
    checkScopeExists(*capacitySpec.scope());
    checkDimensionExistsOrEmpty(*capacitySpec.dimension());
    checkLimitForScopeItems(*capacitySpec.scope(), *capacitySpec.limit());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::checkLogicalOrSpec(const LogicalOrSpec& spec) {
  for (const auto& genericSpec : *spec.genericSpecs()) {
    checkGenericSpec(genericSpec);
  }
}

void ProblemChecker::checkLogicalAndSpec(const LogicalAndSpec& spec) {
  for (const auto& genericSpec : *spec.genericSpecs()) {
    checkGenericSpec(genericSpec);
  }
}

void ProblemChecker::checkCapacitySpec(const CapacitySpec& spec) {
  checkScopeExists(*spec.scope());
  // CapacitySpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  checkLimitForScopeItems(*spec.scope(), *spec.limit());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  if (spec.utilizationBound() && spec.groupBundleUtilization()) {
    throw std::runtime_error(
        "CapacitySpec.utilizationBound and groupBundleUtilization cannot both be set");
  }
  if (auto utilBound = spec.utilizationBound()) {
    check(utilBound->groupUtilizationBound().value(), *spec.scope());
  }
  if (auto bundleUtilization = spec.groupBundleUtilization()) {
    check(*bundleUtilization, spec);
  }
  if (*spec.useLegacyFormula()) {
    XLOG(WARNING)
        << "Legacy formula may throw if one of the scope dimension is zero";
  }
}

void ProblemChecker::checkGroupCountSpec(const GroupCountSpec& spec) {
  checkScopeExists(*spec.scope());
  // GroupCountSpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  checkPartitionExists(*spec.partitionName());
  checkLimitForGroups(*spec.scope(), *spec.partitionName(), *spec.limit());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  if (*spec.limitRelativeTo() ==
          GroupCountSpecLimitRelativeTo::SCOPE_ITEM_UTIL &&
      *spec.bound() == GroupCountSpecBound::MIN && *spec.zeroAllowed()) {
    throw std::runtime_error(
        "limitRelativeTo=SCOPE_ITEM_UTIL is not compatible with bound=MIN and "
        "zeroAllowed=true");
  }

  if (*spec.limitRelativeTo() ==
      GroupCountSpecLimitRelativeTo::GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC) {
    if (!spec.routingConfigForLimit().has_value()) {
      throw std::runtime_error(
          "routingConfigForLimit parameter should be set when using GroupCountSpecLimitRelativeTo::GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC");
    }
    checkRoutingConfigExists(*spec.routingConfigForLimit());
  }

  // TODO: check zeroAllowed is enabled only when bound is MIN or EXACT
  if (*spec.minimumLimit() != 0 && !*spec.zeroAllowed()) {
    throw std::runtime_error("minimumLimit requires zeroAllowed to be true");
  }
}

void ProblemChecker::checkGroupCapacitySpec(const GroupCapacitySpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partitionName());
  if (spec.contributionPartition()) {
    checkPartitionExists(*spec.contributionPartition());
    checkLimitForGroups(
        *spec.scope(), *spec.contributionPartition(), *spec.contribution());
  }
  checkLimitForGroups(*spec.scope(), *spec.partitionName(), *spec.limit());
  if (auto bundleConfig = spec.bundleConfig()) {
    if (bundleConfig->type() != LimitType::ABSOLUTE) {
      throw std::runtime_error("only absolute limits supported");
    }
    if (spec.utilType() != GroupCapacitySpecUtilType::STEP_MOD_K) {
      throw std::runtime_error(
          "bundleConfig is only supported for GroupCapacitySpecUtilType::STEP_MOD_K");
    }
    if (!bundleConfig->groupLimits()->empty() ||
        !bundleConfig->scopeItemToGroupLimits()->empty()) {
      throw std::runtime_error("only scopeItem limits are supported");
    }
    checkIfPositiveInteger(
        *bundleConfig->globalLimit(),
        fmt::format("{}'s bundleConfig global limit", *spec.name()));
    for (const auto& [_, limit] : *bundleConfig->scopeItemLimits()) {
      checkIfPositiveInteger(
          limit,
          fmt::format("{}'s bundleConfig scopeItem limit", *spec.name()));
    }
  }
}

void ProblemChecker::checkIfPositiveInteger(
    const double value,
    const std::string& attributeName) {
  if (algopt::Precision::isInteger(value) && value > 0) {
    return;
  }
  throw std::runtime_error(
      fmt::format(
          "value {} of {} is not a positive integer", value, attributeName));
}

void ProblemChecker::checkGenericSpec(const GenericSpec& spec) {
  switch (spec.getType()) {
    case interface::GenericSpec::Type::logicalOrSpec:
      checkLogicalOrSpec(spec.logicalOrSpec().value());
      break;
    case interface::GenericSpec::Type::logicalAndSpec:
      checkLogicalAndSpec(spec.logicalAndSpec().value());
      break;
    case interface::GenericSpec::Type::capacitySpec:
      checkCapacitySpec(spec.capacitySpec().value());
      break;
    case interface::GenericSpec::Type::groupCountSpec:
      checkGroupCountSpec(spec.groupCountSpec().value());
      break;
    case interface::GenericSpec::Type::groupCapacitySpec:
      checkGroupCapacitySpec(spec.groupCapacitySpec().value());
      break;
    case interface::GenericSpec::Type::__EMPTY__:
      throw std::runtime_error("Uninitialized generic spec");
  }
}

void ProblemChecker::addSpec(const LogicalOrSpec& spec) {
  checkLogicalOrSpec(spec);
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const LogicalAndSpec& spec) {
  checkLogicalAndSpec(spec);
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const GroupCountSpec& spec) {
  checkGroupCountSpec(spec);
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const AggregatedGroupSpec& spec) {
  checkScopeExists(*spec.scope());
  // AggregatedGroupSpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  checkPartitionExists(*spec.partitionName());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const GroupCapacitySpec& spec) {
  checkGroupCapacitySpec(spec);
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MinimizeContainersSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());

  // The deprecated maxFreeLimit field is only read by
  // BackwardCompatabilityUtils when migrating persisted instances (which never
  // reach the checker); live producers must use `target`.
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  if (spec.maxFreeLimit()) {
    throw std::runtime_error(
        "The field 'maxFreeLimit' in MinimizeContainersSpec is deprecated; use "
        "`MinimizeContainersSpec.target` instead");
  }
  FOLLY_POP_WARNING

  if (spec.target()) {
    switch (spec.target()->getType()) {
      case MinimizeContainersTarget::Type::maxFreeLimit:
        checkValue(
            spec.target()->maxFreeLimit().value(),
            fmt::format(
                "maxFreeLimit for MinimizeContainersSpec dimension '{}'",
                *spec.dimension()),
            ValueRequirement::POSITIVE);
        break;
      case MinimizeContainersTarget::Type::minUsedLimit:
        if (spec.target()->minUsedLimit().value() < 0) {
          throw std::runtime_error(
              fmt::format(
                  "Minimize containers spec on dimension {} has negative "
                  "minUsedLimit {}",
                  *spec.dimension(),
                  spec.target()->minUsedLimit().value()));
        }
        break;
      case MinimizeContainersTarget::Type::__EMPTY__:
        throw std::runtime_error(
            "Minimize containers spec target is set but empty");
    }
  }

  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MinimizeMovementSpec& spec) {
  if (!spec.scope()->empty()) {
    checkScopeExists(*spec.scope());
  }
  // MinimizeMovementSpec accepts an empty dimension, it means object count.
  checkDimensionExistsOrEmpty(*spec.dimension());
  if (*spec.allowance() < 0) {
    throw std::runtime_error(
        "MinimizeMovementSpec::allowance must be non-negative");
  }
  if (*spec.allowance() != 0 && !*spec.doNotNormalize()) {
    throw std::runtime_error(
        "Non-zero values of MinimizeMovementSpec::allowance are not supported "
        "when MinimizeMovementSpec::doNotNormalize is set to false");
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ThrottlingSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkLimitForScopeItems(*spec.scope(), *spec.limit());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const AvoidMovingSpec& spec) {
  for (auto& object : *spec.objects()) {
    checkObjectExists(object);
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ToFreeSpec& spec, bool isConstraint) {
  for (auto& container : *spec.containers()) {
    checkContainerExists(container);
  }
  if (spec.dimension().has_value()) {
    checkDimensionExists(*spec.dimension());
  }

  if (isConstraint) {
    if (spec.formula() !=
        interface::ToFreeSpecFormula::MINIMIZE_TOTAL_UTILIZATION) {
      throw std::runtime_error(
          "Choosing a formula is not supported when using ToFreeSpec as a constraint;"
          "only the default formula ToFreeSpecFormula::MINIMIZE_TOTAL_UTILIZATION is supported");
    }
  }

  addSpecName(*spec.name());
}

void ProblemChecker::checkDescendingHotnessContainers(
    const std::vector<std::string>& containers) {
  std::unordered_set<std::string> containersUnique;
  for (auto& container : containers) {
    checkContainerExists(container);
    if (!containersUnique.insert(container).second) {
      throw std::runtime_error(
          fmt::format(
              "container {} specified more than once in descending hotness "
              "override",
              container));
    }
  }

  checkContainerNameIsSet();
  if (auto scope = folly::get_ptr(scopes, containerName)) {
    if (scope->size() != containersUnique.size()) {
      throw std::runtime_error(
          fmt::format(
              "{} != {}, total containers != specified descending hotness "
              "override",
              scope->size(),
              containersUnique.size()));
    }
  } else {
    throw std::runtime_error(
        "Descending hotness containers set before containers set");
  }
}

void ProblemChecker::addSpec(const MovesInProgressSpec& spec) {
  for (auto& move : *spec.moves()) {
    checkObjectExists(*move.objName());
    checkContainerExists(*move.toContainer());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MoveGroupSpec& spec) {
  if (*spec.partitionName() == fmt::format("{}set", objectName)) {
    return;
  }
  checkPartitionExists(*spec.partitionName());
}

void ProblemChecker::addSpec(const RasRebalancingMovementSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.stayedDimension());
  checkDimensionExistsOrEmpty(*spec.incomingDimension());
  checkLimitForScopeItems(*spec.scope(), *spec.limit());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const GroupMoveLimitSpec& spec) {
  checkPartitionExists(*spec.partitionName());
  checkScopeItemFilterSpec(
      *spec.sourceScopeItemsAffectingLimitFilter(),
      containerName /*scopeName*/);
  checkScopeItemFilterSpec(
      *spec.destinationScopeItemsAffectingLimitFilter(),
      containerName /*scopeName*/);
  if (spec.dimension().has_value()) {
    checkDimensionExists(*spec.dimension());
  }

  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ExclusiveScopeItemsSpec& spec) {
  checkScopeExists(*spec.scope());
  if (!spec.pairs()->empty()) {
    throw std::runtime_error(
        "ExclusiveScopeItemsSpecBuilder no longer supports pairs for conflicts. Use conflictInfoList instead.");
  }
  for (auto& conflictInfo : *spec.conflictInfoList()) {
    checkScopeItemExists(*spec.scope(), *conflictInfo.scopeItem());
    for (auto& conflictingScopeItem : *conflictInfo.conflictingScopeItems()) {
      checkScopeItemExists(*spec.scope(), conflictingScopeItem);
    }
    for (auto& conflictingScopeItemInfo :
         *conflictInfo.conflictingScopeItemsWithOverlap()) {
      checkScopeItemExists(
          *spec.scope(), *conflictingScopeItemInfo.conflictingScopeItem());
    }
  }
  if (auto partitionName = spec.partitionName()) {
    checkPartitionExists(*partitionName);
  }
  checkDimensionExists(*spec.dimension());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const GroupIsolationLimitSpec& spec) {
  checkPartitionExists(*spec.partitionName());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MaximizeAllocationSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MaximizeFreeCapacityUnitsSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkLimitType(*spec.unitSize()->type(), LimitType::ABSOLUTE);
  checkLimitForScopeItems(
      *spec.scope(), *spec.unitSize(), ValueRequirement::POSITIVE);
  if (*spec.unitSize()->isDefaultLimitUnbounded()) {
    throw std::runtime_error(
        "MaximizeFreeCapacityUnitsSpec unit sizes must be bounded");
  }
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const WorkingSetSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  for (auto& it : *spec.workingUnits()) {
    for (auto& object : *it.endpoints()) {
      checkObjectExists(object);
    }
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const AssignmentAffinitiesSpec& spec) {
  const auto& scope = spec.scope()->empty() ? containerName : *spec.scope();
  checkScopeExists(scope);

  if (spec.dimension().has_value()) {
    if (!spec.affinities()->empty()) {
      throw std::runtime_error(
          "AssignmentAffinitiesSpec cannot specify both affinities and a dimension");
    }
    const auto& dimensionName = *spec.dimension();
    checkDimensionExists(dimensionName);
    const auto* dimensionScope =
        folly::get_ptr(dynamicObjectDimensionToScope_, dimensionName);
    if (dimensionScope && *dimensionScope != scope) {
      throw std::runtime_error(
          fmt::format(
              "AssignmentAffinitiesSpec dimension '{}' must use the same scope '{}' as the spec",
              dimensionName,
              scope));
    }
  }

  for (auto& it : *spec.affinities()) {
    checkObjectExists(*it.objectName());
    checkScopeItemExists(scope, *it.scopeItemName());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const GroupAssignmentAffinitiesSpec& spec) {
  auto& scope = *spec.scope();
  checkScopeExists(scope);

  auto& partition = *spec.partition();
  checkPartitionExists(partition);

  auto& dimension = *spec.dimension();
  checkDimensionExists(dimension);

  for (auto& affinity : *spec.affinities()) {
    checkGroupExists(partition, *affinity.group());
    checkScopeItemExists(scope, *affinity.scopeItem());
  }

  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ExclusiveObjectsSpec& spec) {
  checkScopeExists(*spec.scope());
  for (auto& it : *spec.pairs()) {
    checkObjectExists(*it.object1());
    checkObjectExists(*it.object2());
  }
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const PairAffinitiesSpec& spec) {
  const auto& scope = spec.scope()->empty() ? containerName : *spec.scope();
  checkScopeExists(scope);

  for (auto& it : *spec.affinities()) {
    checkObjectExists(*it.pair()->object1());
    checkObjectExists(*it.pair()->object2());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ObjectAffinitiesSpec& spec) {
  const auto& scope = spec.scope()->empty() ? containerName : *spec.scope();
  checkScopeExists(scope);
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());

  for (auto& it : *spec.affinities()) {
    checkObjectExists(*it.object0());
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ScopeAffinitiesSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const MinimizeSquaresSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ColocateGroupsSpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partitionName());
  checkLimitForGroups(
      *spec.partitionName(), *spec.limits(), LimitType::ABSOLUTE);
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  if (spec.dimension().has_value()) {
    checkDimensionExists(*spec.dimension());
  }
  for (const auto& [groupName, _] : *spec.groupToWeight()) {
    checkGroupExists(*spec.partitionName(), groupName);
  }

  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const UtilIncreaseCostSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ExclusiveSwapsSpec& spec) {
  if (spec.subsetObjects()) {
    for (auto& object : *spec.subsetObjects()) {
      checkObjectExists(object);
    }
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const BipartiteSwapsSpec& spec) {
  if (spec.subsetContainers()->size() == 0) {
    throw std::runtime_error(
        "subsetContainers not set in BipartiteSwapsSpec"
        " so cannot determine left and right bipartite");
  }
  for (auto& name : *spec.subsetContainers()) {
    checkContainerExists(name);
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const CapacityWithSupplyAndDrSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  checkScopeExists(*spec.prodScope());
  checkScopeItemExists(*spec.prodScope(), *spec.prodItem());
  checkPartitionExists(*spec.partitionName());
  checkPartitionExists(*spec.supplyPartition());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const DisasterRecoveryCapacitySpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  addSpecName(*spec.name());
  for (auto& [primaryObject, secondaryObjects] :
       *spec.primaryToSetOfSecondaryObjects()) {
    checkObjectExists(primaryObject);

    std::set<std::string> previouslySeenObjects;
    for (auto& secondaryObject : secondaryObjects) {
      checkObjectExists(secondaryObject);
      if (!previouslySeenObjects.insert(secondaryObject).second) {
        throw std::runtime_error(
            fmt::format(
                "The set of secondary objects associated with primaryObject {} has a repetition.",
                primaryObject));
      }
    }
  }

  // Check if the scopeItems in each sharedDisasterGroup exist
  for (auto& sharedDisasterGroup : *spec.sharedDisasterGroups()) {
    for (auto& scopeItem : sharedDisasterGroup) {
      checkScopeItemExists(*spec.scope(), scopeItem);
    }
  }
}

void ProblemChecker::addSpec(const MinimizeNthLargestUtilizationSpec& spec) {
  checkScopeExists(*spec.scope());
  checkDimensionExists(*spec.dimension());
  if (*spec.n() < 0) {
    throw std::runtime_error(
        "MinimizeNthLargestUtilizationSpec::n must be non-negative");
  }
  if (*spec.targetUtilization() < 0) {
    throw std::runtime_error(
        "MinimizeNthLargestUtilizationSpec::targetUtilization must be non-negative");
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const AvoidAssignmentsSpec& spec) {
  checkScopeExists(*spec.scope());
  for (auto& assignment : *spec.assignments()) {
    checkObjectExists(*assignment.object());
    for (auto& scopeItem : *assignment.scopeItems()) {
      checkScopeItemExists(*spec.scope(), scopeItem);
    }
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const ExclusiveGroupsSpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partitionName());
  checkDimensionExists(*spec.dimension());
  if (!spec.name()->empty()) {
    addSpecName(*spec.name());
  }
}

void ProblemChecker::addSpec(const NestedScopeLimitSpec& spec) {
  checkScopeExists(*spec.scope());
  checkScopeExists(*spec.outerScope());
  checkDimensionExists(*spec.dimension());
  if (!spec.name()->empty()) {
    addSpecName(*spec.name());
  }
}

void ProblemChecker::addSpec(const NonAcceptingSpec& spec) {
  checkScopeExists(*spec.scope());
  for (auto& scopeItemName : *spec.items()) {
    checkScopeItemExists(*spec.scope(), scopeItemName);
  }
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const LargeShardSpec& spec) {
  checkScopeExists(*spec.scope());
  checkScopeItemExists(*spec.scope(), *spec.unassignedScopeItemName());
  checkDimensionExists(*spec.dimension());
  checkScopeItemFilterSpec(*spec.filter(), containerName);
  addSpecName(*spec.name());
}

void ProblemChecker::checkDestinationScopeItemSets(
    const std::vector<std::vector<std::string>>& destinationScopeItemSets,
    const std::string& scopeName) const {
  for (auto& destinationScopeItemSet : destinationScopeItemSets) {
    entities::Set<std::string> seenDestinations;
    for (auto& destinationScopeItem : destinationScopeItemSet) {
      checkScopeItemExists(scopeName, destinationScopeItem);
      if (!seenDestinations.insert(destinationScopeItem).second) {
        throw std::runtime_error(
            fmt::format(
                "duplicate destination scope item {}", destinationScopeItem));
      }
    }
  }
}

void ProblemChecker::addRoutingConfig(
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
  checkScopeExists(scopeName);
  checkPartitionExists(partitionName);

  auto [_, insertSuccess] = routingConfigNames_.insert(configName);
  if (!insertSuccess) {
    throw std::runtime_error(
        fmt::format("Routing config '{}' previously added", configName));
  }

  for (auto& [group, routingRings] : groupToRoutingRings) {
    checkGroupExists(partitionName, group);

    entities::Set<std::string> seenOrigins;
    for (auto& routingRing : *routingRings.routingRings()) {
      checkScopeItemExists(scopeName, *routingRing.originScopeItem());
      if (!seenOrigins.insert(*routingRing.originScopeItem()).second) {
        throw std::runtime_error(
            fmt::format(
                "duplicate origin scope item {}",
                *routingRing.originScopeItem()));
      }

      if (*routingRing.originTraffic() < 0) {
        throw std::runtime_error(
            fmt::format(
                "origin traffic from {} is {} but it must be non-negative",
                *routingRing.originScopeItem(),
                *routingRing.originTraffic()));
      }

      if (routingRing.destinationScopeItemSets().has_value()) {
        checkDestinationScopeItemSets(
            *routingRing.destinationScopeItemSets(), scopeName);
      } else if (
          !defaultOriginToDestinationScopeItemSets.has_value() ||
          !defaultOriginToDestinationScopeItemSets->contains(
              *routingRing.originScopeItem())) {
        throw std::runtime_error(
            fmt::format(
                "origin scope item '{}' is specified for routingRing w.r.t. group '{}', but routinRing has neither destinationScopeItemSets defined nor is there a default destinationScopeItemSets given for this origin scope item",
                *routingRing.originScopeItem(),
                group));
      }
    }
  }

  for (auto& [originScopeItem, destinationLatency] :
       originToDestinationLatency) {
    checkScopeItemExists(scopeName, originScopeItem);
    for (auto& [destinationScopeItem, latency] : destinationLatency) {
      checkScopeItemExists(scopeName, destinationScopeItem);

      if (latency < 0) {
        throw std::runtime_error(
            fmt::format(
                "latency from {} to {} is {} but it must be non-negative",
                originScopeItem,
                destinationScopeItem,
                latency));
      }
    }
  }

  if (defaultOriginToDestinationScopeItemSets.has_value()) {
    entities::Set<std::string> seenOrigins;
    for (auto& [originScopeItem, destinationScopeItemSets] :
         defaultOriginToDestinationScopeItemSets.value()) {
      checkScopeItemExists(scopeName, originScopeItem);
      if (!seenOrigins.insert(originScopeItem).second) {
        throw std::runtime_error(
            fmt::format("duplicate origin scope item {}", originScopeItem));
      }
      checkDestinationScopeItemSets(destinationScopeItemSets, scopeName);
    }
  }
}

void ProblemChecker::addSpec(const RoutingLatencySpec& spec) {
  checkRoutingConfigExists(*spec.routingConfigName());
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partition());
  checkLimitForGroups(*spec.scope(), *spec.partition(), *spec.limit());
  checkGroupFilterSpec(*spec.filter(), *spec.partition());

  // if non default metric is used w.r.t. field 'metric' throw; this field is
  // going to be deprecated
  if (*spec.metric() != interface::RoutingLatencyMetric::AVG) {
    throw std::runtime_error(
        "The field 'metric' in RoutingLatencySpec is deprecated; use `RoutingLatencySpec.latencyMetric` instead");
  }

  auto& metric = *spec.latencyMetric();
  if (*metric.type() == interface::RoutingLatencyMetric::PERCENTILE) {
    if (!metric.percentile()) {
      throw std::runtime_error(
          "Expected 'latencyMetric.Percentile' to be set when metric type is RoutingLatencyMetric.PERCENTILE");
    }

    // percentile value must be in the range (0, 100]
    auto percentile = *metric.percentile();
    if (percentile <= 0 || percentile > 100) {
      throw std::runtime_error(
          fmt::format(
              "Expected 'latencyMetric.Percentile' to be in the range (0, 100], but got {}",
              percentile));
    }
  }

  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const GroupDiversitySpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partition());
  checkDimensionExists(*spec.dimension());
  checkLimitForScopeItems(*spec.scope(), *spec.limit());
  checkScopeItemFilterSpec(*spec.filter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const CapacityWithGroupPresenceSpec& spec) {
  const auto& mainScope = *spec.scope();
  checkScopeExists(mainScope);

  const auto& mainPartition = *spec.partition();
  checkPartitionExists(mainPartition);

  checkDimensionExists(*spec.dimension());

  const auto& aggregationScope =
      spec.aggregationScope() ? *spec.aggregationScope() : mainScope;
  checkScopeExists(aggregationScope);

  const auto& aggregationPartition = spec.aggregationPartition()
      ? *spec.aggregationPartition()
      : mainPartition;
  checkPartitionExists(aggregationPartition);

  switch (*spec.intent()) {
    case interface::CapacityWithGroupPresenceUsageIntent::PER_SCOPE_ITEM: {
      if (mainPartition != aggregationPartition) {
        throw std::runtime_error(
            fmt::format(
                "main partition and aggregation partition must be the same when CapacityWithGroupPresenceUsageIntent is PER_SCOPE_ITEM , but got mainPartition='{}', aggregationPartition='{}'",
                mainPartition,
                aggregationPartition));
      }
      checkLimitForScopeItems(
          mainScope, *spec.scopeItemToLimit(), ValueRequirement::NON_NEGATIVE);
      break;
    }
    case interface::CapacityWithGroupPresenceUsageIntent::
        PER_GROUP_AND_SCOPE_ITEM: {
      checkLimitForGroups(
          mainScope,
          mainPartition,
          *spec.scopeItemToLimit(),
          /*expectedType=*/std::nullopt,
          ValueRequirement::NON_NEGATIVE);
      break;
    }
  }

  checkLimitForGroups(
      aggregationScope,
      aggregationPartition,
      *spec.groupToPresenceWeight(),
      interface::LimitType::ABSOLUTE,
      ValueRequirement::NON_NEGATIVE);
  checkScopeItemFilterSpec(*spec.scopeItemFilter(), mainScope);

  for (auto& multiplier : *spec.multiplierList()) {
    checkLimitForGroups(
        aggregationScope,
        aggregationPartition,
        multiplier,
        interface::LimitType::ABSOLUTE,
        ValueRequirement::NON_NEGATIVE);
  }

  for (const auto& multiplier : *spec.groupUtilMultipliers()) {
    checkLimitForGroups(
        aggregationScope,
        aggregationPartition,
        *multiplier.value(),
        interface::LimitType::ABSOLUTE,
        ValueRequirement::NON_NEGATIVE);
  }

  checkLimitForGroups(
      aggregationScope,
      aggregationPartition,
      *spec.groupToExtraAdditivePenalty(),
      interface::LimitType::ABSOLUTE);

  checkGroupFilterSpec(*spec.groupFilter(), mainPartition);

  for (const auto& [scopeItem, groups] :
       *spec.scopeItemToAlwaysPresentGroups()) {
    checkScopeItemExists(aggregationScope, scopeItem);
    for (const auto& group : groups) {
      checkGroupExists(aggregationPartition, group);
    }
  }

  addSpecName(*spec.name());
}

void ProblemChecker::addSpec(const DiversifyWithinScopeItemSpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partition());
  checkDimensionExists(*spec.dimension());
  checkLimitForGroups(
      *spec.scope(),
      *spec.partition(),
      *spec.groupToLimit(),
      std::nullopt, /*expectType*/
      ValueRequirement::NON_NEGATIVE);
  checkScopeItemFilterSpec(*spec.scopeItemFilter(), *spec.scope());
  addSpecName(*spec.name());
}

void ProblemChecker::checkScopeItemNoOverlap(
    std::vector<std::string> items1,
    std::vector<std::string> items2) {
  std::vector<std::string> intersection;

  std::sort(items1.begin(), items1.end());
  std::sort(items2.begin(), items2.end());

  std::set_intersection(
      items1.begin(),
      items1.end(),
      items2.begin(),
      items2.end(),
      back_inserter(intersection));
  if (intersection.size()) {
    throw std::runtime_error("The two item lists cannot have identical item");
  }
}

void ProblemChecker::addSpec(const ItemsAffinitySpec& spec) {
  checkScopeExists(*spec.scope());
  checkPartitionExists(*spec.partitionName());
  for (auto& item : *spec.scopeItemsOfType1()) {
    checkScopeItemExists(*spec.scope(), item);
  }
  for (auto& item : *spec.scopeItemsOfType2()) {
    checkScopeItemExists(*spec.scope(), item);
  }
  checkScopeItemNoOverlap(*spec.scopeItemsOfType1(), *spec.scopeItemsOfType2());
}

void ProblemChecker::checkLimitType(
    const interface::LimitType& type,
    const interface::LimitType& expectedType) {
  if (type != expectedType) {
    throw std::runtime_error(
        fmt::format(
            "expected limit of type {}, but got limit of type {}",
            apache::thrift::util::enumNameSafe(expectedType),
            apache::thrift::util::enumNameSafe(type)));
  }
}

void ProblemChecker::checkLimitForScopeItems(
    const std::string& scope,
    const Limit& limit,
    ValueRequirement requirement) const {
  if (requirement != ValueRequirement::NONE) {
    checkLimitValues(limit, requirement);
  }
  for (const auto& [scopeItem, _] : *limit.scopeItemLimits()) {
    checkScopeItemExists(scope, scopeItem);
  }
  if (!limit.groupLimits()->empty()) {
    throw std::runtime_error("unexpected group limits");
  }
  if (!limit.scopeItemToGroupLimits()->empty()) {
    throw std::runtime_error("unexpected group to scope item limits");
  }
}

void ProblemChecker::checkValue(
    double value,
    std::string_view attribute,
    ValueRequirement requirement) {
  switch (requirement) {
    case ValueRequirement::NONE:
      return;
    case ValueRequirement::NON_NEGATIVE:
      if (value < 0) {
        throw std::runtime_error(
            fmt::format(
                "expected {} to be non-negative but got {}", attribute, value));
      }
      return;
    case ValueRequirement::POSITIVE:
      if (value <= 0) {
        throw std::runtime_error(
            fmt::format(
                "expected {} to be positive but got {}", attribute, value));
      }
      return;
  }
}

void ProblemChecker::checkNonNegativeValue(
    double value,
    std::string_view attribute) {
  checkValue(value, attribute, ValueRequirement::NON_NEGATIVE);
}

void ProblemChecker::checkLimitValues(
    const Limit& limit,
    ValueRequirement requirement) {
  checkValue(*limit.globalLimit(), "global limit value", requirement);

  for (auto& [key, value] : *limit.scopeItemLimits()) {
    checkValue(
        value, fmt::format("limit for scope item '{}'", key), requirement);
  }
  for (auto& [key, value] : *limit.groupLimits()) {
    checkValue(value, fmt::format("limit for group '{}'", key), requirement);
  }

  for (auto& [scopeItem, groups] : *limit.scopeItemToGroupLimits()) {
    for (auto& [group, value] : groups) {
      checkValue(
          value,
          fmt::format(
              "limit for (scopeItem '{}', group '{}', )", scopeItem, group),
          requirement);
    }
  }
}

void ProblemChecker::checkLimitForGroups(
    const std::string& scope,
    const std::string& partition,
    const Limit& limit,
    std::optional<interface::LimitType> expectedType,
    ValueRequirement requirement) const {
  if (expectedType.has_value()) {
    checkLimitType(*limit.type(), expectedType.value());
  }
  if (requirement != ValueRequirement::NONE) {
    checkLimitValues(limit, requirement);
  }

  for (auto& [scopeItem, value] : *limit.scopeItemLimits()) {
    checkScopeItemExists(scope, scopeItem);
  }

  for (auto& [group, value] : *limit.groupLimits()) {
    checkGroupExists(partition, group);
  }

  for (auto& [scopeItem, groups] : *limit.scopeItemToGroupLimits()) {
    checkScopeItemExists(scope, scopeItem);
    for (auto& [group, value] : groups) {
      checkGroupExists(partition, group);
    }
  }
}

void ProblemChecker::checkLimitForGroups(
    const std::string& partition,
    const Limit& limit,
    std::optional<LimitType> type) const {
  if (type && limit.type() != *type) {
    checkLimitType(*limit.type(), *type);
  }

  if (!limit.scopeItemLimits()->empty()) {
    throw std::runtime_error(
        "unexpected scopeItemLimits found in Limit struct; expected only groupLimits to be set");
  }

  if (!limit.scopeItemToGroupLimits()->empty()) {
    throw std::runtime_error(
        "unexpected scopeItemToGroupLimits found in Limit struct; expected only groupLimits to be set");
  }

  for (auto& [group, _] : *limit.groupLimits()) {
    checkGroupExists(partition, group);
  }
}

void ProblemChecker::addSpecName(const std::string& name) {
  if (!name.empty() && !specNames.insert(name).second) {
    throw std::runtime_error(fmt::format("duplicate spec name {}", name));
  }
}

void ProblemChecker::addSpecName(const folly::Optional<std::string>& name) {
  if (name) {
    addSpecName(*name);
  }
}

void ProblemChecker::addGlobalName(const std::string& name, EntityType type) {
  if (name.empty()) {
    throw std::runtime_error("entity names must not be empty");
  }
  auto it = globalNameToType.find(name);
  if (it == globalNameToType.end()) {
    globalNameToType[name] = type;
    return;
  }
  if (it->second != type) {
    throw std::runtime_error(
        fmt::format(
            "name {} already used as {} name", name, toString(it->second)));
  }
}

void ProblemChecker::checkScopeItemFilterSpec(
    const Filter& spec,
    const std::string& scope) const {
  if (*spec.type() != FilterType::SCOPE_ITEM) {
    throw std::runtime_error(
        "Expected filter to be of type FilterType::SCOPE_ITEM");
  }

  if (spec.itemsBlacklist() && spec.itemsWhitelist()) {
    throw std::runtime_error(
        "Expected at most one of itemsBlacklist or itemWhitelist to be set when defining a Filter");
  }
  if (spec.itemsBlacklist()) {
    for (auto& item : *spec.itemsBlacklist()) {
      checkScopeItemExists(scope, item);
    }
  }
  if (spec.itemsWhitelist()) {
    for (auto& item : *spec.itemsWhitelist()) {
      checkScopeItemExists(scope, item);
    }
  }
}
void ProblemChecker::checkGroupFilterSpec(
    const Filter& spec,
    const std::string& partition) const {
  if (*spec.type() != FilterType::GROUP) {
    throw std::runtime_error("Expected filter to be of type FilterType::GROUP");
  }

  if (spec.itemsBlacklist() && spec.itemsWhitelist()) {
    throw std::runtime_error(
        "Expected at most one of itemsBlacklist or itemWhitelist to be set when defining a Filter");
  }

  if (spec.itemsBlacklist()) {
    for (auto& group : *spec.itemsBlacklist()) {
      checkGroupExists(partition, group);
    }
  }
  if (spec.itemsWhitelist()) {
    for (auto& group : *spec.itemsWhitelist()) {
      checkGroupExists(partition, group);
    }
  }
}

std::string ProblemChecker::getObjectCountDimensionName() const {
  checkObjectNameIsSet();
  return fmt::format("{}_count", objectName);
}

void ProblemChecker::enableMoveStats(const MoveStatsSpec& spec) const {
  if (*spec.trackObjects() && spec.trackObjectsWhitelist().has_value()) {
    for (auto& name : *spec.trackObjectsWhitelist()) {
      checkObjectExists(name);
    }
  }
  if (*spec.trackContainers() &&
      spec.printSourceContainersWhitelist().has_value()) {
    for (auto& name : *spec.printSourceContainersWhitelist()) {
      checkContainerExists(name);
    }
  }
}

std::string ProblemChecker::toString(EntityType type) {
  switch (type) {
    case EntityType::OBJECT:
      return "object";
    case EntityType::SCOPE:
      return "scope";
    case EntityType::PARTITION:
      return "partition";
    case EntityType::DIMENSION:
      return "dimension";
  }
  throw std::runtime_error("unknown entity type");
}

void ProblemChecker::checkBalanceLegacy(const BalanceSpec& spec) {
  if (*spec.formula() != BalanceSpecFormula::LEGACY) {
    return;
  }
  if (*spec.balanceMetric() == BalanceSpecMetric::CAPACITY_PER_ITEM) {
    throw std::runtime_error(
        "LEGACY formula is not supported with CAPACITY_PER_ITEM metric");
  }
  if (spec.softUpperBound()) {
    throw std::runtime_error(
        "soft upper bound is not supported by legacy formula");
  }
  if (*spec.boundType() != BalanceSpecBoundType::RELATIVE) {
    throw std::runtime_error(
        "only relative bound type is supported by legacy formula");
  }
  if (*spec.definition() != BalanceSpecDefinition::AFTER) {
    throw std::runtime_error(
        "only after definition is supported by legacy formula");
  }
  if (!*spec.fixAverageToInitial()) {
    throw std::runtime_error("legacy formula must fix average to initial");
  }
  if (!spec.includeInInitialAverage()->empty()) {
    throw std::runtime_error(
        "include in initial average override is not supported by legacy formula");
  }
}

void ProblemChecker::checkBalanceIgnoreUpperBound(const BalanceSpec& spec) {
  if (*spec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes()) {
    return;
  }
  if (*spec.formula() != BalanceSpecFormula::IDEAL) {
    throw std::runtime_error(
        "ignoreUpperBoundForIdealWithAbsOrRelBoundTypes can only be set to False when using IDEAL formula");
  }
}

void ProblemChecker::checkSolverSpec(
    const interface::LocalSearchStageSolverSpec& stageSolverspec) const {
  if (stageSolverspec.parallelExecutionConfig()) {
    checkExecutionConfig(*stageSolverspec.parallelExecutionConfig());
  }
  for (auto& multiStageConfig : *stageSolverspec.multiStageConfigs()) {
    checkMultiStageConfig(multiStageConfig);
  }
  for (auto& stageSpec : *stageSolverspec.stageSpecs()) {
    checkNonNegativeValue(*stageSpec.begin(), "stageSpec.begin()");
    checkNonNegativeValue(*stageSpec.end(), "stageSpec.end()");
    if (*stageSpec.begin() >= *stageSpec.end()) {
      throw std::runtime_error(
          fmt::format(
              "Expected stageSpec.begin() < stageSpec.end(), but got begin={}, end={}",
              *stageSpec.begin(),
              *stageSpec.end()));
    }

    checkSolverSpec(*stageSpec.solverSpec());
    if (stageSpec.higherPriorityObjConfig()) {
      auto& config = *stageSpec.higherPriorityObjConfig();
      for (auto& [tuplePos, allowedWorsening] :
           *config.tuplePosToAllowedWorsening()) {
        if (tuplePos < 0 || tuplePos >= *stageSpec.begin()) {
          throw std::runtime_error(
              fmt::format(
                  "Expected each tuple position in HigherPriorityObjConfig.tuplePosToAllowedWorsening() to be in the interval [0, stageSpec.begin()), but got tuplePos={} and stageSpec.begin()={}",
                  tuplePos,
                  *stageSpec.begin()));
        }
        check(allowedWorsening);
      }
    }
  }
}

void ProblemChecker::checkSolverSpec(
    const interface::LocalSearchSolverSpec& solverSpec) const {
  checkMoveTypeSpecs(solverSpec);
  if (solverSpec.parallelExecutionConfig()) {
    checkExecutionConfig(*solverSpec.parallelExecutionConfig());
  }
  if (solverSpec.customEquivalenceSetConfig()) {
    checkCustomEquivalenceSetsConfig(*solverSpec.customEquivalenceSetConfig());
  }
  if (solverSpec.minCycleObjectiveImprovement().has_value()) {
    check(*solverSpec.minCycleObjectiveImprovement()->defaultThreshold());
  }
}

void ProblemChecker::checkBatchingConfig(
    const interface::BatchingExecutionConfig& config) {
  checkNonNegativeValue(
      *config.maxConcurrency(), "BatchingExecutionConfig.maxConcurrency");
}

void ProblemChecker::checkExecutionConfig(
    const interface::ParallelExecutionConfig& config) {
  switch (config.getType()) {
    case interface::ParallelExecutionConfig::Type::__EMPTY__:
      throw std::runtime_error("ParallelExecutionConfig cannot be empty");
    case interface::ParallelExecutionConfig::Type::slidingWindow:
      return;
    case interface::ParallelExecutionConfig::Type::batching:
      checkBatchingConfig(config.batching().value());
      return;
    case interface::ParallelExecutionConfig::Type::autoExecution:
      checkBatchingConfig(*config.autoExecution()->batching());
      return;
  }
  throw std::runtime_error("Unknown ParallelExecutionConfig type");
}

void ProblemChecker::checkSolverSpec(
    const interface::OptimalSolverSpec& solverSpec) {
  check(*solverSpec.multiObjSolveSettings());
}

void ProblemChecker::checkMultiStageConfig(
    const interface::MultiStageConfig& multiStageConfig) {
  if (!multiStageConfig.solveTime().has_value() &&
      !multiStageConfig.moveLimit().has_value()) {
    throw std::runtime_error(
        "MultiStageConfig must have either moveLimit or solveTime set");
  }
  if (multiStageConfig.moveLimit().has_value() &&
      *multiStageConfig.moveLimit() < 0) {
    throw std::runtime_error("MultiStageConfig moveLimit must be non-negative");
  }
  if (multiStageConfig.solveTime().has_value() &&
      *multiStageConfig.solveTime() < 0) {
    throw std::runtime_error("MultiStageConfig solveTime must be non-negative");
  }
  if (multiStageConfig.stageIds()->size() == 0) {
    throw std::runtime_error("MultiStageConfig stage list must be non-empty");
  }
  for (auto& stageId : *multiStageConfig.stageIds()) {
    if (stageId < 0) {
      throw std::runtime_error(
          "MultiStageConfig stage id in stage list must be non-negative");
    }
  }
}

void ProblemChecker::checkPrecision(
    const algopt::common::thrift::PrecisionTolerances& precisionTolerances) {
  checkAbsoluteEpsilon(*precisionTolerances.absolute());
  checkRelativeEpsilon(*precisionTolerances.relative());
}

void ProblemChecker::checkAbsoluteEpsilon(const double absoluteEpsilon) {
  if (absoluteEpsilon < std::numeric_limits<double>::epsilon()) {
    throw std::runtime_error(
        fmt::format(
            "absoluteEpsilon must be bigger than std::numeric_limits<double>::epsilon(), but got {}",
            absoluteEpsilon));
  }
}

void ProblemChecker::checkRelativeEpsilon(const double relativeEpsilon) {
  if (relativeEpsilon < std::numeric_limits<double>::epsilon()) {
    throw std::runtime_error(
        fmt::format(
            "relativeEpsilon must be bigger than std::numeric_limits<double>::epsilon(), but got {}",
            relativeEpsilon));
  }
}

void ProblemChecker::checkMoveTypeSpecs(
    const interface::LocalSearchSolverSpec& spec) const {
  if (!spec.moveTypes()->empty()) {
    throw std::runtime_error(
        "The field 'moveTypes' in LocalSearchSolverSpec is no longer supported, use 'moveTypeList' instead");
  }
  auto emptyStruct = spec.moveTypeList()->empty();
  if (emptyStruct) {
    throw std::runtime_error(
        "Field 'moveTypeList' in LocalSearchSolverSpec cannot be empty");
  }
  if (spec.singleRandomStratifiedMoveTypeSpec().has_value()) {
    checkMoveTypeSpec(*spec.singleRandomStratifiedMoveTypeSpec());
  }

  for (auto& moveTypeSpec : *spec.moveTypeList()) {
    auto moveTypeSpecType = moveTypeSpec.getType();
    switch (moveTypeSpecType) {
      case interface::MoveTypeSpec::Type::
          singleRandomObjectStratifiedMoveTypeSpec: {
        checkMoveTypeSpec(
            moveTypeSpec.singleRandomObjectStratifiedMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::
          groupMoveWithHintStrategiesMoveTypeSpec: {
        checkMoveTypeSpec(
            moveTypeSpec.groupMoveWithHintStrategiesMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::swapMoveTypeSpec: {
        checkMoveTypeSpec(moveTypeSpec.swapMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::fixedDestMoveTypeSpec: {
        checkMoveTypeSpec(moveTypeSpec.fixedDestMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::singleFixedSourceMoveTypeSpec: {
        checkMoveTypeSpec(moveTypeSpec.singleFixedSourceMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::colocateGroupsMoveTypeSpec: {
        checkMoveTypeSpec(moveTypeSpec.colocateGroupsMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::singleRandomStratifiedMoveTypeSpec: {
        checkMoveTypeSpec(
            moveTypeSpec.singleRandomStratifiedMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::singleFastMoveTypeSpec: {
        checkMoveTypeSpec(moveTypeSpec.singleFastMoveTypeSpec().value());
        break;
      }
      case interface::MoveTypeSpec::Type::greedyGroupToScopeItemMoveTypeSpec: {
        checkMoveTypeSpec(
            moveTypeSpec.greedyGroupToScopeItemMoveTypeSpec().value());
        break;
      }
      case MoveTypeSpec::Type::__EMPTY__: {
        throw std::runtime_error(
            "MoveTypeSpec is empty; you need to specify one of the options");
      }
      default: {
        // no checks needed for these types currently
        break;
      }
    }
  }
}

void ProblemChecker::checkMoveTypeSpec(
    const SingleRandomObjectStratifiedMoveTypeSpec& spec) const {
  checkObjectsToExploreOptions(
      *spec.objectsToExploreOptions(), ObjectBundleExpectation::DISALLOWED);
  auto& sampleSize = *spec.stratifiedSampleSize();
  check(sampleSize, false);
}

void ProblemChecker::checkMoveTypeSpec(
    const GroupMoveWithHintStrategiesMoveTypeSpec& spec) const {
  checkPartitionExists(*spec.primaryPartition());
  checkPartitionExists(*spec.secondaryPartition());
  if (spec.unassignedContainer().has_value()) {
    checkContainerExists(*spec.unassignedContainer());
  }
  checkMoveStrategies(*spec.secondaryPartition(), *spec.moveStrategies());
  checkSecondaryGroupReplacementConfig(
      *spec.secondaryPartition(), *spec.secondaryGroupReplacementConfig());
}

void ProblemChecker::checkMoveTypeSpec(
    const interface::SwapMoveTypeSpec& spec) const {
  /*
  // This check breaks some GSP tests, enable after T203645798 is done!
  if (auto partitionName =
          spec.partitionNameToExploreSwapsWithinObjectGroup()) {
    checkPartitionExists(*partitionName);
  }*/

  if (spec.destinationsToExplore().has_value()) {
    const auto& destinationOptions = spec.destinationsToExplore().value();
    if (destinationOptions.getType() ==
        interface::DestinationsToExploreOptions::Type::moveToScopeItems) {
      // SwapMoveType reads only defaultScopeItems.
      const auto unsupported = [](std::string_view field) {
        return fmt::format(
            "SwapMoveType does not support {}: only unfiltered MoveToScopeItemsSpec.defaultScopeItems is supported",
            field);
      };
      const auto& moveToScopeItems =
          destinationOptions.moveToScopeItems().value();
      if (moveToScopeItems.defaultScopeItems()->filter().has_value()) {
        throw std::runtime_error(
            unsupported("MoveToScopeItemsSpec.defaultScopeItems.filter"));
      }
      if (!moveToScopeItems.objectToScopeItems()->empty()) {
        throw std::runtime_error(
            unsupported("MoveToScopeItemsSpec.objectToScopeItems"));
      }
      if (!moveToScopeItems.scopeItemsPerGroups()
               ->groupToScopeItemList()
               ->empty()) {
        throw std::runtime_error(
            unsupported("MoveToScopeItemsSpec.scopeItemsPerGroups"));
      }
    }

    checkDestinationsToExploreOptions(destinationOptions);
  }

  if (auto sampleSize = spec.sampleSize()) {
    check(*sampleSize, false);
  }
}

void ProblemChecker::checkMoveTypeSpec(
    const interface::SingleFastMoveTypeSpec& spec) const {
  if (spec.destinationsToExplore().has_value()) {
    checkDestinationsToExploreOptions(spec.destinationsToExplore().value());
  }
}

void ProblemChecker::checkMoveTypeSpec(
    const interface::FixedDestMoveTypeSpec& spec) const {
  if (auto sampleSize = spec.sampleSize()) {
    check(*sampleSize, false);
  }
}

void ProblemChecker::checkMoveTypeSpec(
    const SingleFixedSourceMoveTypeSpec& spec) const {
  if (!spec.specialContainer() && !spec.scopeItemList()->scopeItems()) {
    throw std::runtime_error(
        "SingleFixedSourceMoveTypeSpec needs a special container or a list of scope items to perform moves from");
  }

  if (spec.objectBundleFormationHints() &&
      spec.objectBundleFormationHints()->scopeItemToObjectsToExploreOptions()) {
    for (const auto& [scopeItem, objectsToExploreOptions] :
         *spec.objectBundleFormationHints()
              ->scopeItemToObjectsToExploreOptions()) {
      checkObjectsToExploreOptions(
          objectsToExploreOptions, ObjectBundleExpectation::REQUIRED);
    }
  }

  if (auto sampleSize = spec.sampleSize()) {
    check(*sampleSize, false);
  }
}

void ProblemChecker::checkSecondaryGroupReplacementConfig(
    const std::string& secondaryPartition,
    const interface::SecondaryGroupReplacementConfig&
        secondaryGroupReplacementConfig) const {
  const auto& map =
      *secondaryGroupReplacementConfig.secondaryGroupToAllowedReplacements();
  for (auto& [groupName, groups] : map) {
    checkGroupExists(secondaryPartition, groupName);
    for (auto& group : groups) {
      checkGroupExists(secondaryPartition, group);
    }
  }
}

void ProblemChecker::checkMoveStrategies(
    const std::string& secondaryPartition,
    const interface::MoveStrategies& moveStrategies) const {
  auto& map = *moveStrategies.groupToMoveStrategy();
  if (map.empty()) {
    throw std::runtime_error(
        "groupToMoveStrategy is empty; you need to specify groupToMoveStrategy");
  }
  for (auto& [groupName, hintOption] : map) {
    checkGroupExists(secondaryPartition, groupName);
    if (hintOption.moveSetsGeneratedPerScopeItem() < 0) {
      throw std::runtime_error(
          "moveSetsGeneratedPerScopeItem must be non-negative in MoveStrategyType");
    }
    checkExploreOption(*hintOption.moveToScopeItems());
    if (hintOption.tertiaryPartition().has_value()) {
      checkPartitionExists(*hintOption.tertiaryPartition());
      if (!hintOption.numScopeItemsToExplorePerTertiaryGroup().has_value()) {
        throw std::runtime_error(
            "numScopeItemsToExplorePerTertiaryGroup must be set when tertiaryPartition is set in MoveStrategyType");
      }
      if (*hintOption.numScopeItemsToExplorePerTertiaryGroup() <= 0) {
        throw std::runtime_error(
            "numScopeItemsToExplorePerTertiaryGroup must be positive when tertiaryPartition is set in MoveStrategyType");
      }
    } else if (hintOption.numScopeItemsToExplorePerTertiaryGroup()
                   .has_value()) {
      throw std::runtime_error(
          "numScopeItemsToExplorePerTertiaryGroup must not be set when tertiaryPartition is not set in MoveStrategyType");
    }
    if (*hintOption.distinctScopeItemsAcrossTertiaryGroups()) {
      if (!hintOption.tertiaryPartition().has_value()) {
        throw std::runtime_error(
            "distinctScopeItemsAcrossTertiaryGroups requires tertiaryPartition in MoveStrategyType");
      }
      if (*hintOption.type() !=
          interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT) {
        throw std::runtime_error(
            "distinctScopeItemsAcrossTertiaryGroups requires RANDOM_SAMPLING_WITHOUT_REPLACEMENT in MoveStrategyType");
      }
    }
  }
}

void ProblemChecker::checkObjectsToExploreOptions(
    const interface::ObjectsToExploreOptions& objectsToExploreOptions,
    ObjectBundleExpectation objectBundleExpectation) const {
  auto exploreOption = objectsToExploreOptions.getType();
  switch (exploreOption) {
    case interface::ObjectsToExploreOptions::Type::objectsFromGroupsSpec: {
      checkObjectsFromGroupsSpec(
          objectsToExploreOptions.objectsFromGroupsSpec().value(),
          objectBundleExpectation);
      break;
    }
    case interface::ObjectsToExploreOptions::Type::__EMPTY__: {
      throw std::runtime_error(
          "ObjectsToExploreOptions is empty; you need to specify one of the options");
    }
  }
}

void ProblemChecker::checkObjectsFromGroupsSpec(
    const interface::ObjectsFromGroupsSpec& objectsFromGroupsSpec,
    ObjectBundleExpectation objectBundleExpectation) const {
  check(*objectsFromGroupsSpec.groupList());
  const int bundleSize = objectsFromGroupsSpec.bundleSize().value_or(1);
  if (objectBundleExpectation == ObjectBundleExpectation::DISALLOWED &&
      objectsFromGroupsSpec.bundleSize() && bundleSize != 1) {
    throw std::runtime_error(
        "bundleSize != 1 is not supported in this move type");
  }
  if (objectBundleExpectation == ObjectBundleExpectation::REQUIRED &&
      (!objectsFromGroupsSpec.bundleSize() || bundleSize <= 1)) {
    throw std::runtime_error(
        "bundleSize <=1 is not supported in this move type");
  }
}

void ProblemChecker::check(const interface::GroupList& groupList) const {
  checkPartitionExists(*groupList.partitionName());
}

void ProblemChecker::checkMoveTypeSpec(
    const interface::SingleRandomStratifiedMoveTypeSpec& spec) const {
  checkDestinationsToExploreOptions(*spec.destinationsToExplore());
  check(*spec.stratifiedSampleSize());
}

void ProblemChecker::checkCustomEquivalenceSetsConfig(
    const CustomEquivalenceSetConfig& config) const {
  for (auto& goalName : *config.goalSelectionConfig()->stringsToFilter()) {
    checkSpecNamesExists(goalName);
  }
  for (auto& constraintName :
       *config.constraintSelectionConfig()->stringsToFilter()) {
    checkSpecNamesExists(constraintName);
  }
  for (const auto& partitionName : *config.partitionNames()) {
    checkPartitionExists(partitionName);
  }
}

void ProblemChecker::checkSpecNamesExists(const std::string& specName) const {
  if (!specNames.contains(specName)) {
    throw std::runtime_error(fmt::format("Spec '{}' does not exist", specName));
  }
}

void ProblemChecker::addConstraintName(const std::string& name) {
  if (!name.empty()) {
    constraintNames_.insert(name);
  }
}

void ProblemChecker::checkConstraintNameExists(
    const std::string& constraintName) const {
  if (!constraintNames_.contains(constraintName)) {
    throw std::runtime_error(
        fmt::format("Constraint '{}' does not exist", constraintName));
  }
}

void ProblemChecker::addDestinationsToExploreOptions(
    const std::string& name,
    const interface::DestinationsToExploreOptions&
        destinationsToExploreOptions) {
  if (name.empty()) {
    throw std::runtime_error(
        "addDestinationsToExploreOptions must have non-empty names");
  }
  auto exploreOption = destinationsToExploreOptions.getType();
  if (exploreOption ==
      interface::DestinationsToExploreOptions::Type::destinationToExploreName) {
    throw std::runtime_error(
        "interface::DestinationsToExploreOptions::Type::destinationToExploreName should not be directly added. Please use interface::ProblemSolver::addDestinationToExploreOptions instead");
  } else {
    auto [_, insertSuccess] = destinationsToExploreOptionNames_.insert(name);
    if (!insertSuccess) {
      throw std::runtime_error(
          fmt::format(
              "DestinationsToExploreOptions'{}' previously added", name));
    }
    checkDestinationsToExploreOptions(destinationsToExploreOptions);
  }
}

void ProblemChecker::checkDestinationsToExploreOptions(
    const interface::DestinationsToExploreOptions& destinationOptions) const {
  auto exploreOption = destinationOptions.getType();
  switch (exploreOption) {
    case interface::DestinationsToExploreOptions::Type::
        moveToCurrentScopeItem: {
      checkExploreOption(destinationOptions.moveToCurrentScopeItem().value());
      break;
    }
    case interface::DestinationsToExploreOptions::Type::moveToScopeItems: {
      checkExploreOption(destinationOptions.moveToScopeItems().value());
      break;
    }
    case interface::DestinationsToExploreOptions::Type::
        destinationToExploreName: {
      checkDestinationsToExploreOptionExists(
          destinationOptions.destinationToExploreName().value());
      break;
    }
    case interface::DestinationsToExploreOptions::Type::__EMPTY__: {
      throw std::runtime_error(
          "DestinationsToExploreOptions is empty; you need to specify one of the options");
    }
  }
}

void ProblemChecker::checkExploreOption(
    const interface::MoveToCurrentScopeItemSpec& moveToCurrentScopeItem) const {
  checkScopeExists(
      *moveToCurrentScopeItem.scopeNameForExploringMovesToCurrentScopeItem());
}

void ProblemChecker::checkExploreOption(
    const interface::MoveToScopeItemsSpec& moveToScopeItems) const {
  check(*moveToScopeItems.defaultScopeItems());
  for (auto& [object, scopeItemList] : *moveToScopeItems.objectToScopeItems()) {
    checkObjectExists(object);
    check(scopeItemList);
  }

  auto groupToScopeItemList = moveToScopeItems.scopeItemsPerGroups();
  if (groupToScopeItemList->groupToScopeItemList()->size() > 0) {
    auto& partition = *groupToScopeItemList->partitionName();
    checkPartitionExists(partition);
    for (auto& [group, scopeItemList] :
         *groupToScopeItemList->groupToScopeItemList()) {
      checkGroupExists(partition, group);
      check(scopeItemList);
    }
  }
}

void ProblemChecker::check(
    const interface::ScopeItemList& scopeItemList) const {
  checkScopeExists(*scopeItemList.scopeName());
  if (scopeItemList.scopeItems().has_value()) {
    for (auto& scopeItem : scopeItemList.scopeItems().value()) {
      checkScopeItemExists(*scopeItemList.scopeName(), scopeItem);
    }
  }
  if (scopeItemList.filter().has_value()) {
    const auto& filter = *scopeItemList.filter();
    switch (filter.getType()) {
      case interface::ScopeItemFilter::Type::objectDimensionBased: {
        const auto& dimensionName =
            *filter.objectDimensionBased()->dimensionName();
        checkDimensionExists(dimensionName);
        const auto& scopeName = *scopeItemList.scopeName();
        const auto* dimensionScope =
            folly::get_ptr(dynamicObjectDimensionToScope_, dimensionName);
        if (dimensionScope == nullptr || *dimensionScope != scopeName) {
          throw std::runtime_error(
              fmt::format(
                  "ObjectDimensionBasedFilter dimension '{}' is expected to be dynamic on scope '{}'",
                  dimensionName,
                  scopeName));
        }
        break;
      }
      case interface::ScopeItemFilter::Type::__EMPTY__:
        throw std::runtime_error("ScopeItemFilter is not set");
    }
  }
}

void ProblemChecker::check(
    const interface::SampleSize& sampleSize,
    bool isObjectToSampleSizeSupported) const {
  auto defaultSampleSize = *sampleSize.defaultSampleSize();
  checkNonNegativeValue(defaultSampleSize, "default sample size");

  if (!isObjectToSampleSizeSupported &&
      sampleSize.objectToSampleSize()->size() > 0) {
    throw std::runtime_error(
        "objectToSampleSize is not supported in this move type");
  }

  bool foundAtleastOnePositiveValue = defaultSampleSize > 0 ? true : false;
  for (auto& [object, objectSampleSize] : *sampleSize.objectToSampleSize()) {
    checkObjectExists(object);
    if (objectSampleSize > 0) {
      foundAtleastOnePositiveValue = true;
    }
    checkNonNegativeValue(
        objectSampleSize, fmt::format("sample size for '{}'", object));
  }

  // throw if no value is > 0
  if (!foundAtleastOnePositiveValue) {
    throw std::runtime_error(
        "No positive value found in SampleSize struct. Make it sure it is initialized properly and atleast one value should be > 0");
  }
}

void ProblemChecker::checkMoveTypeSpec(
    const interface::ColocateGroupsMoveTypeSpec& spec) const {
  checkPartitionExists(*spec.partitionName());
  checkScopeExists(*spec.colocationScopeName());

  auto& relatedGroupsList = *spec.relatedGroupsList();
  if (relatedGroupsList.empty()) {
    throw std::runtime_error(
        "ColocateGroupsMoveTypeSpec.relatedGroupsList is empty. It is expeced to have at least one entry when using ColocateGroupsMoveTypeSpec");
  }

  // check relatedGroupsList
  folly::F14FastMap<std::string, int> groupToNRelatedGroups;
  for (auto& relatedGroupInfo : relatedGroupsList) {
    for (auto& groupName : *relatedGroupInfo.relatedGroups()) {
      checkGroupExists(*spec.partitionName(), groupName);
      auto n = ++groupToNRelatedGroups[groupName];
      if (n > 1) {
        throw std::runtime_error(
            fmt::format(
                "Group '{}' is present in multiple related groups in ColocateGroupsMoveTypeSpec. "
                "Across all ColocateGroupsMoveTypeRelatedGroupsInfo, the relatedGroups sets need to be disjoint",
                groupName));
      }
    }

    if (relatedGroupInfo.destinationScopeItems().has_value()) {
      for (auto& scopeItem : *relatedGroupInfo.destinationScopeItems()) {
        checkScopeItemExists(*spec.colocationScopeName(), scopeItem);
      }
    }
  }

  // check colocationScopeItemToGroupToContainers
  for (auto& [scopeItem, groupToContainers] :
       *spec.colocationScopeItemToGroupToContainers()) {
    checkScopeItemExists(*spec.colocationScopeName(), scopeItem);
    for (auto& [group, containers] : groupToContainers) {
      checkGroupExists(*spec.partitionName(), group);
      for (auto& container : containers) {
        checkContainerExists(container);
      }
    }
  }

  if (spec.defaultSampleSize().has_value()) {
    checkNonNegativeValue(
        *spec.defaultSampleSize(),
        "ColocateGroupsMoveTypeSpec.defaultSampleSize");
  }
}

void ProblemChecker::checkMoveTypeSpec(
    const interface::GreedyGroupToScopeItemMoveTypeSpec& spec) const {
  // Destinations come from either scopeItemMovesScope (all its scope items) or
  // an explicit destinationsToExplore; without one of them there is nowhere to
  // move groups.
  if (spec.scopeItemMovesScope()->empty() &&
      !spec.destinationsToExplore().has_value()) {
    throw std::runtime_error(
        "GreedyGroupToScopeItemMoveTypeSpec must have at least one of 'scopeItemMovesScope' or 'destinationsToExplore' set");
  }
  if (spec.groupMovesPartition()->empty()) {
    throw std::runtime_error(
        "GreedyGroupToScopeItemMoveTypeSpec requires 'groupMovesPartition' to be set");
  }
  checkNonNegativeValue(
      *spec.nSampleSetsToExplore(),
      "GreedyGroupToScopeItemMoveTypeSpec.nSampleSetsToExplore");
  checkPartitionExists(*spec.groupMovesPartition());
  if (!spec.scopeItemMovesScope()->empty()) {
    checkScopeExists(*spec.scopeItemMovesScope());
  }
  if (spec.destinationsToExplore().has_value()) {
    const auto& destinations = *spec.destinationsToExplore();
    if (destinations.getType() ==
        interface::DestinationsToExploreOptions::Type::moveToScopeItems) {
      const auto& moveToScopeItems = destinations.moveToScopeItems().value();
      // This move type relocates a whole group to a single scope item.
      // objectToScopeItems is per-object, so it is not supported.
      if (!moveToScopeItems.objectToScopeItems()->empty()) {
        throw std::runtime_error(
            "GreedyGroupToScopeItemMoveTypeSpec does not support 'objectToScopeItems' in 'destinationsToExplore'; it moves a whole group to one scope item. Use 'scopeItemsPerGroups' or 'defaultScopeItems' instead");
      }
      // scopeItemsPerGroups looks up the moved group in its own partition to
      // pick the destination, so that partition must be the one we move. An
      // empty map is inert (its partitionName is never consulted), so we only
      // check the partition when the map is non-empty.
      const auto& perGroups = *moveToScopeItems.scopeItemsPerGroups();
      if (!perGroups.groupToScopeItemList()->empty() &&
          *perGroups.partitionName() != *spec.groupMovesPartition()) {
        throw std::runtime_error(
            fmt::format(
                "GreedyGroupToScopeItemMoveTypeSpec 'scopeItemsPerGroups' partition '{}' must match 'groupMovesPartition' '{}'",
                *perGroups.partitionName(),
                *spec.groupMovesPartition()));
      }
    }
    checkDestinationsToExploreOptions(destinations);
  }
  for (const auto& constraintName :
       *spec.candidatePruning()->constraintNames()) {
    checkConstraintNameExists(constraintName);
  }
}

void ProblemChecker::check(
    const algopt::common::thrift::AllowedWorsening& allowedWorsening) {
  checkNonNegativeValue(
      *allowedWorsening.percent(), "allowedWorsening.percent");
  checkNonNegativeValue(
      *allowedWorsening.absolute(), "allowedWorsening.absolute");
}

void ProblemChecker::check(const algopt::common::thrift::Threshold& threshold) {
  if (!threshold.percent().has_value() && !threshold.absolute().has_value()) {
    throw std::runtime_error(
        "Threshold must have at least one of percent or absolute set");
  }
  if (threshold.percent().has_value()) {
    checkNonNegativeValue(*threshold.percent(), "Threshold.percent");
  }
  if (threshold.absolute().has_value()) {
    checkNonNegativeValue(*threshold.absolute(), "Threshold.absolute");
  }
}

void ProblemChecker::check(
    const algopt::common::thrift::PerObjectiveValue& perObjectiveValue) {
  // TODO: Add checks here that may be relevent in the future
  for (auto& [objectiveIndex, _] : *perObjectiveValue.objectiveIndexToValue()) {
    checkNonNegativeValue(
        objectiveIndex,
        "perObjectiveValue.objectiveIndexToValue.objectiveIndex");
  }
}

void ProblemChecker::check(
    const interface::MultiObjectiveSolveSettings& multiObjSettings) {
  if (multiObjSettings.firstObjectiveIdx()) {
    checkNonNegativeValue(
        *multiObjSettings.firstObjectiveIdx(),
        "MultiObjectiveSolveSettings.firstObjectiveIdx");
  }

  if (multiObjSettings.lastObjectiveIdx()) {
    checkNonNegativeValue(
        *multiObjSettings.lastObjectiveIdx(),
        "MultiObjectiveSolveSettings.lastObjectiveIdx");
  }

  if (multiObjSettings.firstObjectiveIdx() &&
      multiObjSettings.lastObjectiveIdx()) {
    if (*multiObjSettings.firstObjectiveIdx() >
        *multiObjSettings.lastObjectiveIdx()) {
      throw std::runtime_error(
          fmt::format(
              "Expected MultiObjectiveSolveSettings.firstObjectiveIdx <= MultiObjectiveSolveSettings.lastObjectiveIdx, but got firstObjectiveIdx={} and lastObjectiveIdx={}",
              *multiObjSettings.firstObjectiveIdx(),
              *multiObjSettings.lastObjectiveIdx()));
    }
  }

  if (multiObjSettings.higherPriorityObjConfig().has_value()) {
    auto& config = *multiObjSettings.higherPriorityObjConfig();
    for (auto& [tuplePos, allowedWorsening] :
         *config.tuplePosToAllowedWorsening()) {
      checkNonNegativeValue(
          tuplePos,
          "tuplePos in higherPriorityObjConfig.tuplePosToAllowedWorsening()");
      check(allowedWorsening);
    }
  }

  if (multiObjSettings.paramNamesToValues().has_value()) {
    for (const auto& [paramName, value] :
         *multiObjSettings.paramNamesToValues()) {
      check(value);
    }
  }
}

void ProblemChecker::check(
    const interface::GroupUtilizationBound& groupUtilizationBound,
    const std::string& scope) const {
  auto& partitionName = *groupUtilizationBound.partitionName();
  checkPartitionExists(partitionName);
  checkLimitForGroups(
      scope, partitionName, *groupUtilizationBound.perGroupValues());
  if (auto scopeName = groupUtilizationBound.aggregationScope()) {
    checkScopeExists(*scopeName);
  }
}

void ProblemChecker::check(
    const interface::GroupBundleUtilization& bundleUtilization,
    const interface::CapacitySpec& spec) const {
  checkPartitionExists(*bundleUtilization.partitionName());
  const auto& scope = *bundleUtilization.scope();
  checkScopeExists(scope);
  if (*spec.definition() != CapacitySpecDefinition::AFTER) {
    throw std::runtime_error(
        fmt::format(
            "GroupBundleUtilization requires CapacitySpec.definition AFTER, got {}",
            apache::thrift::util::enumNameSafe(*spec.definition())));
  }
  if (*spec.zeroAllowed()) {
    throw std::runtime_error(
        "GroupBundleUtilization is not supported with CapacitySpec.zeroAllowed");
  }
  if (bundleUtilization.bundleSizes()->empty()) {
    throw std::runtime_error(
        "GroupBundleUtilization.bundleSizes must not be empty");
  }
  for (const auto& [scopeItem, bundleSize] : *bundleUtilization.bundleSizes()) {
    checkScopeItemExists(scope, scopeItem);
    if (!std::isfinite(bundleSize) || bundleSize <= 0) {
      throw std::runtime_error(
          fmt::format(
              "GroupBundleUtilization.bundleSizes[{}] must be positive and finite, got {}",
              scopeItem,
              bundleSize));
    }
  }
}

} // namespace facebook::rebalancer::interface
