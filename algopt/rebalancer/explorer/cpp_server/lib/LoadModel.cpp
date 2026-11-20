// (c) Facebook, Inc. and its affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/LoadModel.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/interface/standalone/BackwardCompatabilityUtils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/materializer/Materializer.h"
#include "algopt/rebalancer/solver/solvers/SolverFactory.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "rebalancer/explorer/cpp_server/lib/Utils.h"
#include "rebalancer/explorer/if/gen-cpp2/explorer_types.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/String.h>
#include <folly/system/HardwareConcurrency.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace facebook::rebalancer::explorer {

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace {

static const std::string kEmptyString;
static const std::string kDefaultRowName = "default";

std::string makeScalarDimensionName(
    const std::string& dimensionName,
    const int scalarCount,
    const int scalarIndex) {
  return scalarCount > 1 ? fmt::format("{}_{}", dimensionName, scalarIndex)
                         : dimensionName;
}

struct DynamicObjectDimensionColumns {
  std::shared_ptr<const Column> source;
  std::shared_ptr<const Column> destination;
};

using ObjectIdToContainerId = std::vector<ContainerId>;

struct ObjectAssignments {
  ObjectIdToContainerId objectIdToInitialContainerId;
  ObjectIdToContainerId objectIdToFinalContainerId;
};

struct NamedTableBuildTask {
  std::string tableName;
  folly::coro::Task<Table> buildTask;
};

struct TableStoreBuildResult {
  TableStore tableStore;
  std::vector<std::string> dynamicDimensionNames;
};

ObjectIdToContainerId buildObjectIdToContainerId(
    const Map<ContainerId, std::vector<ObjectId>>& containerIdToObjectIds,
    const std::size_t objectCount) {
  if (containerIdToObjectIds.empty()) {
    return {};
  }

  // A nonempty assignment contains every object exactly once, so every entry
  // is overwritten below.
  ObjectIdToContainerId objectIdToContainerId(
      objectCount, containerIdToObjectIds.begin()->first);
  for (const auto& [containerId, objectIds] : containerIdToObjectIds) {
    for (const auto objectId : objectIds) {
      objectIdToContainerId.at(objectId.asIndex()) = containerId;
    }
  }
  return objectIdToContainerId;
}

} // namespace

static EquivalenceSetsData buildEquivalenceSetData(
    const interface::EquivalenceSetInfo& equivSetInfo,
    const Universe& universe) {
  const auto& equivSets = *equivSetInfo.equivalenceSets();
  EquivalenceSetsData data;
  data.partitionName = std::string(kEquivSetPartition);
  for (const auto& equivSet : equivSets) {
    const auto& groupName = *equivSet.name();
    data.groupNames.emplace_back(groupName);
    for (const auto& objectName : *equivSet.objectNames()) {
      const auto objectId = universe.getObjectId(objectName);
      data.objectIdToGroupName.emplace(objectId, groupName);
    }
  }

  return data;
}

static Map<entities::ContainerId, std::vector<entities::ObjectId>>
parseFinalAssignment(
    const interface::AssignmentSolution& solution,
    const Universe& universe) {
  /* Parse the solution and return final assignment */
  Map<entities::ContainerId, std::vector<ObjectId>> finalAssignment;

  // Collect container and object names for quick lookup by extraneous entity
  // sanity check below.
  Set<std::string> containerNames, objectNames;
  for (auto containerId : universe.getContainers().getContainerIds()) {
    containerNames.insert(universe.getEntityName(containerId));
  }
  for (auto objectId : universe.getObjects().getObjectIds()) {
    objectNames.insert(universe.getEntityName(objectId));
  }

  // create entry for all containers
  for (auto containerId : universe.getContainers().getContainerIds()) {
    finalAssignment.emplace(containerId, std::vector<ObjectId>());
  }

  auto& objectToContainer = *solution.assignment();
  Set<entities::ObjectId> seenObjectIds;
  for (auto& [objectName, containerName] : objectToContainer) {
    if (!containerNames.contains(containerName)) {
      throw std::runtime_error(
          fmt::format("extraneous container in solution: {}", containerName));
    }
    if (!objectNames.contains(objectName)) {
      throw std::runtime_error(
          fmt::format("extraneous object in solution: {}", objectName));
    }
    auto objectId = universe.getObjectId(objectName);
    auto containerId = universe.getContainerId(containerName);
    finalAssignment[containerId].push_back(objectId);

    seenObjectIds.emplace(objectId);
  }

  // Look for missing objects in solution.
  for (auto objectId : universe.getObjects().getObjectIds()) {
    if (!seenObjectIds.contains(objectId)) {
      throw std::runtime_error(
          fmt::format(
              "missing object in solution: {}",
              universe.getEntityName(objectId)));
    }
  }

  return finalAssignment;
}

