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

#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/summary/metrics/Metrics.h"
#include "algopt/rebalancer/solver/summary/metrics/tests/MetricsTestBase.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/F14Map.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Hint.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <coroutine>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using UtilMetric = facebook::rebalancer::materializer::UtilMetric;
namespace thriftUtils = facebook::rebalancer::interface::thriftUtils;

namespace facebook::rebalancer::packer::tests {

class MetricsTest : public MetricsTestBase {
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

    co_await addObjectDimension("load", {{"task0", 10}, {"task1", 20}}, 0.0);
    co_await addObjectDimension("memory", {{"task0", 50}, {"task1", 5}}, 0.0);
    co_await addScope("region", {{"region0", {"host0", "host1"}}});
    co_await addPartition(
        "tenant", {{"tenant1", {"task0"}}, {"tenant2", {"task1", "task2"}}});
    co_await addEmptyRoutingConfig("routingConfig1", "region", "tenant");
  }

  void addToUtilMetricCollection(
      ExprPtr expr,
      UtilMetric utilMetric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId) {
    metricsBuilder_.addToUtilCollection(
        expr,
        utilMetric,
        materializer::Descriptor{
            .dimensionId = dimensionId,
            .scopeId = scopeId,
            .scopeItemId = scopeItemId});
  }

  Metrics::Builder metricsBuilder_;
};

TEST_F(MetricsTest, UtilCollection) {
  auto hostScopeId = scopeId("host");
  auto host0ScopeItemId = scopeItemId(hostScopeId, "host0");
  auto host1ScopeItemId = scopeItemId(hostScopeId, "host1");
  auto regionScopeId = scopeId("region");
  auto region0ScopeItemId = scopeItemId(regionScopeId, "region0");
  auto load = dimensionId("load");
  auto memory = dimensionId("memory");

  // add some util metrics
  const auto universe = buildUniverse();
  auto lookupLoadHost0 = const_expr(10, *universe);
  lookupLoadHost0->description = "load util of host0";
  auto lookupLoadHost1 = const_expr(20, *universe);
  lookupLoadHost1->description = "load util of host1";
  auto lookupMem = const_expr(55, *universe);
  lookupMem->description = "memory util of region0";

  addToUtilMetricCollection(
      lookupLoadHost0, UtilMetric::AFTER, load, hostScopeId, host0ScopeItemId);
  addToUtilMetricCollection(
      lookupLoadHost1, UtilMetric::AFTER, load, hostScopeId, host1ScopeItemId);
  addToUtilMetricCollection(
      lookupLoadHost1, UtilMetric::DURING, load, hostScopeId, host1ScopeItemId);
  addToUtilMetricCollection(
      lookupMem, UtilMetric::AFTER, memory, regionScopeId, region0ScopeItemId);

  auto metrics = metricsBuilder_.build(universe);

  // get metrics summary
  auto metricsSummary = metrics.getSummary(
      *universe, Assignment(universe->getContainers().getInitialAssignment()));

  // expect size to be 2 since util exprs added w.r.t. both "after" and "during"
  EXPECT_EQ(metricsSummary.utilMetricToScopeUtils()->size(), 2);

  // First check all "after" utils
  {
    // expect size to be 2 since a after util expr was added w.r.t. both "host"
    // and "region" scopes
    auto& afterUtils = metricsSummary.utilMetricToScopeUtils()->at("after");
    EXPECT_EQ(
        afterUtils.scopeToDimensionUtils()->size(),
        2); // two scopes: "host" and "region"

    // expect size to be 1 since only "load" dimension util was added w.r.t
    // "host"
    auto& hostUtils = afterUtils.scopeToDimensionUtils()->at("host");
    EXPECT_EQ(hostUtils.dimensionToScopeItemUtils()->size(), 1);

    // expect size to be 1 since only "memory" dimension util was added w.r.t
    // "region"
    auto& regionDimensionVals =
        afterUtils.scopeToDimensionUtils()->at("region");
    EXPECT_EQ(regionDimensionVals.dimensionToScopeItemUtils()->size(), 1);

    // expect size to be 2 since "after" load util added w.r.t. both "host0" and
    // "host1"
    auto& loadUtils = hostUtils.dimensionToScopeItemUtils()->at("load");
    EXPECT_EQ(loadUtils.scopeItemToValue()->size(), 2);

    // expect size to be 1 since "after" memory util added w.r.t. only "region0"
    auto& memUtils =
        regionDimensionVals.dimensionToScopeItemUtils()->at("memory");
    EXPECT_EQ(memUtils.scopeItemToValue()->size(), 1);

    // check values and descriptions in summary for each scopeItem
    EXPECT_NEAR(loadUtils.scopeItemToValue()->at("host0"), 10, 1e-8);
    EXPECT_NEAR(loadUtils.scopeItemToValue()->at("host1"), 20, 1e-8);

    EXPECT_NEAR(memUtils.scopeItemToValue()->at("region0"), 55, 1e-8);
  }

  // check all "during" utils
  {
    // expect size to be 1 since a during util expr was only added w.r.t. one
    // scope "host"
    auto& duringUtils = metricsSummary.utilMetricToScopeUtils()->at("during");
    EXPECT_EQ(duringUtils.scopeToDimensionUtils()->size(), 1);

    // expect size to be 1 since only "load" dimension during util was added
    // w.r.t "host"
    auto& hostUtils = duringUtils.scopeToDimensionUtils()->at("host");
    EXPECT_EQ(hostUtils.dimensionToScopeItemUtils()->size(), 1);

    // expect size to be 1 since during util was added only w.r.t. one
    // scopeItem: "host1"
    auto& loadUtils = hostUtils.dimensionToScopeItemUtils()->at("load");
    EXPECT_EQ(loadUtils.scopeItemToValue()->size(), 1);

    // check values and descriptions
    EXPECT_NEAR(loadUtils.scopeItemToValue()->at("host1"), 20, 1e-8);
  }
}

