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

#include "algopt/rebalancer/materializer/utils/ExpressionBuilder.h"

#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectScalarDimension.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingTrafficLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"

#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/futures/Future.h>
#include <folly/hash/Hash.h>

#include <functional>
#include <optional>
#include <stdexcept>

namespace facebook::rebalancer::materializer {
namespace {

const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>
    kEmptyMap;
const std::vector<entities::ObjectId> kEmptyVector;

bool isDefaultUnbounded(std::optional<double> defaultValue, bool isUpperBound) {
  if (!defaultValue) {
    // if not specified, then we assume it is unbounded
    return true;
  }

  if (isUpperBound) {
    // when used as upperbound, any value bigger than the largest possible
    // double value denotes unbounded
    return *defaultValue >= std::numeric_limits<double>::max();
  } else {
    // when used as lowerbound, any value smaller than  zero (the smallest
    // possible utilization) denotes unbounded
    return *defaultValue <= 0;
  }
}

} // namespace

ExpressionBuilder::ExpressionBuilder(
    std::shared_ptr<const entities::Universe> universe,
    const entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>&
        updatedInitialAssignment,
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
    std::shared_ptr<Metrics::Builder> metrics)
    : universe_(std::move(universe)),
      executor_(std::move(executor)),
      metrics_(std::move(metrics)) {
  updatedInitialAssignment_ = Assignment(updatedInitialAssignment);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId) FOLLY_TS_REQUIRES(!applyfunc) {
  return getAbsoluteUtil(
      metric,
      {.dimensionId = dimensionId,
       .scopeId = scopeId,
       .scopeItemId = scopeItemId,
       .partitionId = std::nullopt,
       .groupId = std::nullopt});
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getRelativeUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId) {
  return getRelativeUtil(
      metric,
      {.dimensionId = dimensionId,
       .scopeId = scopeId,
       .scopeItemId = scopeItemId,
       .partitionId = std::nullopt,
       .groupId = std::nullopt});
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    entities::PartitionId partitionId,
    entities::GroupId groupId) FOLLY_TS_REQUIRES(!applyfunc) {
  return getAbsoluteUtil(
      metric,
      {.dimensionId = dimensionId,
       .scopeId = scopeId,
       .scopeItemId = scopeItemId,
       .partitionId = partitionId,
       .groupId = groupId});
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getRelativeUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    entities::PartitionId partitionId,
    entities::GroupId groupId) {
  return getRelativeUtil(
      metric,
      {.dimensionId = dimensionId,
       .scopeId = scopeId,
       .scopeItemId = scopeItemId,
       .partitionId = partitionId,
       .groupId = groupId});
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    int dimensionIndex) FOLLY_TS_REQUIRES(!applyfunc) {
  co_return co_await getAbsoluteUtil(
      metric,
      {.dimensionId = dimensionId,
       .scopeId = scopeId,
       .scopeItemId = scopeItemId,
       .partitionId = std::nullopt,
       .groupId = std::nullopt},
      dimensionIndex);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    const entities::ObjectScalarDimension& objectDimension,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId) FOLLY_TS_REQUIRES(!applyfunc) {
  co_return co_await getAbsoluteUtil(
      metric,
      {.dimensionId = std::nullopt,
       .scopeId = scopeId,
       .scopeItemId = scopeItemId,
       .partitionId = std::nullopt,
       .groupId = std::nullopt},
      objectDimension);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilOutOfScope(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId) FOLLY_TS_REQUIRES(!applyfunc) {
  return getAbsoluteUtil(
      metric,
      {.dimensionId = dimensionId,
       .scopeId = scopeId,
       .scopeItemId = std::nullopt,
       .partitionId = std::nullopt,
       .groupId = std::nullopt,
       .outOfScope = true});
}

// public
std::shared_ptr<GroupRoutingTrafficLookup>
ExpressionBuilder::getGroupRoutingTrafficLookup(
    entities::RoutingConfigId routingConfigId,
    entities::PartitionId partitionId,
    entities::GroupId groupId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId) {
  auto& routingConfig = universe_->getRoutingConfig(routingConfigId);
  if (routingConfig.getScopeId() != scopeId) {
    throw std::runtime_error(
        fmt::format(
            "routing config is defined on scope '{}', but routing traffic lookup requested for scope '{}",
            universe_->getEntityName(routingConfig.getScopeId()),
            universe_->getEntityName(scopeId)));
  }

  if (routingConfig.getPartitionId() != partitionId) {
    throw std::runtime_error(
        fmt::format(
            "routing config is defined on partition '{}', but routing traffic lookup requested for partition '{}",
            universe_->getEntityName(routingConfig.getPartitionId()),
            universe_->getEntityName(partitionId)));
  }

  return getGroupRoutingTrafficLookup(routingConfigId, groupId, scopeItemId);
}

std::shared_ptr<Expression> ExpressionBuilder::getAbsoluteUtilOutOfScope(
    UtilMetric metric,
    const entities::ObjectScalarDimension& objectDimension,
    entities::ScopeId scopeId) {
  // special kind of scalar dimension that is based on an objectPartition and a
  // routingConfig; Not supported with getAbsoluteUtilOutOfScope(...)
  if (objectDimension.isRoutingConfigBased()) {
    throw std::runtime_error(
        "ObjectPartitionRoutingDimension is not supported with getAbsoluteUtilOutOfScope");
  }

  if (metric != UtilMetric::AFTER) {
    throw std::runtime_error("util metric not supported");
  }
  auto objectVector = getObjectVector(objectDimension);
  return getObjectLookupOutOfScope(scopeId, objectVector);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    const std::vector<entities::ScopeItemId>& scopeItemIds,
    std::optional<entities::PartitionId> partitionId,
    std::optional<entities::GroupId> groupId) FOLLY_TS_REQUIRES(!applyfunc) {
  if (scopeItemIds.size() == 0) {
    co_return const_expr(0, *universe_);
  }

  if (metric != UtilMetric::AFTER) {
    throw std::runtime_error(
        "getAbsoluteUtil() with a set of scopeItems is only supported with UtilMetric::AFTER");
  }

  auto [relevantContainerIds, disjoint] =
      getContainersIdsInScopeItems(scopeId, scopeItemIds);
  if (scopeItemIds.size() == 1 || !disjoint) {
    //  if there is only one scopeItemId or the sets of containers of scopeItems
    //  is not disjoint, then use the corresponding getAbsoluteUtil() as they
    //  are cached; the 'not disjoint' case can be optimzed further if required
    auto totalUtil = const_expr(0, *universe_);
    if (partitionId.has_value() && groupId.has_value()) {
      for (auto scopeItemId : scopeItemIds) {
        totalUtil += co_await getAbsoluteUtil(
            UtilMetric::AFTER,
            dimensionId,
            scopeId,
            scopeItemId,
            partitionId.value(),
            groupId.value());
      }
    } else {
      for (auto scopeItemId : scopeItemIds) {
        totalUtil += co_await getAbsoluteUtil(
            UtilMetric::AFTER, dimensionId, scopeId, scopeItemId);
      }
    }
    co_return totalUtil;
  }

  auto& objectDimension = universe_->getObjects().getDimension(dimensionId);
  if (objectDimension.size() > 1 || objectDimension.isDynamic() ||
      objectDimension.at(0).isRoutingConfigBased()) {
    throw std::runtime_error(
        "getAbsoluteUtilOverScopeItems() is not supported with a) dynamic object dimensions, b) when object dimension size is greater than 1, c) an ObjectPartitionRoutingDimension");
  }

  auto objectVector = (partitionId.has_value() && groupId.has_value())
      ? getObjectVector(dimensionId, 0, partitionId.value(), groupId.value())
      : getObjectVector(dimensionId, 0);

  co_return object_lookup(
      objectVector, relevantContainerIds, updatedInitialAssignment_);
}

double ExpressionBuilder::getUpperBound(const Expression& expression) {
  const folly::AnnotatedLockGuard lock(applyfunc);
  return expression.lowerAndUpperBounds(context_).upper_bound;
}

double ExpressionBuilder::getLowerBound(const Expression& expression) {
  const folly::AnnotatedLockGuard lock(applyfunc);
  return expression.lowerAndUpperBounds(context_).lower_bound;
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    Descriptor descriptor) FOLLY_TS_REQUIRES(!applyfunc) {
  auto key = std::make_tuple(metric, descriptor);

  co_return co_await absoluteUtilCache_.getSavedOrCompute(
      key, [&, this]() -> folly::coro::Task<ExprPtr> {
        auto& objectDimension = universe_->getObjects().getDimension(
            descriptor.dimensionId.value());

        ExprPtr absoluteUtil;
        if (objectDimension.size() == 1) {
          absoluteUtil = co_await getAbsoluteUtil(metric, descriptor, 0);
        } else {
          std::vector<std::shared_ptr<Expression>> scalarUtils;
          scalarUtils.reserve(objectDimension.size());
          for (const auto i : folly::irange(objectDimension.size())) {
            scalarUtils.push_back(
                co_await getAbsoluteUtil(metric, descriptor, i));
          }
          absoluteUtil = max(scalarUtils, *universe_);
        }

        if (metrics_ && descriptor.outOfScope == false) {
          metrics_->addToUtilCollection(absoluteUtil, metric, descriptor);
        }

        co_return absoluteUtil;
      });
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getRelativeUtil(
    UtilMetric metric,
    Descriptor descriptor) {
  auto key = std::make_tuple(metric, descriptor);

  co_return co_await relativeUtilCache_.getSavedOrCompute(
      key, [&, this]() -> folly::coro::Task<ExprPtr> {
        const double scopeItemCapacity =
            universe_->getScope(descriptor.scopeId)
                .getDimension(descriptor.dimensionId.value())
                .getValue(descriptor.scopeItemId.value());
        if (scopeItemCapacity == 0) {
          throw std::runtime_error(
              fmt::format(
                  "capacity of {} {} on dimension {} is zero",
                  universe_->getEntityName(descriptor.scopeId),
                  universe_->getEntityName(descriptor.scopeItemId.value()),
                  universe_->getEntityName(descriptor.dimensionId.value())));
        }

        auto absoluteUtil = co_await getAbsoluteUtil(metric, descriptor);
        co_return absoluteUtil / scopeItemCapacity;
      });
}

ExprPtr ExpressionBuilder::maybeApplyBoundsOverrideForDuringExpr(
    ExprPtr duringExpr,
    const entities::ObjectScalarDimension& dimension) {
  if (dimension.hasNegativeValues()) {
    return duringExpr;
  } else {
    const auto duringLb = duringExpr->getInitialValue();
    return boundsOverride(std::move(duringExpr), duringLb, /*ub=*/std::nullopt);
  }
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    const Descriptor& descriptor,
    const entities::ObjectPartitionRoutingDimension&
        objectPartitionRoutingDimension) {
  if (metric != UtilMetric::AFTER && metric != UtilMetric::DURING) {
    throw std::runtime_error(
        "Using an ObjectPartitionRoutingDimension is currently only supported when using AFTER or DURING definition");
  }
  if (descriptor.outOfScope || (!descriptor.scopeItemId.has_value())) {
    throw std::runtime_error(
        "Using an ObjectPartitionRoutingDimension is currently NOT supported when either the outOfScope is set in the descriptor OR when scopeItemId is not set");
  }

  auto routingConfigId = objectPartitionRoutingDimension.getRoutingConfigId();
  auto& routingConfig = universe_->getRoutingConfig(routingConfigId);
  auto routingConfigScopeId = routingConfig.getScopeId();

  // 'Universe' already checks that the routingConfig and
  // objectPartitionRoutingDimension are defined on the same partition. Here we
  // need to check that the scopeId in routingConfig is the same as the scopeId
  // absoluteUtil is requested on.
  if (descriptor.scopeId != routingConfigScopeId) {
    throw std::runtime_error(
        fmt::format(
            "Absolute util is requested on scope '{}', but routingConfig is defined on scope '{}'",
            universe_->getEntityName(descriptor.scopeId),
            universe_->getEntityName(routingConfigScopeId)));
  }

  // we expect both to be set
  if (descriptor.partitionId.has_value() ^ descriptor.groupId.has_value()) {
    throw std::runtime_error("Expected both partitionId and groupId to be set");
  }

  auto getGroupUtil = [&](entities::RoutingConfigId routingConfigId,
                          entities::GroupId groupId) {
    auto totalFractionOfTrafficToScopeItem = getGroupRoutingTrafficLookup(
        routingConfigId, groupId, descriptor.scopeItemId.value());
    const auto groupValue = objectPartitionRoutingDimension.getValue(groupId);
    auto groupUtil = totalFractionOfTrafficToScopeItem * groupValue;
    if (metric == UtilMetric::DURING) {
      const auto initialFractionOfTrafficToScopeItem =
          totalFractionOfTrafficToScopeItem->getInitialValue();
      const auto initialGroupUtil =
          initialFractionOfTrafficToScopeItem * groupValue;
      groupUtil = max(groupUtil, initialGroupUtil);
    }
    groupUtil += step(totalFractionOfTrafficToScopeItem) *
        objectPartitionRoutingDimension.getStaticValue(groupId);
    if (metrics_) {
      // we need to create a copy of the descriptor, since we need to set
      // groupId and partitionId
      auto descriptorKey = descriptor;
      descriptorKey.groupId = groupId;
      descriptorKey.partitionId =
          objectPartitionRoutingDimension.getPartitionId();
      metrics_->addToUtilCollection(groupUtil, metric, descriptorKey);
    }
    return groupUtil;
  };

  if (descriptor.partitionId.has_value() && descriptor.groupId.has_value()) {
    auto requestedPartitionId = descriptor.partitionId.value();
    auto groupId = descriptor.groupId.value();
    auto objectPartitionRoutingDimPartitionId =
        objectPartitionRoutingDimension.getPartitionId();
    if (objectPartitionRoutingDimPartitionId != requestedPartitionId) {
      throw std::runtime_error(
          fmt::format(
              "Absolute util is requested on partition '{}', but ObjectPartitionRoutingDimension is defined on partition '{}'",
              universe_->getEntityName(requestedPartitionId),
              universe_->getEntityName(objectPartitionRoutingDimPartitionId)));
    }

    co_return getGroupUtil(routingConfigId, groupId);
  }

  auto totalUtil = const_expr(0, *universe_);
  for (auto& [groupId, _] : routingConfig.getGroupToRoutingRings()) {
    inplace_add(totalUtil, getGroupUtil(routingConfigId, groupId));
  }

  co_return totalUtil;
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilAfter(
    const Descriptor& descriptor,
    int dimensionIndex) {
  auto key = std::make_tuple(descriptor, dimensionIndex);

  co_return co_await absoluteUtilAfterCache_.getSavedOrCompute(
      key, [&, this]() -> folly::coro::Task<ExprPtr> {
        auto& objectDimension =
            universe_->getObjects()
                .getDimension(descriptor.dimensionId.value())
                .at(dimensionIndex);
        if (objectDimension.isDynamic()) {
          co_return co_await getAbsoluteUtilAfterDynamic(
              descriptor, dimensionIndex);
        }
        if (descriptor.outOfScope) {
          co_return getObjectLookupOutOfScope(
              descriptor.scopeId,
              getObjectVector(descriptor.dimensionId.value(), dimensionIndex));
        }
        auto objectVector = descriptor.partitionId.has_value()
            ? getObjectVector(
                  descriptor.dimensionId.value(),
                  dimensionIndex,
                  descriptor.partitionId.value(),
                  descriptor.groupId.value())
            : getObjectVector(descriptor.dimensionId.value(), dimensionIndex);

        co_return getObjectLookup(
            descriptor.scopeId,
            descriptor.scopeItemId.value(),
            std::move(objectVector));
      });
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilAfter(
    const Descriptor& descriptor,
    const entities::ObjectScalarDimension& objectDimension) {
  auto objectVector = getObjectVector(objectDimension);
  co_return getObjectLookup(
      descriptor.scopeId,
      descriptor.scopeItemId.value(),
      std::move(objectVector));
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilAfterDynamic(
    const Descriptor& descriptor,
    int dimensionIndex) {
  const auto& objectDimension =
      universe_->getObjects()
          .getDimension(descriptor.dimensionId.value())
          .at(dimensionIndex);

  const auto& scopeImage = descriptor.outOfScope
      ? getOutOfScopeImage(objectDimension.getScopeId(), descriptor.scopeId)
      : getScopeImage(
            objectDimension.getScopeId(),
            descriptor.scopeId,
            descriptor.scopeItemId.value());

  auto result = co_await CoroUtils::runEachAndGetAccumulatedWithBatching(
      scopeImage.inScope,
      [&](const auto& it) -> ExprPtr {
        const auto& [dimensionScopeItemId, containerIds] = *it;
        if (containerIds == nullptr) {
          return const_expr(0, *universe_);
        }

        auto objectVector = descriptor.partitionId.has_value()
            ? getObjectVector(
                  descriptor.dimensionId.value(),
                  dimensionIndex,
                  descriptor.partitionId.value(),
                  descriptor.groupId.value(),
                  dimensionScopeItemId)
            : getObjectVector(
                  descriptor.dimensionId.value(),
                  dimensionIndex,
                  dimensionScopeItemId);

        return getObjectLookup(containerIds, std::move(objectVector));
      },
      [&](ExprPtr& accumulator, const ExprPtr& partial) {
        accumulator += partial;
      },
      executor_);

  if (scopeImage.outOfScope) {
    auto objectVector = descriptor.partitionId.has_value()
        ? getObjectVector(
              descriptor.dimensionId.value(),
              dimensionIndex,
              descriptor.partitionId.value(),
              descriptor.groupId.value(),
              std::nullopt)
        : getObjectVector(
              descriptor.dimensionId.value(), dimensionIndex, std::nullopt);

    result += getObjectLookup(scopeImage.outOfScope, std::move(objectVector));
  }

  if (!result) {
    co_return const_expr(0, *universe_);
  }

  co_return object_lookup_dynamic(result, objectDimension);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilInitial(
    const Descriptor& descriptor,
    int dimensionIndex) {
  auto key = std::make_tuple(descriptor, dimensionIndex);

  co_return co_await absoluteUtilInitialCache_.getSavedOrCompute(
      key, [&, this]() -> folly::coro::Task<ExprPtr> {
        auto& objectDimension =
            universe_->getObjects()
                .getDimension(descriptor.dimensionId.value())
                .at(dimensionIndex);
        co_return co_await getAbsoluteUtilInitial(descriptor, objectDimension);
      });
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilInitial(
    const Descriptor& descriptor,
    const entities::ObjectScalarDimension& objectDimension) {
  auto& containerIds = descriptor.outOfScope
      ? *getContainersOutOfScopePtr(descriptor.scopeId)
      : universe_->getScope(descriptor.scopeId)
            .getContainerIds(descriptor.scopeItemId.value());
  auto objectDimensionScope = objectDimension.isDynamic()
      ? std::make_optional(
            std::cref(universe_->getScope(objectDimension.getScopeId())))
      : std::nullopt;

  double util = 0;
  for (const auto containerId : containerIds) {
    const auto objectDimensionScopeItemId = objectDimensionScope.has_value()
        ? objectDimensionScope->get().getScopeItemId(containerId)
        : std::nullopt;
    auto& objectIds = descriptor.partitionId.has_value()
        ? getInitialObjects(
              descriptor.partitionId.value(),
              descriptor.groupId.value(),
              containerId)
        : universe_->getContainers().getInitialObjectIds(containerId);
    util += objectDimension.values(objectDimensionScopeItemId).sum(objectIds);
  }
  co_return const_expr(util, *universe_);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilStayed(
    const Descriptor& descriptor,
    int dimensionIndex) {
  auto key = std::make_tuple(descriptor, dimensionIndex);

  co_return co_await absoluteUtilStayedCache_.getSavedOrCompute(
      key, [&, this]() -> folly::coro::Task<ExprPtr> {
        auto initialObjectVector =
            getInitialObjectVector(descriptor, dimensionIndex);
        auto fullObjectVector =
            getInitialObjectVectorFull(descriptor, dimensionIndex);
        co_return co_await getAbsoluteUtilStayed(
            descriptor,
            std::move(initialObjectVector),
            std::move(fullObjectVector));
      });
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilStayed(
    const Descriptor& descriptor,
    std::shared_ptr<ObjectVector> initialObjectVector,
    std::shared_ptr<ObjectVector> fullObjectVector) {
  if (universe_->getStableOptimization()) {
    if (!fullObjectVector) {
      throw std::runtime_error(
          "full object vector is required for stable stayed");
    }
    co_return descriptor.outOfScope
        ? getStableStayedOutOfScope(
              descriptor.scopeId, initialObjectVector, fullObjectVector)
        : getStableStayed(
              descriptor.scopeId,
              descriptor.scopeItemId.value(),
              initialObjectVector,
              fullObjectVector);
  }
  co_return descriptor.outOfScope
      ? getObjectLookupOutOfScope(descriptor.scopeId, initialObjectVector)
      : getObjectLookup(
            descriptor.scopeId,
            descriptor.scopeItemId.value(),
            initialObjectVector);
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtilStayed(
    const Descriptor& descriptor,
    const entities::ObjectScalarDimension& objectDimension) {
  auto initialObjectVector =
      getInitialObjectVector(descriptor, objectDimension);
  auto fullObjectVector =
      getInitialObjectVectorFull(descriptor, objectDimension);
  co_return co_await getAbsoluteUtilStayed(
      descriptor, std::move(initialObjectVector), std::move(fullObjectVector));
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getObjectVector(
    entities::DimensionId dimensionId,
    int dimensionIndex) {
  auto key = std::make_tuple(dimensionId, dimensionIndex);

  return objectVectorCache_.getSavedOrCompute(key, [&]() {
    auto& objectDimension =
        universe_->getObjects().getDimension(dimensionId).at(dimensionIndex);
    return getObjectVector(objectDimension);
  });
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getObjectVector(
    entities::DimensionId dimensionId,
    int dimensionIndex,
    entities::PartitionId partitionId,
    entities::GroupId groupId) {
  auto key = std::make_tuple(dimensionId, dimensionIndex, partitionId, groupId);

  return objectVectorWithGroupCache_.getSavedOrCompute(key, [&]() {
    const auto& objectDimension =
        universe_->getObjects().getDimension(dimensionId).at(dimensionIndex);
    return object_vector(
        objectDimension.values().sliceGroup(
            universe_->getPartition(partitionId), groupId),
        *universe_);
  });
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getObjectVector(
    entities::DimensionId dimensionId,
    int dimensionIndex,
    std::optional<entities::ScopeItemId> scopeItemId) {
  auto key = std::make_tuple(dimensionId, dimensionIndex, scopeItemId);

  return objectVectorDynamicCache_.getSavedOrCompute(key, [&]() {
    auto& objectDimension =
        universe_->getObjects().getDimension(dimensionId).at(dimensionIndex);
    return getObjectVector(objectDimension, scopeItemId);
  });
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getObjectVector(
    entities::DimensionId dimensionId,
    int dimensionIndex,
    entities::PartitionId partitionId,
    entities::GroupId groupId,
    std::optional<entities::ScopeItemId> scopeItemId) {
  auto key = std::make_tuple(
      dimensionId, dimensionIndex, partitionId, groupId, scopeItemId);

  return objectVectorDynamicWithGroupCache_.getSavedOrCompute(key, [&]() {
    const auto& objectDimension =
        universe_->getObjects().getDimension(dimensionId).at(dimensionIndex);
    return object_vector(
        objectDimension.values(scopeItemId)
            .sliceGroup(universe_->getPartition(partitionId), groupId),
        *universe_);
  });
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getObjectVector(
    const entities::ObjectScalarDimension& objectDimension) {
  return object_vector(objectDimension.values(), *universe_);
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getObjectVector(
    const entities::ObjectScalarDimension& objectDimension,
    std::optional<entities::ScopeItemId> scopeItemId) {
  const auto numObjects = universe_->getNumObjects();
  const double defaultObjectValue = objectDimension.getDefaultValue();
  if (!scopeItemId) {
    return object_vector(
        std::make_shared<entities::ObjectIdToDoubleMap>(
            numObjects,
            defaultObjectValue,
            /*expectedNonDefaultSize=*/0),
        *universe_);
  }
  return object_vector(objectDimension.values(*scopeItemId), *universe_);
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getInitialObjectVector(
    const Descriptor& descriptor,
    int dimensionIndex) {
  auto& objectDimension = universe_->getObjects()
                              .getDimension(descriptor.dimensionId.value())
                              .at(dimensionIndex);
  return getInitialObjectVector(descriptor, objectDimension);
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getInitialObjectVector(
    const Descriptor& descriptor,
    const entities::ObjectScalarDimension& objectDimension) {
  auto& containerIds = descriptor.outOfScope
      ? *getContainersOutOfScopePtr(descriptor.scopeId)
      : universe_->getScope(descriptor.scopeId)
            .getContainerIds(descriptor.scopeItemId.value());
  auto objectDimensionScope = objectDimension.isDynamic()
      ? std::make_optional(
            std::cref(universe_->getScope(objectDimension.getScopeId())))
      : std::nullopt;

  const auto numObjects = universe_->getNumObjects();
  std::size_t initialObjectCount = 0;
  for (const auto containerId : containerIds) {
    initialObjectCount += descriptor.partitionId.has_value()
        ? getInitialObjects(
              descriptor.partitionId.value(),
              descriptor.groupId.value(),
              containerId)
              .size()
        : universe_->getContainers().getInitialObjectIds(containerId).size();
  }

  auto objectToInitialValue = std::make_shared<entities::ObjectIdToDoubleMap>(
      numObjects,
      /*defaultValue=*/0.0,
      /*expectedNonDefaultSize=*/initialObjectCount);
  for (const auto containerId : containerIds) {
    const auto objectDimensionScopeItemId = objectDimensionScope.has_value()
        ? objectDimensionScope->get().getScopeItemId(containerId)
        : std::nullopt;
    const auto& objectIds = descriptor.partitionId.has_value()
        ? getInitialObjects(
              descriptor.partitionId.value(),
              descriptor.groupId.value(),
              containerId)
        : universe_->getContainers().getInitialObjectIds(containerId);
    const auto& dimensionValues =
        objectDimension.values(objectDimensionScopeItemId);
    for (const auto objectId : objectIds) {
      objectToInitialValue->emplace(
          objectId, dimensionValues.getObjectValue(objectId));
    }
  }

  return object_vector(std::move(objectToInitialValue), *universe_);
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getInitialObjectVectorFull(
    const Descriptor& descriptor,
    int dimensionIndex) {
  if (universe_->getStableOptimization()) {
    auto& objectDimension = universe_->getObjects()
                                .getDimension(descriptor.dimensionId.value())
                                .at(dimensionIndex);
    if (objectDimension.isDynamic()) {
      throw std::runtime_error(
          "Stable optimization with dynamic dimensions is not supported");
    }
    return descriptor.partitionId.has_value()
        ? getObjectVector(
              descriptor.dimensionId.value(),
              dimensionIndex,
              descriptor.partitionId.value(),
              descriptor.groupId.value())
        : getObjectVector(descriptor.dimensionId.value(), dimensionIndex);
  }
  return nullptr;
}

std::shared_ptr<ObjectVector> ExpressionBuilder::getInitialObjectVectorFull(
    [[maybe_unused]] const Descriptor& descriptor,
    const entities::ObjectScalarDimension& objectDimension) {
  if (universe_->getStableOptimization()) {
    if (objectDimension.isDynamic()) {
      throw std::runtime_error(
          "Stable optimization with dynamic dimensions is not supported");
    }
    return getObjectVector(objectDimension);
  }
  return nullptr;
}

std::shared_ptr<ObjectLookup> ExpressionBuilder::getObjectLookup(
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    std::shared_ptr<ObjectVector> objectVector) {
  return getObjectLookup(
      universe_->getScope(scopeId).getContainerIdsPtr(scopeItemId),
      std::move(objectVector));
}

std::shared_ptr<ObjectLookup> ExpressionBuilder::getObjectLookup(
    std::shared_ptr<const PackerSet<entities::ContainerId>> containerIds,
    std::shared_ptr<ObjectVector> objectVector) {
  return object_lookup(
      std::move(objectVector),
      std::move(containerIds),
      updatedInitialAssignment_);
}

std::shared_ptr<ObjectLookup> ExpressionBuilder::getObjectLookupOutOfScope(
    entities::ScopeId scopeId,
    std::shared_ptr<ObjectVector> objectVector) {
  return object_lookup(
      std::move(objectVector),
      getContainersOutOfScopePtr(scopeId),
      updatedInitialAssignment_);
}

std::shared_ptr<StableStayed> ExpressionBuilder::getStableStayed(
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    std::shared_ptr<ObjectVector> initialObjectVector,
    std::shared_ptr<ObjectVector> fullObjectVector) {
  return getStableStayed(
      universe_->getScope(scopeId).getContainerIdsPtr(scopeItemId),
      std::move(initialObjectVector),
      std::move(fullObjectVector));
}

std::shared_ptr<StableStayed> ExpressionBuilder::getStableStayed(
    std::shared_ptr<const PackerSet<entities::ContainerId>> containerIds,
    std::shared_ptr<ObjectVector> initialObjectVector,
    std::shared_ptr<ObjectVector> fullObjectVector) {
  return stable_stayed(
      std::move(initialObjectVector),
      std::move(fullObjectVector),
      std::move(containerIds),
      *universe_,
      updatedInitialAssignment_);
}

std::shared_ptr<StableStayed> ExpressionBuilder::getStableStayedOutOfScope(
    entities::ScopeId scopeId,
    std::shared_ptr<ObjectVector> initialObjectVector,
    std::shared_ptr<ObjectVector> fullObjectVector) {
  return getStableStayed(
      getContainersOutOfScopePtr(scopeId),
      std::move(initialObjectVector),
      std::move(fullObjectVector));
}

const std::vector<entities::ObjectId>& ExpressionBuilder::getInitialObjects(
    entities::PartitionId partitionId,
    entities::GroupId groupId,
    entities::ContainerId containerId) {
  auto& containerToObjects = folly::get_ref_default(
      getInitialObjects(partitionId), groupId, kEmptyMap);
  return folly::get_ref_default(containerToObjects, containerId, kEmptyVector);
}

const entities::Map<
    entities::GroupId,
    entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>>&
ExpressionBuilder::getInitialObjects(entities::PartitionId partitionId) {
  return initialObjectsCache_.getSavedOrCompute(partitionId, [&]() {
    auto initialObjectsPtr = std::make_shared<entities::Map<
        entities::GroupId,
        entities::
            Map<entities::ContainerId, std::vector<entities::ObjectId>>>>();

    auto& initialObjects = *initialObjectsPtr;

    entities::Map<entities::ObjectId, entities::ContainerId> initialContainer;
    const auto containerIds = universe_->getContainers().getContainerIds();
    for (auto containerId : containerIds) {
      auto& objectIds =
          universe_->getContainers().getInitialObjectIds(containerId);
      for (auto objectId : objectIds) {
        initialContainer.emplace(objectId, containerId);
      }
    }

    auto& groupIds = universe_->getPartition(partitionId).getGroupIds();
    for (auto groupId : groupIds) {
      auto& objectIds =
          universe_->getPartition(partitionId).getObjectIds(groupId);
      for (auto objectId : objectIds) {
        auto containerId = initialContainer.at(objectId);
        initialObjects[groupId][containerId].push_back(objectId);
      }
    }

    return initialObjects;
  });
}

std::pair<std::shared_ptr<const PackerSet<entities::ContainerId>>, bool>
ExpressionBuilder::getContainersIdsInScopeItems(
    entities::ScopeId scopeId,
    const std::vector<entities::ScopeItemId>& scopeItemIds) const {
  bool disjoint = true;
  auto relevantContainerIds =
      std::make_shared<PackerSet<entities::ContainerId>>();
  for (auto scopeItemId : scopeItemIds) {
    auto& containerIds =
        universe_->getScope(scopeId).getContainerIds(scopeItemId);

    for (auto containerId : containerIds) {
      auto [it, insertSuccess] = relevantContainerIds->insert(containerId);
      if (!insertSuccess) {
        disjoint = false;
      }
    }
  }

  return std::make_pair(relevantContainerIds, disjoint);
}

const std::shared_ptr<const entities::Set<entities::ContainerId>>
ExpressionBuilder::getContainersOutOfScopePtr(entities::ScopeId scopeId) {
  return containersOutOfScopeCache_.getSavedOrCompute(scopeId, [&]() {
    entities::Set<entities::ContainerId> containerIdsInScope;

    auto& scope = universe_->getScope(scopeId);
    for (auto scopeItemId : scope.getScopeItemIds()) {
      auto& containerIds = scope.getContainerIds(scopeItemId);
      containerIdsInScope.insert(containerIds.begin(), containerIds.end());
    }

    auto containerIdsOutOfScope =
        std::make_shared<entities::Set<entities::ContainerId>>();
    for (auto containerId : universe_->getContainers().getContainerIds()) {
      if (!containerIdsInScope.contains(containerId)) {
        containerIdsOutOfScope->insert(containerId);
      }
    }

    return containerIdsOutOfScope;
  });
}

const ExpressionBuilder::ScopeImage& ExpressionBuilder::getScopeImage(
    entities::ScopeId destinationScopeId,
    entities::ScopeId sourceScopeId,
    entities::ScopeItemId sourceScopeItemId) {
  static const ExpressionBuilder::ScopeImage empty;
  return folly::get_ref_default(
      getScopeImages(destinationScopeId, sourceScopeId).inScope,
      sourceScopeItemId,
      empty);
}

const ExpressionBuilder::ScopeImage& ExpressionBuilder::getOutOfScopeImage(
    entities::ScopeId destinationScopeId,
    entities::ScopeId sourceScopeId) {
  return getScopeImages(destinationScopeId, sourceScopeId).outOfScope;
}

const ExpressionBuilder::ScopeImages& ExpressionBuilder::getScopeImages(
    entities::ScopeId destinationScopeId,
    entities::ScopeId sourceScopeId) {
  auto insertToSetPtr = [](auto& setPtr, entities::ContainerId element) {
    if (setPtr == nullptr) {
      setPtr = std::make_shared<entities::Set<entities::ContainerId>>();
    }
    setPtr->insert(element);
  };

  auto key = std::make_tuple(destinationScopeId, sourceScopeId);

  return scopeImagesCache_.getSavedOrCompute(key, [&]() {
    entities::Map<entities::ContainerId, entities::ScopeItemId> containerImage;
    auto& destinationScope = universe_->getScope(destinationScopeId);
    for (auto scopeItemId : destinationScope.getScopeItemIds()) {
      for (auto containerId : destinationScope.getContainerIds(scopeItemId)) {
        containerImage.emplace(containerId, scopeItemId);
      }
    }

    ScopeImages scopeImages;
    auto& sourceScope = universe_->getScope(sourceScopeId);
    for (auto sourceScopeItemId : sourceScope.getScopeItemIds()) {
      auto& scopeImage = scopeImages.inScope[sourceScopeItemId];

      for (auto containerId : sourceScope.getContainerIds(sourceScopeItemId)) {
        auto destinationScopeItemIdPtr =
            folly::get_ptr(containerImage, containerId);
        destinationScopeItemIdPtr
            ? insertToSetPtr(
                  scopeImage.inScope[*destinationScopeItemIdPtr], containerId)
            : insertToSetPtr(scopeImage.outOfScope, containerId);
      }
    }

    for (auto containerId : *getContainersOutOfScopePtr(sourceScopeId)) {
      auto destinationScopeItemIdPtr =
          folly::get_ptr(containerImage, containerId);
      destinationScopeItemIdPtr
          ? insertToSetPtr(
                scopeImages.outOfScope.inScope[*destinationScopeItemIdPtr],
                containerId)
          : insertToSetPtr(scopeImages.outOfScope.outOfScope, containerId);
    }

    return scopeImages;
  });
}

std::shared_ptr<Expression> ExpressionBuilder::isAssigned(
    entities::ContainerId containerId,
    entities::ObjectId objectId) {
  auto key = std::make_tuple(objectId, containerId);
  return isAssignedContainerCache_.getSavedOrCompute(key, [&]() {
    return rebalancer::variable(
        objectId, containerId, *universe_, updatedInitialAssignment_);
  });
}

std::shared_ptr<Expression> ExpressionBuilder::isAssigned(
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    entities::ObjectId objectId) {
  // create variable for object, container pair and returns expression
  // representing if object belongs to the scope item
  auto key = std::make_tuple(scopeItemId, objectId);
  return isAssignedScopeItemCache_.getSavedOrCompute(key, [&]() {
    std::shared_ptr<Expression> expr = const_expr(0, *universe_);
    auto& scope = universe_->getScope(scopeId);
    for (auto containerId : scope.getContainerIds(scopeItemId)) {
      inplace_binary_max(expr, isAssigned(containerId, objectId));
    }
    return expr;
  });
}

ExprPtr ExpressionBuilder::getObjectPartition(
    const entities::Map<entities::GroupId, double>& groupLimits,
    entities::DimensionId dimensionId,
    entities::PartitionId partitionId,
    bool normalizeByGroupSize,
    const std::optional<ScopeParams>& scopeParams,
    std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds,
    double defaultGroupCoefficient) {
  // Only use cache when groupLimits is empty since entities::Map<GroupId,
  // double> doesn't have a standard hash function
  if (groupLimits.empty()) {
    // Compute hash of filteredGroupIds for cache key using commutative hash
    std::optional<size_t> filteredGroupIdsHash;
    if (filteredGroupIds) {
      filteredGroupIdsHash = folly::hash::commutative_hash_combine_range(
          filteredGroupIds->begin(), filteredGroupIds->end());
    }

    const auto key = std::make_tuple(
        dimensionId,
        partitionId,
        normalizeByGroupSize,
        scopeParams,
        filteredGroupIdsHash,
        defaultGroupCoefficient);

    return objectPartitionCache_.getSavedOrCompute(key, [&]() {
      return createObjectPartition(
          groupLimits,
          dimensionId,
          partitionId,
          normalizeByGroupSize,
          scopeParams,
          std::move(filteredGroupIds),
          defaultGroupCoefficient);
    });
  }

  // Not cacheable, compute directly
  return createObjectPartition(
      groupLimits,
      dimensionId,
      partitionId,
      normalizeByGroupSize,
      scopeParams,
      std::move(filteredGroupIds),
      defaultGroupCoefficient);
}

ExprPtr ExpressionBuilder::createObjectPartition(
    const entities::Map<entities::GroupId, double>& groupLimits,
    entities::DimensionId dimensionId,
    entities::PartitionId partitionId,
    bool normalizeByGroupSize,
    const std::optional<ScopeParams>& scopeParams,
    std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds,
    double defaultGroupCoefficient) {
  auto& dimension = universe_->getObjects().getDimension(dimensionId);
  if (dimension.size() != 1 || dimension.at(0).isRoutingConfigBased()) {
    throw std::runtime_error(
        "non-scalar dimensions or ObjectPartitionRoutingDimensions are not currently supported with objectPartition");
  }

  /* when normalizeByGroupSize is set, each group's normalization coef is set to
   * 1 / (number of objects in the group) */
  entities::Map<entities::GroupId, double> normalizationCoefs;
  const auto& partition = universe_->getPartition(partitionId);
  if (normalizeByGroupSize) {
    for (const auto& groupId : partition.getGroupIds()) {
      if (filteredGroupIds && !filteredGroupIds->contains(groupId)) {
        continue;
      }
      normalizationCoefs[groupId] =
          1.0 / partition.getObjectIds(groupId).size();
    }
  }

  std::optional<PackerSet<entities::ScopeItemId>> scopeItemIdsOpt =
      std::nullopt;
  if (scopeParams.has_value()) {
    const auto scopeId = scopeParams.value().scopeId;
    const auto scopeItemId = scopeParams.value().scopeItemId;
    const auto dimensionScopeId = dimension.only().getScopeId();
    scopeItemIdsOpt.emplace();
    if (scopeId != dimensionScopeId) {
      // If the util scope and dimension scopes are not the same, we need to
      // provide all of the scope Ids from the scope image
      auto scopeImage = getScopeImage(dimensionScopeId, scopeId, scopeItemId);
      for (const auto& [dimensionScopeItemId, _] : scopeImage.inScope) {
        scopeItemIdsOpt->insert(dimensionScopeItemId);
      }
      if (scopeImage.outOfScope && !scopeImage.outOfScope->empty()) {
        // If the util scope has containers that are not in the dimension's
        // scope, we also include the out of scope scopeItemId
        scopeItemIdsOpt->insert(scopeItemId);
      }
    } else {
      scopeItemIdsOpt->insert(scopeItemId);
    }
  }

  return object_partition(
      getPartitionInfo(partitionId, std::move(filteredGroupIds)),
      dimensionId,
      groupLimits,
      std::move(scopeItemIdsOpt),
      std::move(normalizationCoefs),
      // TODO: consider removing defaultGroupLimit as it seems to be only used
      // in tests (?)
      0.0 /*defaultGroupLimit*/,
      defaultGroupCoefficient);
}

std::shared_ptr<const PartitionInfo> ExpressionBuilder::getPartitionInfo(
    entities::PartitionId partitionId,
    std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds) {
  return partitionInfoCache_.getSavedOrCompute(
      PartitionInfoCacheKey{
          .partitionId = partitionId, .filteredGroupIds = filteredGroupIds},
      [&]() {
        return std::make_shared<const PartitionInfo>(
            *universe_, partitionId, std::move(filteredGroupIds));
      });
}

std::shared_ptr<Expression> ExpressionBuilder::getObjectPartitionLookup(
    UtilMetric metric,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    ExprPtr objectPartition,
    const entities::Map<entities::GroupId, double>& overrides,
    ObjectPartitionLookupPenaltyTransform penaltyTransform,
    int groupsAllowed,
    bool minBound) {
  if (metric != UtilMetric::AFTER && metric != UtilMetric::DURING) {
    throw std::runtime_error("util metric not supported");
  }

  // Only use cache when overrides is empty since entities::Map<GroupId,
  // double> doesn't have a standard hash function
  if (overrides.empty()) {
    auto key = std::make_tuple(
        metric,
        scopeId,
        scopeItemId,
        objectPartition,
        penaltyTransform,
        groupsAllowed,
        minBound);

    return objectPartitionLookupCache_.getSavedOrCompute(key, [&]() {
      return createObjectPartitionLookup(
          metric,
          scopeId,
          scopeItemId,
          objectPartition,
          overrides,
          penaltyTransform,
          groupsAllowed,
          minBound);
    });
  }

  // Not cacheable, compute directly
  return createObjectPartitionLookup(
      metric,
      scopeId,
      scopeItemId,
      objectPartition,
      overrides,
      penaltyTransform,
      groupsAllowed,
      minBound);
}

std::shared_ptr<Expression> ExpressionBuilder::createObjectPartitionLookup(
    UtilMetric metric,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    ExprPtr objectPartition,
    const entities::Map<entities::GroupId, double>& overrides,
    ObjectPartitionLookupPenaltyTransform penaltyTransform,
    int groupsAllowed,
    bool minBound) {
  entities::Set<entities::ObjectId> initialObjects;
  if (metric == UtilMetric::DURING) {
    auto& containerIds =
        universe_->getScope(scopeId).getContainerIds(scopeItemId);
    for (auto containerId : containerIds) {
      auto& objectIds =
          universe_->getContainers().getInitialObjectIds(containerId);
      initialObjects.insert(objectIds.begin(), objectIds.end());
    }
  }

  auto objectPartitionLookup = object_partition_lookup(
      std::move(objectPartition),
      universe_->getScope(scopeId).getContainerIdsPtr(scopeItemId),
      scopeId,
      scopeItemId,
      updatedInitialAssignment_,
      overrides,
      std::move(initialObjects),
      // TODO: consider removing defaultGroupLimitOverride as it seems to be
      // only used in tests (?)
      std::nullopt /* defaultGroupLimitOverride*/,
      penaltyTransform,
      groupsAllowed,
      minBound);

  if (metrics_) {
    auto lookup = std::dynamic_pointer_cast<ObjectPartitionLookupDefault>(
        objectPartitionLookup);
    metrics_->addToUtilCollection(std::move(lookup), metric);
  }

  return objectPartitionLookup;
}

std::shared_ptr<ObjectPartitionMoveLimit>
ExpressionBuilder::getObjectPartitionMoveLimit(
    const entities::Map<entities::GroupId, double>& groupLimits,
    entities::PartitionId partitionId,
    entities::DimensionId dimensionId,
    entities::Set<entities::ContainerId> sourceContainerIdsNotAffectingLimit,
    entities::Set<entities::ContainerId>
        destinationContainerIdsNotAffectingLimit) const {
  const entities::Map<entities::ObjectId, std::vector<entities::GroupId>>
      objectToGroupIds;
  return std::make_shared<ObjectPartitionMoveLimit>(
      *universe_,
      updatedInitialAssignment_,
      partitionId,
      dimensionId,
      groupLimits,
      std::move(sourceContainerIdsNotAffectingLimit),
      std::move(destinationContainerIdsNotAffectingLimit));
}

std::shared_ptr<GroupRoutingRing> ExpressionBuilder::getGroupRoutingRing(
    entities::RoutingConfigId routingConfigId,
    entities::GroupId groupId) {
  auto key = std::make_pair(routingConfigId, groupId);
  return routingConfigToGroupRoutingRing_.getSavedOrCompute(key, [&]() {
    auto groupRoutingRing = std::make_shared<GroupRoutingRing>(
        routingConfigId, groupId, *universe_, updatedInitialAssignment_);

    if (metrics_) {
      metrics_->addToGroupRoutingTrafficCollection(
          groupRoutingRing, routingConfigId, groupId);
    }

    return groupRoutingRing;
  });
}

// private
std::shared_ptr<GroupRoutingTrafficLookup>
ExpressionBuilder::getGroupRoutingTrafficLookup(
    entities::RoutingConfigId routingConfigId,
    entities::GroupId groupId,
    entities::ScopeItemId scopeItemId) {
  return groupRoutingTrafficLookupCache_.getSavedOrCompute(
      std::tuple(routingConfigId, groupId, scopeItemId), [&]() {
        auto routingRingExpr = getGroupRoutingRing(routingConfigId, groupId);
        return std::make_shared<GroupRoutingTrafficLookup>(
            routingRingExpr, scopeItemId, *universe_);
      });
}

std::shared_ptr<GroupRoutingLatencyLookup>
ExpressionBuilder::getGroupRoutingLatencyLookup(
    entities::RoutingConfigId routingConfigId,
    entities::GroupId groupId,
    const interface::RoutingLatencyMetricInfo& metric) {
  auto key = std::tuple(
      routingConfigId,
      groupId,
      *metric.type(),
      metric.percentile().to_optional());
  return groupRoutingLatencyLookupCache_.getSavedOrCompute(key, [&]() {
    auto latencyLookup = std::make_shared<GroupRoutingLatencyLookup>(
        getGroupRoutingRing(routingConfigId, groupId), metric, *universe_);
    latencyLookup->description = fmt::format(
        "{} latency of group '{}' w.r.t. routing config '{}'",
        interface::thriftUtils::toString(metric),
        universe_->getEntityName(groupId),
        universe_->getEntityName(routingConfigId));

    if (metrics_) {
      metrics_->addToGroupRoutingLatencyCollection(
          latencyLookup, routingConfigId, metric, groupId);
    }

    return latencyLookup;
  });
}

/*
cached operations on ExprPtr
*/
ExprPtr ExpressionBuilder::binaryMin(ExprPtr A, ExprPtr B) {
  return binaryMinCache_.getSavedOrCompute(std::make_pair(A, B), [&]() {
    return binary_min(std::move(A), std::move(B));
  });
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getBoundedAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    entities::PartitionId partitionId,
    const entities::Map<entities::GroupId, double>& groupLimits,
    bool isUpperBound,
    std::optional<double> defaultValue,
    std::optional<entities::ScopeId> aggregationScopeId)
    FOLLY_TS_REQUIRES(!applyfunc) {
  auto& partition = universe_->getPartition(partitionId);
  // require the partition to be disjoint
  if (!partition.isDisjoint()) {
    throw std::runtime_error(
        "BoundedUtil is not supported for overlapping partitions");
  }

  auto getGroupUtilExpr =
      [&, this](entities::GroupId groupId) -> folly::coro::Task<ExprPtr> {
    if (aggregationScopeId) {
      co_return co_await getAggregatedAbsoluteUtil(
          metric,
          dimensionId,
          scopeId,
          *aggregationScopeId,
          scopeItemId,
          partitionId,
          groupId);
    } else {
      co_return co_await getAbsoluteUtil(
          metric, dimensionId, scopeId, scopeItemId, partitionId, groupId);
    }
  };

  auto boundedUtilExpr = [&](const auto& groupUtilExpr, auto boundValue) {
    auto boundedExpr = isUpperBound
        ? min(groupUtilExpr, const_expr(boundValue, *universe_))
        : max(groupUtilExpr, step(groupUtilExpr) * boundValue);
    return boundedExpr;
  };

  auto utilExpr = const_expr(0, *universe_);
  if (!isDefaultUnbounded(defaultValue, isUpperBound)) {
    assert(defaultValue.has_value());
    // Bound utilization of all groups
    // Standard representation size: one expression per group = |groups|
    for (auto groupId : partition.getGroupIds()) {
      auto boundValue = folly::get_default(groupLimits, groupId, *defaultValue);
      auto groupUtilExpr = co_await getGroupUtilExpr(groupId);
      inplace_add(utilExpr, boundedUtilExpr(groupUtilExpr, boundValue));
    }
  } else {
    // Optimized way: only bound utilization of groups that have a limit
    // associated with them, Representation size: 2 * |groupToCapValue|
    auto boundedGroupsTotalUtil = const_expr(0, *universe_);
    for (auto& [groupId, boundValue] : groupLimits) {
      auto groupUtilExpr = co_await getGroupUtilExpr(groupId);
      inplace_add(boundedGroupsTotalUtil, groupUtilExpr);
      inplace_add(utilExpr, boundedUtilExpr(groupUtilExpr, boundValue));
    }
    // collect util of remaining groups succinctly as follows
    auto remainingGroupsUtil =
        co_await getAbsoluteUtil(metric, dimensionId, scopeId, scopeItemId) -
        boundedGroupsTotalUtil;
    inplace_add(utilExpr, remainingGroupsUtil);
  }
  co_return utilExpr;
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getBundledAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId capacityScopeId,
    entities::ScopeItemId capacityScopeItemId,
    entities::PartitionId partitionId,
    entities::ScopeId configuredScopeId,
    const std::vector<BundleItem>& bundleItems) FOLLY_TS_REQUIRES(!applyfunc) {
  if (!universe_->getPartition(partitionId).isDisjoint()) {
    throw std::runtime_error(
        fmt::format(
            "BundledUtil is not supported for overlapping partition {}",
            universe_->getEntityName(partitionId)));
  }

  auto getBundledItemUtil =
      [&, this](const BundleItem& bundleItem) -> folly::coro::Task<ExprPtr> {
    const auto key = std::make_tuple(
        metric,
        dimensionId,
        partitionId,
        configuredScopeId,
        bundleItem.scopeItemId,
        bundleItem.size);
    co_return co_await bundledItemAbsoluteUtilCache_.getSavedOrCompute(
        key, [&, this]() -> folly::coro::Task<ExprPtr> {
          auto utilExpr = const_expr(0, *universe_);
          for (const auto groupId : *bundleItem.groupIds) {
            auto groupUtil = co_await getAbsoluteUtil(
                metric,
                dimensionId,
                configuredScopeId,
                bundleItem.scopeItemId,
                partitionId,
                groupId);
            inplace_add(
                utilExpr,
                floor(std::move(groupUtil) / bundleItem.size),
                bundleItem.size);
          }
          co_return utilExpr;
        });
  };

  const auto& configuredScope = universe_->getScope(configuredScopeId);
  auto utilExpr = const_expr(0, *universe_);
  size_t bundledContainerCount = 0;
  for (const auto& bundleItem : bundleItems) {
    inplace_add(utilExpr, co_await getBundledItemUtil(bundleItem));
    bundledContainerCount +=
        configuredScope.getContainerIds(bundleItem.scopeItemId).size();
  }

  const auto containerCount = universe_->getScope(capacityScopeId)
                                  .getContainerIds(capacityScopeItemId)
                                  .size();
  if (bundledContainerCount < containerCount) {
    inplace_add(
        utilExpr,
        co_await getAbsoluteUtil(
            metric, dimensionId, capacityScopeId, capacityScopeItemId));
    for (const auto& bundleItem : bundleItems) {
      inplace_add(
          utilExpr,
          co_await getAbsoluteUtil(
              metric, dimensionId, configuredScopeId, bundleItem.scopeItemId),
          -1);
    }
  }
  co_return utilExpr;
}

std::vector<entities::ScopeItemId> ExpressionBuilder::getNestedImage(
    entities::ScopeId outerScopeId,
    entities::ScopeId innerScopeId,
    entities::ScopeItemId outerScopeItemId) {
  if (outerScopeId == innerScopeId) {
    return {outerScopeItemId};
  }

  const auto& scopeImage =
      getScopeImage(innerScopeId, outerScopeId, outerScopeItemId);
  if (scopeImage.inScope.empty()) {
    throw std::runtime_error(
        fmt::format(
            "expect that {}'s image in {} scope is non-empty",
            universe_->getEntityName(outerScopeItemId),
            universe_->getEntityName(innerScopeId)));
  } else if (scopeImage.outOfScope && !scopeImage.outOfScope->empty()) {
    throw std::runtime_error(
        fmt::format(
            "{} must be fully covered by scopeitems of scope {}",
            universe_->getEntityName(outerScopeItemId),
            universe_->getEntityName(innerScopeId)));
  }
  auto innerScopeItemView = scopeImage.inScope | std::views::keys;
  const auto& outerScope = universe_->getScope(outerScopeId);
  const auto& innerScope = universe_->getScope(innerScopeId);
  // Ensure that innerScopeItems are fully contained (nested) in
  // outerScopeItemId => ensure that all containers of innerScopeItems belong to
  // outerScopeItemId
  for (auto innerScopeItemId : innerScopeItemView) {
    for (auto containerId : innerScope.getContainerIds(innerScopeItemId)) {
      if (outerScope.getScopeItemId(containerId) != outerScopeItemId) {
        throw std::runtime_error(
            fmt::format(
                "Expect scopeItem {} to be fully contained in {}",
                universe_->getEntityName(innerScopeItemId),
                universe_->getEntityName(outerScopeItemId)));
      }
    }
  }
  return std::vector(innerScopeItemView.begin(), innerScopeItemView.end());
}

std::shared_ptr<const entities::Set<entities::GroupId>>
ExpressionBuilder::getNestedImage(
    entities::PartitionId outerPartitionId,
    entities::PartitionId innerPartitionId,
    entities::GroupId outerGroupId) {
  auto key = std::make_tuple(outerPartitionId, innerPartitionId, outerGroupId);
  return nestedPartitionImageCache_.getSavedOrCompute(key, [&]() {
    if (outerPartitionId == innerPartitionId) {
      return std::make_shared<entities::Set<entities::GroupId>>(
          entities::Set<entities::GroupId>{outerGroupId});
    }

    const auto& outerPartition = universe_->getPartition(outerPartitionId);
    const auto& innerPartition = universe_->getPartition(innerPartitionId);
    const auto& outerGroupObjects = outerPartition.getObjectIds(outerGroupId);
    const auto& innerObjectToGroupIds = innerPartition.getObjectIdToGroupIds();

    entities::Set<entities::GroupId> innerGroups;
    for (const auto objectId : outerGroupObjects) {
      const auto innerGroupIds =
          folly::get_ptr(innerObjectToGroupIds, objectId);
      if (innerGroupIds == nullptr) {
        throw std::runtime_error(
            fmt::format(
                "Expected all objects in outer group '{}' to be part of at least one of the groups of inner partition '{}', but did not find any group for object '{}'",
                universe_->getEntityName(outerGroupId),
                universe_->getEntityName(innerPartitionId),
                universe_->getEntityName(objectId)));
      }
      if (innerGroupIds->size() > 1) {
        throw std::runtime_error(
            fmt::format(
                "Expected all objects in outer group '{}' to be part of at most one of the groups of inner partition '{}', but found object '{}' that is part of multiple groups",
                universe_->getEntityName(outerGroupId),
                universe_->getEntityName(innerPartitionId),
                universe_->getEntityName(objectId)));
      }

      innerGroups.insert(innerGroupIds->at(0));
    }

    // check that all the objects in the each of innerGroups is part of the
    // outerGroup (in other words, each group in innerGroups is a subset of the
    // outerGroup)
    const auto& innerGroupToObjectIds = innerPartition.getGroupToObjectIds();
    const entities::Set<entities::ObjectId> outerGroupObjectsSet(
        outerGroupObjects.begin(), outerGroupObjects.end());
    for (const auto& innerGroupId : innerGroups) {
      const auto& innerGroupObjects = innerGroupToObjectIds.at(innerGroupId);
      for (const auto objectId : innerGroupObjects) {
        if (!outerGroupObjectsSet.contains(objectId)) {
          throw std::runtime_error(
              fmt::format(
                  "Expect inner group '{}' to be a subset of outer group '{}', but found object '{}' that is not in '{}'",
                  universe_->getEntityName(innerGroupId),
                  universe_->getEntityName(outerGroupId),
                  universe_->getEntityName(objectId),
                  universe_->getEntityName(outerGroupId)));
        }
      }
    }

    return std::make_shared<entities::Set<entities::GroupId>>(
        std::move(innerGroups));
  });
}

folly::coro::Task<ExprPtr> ExpressionBuilder::getAggregatedAbsoluteUtil(
    UtilMetric metric,
    entities::DimensionId dimensionId,
    entities::ScopeId scopeId,
    entities::ScopeId aggregationScopeId,
    entities::ScopeItemId scopeItemId,
    entities::PartitionId partitionId,
    entities::GroupId groupId) FOLLY_TS_REQUIRES(!applyfunc) {
  // Explaination of scopeImage in this context:
  // scope:            |---s1----|---s2---|
  // aggregationScope: |-a1-|-a2-|---a3---|
  // image(s1) = {a1, a2}, image(s2) = {a3}

  auto aggregatedUtil = const_expr(0, *universe_);
  for (auto aggregationScopeItemId :
       getNestedImage(scopeId, aggregationScopeId, scopeItemId)) {
    aggregatedUtil += co_await getAbsoluteUtil(
        metric,
        dimensionId,
        aggregationScopeId,
        aggregationScopeItemId,
        partitionId,
        groupId);
  }
  co_return aggregatedUtil;
}

} // namespace facebook::rebalancer::materializer