static std::shared_ptr<Universe> buildUniverse(
    interface::AssignmentProblem& problem) {
  /* Build the universe and return the object for further processing. */
  if (problem.universe()) {
    // A run saved before useSeparatedLocalSearchPenaltyObjective existed was
    // solved with the legacy combined objective; keep possiblyModify() from
    // filling in today's default so the explorer matches that solve.
    if (!apache::thrift::is_non_optional_field_set_manually_or_by_serializer(
            problem.rolloutConfig()
                ->useSeparatedLocalSearchPenaltyObjective())) {
      problem.rolloutConfig()->useSeparatedLocalSearchPenaltyObjective() =
          false;
    }
    BackwardCompatabilityUtils::possiblyModify(problem);
    return std::make_shared<Universe>(*problem.universe());
  }
  throw std::runtime_error("Universe missing in problem");
}

static std::vector<std::shared_ptr<const Column>> buildPartitionCols(
    const TableBuilder<ObjectId>& builder,
    const Universe& universe,
    const EquivalenceSetsData& eqSetsData) {
  std::vector<std::shared_ptr<const Column>> columns;
  columns.reserve(universe.getPartitionIds().size() + 1);
  for (const auto partitionId : universe.getPartitionIds()) {
    const auto& partition = universe.getPartition(partitionId);
    const ColumnMetadata metadata = {
        .name = universe.getEntityName(partitionId),
        .type = ColumnType::PARTITION};
    if (partition.isDisjoint()) {
      columns.push_back(builder.make(
          metadata, [&universe, &partition](const ObjectId objectId) {
            const auto* groupIds =
                folly::get_ptr(partition.getObjectIdToGroupIds(), objectId);
            return std::cref(
                groupIds && groupIds->size() == 1
                    ? universe.getEntityName(groupIds->front())
                    : kEmptyString);
          }));
    } else {
      columns.push_back(builder.make(
          metadata,
          [&universe, &partition](const ObjectId objectId) -> std::string {
            const auto* groupIds =
                folly::get_ptr(partition.getObjectIdToGroupIds(), objectId);
            if (!groupIds) {
              return std::string();
            }
            if (groupIds->size() == 1) {
              return universe.getEntityName(groupIds->front());
            }

            std::vector<std::string> groupNames;
            groupNames.reserve(groupIds->size());
            for (const auto groupId : *groupIds) {
              groupNames.push_back(universe.getEntityName(groupId));
            }
            std::sort(groupNames.begin(), groupNames.end());
            return folly::join(", ", groupNames);
          }));
    }
  }

  columns.push_back(builder.make(
      {.name = eqSetsData.partitionName, .type = ColumnType::PARTITION},
      [&eqSetsData](const ObjectId objectId) {
        const auto* groupName =
            folly::get_ptr(eqSetsData.objectIdToGroupName, objectId);
        return std::cref(groupName ? *groupName : kEmptyString);
      }));
  return columns;
}

static folly::coro::Task<Table> buildDynamicDimensionTable(
    std::shared_ptr<const Universe> universePtr,
    const DimensionId dimensionId,
    const int scalarIndex,
    std::string dimensionName) {
  const auto& universe = *universePtr;
  const auto& dimension =
      universe.getObjects().getDimension(dimensionId).at(scalarIndex);
  if (!dimension.isDynamic()) {
    throw std::runtime_error(
        fmt::format(
            "Expected to be called only for a dynamic dimension, called for {}.",
            dimensionName));
  }

  const auto scopeId = dimension.getScopeId();
  const auto& scope = universe.getScope(scopeId);
  const auto defaultValue = dimension.getDefaultValue();

  // Avoid reallocating potentially millions of rows.
  std::size_t rowCount = 1;
  for (const auto scopeItemId : scope.getScopeItemIds()) {
    rowCount +=
        dimension.values(scopeItemId)
            .visit(
                [](const ObjectIdToDoubleMap& objectValues) -> std::size_t {
                  return objectValues.nonDefaultSize();
                },
                [](const PartitionId, const GroupIdToDoubleMap& groupValues)
                    -> std::size_t { return groupValues.size(); });
  }

  std::vector<BorrowedString> entityNames;
  std::vector<BorrowedString> scopeItemNames;
  std::vector<double> values;
  entityNames.reserve(rowCount);
  scopeItemNames.reserve(rowCount);
  values.reserve(rowCount);
  const auto addRow = [&](const std::string& entityName,
                          const std::string& scopeItemName,
                          const double value) {
    entityNames.push_back(std::cref(entityName));
    scopeItemNames.push_back(std::cref(scopeItemName));
    values.push_back(value);
  };

  // Add a default row for the dimension table.
  // TODO: add a new UI component to show default value.
  // Also show an error message if dimension is too big to display, for
  // example when more than 10M values.
  addRow(kDefaultRowName, kDefaultRowName, defaultValue);
  auto entityColumnName = std::string_view(universe.getObjectTypeName());
  for (const auto scopeItemId : scope.getScopeItemIds()) {
    const auto& scopeItemName = universe.getEntityName(scopeItemId);
    dimension.values(scopeItemId)
        .visit(
            [&](const ObjectIdToDoubleMap& objectValues) {
              entityColumnName = universe.getObjectTypeName();
              objectValues.forEachNonDefault([&](const ObjectId objectId,
                                                 const double value) {
                addRow(universe.getEntityName(objectId), scopeItemName, value);
              });
            },
            [&](const PartitionId partitionId,
                const GroupIdToDoubleMap& groupValues) {
              entityColumnName = universe.getEntityName(partitionId);
              for (const auto& [groupId, value] : groupValues) {
                addRow(universe.getEntityName(groupId), scopeItemName, value);
              }
            });
  }

  const auto& scopeName = universe.getEntityName(scopeId);
  Table table(rowCount);
  table.insertColumn(
      {.name = std::string(entityColumnName),
       .type = ColumnType::ENTITY_NAME,
       .isPrimaryKey = true},
      std::move(entityNames));
  table.insertColumn(
      {.name = scopeName, .type = ColumnType::SCOPE, .isPrimaryKey = true},
      std::move(scopeItemNames));
  table.insertColumn(
      {.name = dimensionName, .type = ColumnType::DIMENSION},
      std::move(values));
  co_return table;
}