TEST_F(MetricsTest, GroupLatencyAndTrafficCollection) {
  auto routingConfig1Id = routingConfigId("routingConfig1");
  auto tenantId = partitionId("tenant");
  auto tenant1Id = groupId(tenantId, "tenant1");
  auto tenant2Id = groupId(tenantId, "tenant2");

  // add some latency metrics
  const auto universe = buildUniverse();
  auto p99Metric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::PERCENTILE, 99);
  auto p100Metric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::PERCENTILE, 100);
  auto p90Metric = thriftUtils::makeRoutingLatencyMetric(
      interface::RoutingLatencyMetric::PERCENTILE, 90);
  metricsBuilder_.addToGroupRoutingLatencyCollection(
      const_expr(13, *universe), routingConfig1Id, p99Metric, tenant1Id);
  metricsBuilder_.addToGroupRoutingLatencyCollection(
      const_expr(13, *universe), routingConfig1Id, p90Metric, tenant1Id);
  metricsBuilder_.addToGroupRoutingLatencyCollection(
      const_expr(5, *universe), routingConfig1Id, p100Metric, tenant2Id);

  // get metrics summary
  auto metrics = metricsBuilder_.build(universe);
  auto metricsSummary = metrics.getSummary(
      *universe, Assignment(universe->getContainers().getInitialAssignment()));
  // check all routing latency metrics
  {
    // expect size to be 1 since latency metrics are added w.r.t. one routing
    // config: "routingConfig1"
    EXPECT_EQ(metricsSummary.routingConfigToGroupLatencyMetrics()->size(), 1);

    auto& config1Metrics =
        metricsSummary.routingConfigToGroupLatencyMetrics()->at(
            "routingConfig1");
    EXPECT_EQ(
        config1Metrics.groupToMetricValues()->size(),
        2); // two groups: "tenant1" and "tenant2"

    // expect two latency metrics w.r.t. "tenant1"-- one for p99 and one for p90
    auto& tenant1Config1LatencyMetrics =
        config1Metrics.groupToMetricValues()->at("tenant1");
    EXPECT_EQ(tenant1Config1LatencyMetrics.size(), 2);

    auto expectedTenant1Metrics =
        std::set<interface::RoutingLatencyMetricInfo>{p99Metric, p90Metric};
    auto actualTenant1Metrics = std::set<interface::RoutingLatencyMetricInfo>{
        *tenant1Config1LatencyMetrics[0].metric(),
        *tenant1Config1LatencyMetrics[1].metric()};
    EXPECT_EQ(expectedTenant1Metrics, actualTenant1Metrics);

    // expect one latency metric w.r.t. "tenant2" (p100)
    auto& tenant2Config1LatencyMetrics =
        config1Metrics.groupToMetricValues()->at("tenant2");
    EXPECT_EQ(tenant2Config1LatencyMetrics.size(), 1);

    // tenant2 p100
    auto& tenant2Config1MaxLatency = *tenant2Config1LatencyMetrics[0].value();
    EXPECT_NEAR(tenant2Config1MaxLatency, 5, 1e-8);
  }
}

} // namespace facebook::rebalancer::packer::tests
