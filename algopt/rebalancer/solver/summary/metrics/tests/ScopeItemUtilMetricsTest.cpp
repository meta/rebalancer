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

#include "algopt/rebalancer/solver/summary/metrics/tests/ScopeItemUtilMetricsTest.h"

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/entities/builders/AssignmentBuilder.h"
#include "algopt/rebalancer/entities/builders/DimensionsBuilder.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/ObjectDimension.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Metrics_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/summary/metrics/ScopeItemUtilMetrics.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashMap.h"
#endif

#include "gmock/gmock.h"
#include <folly/container/F14Map.h>
#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/lang/Assume.h>
#include <folly/lang/Bits.h>
#include <folly/lang/Hint.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <ranges>

using UtilMetric = facebook::rebalancer::materializer::UtilMetric;

namespace facebook::rebalancer::packer::tests {

CO_TEST_F(ScopeItemUtilMetricsTest, AddAndGetSummary) {
  co_await setUpUniverse();
  // collect some ids
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

  addToScopeItemUtilCollection(
      lookupLoadHost0, UtilMetric::AFTER, load, hostScopeId, host0ScopeItemId);
  addToScopeItemUtilCollection(
      lookupLoadHost1, UtilMetric::AFTER, load, hostScopeId, host1ScopeItemId);
  addToScopeItemUtilCollection(
      lookupLoadHost1, UtilMetric::DURING, load, hostScopeId, host1ScopeItemId);
  addToScopeItemUtilCollection(
      lookupMem, UtilMetric::AFTER, memory, regionScopeId, region0ScopeItemId);
  metrics.buildRootExpr(universe);

  // Verify the type
  EXPECT_EQ(
      metrics.getType(),
      interface::thrift::MetricCollectionType::SCOPE_ITEM_UTILIZATION_VALUES);

  // get metrics summary
  interface::thrift::Metrics metricsSummary;
  Context context;
  metrics.applyAndAddToSummary(
      Assignment(universe->getContainers().getInitialAssignment()),
      context,
      *universe,
      metricsSummary);

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

CO_TEST_F(ScopeItemUtilMetricsTest, AddAndGetSummaryWithPartitionAndGroup) {
  co_await setUpUniverse();
  // collect some ids
  const auto hostScopeId = scopeId("host");
  const auto host0ScopeItemId = scopeItemId(hostScopeId, "host0");
  const auto load = dimensionId("load");

  const auto partition1Id = partitionId("partition1");
  const auto partition1Group0Id = groupId(partition1Id, "group0");
  const auto partition1Group1Id = groupId(partition1Id, "group1");

  const auto partition2Id = partitionId("partition2");
  const auto partition2Group0Id = groupId(partition2Id, "group0");

  // Add a util metric with partition and group
  const auto universe = buildUniverse();
  auto host0Partition1Group0 = const_expr(15, *universe);
  addToScopeItemUtilCollection(
      host0Partition1Group0,
      UtilMetric::AFTER,
      load,
      hostScopeId,
      host0ScopeItemId,
      partition1Id,
      partition1Group0Id);

  auto host0Partition1Group1 = const_expr(10, *universe);
  addToScopeItemUtilCollection(
      host0Partition1Group1,
      UtilMetric::AFTER,
      load,
      hostScopeId,
      host0ScopeItemId,
      partition1Id,
      partition1Group1Id);

  auto host0Partition2Group0 = const_expr(5, *universe);
  addToScopeItemUtilCollection(
      host0Partition2Group0,
      UtilMetric::AFTER,
      load,
      hostScopeId,
      host0ScopeItemId,
      partition2Id,
      partition2Group0Id);

  // also add a host0 load metric without partition and group
  auto host0Load = const_expr(500, *universe);
  addToScopeItemUtilCollection(
      host0Load, UtilMetric::AFTER, load, hostScopeId, host0ScopeItemId);

  metrics.buildRootExpr(universe);

  // Verify the type
  EXPECT_EQ(
      metrics.getType(),
      interface::thrift::MetricCollectionType::SCOPE_ITEM_UTILIZATION_VALUES);

  // get metrics summary
  interface::thrift::Metrics metricsSummary;
  Context context;
  metrics.applyAndAddToSummary(
      Assignment(universe->getContainers().getInitialAssignment()),
      context,
      *universe,
      metricsSummary);

  // Check the summary structure
  EXPECT_EQ(metricsSummary.utilMetricToScopeUtils()->size(), 1);

  auto& afterUtils = metricsSummary.utilMetricToScopeUtils()->at("after");
  EXPECT_EQ(afterUtils.scopeToDimensionUtils()->size(), 1);

  auto& hostUtils = afterUtils.scopeToDimensionUtils()->at("host");
  EXPECT_EQ(hostUtils.dimensionToScopeItemUtils()->size(), 1);

  auto& loadUtils = hostUtils.dimensionToScopeItemUtils()->at("load");
  EXPECT_EQ(loadUtils.scopeItemToValue()->size(), 1);
  EXPECT_EQ(loadUtils.scopeItemToPartitionUtils()->size(), 1);

  // verift scopeItemToValue is as expected
  EXPECT_NEAR(loadUtils.scopeItemToValue()->at("host0"), 500, 1e-8);

  // verift scopeItemToPartitionUtils are as expected
  EXPECT_TRUE(loadUtils.scopeItemToPartitionUtils()->contains("host0"));
  auto& partitionUtils = loadUtils.scopeItemToPartitionUtils()->at("host0");
  EXPECT_EQ(
      partitionUtils.partitionToGroupUtils()->size(),
      2); // 2 partitions, partition1 and partition2

  EXPECT_TRUE(partitionUtils.partitionToGroupUtils()->contains("partition1"));
  EXPECT_TRUE(partitionUtils.partitionToGroupUtils()->contains("partition2"));

  auto& partition1GroupUtils =
      partitionUtils.partitionToGroupUtils()->at("partition1");
  EXPECT_EQ(
      partition1GroupUtils.groupToValue()->size(),
      2); // 2 groups, group0 and group1
  EXPECT_NEAR(partition1GroupUtils.groupToValue()->at("group0"), 15, 1e-8);
  EXPECT_NEAR(partition1GroupUtils.groupToValue()->at("group1"), 10, 1e-8);

  auto& partition2GroupUtils =
      partitionUtils.partitionToGroupUtils()->at("partition2");
  EXPECT_EQ(partition2GroupUtils.groupToValue()->size(),
            1); // 1 group, group0
  EXPECT_NEAR(partition2GroupUtils.groupToValue()->at("group0"), 5, 1e-8);
}

CO_TEST_F(ScopeItemUtilMetricsTest, FailPartitionAndGroupNotSetTogether) {
  co_await setUpUniverse();
  // collect some ids
  const auto hostScopeId = scopeId("host");
  const auto host0ScopeItemId = scopeItemId(hostScopeId, "host0");
  const auto load = dimensionId("load");

  const auto partition1Id = partitionId("partition1");
  const auto group0Id = groupId(partition1Id, "group0");

  // expect to throw if only one of partitionId and groupId are set
  const auto universe = buildUniverse();
  auto expr1 = const_expr(10, *universe);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      addToScopeItemUtilCollection(
          expr1,
          UtilMetric::AFTER,
          load,
          hostScopeId,
          host0ScopeItemId,
          std::nullopt,
          group0Id),
      "partitionId and groupId are expected to be set together");

  auto expr2 = const_expr(10, *universe);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      addToScopeItemUtilCollection(
          expr2,
          UtilMetric::AFTER,
          load,
          hostScopeId,
          host0ScopeItemId,
          partition1Id,
          std::nullopt),
      "partitionId and groupId are expected to be set together");
}