static std::vector<std::shared_ptr<const Column>>
buildStaticObjectDimensionCols(
    const TableBuilder<ObjectId>& builder,
    const entities::Universe& universe) {
  std::vector<std::shared_ptr<const Column>> columns;

  const auto& objects = universe.getObjects();
  for (const auto dimId : objects.getDimensionIds()) {
    const auto& dimName = universe.getEntityName(dimId);
    const auto& objectDimension = objects.getDimension(dimId);

    for (int i = 0; i < objectDimension.size(); i++) {
      const auto& scalarDimension = objectDimension.at(i);
      // Skip ObjectPartitionRoutingDimension to prevent an exception below.
      if (scalarDimension.isRoutingConfigBased()) {
        continue;
      }
      if (scalarDimension.isDynamic()) {
        continue;
      }
      columns.push_back(builder.make(
          {.name = makeScalarDimensionName(dimName, objectDimension.size(), i),
           .type = ColumnType::DIMENSION},
          [&scalarDimension](const ObjectId objectId) {
            return scalarDimension.getValue(objectId);
          }));
    }
  }
  return columns;
}

static ObjectAssignments buildObjectAssignments(
    const Map<ContainerId, std::vector<ObjectId>>& finalAssignment,
    const Universe& universe) {
  const auto objectCount = universe.getObjects().getObjectIds().size();
  return {
      .objectIdToInitialContainerId = buildObjectIdToContainerId(
          universe.getContainers().getInitialAssignment(), objectCount),
      .objectIdToFinalContainerId =
          buildObjectIdToContainerId(finalAssignment, objectCount)};
}

template <typename RowKey>
static std::optional<ScopeItemId> scopeItemIdForRow(
    const Scope& scope,
    RowKey rowKey) {
  if constexpr (std::is_same_v<RowKey, ScopeItemId>) {
    return rowKey;
  } else {
    static_assert(
        std::is_same_v<RowKey, ContainerId>, "Unsupported table row key");
    return scope.getScopeItemId(rowKey);
  }
}

template <typename RowKey>
static std::vector<std::shared_ptr<const Column>> buildScopeDimensionCols(
    const TableBuilder<RowKey>& builder,
    const Universe& universe,
    ScopeId scopeId) {
  const auto& scope = universe.getScope(scopeId);
  std::vector<std::shared_ptr<const Column>> columns;
  columns.reserve(scope.getDimensionIds().size());
  for (const auto dimensionId : scope.getDimensionIds()) {
    const auto& dimension = scope.getDimension(dimensionId);
    columns.push_back(builder.make(
        {.name = universe.getEntityName(dimensionId),
         .type = ColumnType::DIMENSION},
        [&dimension, &scope](const RowKey rowKey) {
          const auto scopeItemId = scopeItemIdForRow(scope, rowKey);
          return scopeItemId ? dimension.getValue(*scopeItemId)
                             : dimension.getDefaultValue();
        }));
  }
  return columns;
}

