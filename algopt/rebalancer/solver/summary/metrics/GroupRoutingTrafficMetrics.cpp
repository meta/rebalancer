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

#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingTrafficMetrics.h"

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/utils/TrafficTable.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashMap.h"
#endif

#include <folly/container/F14Map.h>
#include <folly/lang/Assume.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <string>

namespace facebook::rebalancer {
namespace {
using GroupId = entities::GroupId;
using RoutingConfigId = entities::RoutingConfigId;

interface::thrift::SourceTrafficMetrics getTrafficSummary(
    const GroupRoutingRing& routingRingExpr,
    const entities::Universe& universe) {
  auto& sourceToDestinationTraffic =
      routingRingExpr.getTrafficTableWithStats().getTrafficTable();

  interface::thrift::SourceTrafficMetrics trafficInfo;
  for (auto& [source, destinationToTrafficLatencyPair] :
       sourceToDestinationTraffic) {
    auto& sourceName = universe.getEntityName(source);

    interface::thrift::ScopeItemUtils scopeItemUtils;
    for (auto& [destination, trafficLatencyPair] :
         destinationToTrafficLatencyPair) {
      auto& destinationName = universe.getEntityName(destination);
      scopeItemUtils.scopeItemToValue()->emplace(
          destinationName, trafficLatencyPair.first);
    }

    trafficInfo.sourceToDestinationTraffic()->emplace(
        sourceName, std::move(scopeItemUtils));
  }

  return trafficInfo;
}
} // namespace

interface::thrift::MetricCollectionType GroupRoutingTrafficMetrics::getType()
    const {
  return interface::thrift::MetricCollectionType::GROUP_ROUTING_TRAFFIC_VALUES;
}

void GroupRoutingTrafficMetrics::add(
    std::shared_ptr<GroupRoutingRing> expr,
    RoutingConfigId routingConfigId,
    GroupId groupId) {
  auto wLockedTrafficExprs = innerCollection_.wlock();
  auto [_, insertSuccess] = wLockedTrafficExprs->emplace(
      std::make_pair(routingConfigId, groupId), std::move(expr));
  throwOnInsertFailure(
      insertSuccess,
      "unexpected failure to insert to groupTrafficMetrics. Duplicates are unexpected since GroupRoutingRing exprs are cached in expressionBuilder");
}

void GroupRoutingTrafficMetrics::addToSummary(
    const entities::Universe& universe,
    interface::thrift::Metrics& metricsSummary) const {
  auto rLockedTrafficExprs = innerCollection_.rlock();
  auto& routingConfigToGroupTrafficMetrics =
      *metricsSummary.routingConfigToGroupTrafficMetrics();
  for (auto& [key, expr] : *rLockedTrafficExprs) {
    auto& [configId, groupId] = key;
    auto& routingConfigName = universe.getEntityName(configId);
    auto& groupName = universe.getEntityName(groupId);

    routingConfigToGroupTrafficMetrics[routingConfigName]
        .groupToSourceTraffic()
        ->emplace(groupName, getTrafficSummary(*expr, universe));
  }
}

} // namespace facebook::rebalancer
