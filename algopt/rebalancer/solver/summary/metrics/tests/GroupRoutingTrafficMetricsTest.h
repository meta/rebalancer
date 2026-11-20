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

#include "algopt/rebalancer/algopt_common/ValueSortedMap.h"
#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/builders/RoutingConfigsBuilder.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/RoutingConfig.h"
#include "algopt/rebalancer/entities/RoutingRing.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingTrafficMetrics.h"
#include "algopt/rebalancer/solver/summary/metrics/tests/MetricsTestBase.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>

#include <coroutine>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class GroupRoutingTrafficMetricsTest : public MetricsTestBase {
 private:
  using SortedDestinationLatencyMap = facebook::algopt::ValueSortedMap<
      entities::ScopeItemId,
      double,
      entities::CompareScopeItemLatencyPair>;

 protected:
  void SetUp() override {
    folly::coro::blockingWait(setUpUniverse());
  }

  folly::coro::Task<void> setUpUniverse() {
    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"host0", {"task0", "task2"}},
            {"host1", {"task1"}},
        });

    // Create scope items for traffic sources and destinations
    co_await addScope(
        "region", {{"region0", {"host0"}}, {"region1", {"host1"}}});

    co_await addPartition(
        "tenant", {{"tenant1", {"task0", "task1"}}, {"tenant2", {"task2"}}});

    const auto scopeId = universeBuilder_.getScopeId("region");
    const auto partitionId = universeBuilder_.getPartitionId("tenant");

    auto latencyTablePtr = std::make_shared<
        entities::Map<entities::ScopeItemId, SortedDestinationLatencyMap>>();
    latencyTablePtr->emplace(
        region(0),
        SortedDestinationLatencyMap({{region(0), 0}, {region(1), 10}}));
    latencyTablePtr->emplace(
        region(1),
        SortedDestinationLatencyMap({{region(0), 5}, {region(1), 5}}));

    auto region0Dest = std::make_optional(
        std::vector<std::vector<entities::ScopeItemId>>{
            {region(0)}, {region(1)}});
    auto region1Dest = std::make_optional(
        std::vector<std::vector<entities::ScopeItemId>>{
            {region(1)}, {region(0)}});
    const entities::RoutingRing groupRoutingRing1(region(0), 60, region0Dest);
    const entities::RoutingRing groupRoutingRing2(region(1), 40, region1Dest);

    std::vector<entities::RoutingRing> groupRoutingRings;
    groupRoutingRings.push_back(groupRoutingRing1);
    groupRoutingRings.push_back(groupRoutingRing2);

    const entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>
        groupToRoutingRings{
            {tenant(1), groupRoutingRings}, {tenant(2), groupRoutingRings}};

    auto routingConfig1 = std::make_shared<entities::RoutingConfig>(
        groupToRoutingRings, latencyTablePtr, scopeId, partitionId, nullptr);

    co_await addRoutingConfig(
        "routingConfig1",
        entities::RoutingConfigData{std::move(routingConfig1)});

    auto routingConfig2 = std::make_shared<entities::RoutingConfig>(
        groupToRoutingRings, latencyTablePtr, scopeId, partitionId, nullptr);

    co_await addRoutingConfig(
        "routingConfig2",
        entities::RoutingConfigData{std::move(routingConfig2)});
  }

  entities::ScopeItemId region(int i) const {
    return scopeItemId(scopeId("region"), fmt::format("region{}", i));
  }

  entities::RoutingConfigId routingConfig(int i) const {
    return routingConfigId(fmt::format("routingConfig{}", i));
  }

  entities::GroupId tenant(int i) const {
    return groupId(partitionId("tenant"), fmt::format("tenant{}", i));
  }

  static std::shared_ptr<GroupRoutingRing> createRoutingRing(
      entities::RoutingConfigId routingConfigId,
      entities::GroupId groupId,
      std::shared_ptr<const entities::Universe> universe) {
    const Assignment assignment(
        universe->getContainers().getInitialAssignment());
    return std::make_shared<GroupRoutingRing>(
        routingConfigId, groupId, *universe, assignment);
  }

  GroupRoutingTrafficMetrics metrics;
};

} // namespace facebook::rebalancer::packer::tests