static std::vector<std::shared_ptr<const Column>> buildAssignmentCols(
    const TableBuilder<ObjectId>& builder,
    const Universe& universe,
    const ObjectAssignments& objectAssignments) {
  const auto& initialObjectIdToContainerId =
      objectAssignments.objectIdToInitialContainerId;
  const auto& finalObjectIdToContainerId =
      objectAssignments.objectIdToFinalContainerId;
  std::vector<std::shared_ptr<const Column>> columns;
  columns.reserve(2 * universe.getScopeIds().size());
  const auto buildAssignmentColumn =
      [&](const Scope& scope,
          std::string name,
          const ObjectIdToContainerId& objectIdToContainerId) {
        return builder.make(
            {.name = std::move(name), .type = ColumnType::ASSIGNMENT},
            [&](const ObjectId objectId) {
              if (objectIdToContainerId.empty()) {
                return std::cref(kEmptyString);
              }
              const auto containerId =
                  objectIdToContainerId.at(objectId.asIndex());
              const auto scopeItemId = scope.getScopeItemId(containerId);
              return std::cref(
                  scopeItemId ? universe.getEntityName(*scopeItemId)
                              : kEmptyString);
            });
      };

  for (const auto scopeId : universe.getScopeIds()) {
    const auto& scope = universe.getScope(scopeId);
    const auto& scopeName = universe.getEntityName(scopeId);
    columns.push_back(buildAssignmentColumn(
        scope, fmt::format("src.{}", scopeName), initialObjectIdToContainerId));
    columns.push_back(buildAssignmentColumn(
        scope, fmt::format("dst.{}", scopeName), finalObjectIdToContainerId));
  }
  return columns;
}

static double getRelativeUtilization(double absValue, double scopDimValue) {
  if (scopDimValue == 0) {
    return absValue * std::numeric_limits<double>::infinity();
  } else {
    return absValue / scopDimValue;
  }
}

static std::pair<Map<ScopeItemId, double>, Map<ScopeItemId, double>>
getInitialAndFinalAbsoluteUtils(
    DimensionId dimensionId,
    ScopeId scopeId,
    const Universe& universe,
    const ObjectAssignments& objectAssignments) {
  const auto& objectIdToInitialContainerId =
      objectAssignments.objectIdToInitialContainerId;
  const auto& objectIdToFinalContainerId =
      objectAssignments.objectIdToFinalContainerId;
  Map<ScopeItemId, double> scopeItemIdToInitialUtilization;
  Map<ScopeItemId, double> scopeItemIdToFinalUtilization;
  const auto& scope = universe.getScope(scopeId);

  const auto updateMax = [](auto& scopeItemIdToUtilization,
                            const auto& scopeItemIdToScalarUtilization) {
    for (const auto& [scopeItemId, scalarUtilization] :
         scopeItemIdToScalarUtilization) {
      auto [it, inserted] =
          scopeItemIdToUtilization.emplace(scopeItemId, scalarUtilization);
      if (!inserted) {
        it->second = std::max(scalarUtilization, it->second);
      }
    }
  };

  /* Calculate initial and final utilization for each scalar dimension and
   * take the max */
  const auto& objectDimension = universe.getObjects().getDimension(dimensionId);
  for (int index = 0; index < objectDimension.size(); ++index) {
    const auto& scalarDimension = objectDimension.at(index);
    if (scalarDimension.isRoutingConfigBased()) {
      throw std::runtime_error(
          "unexpected call to getInitialAndFinalAbsoluteUtils(...) with an ObjectPartitionRoutingDimension");
    }

    Map<ScopeItemId, double> scopeItemIdToInitialScalarUtilization;
    Map<ScopeItemId, double> scopeItemIdToFinalScalarUtilization;
    for (const auto objectId : universe.getObjects().getObjectIds()) {
      const auto srcContainerId =
          objectIdToInitialContainerId.at(objectId.asIndex());
      const auto srcScopeItemId = scope.getScopeItemId(srcContainerId);
      if (srcScopeItemId) {
        scopeItemIdToInitialScalarUtilization[*srcScopeItemId] +=
            scalarDimension.getValue(objectId, *srcScopeItemId);
      }

      // it is possible that the object may not be in the final assignment if
      // solution file is missing
      if (objectIdToFinalContainerId.empty()) {
        continue;
      }

      const auto dstContainerId =
          objectIdToFinalContainerId.at(objectId.asIndex());
      const auto dstScopeItemId = scope.getScopeItemId(dstContainerId);
      if (dstScopeItemId) {
        scopeItemIdToFinalScalarUtilization[*dstScopeItemId] +=
            scalarDimension.getValue(objectId, *dstScopeItemId);
      }
    }

    updateMax(
        scopeItemIdToInitialUtilization, scopeItemIdToInitialScalarUtilization);
    updateMax(
        scopeItemIdToFinalUtilization, scopeItemIdToFinalScalarUtilization);
  }
  return {
      std::move(scopeItemIdToInitialUtilization),
      std::move(scopeItemIdToFinalUtilization)};
}