CO_TEST_F(
    ScopeItemUtilMetricsTest,
    ObjectPartitionLookupMetricWithMultipleContainers) {
  setInitialAssignment(
      {{"host1", {"task1", "task4"}},
       {"host2", {"task2", "task5", "task6"}},
       {"host3", {"task3", "task7", "task8", "task9"}}});

  // Set up partition with objects in different groups
  // Note: task9 is in all groups
  co_await addPartition(
      "partition1",
      entities::Map<std::string, std::vector<std::string>>{
          {"group0", {"task1", "task2", "task3", "task9"}},
          {"group1", {"task4", "task5", "task9"}},
          {"group2", {"task6", "task7", "task8", "task9"}}});

  entities::Map<std::string, double> objectWeights;
  for (const auto i : folly::irange(1, 10)) {
    objectWeights[fmt::format("task{}", i)] = 1.0;
  }
  const auto objectsData = universeBuilder_.getObjects();
  entities::ObjectIdToDoubleMap objectToValue(
      objectsData->numObjects,
      /*defaultValue=*/0.0,
      /*expectedNonDefaultSize=*/objectWeights.size());
  for (const auto& [objectName, value] : objectWeights) {
    objectToValue.emplace(objectsData->getId(objectName), value);
  }

  co_await addObjectDimension(
      "object_count",
      entities::ObjectDimensionData{
          std::make_unique<const entities::ObjectDimension>(
              std::move(objectToValue))});

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partition1Id = partitionId("partition1");
  const auto objCountDimId = dimensionId("object_count");

  // Create object partition with tight group limits to create violations
  auto objectPartition = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partition1Id),
      objCountDimId,
      {});

  auto host = [&](int i) {
    return universe->getContainerId(fmt::format("host{}", i));
  };

  // Create multiple ObjectPartitionLookup instances for different containers
  auto hostScopeId = scopeId("host");
  auto host1ScopeItemId = scopeItemId(hostScopeId, "host1");

  auto lookup1 = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{host(1)}),
          hostScopeId,
          host1ScopeItemId,
          *universe,
          assignment,
          {},
          {},
          std::nullopt,
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
          0,
          ObjectPartitionLookupDefault::Bound::MAX));

  auto lookup2 = std::make_shared<ObjectPartitionLookupDefault>(
      ObjectPartitionLookupDefault(
          objectPartition,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{host(1)}),
          hostScopeId,
          host1ScopeItemId,
          *universe,
          assignment,
          {},
          {},
          std::nullopt,
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
          0,
          ObjectPartitionLookupDefault::Bound::MAX));

  // Add both ObjectPartitionLookup instances
  addToScopeItemUtilCollection(lookup1, UtilMetric::AFTER);
  addToScopeItemUtilCollection(lookup2, UtilMetric::DURING);
  metrics.buildRootExpr(universe);

  // Get metrics summary
  interface::thrift::Metrics metricsSummary;
  Context context;
  metrics.applyAndAddToSummary(
      Assignment(universe->getContainers().getInitialAssignment()),
      context,
      *universe,
      metricsSummary);

  // Verify the summary has both AFTER and DURING metrics
  EXPECT_EQ(metricsSummary.utilMetricToScopeUtils()->size(), 2);
  EXPECT_TRUE(metricsSummary.utilMetricToScopeUtils()->contains("after"));
  EXPECT_TRUE(metricsSummary.utilMetricToScopeUtils()->contains("during"));

  // Check the structure for AFTER metric
  auto& afterUtils = metricsSummary.utilMetricToScopeUtils()->at("after");
  EXPECT_EQ(afterUtils.scopeToDimensionUtils()->size(), 1);
  auto& hostAfterUtils = afterUtils.scopeToDimensionUtils()->at("host");
  EXPECT_EQ(hostAfterUtils.dimensionToScopeItemUtils()->size(), 1);
  auto& dimensionAfterUtils =
      hostAfterUtils.dimensionToScopeItemUtils()->at("object_count");
  EXPECT_EQ(
      dimensionAfterUtils.scopeItemToValue()->size(),
      0); // ObjectPartitionLookup doesn't add direct values
  EXPECT_EQ(dimensionAfterUtils.scopeItemToPartitionUtils()->size(), 1);
  EXPECT_TRUE(
      dimensionAfterUtils.scopeItemToPartitionUtils()->contains("host1"));

  // Check the structure for DURING metric
  auto& duringUtils = metricsSummary.utilMetricToScopeUtils()->at("during");
  EXPECT_EQ(duringUtils.scopeToDimensionUtils()->size(), 1);
  auto& hostDuringUtils = duringUtils.scopeToDimensionUtils()->at("host");
  EXPECT_EQ(hostDuringUtils.dimensionToScopeItemUtils()->size(), 1);
  auto& dimensionDuringUtils =
      hostDuringUtils.dimensionToScopeItemUtils()->at("object_count");
  EXPECT_EQ(
      dimensionDuringUtils.scopeItemToValue()->size(),
      0); // ObjectPartitionLookup doesn't add direct values
  EXPECT_EQ(dimensionDuringUtils.scopeItemToPartitionUtils()->size(), 1);
  EXPECT_TRUE(
      dimensionDuringUtils.scopeItemToPartitionUtils()->contains("host1"));

  // Verify partition-group structure exists and values are reasonable
  auto& partitionAfterUtils =
      dimensionAfterUtils.scopeItemToPartitionUtils()->at("host1");
  EXPECT_EQ(partitionAfterUtils.partitionToGroupUtils()->size(), 1);
  EXPECT_TRUE(
      partitionAfterUtils.partitionToGroupUtils()->contains("partition1"));

  auto& partitionDuringUtils =
      dimensionDuringUtils.scopeItemToPartitionUtils()->at("host1");
  EXPECT_EQ(partitionDuringUtils.partitionToGroupUtils()->size(), 1);
  EXPECT_TRUE(
      partitionDuringUtils.partitionToGroupUtils()->contains("partition1"));

  // Check that group values exist and are non-negative
  auto& afterGroupUtils =
      partitionAfterUtils.partitionToGroupUtils()->at("partition1");
  auto& duringGroupUtils =
      partitionDuringUtils.partitionToGroupUtils()->at("partition1");
  EXPECT_EQ(afterGroupUtils.groupToValue()->size(), 2);
  EXPECT_EQ(duringGroupUtils.groupToValue()->size(), 2);

  EXPECT_EQ(afterGroupUtils.groupToValue()->at("group0"), 1);
  EXPECT_EQ(afterGroupUtils.groupToValue()->at("group1"), 1);
  EXPECT_EQ(duringGroupUtils.groupToValue()->at("group0"), 1);
  EXPECT_EQ(duringGroupUtils.groupToValue()->at("group1"), 1);
}
} // namespace facebook::rebalancer::packer::tests
