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

#include "algopt/rebalancer/solver/summary/metrics/Metrics.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingLatencyMetrics.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingTrafficMetrics.h"
#include "algopt/rebalancer/solver/summary/metrics/MetricCollection.h"
#include "algopt/rebalancer/solver/summary/metrics/ScopeItemUtilMetrics.h"
#include "algopt/rebalancer/solver/utils/Context.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashMap.h"
#endif

#include <boost/iterator/iterator_categories.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/lang/Hint.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <optional>
#include <stdexcept>
#include <string>

namespace facebook::rebalancer {

using UtilMetric = materializer::UtilMetric;
using Descriptor = materializer::Descriptor;
using GroupId = entities::GroupId;
using RoutingConfigId = entities::RoutingConfigId;

template <typename T>
std::shared_ptr<T> Metrics::Builder::getCollection(
    interface::thrift::MetricCollectionType type) {
  {
    const auto rLockedMetricTypeToCollection = metricTypeToCollection_.rlock();
    const auto it = rLockedMetricTypeToCollection->find(type);
    if (it != rLockedMetricTypeToCollection->end()) {
      return std::dynamic_pointer_cast<T>(it->second);
    }
  }

  // Collection doesn't exist, need write lock to create it; also, need to check
  // again if it was created in the meantime
  const auto wLockedMetricTypeToCollection = metricTypeToCollection_.wlock();
  auto it = wLockedMetricTypeToCollection->find(type);
  if (it == wLockedMetricTypeToCollection->end()) {
    it = wLockedMetricTypeToCollection->emplace(type, std::make_shared<T>())
             .first;
    if (it->second->getType() != type) {
      throw std::runtime_error(
          fmt::format(
              "Given MetricType = {}, but created Collection of type {}",
              apache::thrift::util::enumNameSafe(type),
              apache::thrift::util::enumNameSafe(it->second->getType())));
    }
  }
  return std::dynamic_pointer_cast<T>(it->second);
}

void Metrics::Builder::addToUtilCollection(
    ExprPtr expr,
    UtilMetric utilMetric,
    const Descriptor& descriptor) {
  throwIfAttemptToAddAfterBuilding();

  if (!descriptor.dimensionId.has_value() ||
      !descriptor.scopeItemId.has_value()) {
    throw std::runtime_error(
        fmt::format(
            "Unexpected call to addToUtilCollection when either dimensionId or scopeItemId are not set"));
  }

  return addToScopeItemUtilCollection(expr, utilMetric, descriptor);
}

void Metrics::Builder::addToUtilCollection(
    std::shared_ptr<ObjectPartitionLookupDefault> lookup,
    UtilMetric utilMetric) {
  return addToScopeItemUtilCollection(std::move(lookup), std::move(utilMetric));
}

void Metrics::Builder::addToScopeItemUtilCollection(
    ExprPtr expr,
    UtilMetric utilMetric,
    const Descriptor& descriptor) {
  auto scopeItemUtilMetrics = getCollection<ScopeItemUtilMetrics>(
      interface::thrift::MetricCollectionType::SCOPE_ITEM_UTILIZATION_VALUES);
  scopeItemUtilMetrics->add(std::move(expr), utilMetric, descriptor);
}

void Metrics::Builder::addToScopeItemUtilCollection(
    std::shared_ptr<ObjectPartitionLookupDefault> lookup,
    UtilMetric utilMetric) {
  auto scopeItemUtilMetrics = getCollection<ScopeItemUtilMetrics>(
      interface::thrift::MetricCollectionType::SCOPE_ITEM_UTILIZATION_VALUES);
  scopeItemUtilMetrics->add(std::move(lookup), std::move(utilMetric));
}

void Metrics::Builder::addToGroupRoutingTrafficCollection(
    std::shared_ptr<GroupRoutingRing> expr,
    RoutingConfigId routingConfigId,
    GroupId groupId) {
  throwIfAttemptToAddAfterBuilding();
  auto groupRoutingTrafficMetrics = getCollection<GroupRoutingTrafficMetrics>(
      interface::thrift::MetricCollectionType::GROUP_ROUTING_TRAFFIC_VALUES);
  groupRoutingTrafficMetrics->add(std::move(expr), routingConfigId, groupId);
}

void Metrics::Builder::addToGroupRoutingLatencyCollection(
    ExprPtr expr,
    RoutingConfigId routingConfigId,
    const interface::RoutingLatencyMetricInfo& latencyMetric,
    GroupId groupId) {
  throwIfAttemptToAddAfterBuilding();

  auto groupRoutingLatencyMetrics = getCollection<GroupRoutingLatencyMetrics>(
      interface::thrift::MetricCollectionType::GROUP_ROUTING_LATENCY_VALUES);
  groupRoutingLatencyMetrics->add(
      std::move(expr), routingConfigId, latencyMetric, groupId);
}

Metrics Metrics::Builder::build(
    std::shared_ptr<const entities::Universe> universe) {
  throwIfAttemptToAddAfterBuilding();
  built_ = true;
  entities::Map<
      interface::thrift::MetricCollectionType,
      std::shared_ptr<const MetricCollection>>
      metricTypeToCollection;
  const auto wLockedMetricTypeToCollection = metricTypeToCollection_.wlock();
  for (auto& [type, collection] : *wLockedMetricTypeToCollection) {
    collection->buildRootExpr(universe);
    metricTypeToCollection.emplace(type, std::move(collection));
  }
  return Metrics(std::move(metricTypeToCollection));
}

Metrics::Metrics(
    entities::Map<
        interface::thrift::MetricCollectionType,
        std::shared_ptr<const MetricCollection>>&& metricTypeToCollection)
    : metricTypeToCollection_(std::move(metricTypeToCollection)) {}

interface::thrift::Metrics Metrics::getSummary(
    const entities::Universe& universe,
    const Assignment& assignment) const {
  // some of the metric expressions may not be part of constraint or
  // objective expression subgraph. This should ideally not happen, but it
  // does. For example, when a LinearSum is multiplied by a constant, the
  // original LinearSum is "lost" and a new LinearSum is created that has the
  // all its children with updated coefficients. So, for now, we need to
  // ensure that all expressions are 'applied'
  Context context;
  interface::thrift::Metrics metricsSummary;
  for (const auto& [_, collection] : metricTypeToCollection_) {
    collection->applyAndAddToSummary(
        assignment, context, universe, metricsSummary);
  }

  return metricsSummary;
}

const entities::Map<
    interface::thrift::MetricCollectionType,
    std::shared_ptr<const MetricCollection>>&
Metrics::getAvailableCollections() const {
  return metricTypeToCollection_;
}

void Metrics::fullApply(const Assignment& assignment) const {
  Context context;
  for (const auto& [_, collection] : metricTypeToCollection_) {
    collection->fullApply(assignment, context);
  }
}

void Metrics::pushAllExprsTo(std::vector<Expression*>& exprs) const {
  for (const auto& [_, collection] : metricTypeToCollection_) {
    collection->pushRootTo(exprs);
  }
}

void Metrics::Builder::throwIfAttemptToAddAfterBuilding() const {
  if (built_) {
    throw std::runtime_error(
        "Cannot add to collection or build after having built once");
  }
}
} // namespace facebook::rebalancer