static std::pair<Map<ScopeItemId, double>, Map<ScopeItemId, double>>
getScopeItemToInitialAndFinalRelativeUtils(
    DimensionId dimensionId,
    const Universe& universe,
    ScopeId scopeId,
    const ObjectAssignments& objectAssignments) {
  const auto& scope = universe.getScope(scopeId);
  auto [scopeItemIdToInitialUtilization, scopeItemIdToFinalUtilization] =
      getInitialAndFinalAbsoluteUtils(
          dimensionId, scopeId, universe, objectAssignments);

  const auto& dimension = scope.getDimension(dimensionId);
  for (const auto scopeItemId : scope.getScopeItemIds()) {
    const double scopeDimValue = dimension.getValue(scopeItemId);
    const auto initialAbsoluteUtil =
        folly::get_default(scopeItemIdToInitialUtilization, scopeItemId, 0);
    const auto finalAbsoluteUtil =
        folly::get_default(scopeItemIdToFinalUtilization, scopeItemId, 0);

    scopeItemIdToInitialUtilization[scopeItemId] =
        getRelativeUtilization(initialAbsoluteUtil, scopeDimValue);
    scopeItemIdToFinalUtilization[scopeItemId] =
        getRelativeUtilization(finalAbsoluteUtil, scopeDimValue);
  }

  return {
      std::move(scopeItemIdToInitialUtilization),
      std::move(scopeItemIdToFinalUtilization)};
}

template <typename RowKey>
static folly::coro::Task<std::vector<std::shared_ptr<const Column>>>
buildUtilizationCols(
    const TableBuilder<RowKey>& builder,
    const Universe& universe,
    ScopeId scopeId,
    const ObjectAssignments& objectAssignments,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor) {
  XLOGF(
      INFO,
      "building utilization columns for scope '{}' w.r.t. {} dimensions",
      universe.getEntityName(scopeId),
      universe.getObjects().getDimensionIds().size());

  const auto& scope = universe.getScope(scopeId);
  const auto dimensionIds = universe.getObjects().getDimensionIds();
  std::vector<std::shared_ptr<const Column>> columns;
  columns.reserve(2 * dimensionIds.size());
  const auto makeUtilizationColumn =
      [&](const std::string& name,
          const Map<ScopeItemId, double>& scopeItemIdToUtilization) {
        return builder.make(
            {.name = name, .type = ColumnType::UTILIZATION},
            [&scope, &scopeItemIdToUtilization](const RowKey rowKey) {
              const auto scopeItemId = scopeItemIdForRow(scope, rowKey);
              return scopeItemId
                  ? folly::get_default(
                        scopeItemIdToUtilization, *scopeItemId, 0.0)
                  : 0.0;
            });
      };
  co_await CoroUtils::runEachFuncAndUpdate(
      dimensionIds.begin(),
      dimensionIds.end(),
      [&](auto it)
          -> std::optional<
              std::pair<Map<ScopeItemId, double>, Map<ScopeItemId, double>>> {
        const auto dimensionId = *it;
        const auto& scalarDimension =
            universe.getObjects().getDimension(dimensionId).at(0);
        if (scalarDimension.isRoutingConfigBased() ||
            (scalarDimension.isDynamic() &&
             scalarDimension.getScopeId() != scopeId)) {
          return std::nullopt;
        }

        return getScopeItemToInitialAndFinalRelativeUtils(
            dimensionId, universe, scopeId, objectAssignments);
      },
      [&columns, &makeUtilizationColumn, &universe](
          auto&& initialAndFinalUtilMaps, auto it) {
        if (!initialAndFinalUtilMaps) {
          return;
        }

        const auto& dimensionName = universe.getEntityName(*it);
        const auto& [scopeItemIdToInitialUtilization, scopeItemIdToFinalUtilization] =
            *initialAndFinalUtilMaps;
        columns.push_back(makeUtilizationColumn(
            fmt::format("{}.initUtil", dimensionName),
            scopeItemIdToInitialUtilization));
        columns.push_back(makeUtilizationColumn(
            fmt::format("{}.finalUtil", dimensionName),
            scopeItemIdToFinalUtilization));
      },
      std::move(executor));
  co_return columns;
}

static std::vector<std::shared_ptr<const Column>> buildMovableCols(
    const TableBuilder<ObjectId>& builder,
    const Universe& universe) {
  Set<ObjectId> immovableObjectIds;
  Set<ObjectId> moveInProgressObjectIds;
  for (const auto constraintId : universe.getConstraints().getConstraintIds()) {
    const auto& constraint =
        universe.getConstraints().getConstraint(constraintId);
    const auto& spec = constraint.getSpec();
    if (spec.getType() == ConstraintSpecs::Type::movesInProgressSpec) {
      for (const auto& move : *spec.movesInProgressSpec()->moves()) {
        const auto& objectName = *move.objName();
        const auto objectId = universe.getObjectId(objectName);
        immovableObjectIds.emplace(objectId);
        moveInProgressObjectIds.emplace(objectId);
      }
    } else if (spec.getType() == ConstraintSpecs::Type::avoidMovingSpec) {
      for (const auto& objectName : *spec.avoidMovingSpec()->objects()) {
        const auto objectId = universe.getObjectId(objectName);
        immovableObjectIds.emplace(objectId);
      }
    }
  }

  std::vector<std::shared_ptr<const Column>> columns;
  columns.push_back(builder.make(
      {.name = "is_movable", .type = explorer::ColumnType::INTEGER},
      [&immovableObjectIds](const ObjectId objectId) {
        return !immovableObjectIds.contains(objectId);
      }));

  if (!moveInProgressObjectIds.empty()) {
    columns.push_back(builder.make(
        {.name = "has_move_in_progress",
         .type = explorer::ColumnType::INTEGER,
         .description =
             "1 when object is part of a MovesInProgressSpec, 0 otherwise"},
        [&moveInProgressObjectIds](const ObjectId objectId) {
          return moveInProgressObjectIds.contains(objectId);
        }));
  }
  return columns;
}

static std::shared_ptr<const Column> buildObjectCol(
    const TableBuilder<ObjectId>& builder,
    const Universe& universe) {
  return builder.make(
      {.name = universe.getObjectTypeName(),
       .type = ColumnType::ENTITY_NAME,
       .isPrimaryKey = true},
      [&universe](const ObjectId objectId) {
        return std::cref(universe.getEntityName(objectId));
      });
}

static std::shared_ptr<const Column> buildContainerScopeColumn(
    const TableBuilder<ContainerId>& builder,
    const Universe& universe,
    ScopeId scopeId) {
  const auto& scope = universe.getScope(scopeId);
  const auto& scopeName = universe.getEntityName(scopeId);
  const auto isContainer = scopeName == universe.getContainerTypeName();
  return builder.make(
      {.name = scopeName,
       .type = isContainer ? ColumnType::ENTITY_NAME : ColumnType::SCOPE,
       .isPrimaryKey = isContainer},
      [&scope, &universe](const ContainerId containerId) {
        const auto scopeItemId = scope.getScopeItemId(containerId);
        return std::cref(
            scopeItemId ? universe.getEntityName(*scopeItemId) : kEmptyString);
      });
}

static std::shared_ptr<const Column> buildScopeNameColumn(
    const TableBuilder<ScopeItemId>& builder,
    const Universe& universe,
    const ScopeId scopeId) {
  return builder.make(
      {.name = universe.getEntityName(scopeId),
       .type = ColumnType::ENTITY_NAME,
       .isPrimaryKey = true},
      [&universe](const ScopeItemId scopeItemId) {
        return std::cref(universe.getEntityName(scopeItemId));
      });
}

static folly::coro::Task<Table> buildContainerTable(
    std::shared_ptr<const Universe> universePtr,
    std::shared_ptr<const ObjectAssignments> objectAssignments,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor) {
  const auto& universe = *universePtr;
  const auto containerIdsView = universe.getContainers().getContainerIds();
  const std::vector<ContainerId> containerIds(
      containerIdsView.begin(), containerIdsView.end());
  TableBuilder<ContainerId> builder(containerIds);
  for (const auto scopeId : universe.getScopeIds()) {
    builder.add(buildContainerScopeColumn(builder, universe, scopeId));
  }

  const auto containerScopeId =
      universe.getScopeId(universe.getContainerTypeName());
  builder.addSorted(
      buildScopeDimensionCols(builder, universe, containerScopeId));
  builder.addSorted(
      co_await buildUtilizationCols(
          builder,
          universe,
          containerScopeId,
          *objectAssignments,
          std::move(executor)));
  co_return builder.build();
}

static folly::coro::Task<Table> buildScopeTable(
    std::shared_ptr<const Universe> universePtr,
    const ScopeId scopeId,
    std::shared_ptr<const ObjectAssignments> objectAssignments,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor) {
  const auto& universe = *universePtr;
  const auto& scopeItemIds = universe.getScope(scopeId).getScopeItemIds();
  TableBuilder<ScopeItemId> builder(scopeItemIds);
  builder.add(buildScopeNameColumn(builder, universe, scopeId));
  builder.addSorted(buildScopeDimensionCols(builder, universe, scopeId));
  builder.addSorted(
      co_await buildUtilizationCols(
          builder, universe, scopeId, *objectAssignments, std::move(executor)));
  co_return builder.build();
}
// copy-pasted from problemsolverfactoy
static int get_core_count() {
  return folly::available_concurrency();
}

static std::vector<NamedTableBuildTask> buildDynamicDimensionTableTasks(
    std::shared_ptr<const Universe> universe) {
  std::vector<NamedTableBuildTask> tasks;
  const auto& objects = universe->getObjects();
  for (const auto dimensionId : objects.getDimensionIds()) {
    const auto& dimension = objects.getDimension(dimensionId);
    const auto& dimensionName = universe->getEntityName(dimensionId);
    for (const auto scalarIndex : folly::irange(dimension.size())) {
      if (!dimension.at(scalarIndex).isDynamic()) {
        continue;
      }
      auto tableName =
          makeScalarDimensionName(dimensionName, dimension.size(), scalarIndex);
      auto buildTask = buildDynamicDimensionTable(
          universe, dimensionId, scalarIndex, tableName);
      tasks.push_back(
          {.tableName = std::move(tableName),
           .buildTask = std::move(buildTask)});
    }
  }
  return tasks;
}

static folly::coro::Task<DynamicObjectDimensionColumns>
buildDynamicObjectDimensionColumnsAsync(
    const TableBuilder<ObjectId>& builder,
    const Universe& universe,
    DimensionId dimId,
    int index,
    std::string scalarDimName,
    const ObjectAssignments& objectAssignments) {
  const auto& initialObjectIdToContainerId =
      objectAssignments.objectIdToInitialContainerId;
  const auto& finalObjectIdToContainerId =
      objectAssignments.objectIdToFinalContainerId;
  const algopt::treeprof::EventRecorder event(
      "Build dynamic object dimension cols");
  XLOG(INFO) << "Building object dimension cols for " << scalarDimName;
  const algopt::Timer timer(true);

  const ObjectDimension& dimension = universe.getObjects().getDimension(dimId);
  const ObjectScalarDimension& scalarDimension = dimension.at(index);

  const auto& scope = universe.getScope(scalarDimension.getScopeId());
  const auto defaultValue = scalarDimension.getDefaultValue();
  const auto buildColumn =
      [&](std::string name,
          const ObjectIdToContainerId& objectIdToContainerId) {
        return builder.make(
            {.name = std::move(name), .type = ColumnType::DIMENSION},
            [&](const ObjectId objectId) {
              if (objectIdToContainerId.empty()) {
                return defaultValue;
              }
              const auto containerId =
                  objectIdToContainerId.at(objectId.asIndex());
              return scalarDimension.getValue(
                  objectId, scope.getScopeItemId(containerId));
            });
      };
  auto source = buildColumn(
      fmt::format("src.{}", scalarDimName), initialObjectIdToContainerId);
  auto destination = buildColumn(
      fmt::format("dst.{}", scalarDimName), finalObjectIdToContainerId);

  XLOG(INFO) << "Built object dimension cols for " << scalarDimName << " in "
             << timer.getSeconds() << " seconds";
  co_return DynamicObjectDimensionColumns{
      .source = std::move(source), .destination = std::move(destination)};
}

static folly::coro::Task<std::vector<DynamicObjectDimensionColumns>>
buildAllDynamicObjectDimensionColumnsAsync(
    const TableBuilder<ObjectId>& builder,
    const Universe& universe,
    const ObjectAssignments& objectAssignments,
    folly::Executor* executor) {
  const algopt::treeprof::EventRecorder event(
      "Build dynamic object dimension cols async");

  std::vector<folly::coro::TaskWithExecutor<DynamicObjectDimensionColumns>>
      tasks;

  const auto& objects = universe.getObjects();
  for (const DimensionId& dimId : objects.getDimensionIds()) {
    const ObjectDimension& dimension = objects.getDimension(dimId);
    const std::string& dimName = universe.getEntityName(dimId);
    for (int i = 0; i < dimension.size(); i++) {
      const ObjectScalarDimension& scalarDimension = dimension.at(i);
      if (scalarDimension.isDynamic()) {
        auto scalarDimName =
            makeScalarDimensionName(dimName, dimension.size(), i);
        tasks.push_back(
            folly::coro::co_withExecutor(
                executor,
                buildDynamicObjectDimensionColumnsAsync(
                    builder,
                    universe,
                    dimId,
                    i,
                    std::move(scalarDimName),
                    objectAssignments)));
      }
    }
  }
  co_return co_await folly::coro::collectAllRange(std::move(tasks));
}

static folly::coro::Task<Table> buildObjectTable(
    std::shared_ptr<const Universe> universePtr,
    std::shared_ptr<const ObjectAssignments> objectAssignmentsPtr,
    std::shared_ptr<const EquivalenceSetsData> equivalenceSetsDataPtr,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor) {
  const auto& universe = *universePtr;
  const auto& objectAssignments = *objectAssignmentsPtr;
  const auto& equivalenceSetsData = *equivalenceSetsDataPtr;
  const auto objectIdsView = universe.getObjects().getObjectIds();
  const std::vector<ObjectId> objectIds(
      objectIdsView.begin(), objectIdsView.end());
  TableBuilder<ObjectId> builder(objectIds);

  builder.add(buildObjectCol(builder, universe));
  for (auto& column : buildMovableCols(builder, universe)) {
    builder.add(std::move(column));
  }
  builder.addSorted(buildStaticObjectDimensionCols(builder, universe));
  for (auto& columns : co_await buildAllDynamicObjectDimensionColumnsAsync(
           builder, universe, objectAssignments, executor.get())) {
    builder.add(std::move(columns.source)).add(std::move(columns.destination));
  }

  builder.addSorted(buildPartitionCols(builder, universe, equivalenceSetsData));
  for (auto& column :
       buildAssignmentCols(builder, universe, objectAssignments)) {
    builder.add(std::move(column));
  }
  co_return builder.build();
}

static TableStoreBuildResult buildTableStore(
    std::shared_ptr<const Universe> universe,
    std::shared_ptr<const ObjectAssignments> objectAssignments,
    std::shared_ptr<const EquivalenceSetsData> equivalenceSetsData,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor) {
  TableStore::BuildTaskMap tableNameToBuildTask;
  const auto addTable = [&tableNameToBuildTask](
                            const std::string& tableName,
                            folly::coro::Task<Table> buildTask) {
    const auto [it, inserted] =
        tableNameToBuildTask.emplace(tableName, std::move(buildTask));
    if (!inserted) {
      throw std::runtime_error(
          fmt::format("Explorer table '{}' is already defined", it->first));
    }
  };

  addTable(
      universe->getObjectTypeName(),
      buildObjectTable(
          universe, objectAssignments, equivalenceSetsData, executor));

  addTable(
      universe->getContainerTypeName(),
      buildContainerTable(universe, objectAssignments, executor));

  for (const auto scopeId : universe->getScopeIds()) {
    const auto& scopeName = universe->getEntityName(scopeId);
    if (scopeName == universe->getContainerTypeName()) {
      continue;
    }
    addTable(
        scopeName,
        buildScopeTable(universe, scopeId, objectAssignments, executor));
  }

  std::vector<std::string> dynamicDimensionNames;
  for (auto& tableBuild : buildDynamicDimensionTableTasks(universe)) {
    dynamicDimensionNames.push_back(tableBuild.tableName);
    addTable(tableBuild.tableName, std::move(tableBuild.buildTask));
  }

  return {
      .tableStore = TableStore(executor, std::move(tableNameToBuildTask)),
      .dynamicDimensionNames = std::move(dynamicDimensionNames)};
}

ExplorerModel LoadModel::buildData(interface::Bundle&& bundle) {
  auto executor = std::make_shared<folly::CPUThreadPoolExecutor>(
      get_core_count(),
      std::make_unique<folly::LifoSemMPMCQueue<
          folly::CPUThreadPoolExecutor::CPUTask,
          folly::QueueBehaviorIfFull::BLOCK>>(
          folly::CPUThreadPoolExecutor::kDefaultMaxQueueSize),
      std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
  auto wrappedExecutor =
      std::make_shared<algopt::treeprof::ExecutorWrapper>(executor);

  // Build problem, universe, and parse solution
  auto& problemSpec = *bundle.problem();
  auto universe = buildUniverse(problemSpec);
  problemSpec.universe().reset();
  auto solution = bundle.solution()
      ? std::make_optional(std::move(*bundle.solution()))
      : std::nullopt;

  Map<entities::ContainerId, std::vector<entities::ObjectId>> finalAssignment;
  if (solution) {
    finalAssignment = parseFinalAssignment(*solution, *universe);
  }

  // Materialize and build problem for bounds, equivalenceSets, etc.
  auto solver = SolverFactory::createSolver(*problemSpec.strategy());
  auto materialized = materializer::Materializer::materialize(
      wrappedExecutor,
      universe,
      solver->needs_continuous_expressions(),
      std::make_shared<LogCollector>(),
      /*shouldCollectMetrics=*/true,
      /*enableInvalidMoveFilter=*/false,
      *problemSpec.rolloutConfig()->useSeparatedLocalSearchPenaltyObjective() &&
          solver->needs_continuous_expressions());

  const ProblemConfigs problemConfigs{
      .threadPool = executor,
      .moveStatsSpec = {},
      .runId = {},
      .useDynamicObjectOrdering = true,
      .enableParallelizedBoundsComputing = true,
      .addMetricsExprsToOrchestrator = true,
  };
  auto problem =
      std::make_unique<Problem>(universe, materialized, problemConfigs);
  auto equivalenceSetsData = std::make_shared<const EquivalenceSetsData>(
      buildEquivalenceSetData(problem->makeEquivalenceSetInfo(), *universe));
  auto objectAssignments = std::make_shared<const ObjectAssignments>(
      buildObjectAssignments(finalAssignment, *universe));
  auto tableStoreBuildResult = buildTableStore(
      universe, objectAssignments, equivalenceSetsData, wrappedExecutor);

  return ExplorerModel{
      .problemSpec = std::move(problemSpec),
      .universe = std::move(universe),
      .tableStore = std::move(tableStoreBuildResult.tableStore),
      .finalAssignment = std::move(finalAssignment),
      .solution = std::move(solution),
      .materialized = std::move(materialized),
      .dynamicDimensionNames =
          std::move(tableStoreBuildResult.dynamicDimensionNames),
      .problem = std::move(problem),
      .equivalenceSetsData = std::move(equivalenceSetsData),
      .executor = std::move(wrappedExecutor),
  };
}

} // namespace facebook::rebalancer::explorer
