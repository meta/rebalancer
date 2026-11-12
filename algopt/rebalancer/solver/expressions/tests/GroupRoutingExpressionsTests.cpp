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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/interface/thrift/ThriftUtils.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingLatencyLookup.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingTrafficLookup.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace interface = facebook::rebalancer::interface;
namespace thriftUtils = facebook::rebalancer::interface::thriftUtils;
namespace entities = facebook::rebalancer::entities;

namespace facebook::rebalancer::packer::tests {

using SortedDestinationLatencyMap = facebook::algopt::ValueSortedMap<
    entities::ScopeItemId,
    double,
    entities::CompareScopeItemLatencyPair>;

class GroupRoutingExpressionsTest : public ExpressionTestsBase {
 protected:
  folly::coro::Task<void> setup(const bool useDefaultOriginToScopeItemSets) {
    // Sets up a problem with 3 tenants, all in the same group, and 4 regions.
    universeBuilder_.setObjectTypeName("tenant");
    universeBuilder_.setContainerTypeName("region");

    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"region1", {"tenant1"}},
            {"region2", {}},
            {"region3", {"tenant2"}},
            {"region4", {"tenant3"}}});

    co_await addPartition(
        "partition1",
        entities::Map<std::string, std::vector<std::string>>{
            {"group1", {"tenant1", "tenant2", "tenant3"}}});

    const auto regionScopeId = scopeId("region");
    const auto partition1Id = partitionId("partition1");
    const auto partitionData = co_await universeBuilder_.getPartition(
        universeBuilder_.getPartitionId("partition1"));
    const auto group1 = partitionData->getGroupId("group1");

    constexpr double totalTraffic = 100;

    // create latency table
    auto latencyTablePtr = std::make_shared<
        entities::Map<entities::ScopeItemId, SortedDestinationLatencyMap>>();
    latencyTablePtr->emplace(
        scopeItem(1),
        SortedDestinationLatencyMap(
            {{scopeItem(1), 0},
             {scopeItem(2), 10},
             {scopeItem(3), 0},
             {scopeItem(4), 10}}));
    latencyTablePtr->emplace(
        scopeItem(3),
        SortedDestinationLatencyMap(
            {{scopeItem(1), 5},
             {scopeItem(2), 5},
             {scopeItem(3), 0},
             {scopeItem(4), 5}}));

    // for group1, 60% of the traffic originates at region1 and rest at region3
    std::vector<entities::RoutingRing> groupRoutingRings;
    auto destinationScopeItemSet1 = std::make_optional(
        std::vector<std::vector<entities::ScopeItemId>>{
            {scopeItem(2)}, {scopeItem(1), scopeItem(3), scopeItem(4)}});

    auto destinationScopeItemSet2 = std::make_optional(
        std::vector<std::vector<entities::ScopeItemId>>{
            {scopeItem(1)}, {scopeItem(2), scopeItem(3), scopeItem(4)}});

    auto defaultOriginToDestinationScopeItemSets =
        std::make_shared<entities::Map<
            entities::ScopeItemId,
            std::vector<std::vector<entities::ScopeItemId>>>>();
    if (useDefaultOriginToScopeItemSets) {
      defaultOriginToDestinationScopeItemSets->emplace(
          scopeItem(1), destinationScopeItemSet1.value());
      defaultOriginToDestinationScopeItemSets->emplace(
          scopeItem(3), destinationScopeItemSet2.value());
      destinationScopeItemSet1 = std::nullopt;
      destinationScopeItemSet2 = std::nullopt;
    }
    const entities::RoutingRing groupRoutingRing1(
        scopeItem(1), 0.6 * totalTraffic, destinationScopeItemSet1);

    groupRoutingRings.push_back(groupRoutingRing1);
    const entities::RoutingRing groupRoutingRing2(
        scopeItem(3), 0.4 * totalTraffic, destinationScopeItemSet2);

    groupRoutingRings.push_back(groupRoutingRing2);
    auto groupToRoutingRings =
        entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>();
    groupToRoutingRings.emplace(group1, groupRoutingRings);

    auto routingConfig = std::make_shared<entities::RoutingConfig>(
        std::move(groupToRoutingRings),
        latencyTablePtr,
        regionScopeId,
        partition1Id,
        defaultOriginToDestinationScopeItemSets);

    co_await addRoutingConfig(
        "routing_config",
        entities::RoutingConfigData{std::move(routingConfig)});

    // Now build the universe
    universe_ = buildUniverse();
  }

  [[nodiscard]] std::shared_ptr<GroupRoutingRing> buildRoutingRing() const {
    return std::make_shared<GroupRoutingRing>(
        routingConfigId("routing_config"),
        groupId(partitionId("partition1"), "group1"),
        *universe_,
        Assignment(universe_->getContainers().getInitialAssignment()));
  }

  [[nodiscard]] entities::ObjectId tenant(const int i) const {
    return universe_->getObjectId(fmt::format("tenant{}", i));
  }

  [[nodiscard]] entities::ContainerId region(const int i) const {
    return universe_->getContainerId(fmt::format("region{}", i));
  }

  [[nodiscard]] entities::ScopeItemId scopeItem(const int i) const {
    return ExpressionTestsBase::scopeItemId(
        scopeId("region"), fmt::format("region{}", i));
  }

  void testExpression(
      Expression& expr,
      double expectedValueBefore,
      double expectedValueAfter) {
    EXPECT_NEAR(expectedValueBefore, expr.getInitialValue(), 1e-8);

    auto assignment = makeAssignmentWithOverrides(
        {{tenant(1), region(1)},
         {tenant(2), region(3)},
         {tenant(3), region(4)}});
    const double valueBefore = _apply(expr, assignment);
    EXPECT_NEAR(expectedValueBefore, valueBefore, 1e-8);

    auto changes = ChangeSet(
        {Change(tenant(1), region(1), -1), Change(tenant(1), region(2), 1)});
    const double evalChangeValue = evaluate(expr, changes);
    EXPECT_NEAR(expectedValueAfter, evalChangeValue, 1e-8);

    Context context;
    context.changes() = changes;
    // change assignment
    for (auto& change : changes) {
      if (change.getValue() == 1) {
        assignment.moveTo(change.getObject(), change.getContainer());
      }
    }

    auto valueAfter = _applyChanges(expr, context, assignment);
    EXPECT_NEAR(expectedValueAfter, valueAfter, 1e-8);
  }

  void testGroupLatencyExpression(
      interface::RoutingLatencyMetricInfo metric,
      double expectedValueBefore,
      double expectedValueAfter) {
    // This function is used to test the value of a GroupLatencyLookup
    // expression with the given parameter 'metric'. The expectedValueBefore
    // denotes the value of the expression w.r.t. the initial assignment (i.e.,
    // the aggregated latency of the group using the given metric), and
    // expectedValueAfter is the value after a simple change is applied.
    auto groupLatencyLookup = std::make_shared<GroupRoutingLatencyLookup>(
        buildRoutingRing(), metric, *universe_);
    testExpression(
        *groupLatencyLookup, expectedValueBefore, expectedValueAfter);
  }

  void testGroupTrafficExpression(
      entities::ScopeItemId destinationScopeItem,
      double expectedValueBefore,
      double expectedValueAfter) {
    // This function is used to test the value of a GroupTrafficLookup
    // expression for a given destination scope item. The expectedValueBefore
    // denotes the value of the expression w.r.t. the initial assignment, and
    // expectedValueAfter is the value after a simple change is applied.
    auto groupTrafficLookup = std::make_shared<GroupRoutingTrafficLookup>(
        buildRoutingRing(), destinationScopeItem, *universe_);
    testExpression(
        *groupTrafficLookup, expectedValueBefore, expectedValueAfter);
  }

  std::shared_ptr<const entities::Universe> universe_;
};

CO_TEST_F(GroupRoutingExpressionsTest, GroupRoutingLatencyLookup) {
  co_await setup(/*useDefaultOriginToScopeItemSets=*/false);
  // In the initial assignment:
  //    60% of traffic from region1 is equally split between regions 1, 3, 4 =>
  //    0.2*0 + 0.2*0 + 0.2*10 = 2; 40% of traffic from region3 is sent fully to
  //    region1 => 0.4*5 = 2; Therefore, avg latency is (2 + 2) = 4
  //
  // After applying changes (tenant1 moving from region1 to region2)
  //    60% of traffic from region1 is fully sent to region2 => 0.6*10 = 6; 40%
  //    of traffic from region3 is equally split between regions 2, 3, 4 =>
  //    0.4/3*5 + 0.4/3*0 + 0.4/3*5 = 4/3; Therefore, avg latency is (6 + 4/3)
  testGroupLatencyExpression(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::AVG),
      4.0,
      (6 + 4.0 / 3.0));

  // In the initial assignment:
  //    60% of traffic from region1 is equally split between regions 1, 3, 4 =>
  //    max(0, 0 ,10) = 10; 40% of traffic from region3 is sent fully to
  //    region1 => max(5) = 5; Therefore, max latency is max(5, 10) = 10
  //
  // After applying changes (tenant1 moving from region1 to region2)
  //    60% of traffic from region1 is fully sent to region2 => max(10) = 10;
  //    40% of traffic from region3 is equally split between regions 2, 3, 4 =>
  //    max(5, 0, 5) = 5 = 5; Therefore, avg latency is max(5, 10) = 10
  testGroupLatencyExpression(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::PERCENTILE, 100),
      10.0,
      10.0);

  // P99 is the same as MAX in the example (see
  // interface/tests/RoutingLatencyTest.cpp for example were P99 is different
  // from MAX)
  testGroupLatencyExpression(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::PERCENTILE, 99),
      10.0,
      10.0);

  // P50 is 5 w.r.t. initial assignment and 10 w.r.t. the assignment after
  testGroupLatencyExpression(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::PERCENTILE, 50),
      5.0,
      10.0);

  // P20 is 0 w.r.t. initial assignment and 5 w.r.t. the assignment after
  testGroupLatencyExpression(
      thriftUtils::makeRoutingLatencyMetric(
          interface::RoutingLatencyMetric::PERCENTILE, 20),
      0.0,
      5.0);
}

CO_TEST_F(GroupRoutingExpressionsTest, GroupRoutingTrafficLookup) {
  co_await setup(false);
  // In the initial assignment:
  //    60% of traffic from region1 is equally sent to regions 1, 3, 4 =>
  //    each gets 0.2 fraction of the traffic;  40% of traffic from region3 is
  //    sent fully to region1.
  //
  // After applying changes (tenant1 moving from region1 to region2)
  //    60% of traffic from region1 is fully sent to region2; 40% of traffic
  //    from region3 is equally split between regions 2, 3, 4, so each get 0.4/3
  //    fraction of the traffic
  testGroupTrafficExpression(scopeItem(1) /*region1*/, (0.2 + 0.4), 0.0);
  testGroupTrafficExpression(scopeItem(2) /*region2*/, 0.0, (0.6 + 0.4 / 3));
  testGroupTrafficExpression(scopeItem(3) /*region3*/, 0.2, 0.4 / 3);
  testGroupTrafficExpression(scopeItem(4) /*region4*/, 0.2, 0.4 / 3);
}

CO_TEST_F(GroupRoutingExpressionsTest, ApplyMoveOutOfRegionWithTwoTenants) {
  co_await setup(/*useDefaultOriginToScopeItemSets=*/false);
  const auto groupTrafficLookup = std::make_shared<GroupRoutingTrafficLookup>(
      buildRoutingRing(), scopeItem(1) /*region1*/, *universe_);
  auto assignment = makeAssignmentWithOverrides({{tenant(2), region(4)}});
  _apply(*groupTrafficLookup, assignment);

  // Move tenant2 out of region4, which still has tenant3. If the move were
  // applied twice, region4 would look empty and region1 would get 0.3 + 0.4.
  Context context;
  context.changes() = ChangeSet(
      {Change(tenant(2), region(4), -1), Change(tenant(2), region(3), 1)});
  assignment.moveTo(tenant(2), region(3));

  // Use a large object and container count for AffectedByChangeDecisionData so
  // that GroupRoutingRing::isAffectedByChange keeps both containers and
  // objects and does not use any optimizatinos
  // inhttps://fburl.com/code/3uhileg7.
  constexpr int maxNumObjects = 100;
  constexpr int maxNumContainers = 100;
  const AffectedByChangeDecisionData data(maxNumObjects, maxNumContainers);
  Orchestrator orchestrator;
  orchestrator.init({groupTrafficLookup.get()}, data);
  orchestrator.apply(context, assignment);

  EXPECT_DOUBLE_EQ(0.2 + 0.4, groupTrafficLookup->value);
}

CO_TEST_F(GroupRoutingExpressionsTest, useDefaultOriginToScopeItemSets) {
  co_await setup(/*useDefaultOriginToScopeItemSets=*/true);
  {
    testGroupLatencyExpression(
        thriftUtils::makeRoutingLatencyMetric(
            interface::RoutingLatencyMetric::AVG),
        4.0,
        (6 + 4.0 / 3.0));
    testGroupLatencyExpression(
        thriftUtils::makeRoutingLatencyMetric(
            interface::RoutingLatencyMetric::PERCENTILE, 100),
        10.0,
        10.0);
    testGroupLatencyExpression(
        thriftUtils::makeRoutingLatencyMetric(
            interface::RoutingLatencyMetric::PERCENTILE, 99),
        10.0,
        10.0);
  }
  {
    testGroupTrafficExpression(scopeItem(1) /*region1*/, (0.2 + 0.4), 0.0);
    testGroupTrafficExpression(scopeItem(2) /*region2*/, 0.0, (0.6 + 0.4 / 3));
    testGroupTrafficExpression(scopeItem(3) /*region3*/, 0.2, 0.4 / 3);
    testGroupTrafficExpression(scopeItem(4) /*region4*/, 0.2, 0.4 / 3);
  }
}

CO_TEST_F(GroupRoutingExpressionsTest, ThrowWhenTrafficTableNotPopulated) {
  // When all objects are in a dummy container (not in any routing destination),
  // the traffic table is not fully populated and construction of Lookup
  // expressions throws because setInitialValue triggers the validation.
  universeBuilder_.setObjectTypeName("tenant");
  universeBuilder_.setContainerTypeName("region");

  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"region1", {}}, {"dummyRegion", {"tenant1"}}});
  co_await addPartition(
      "partition1",
      entities::Map<std::string, std::vector<std::string>>{
          {"group1", {"tenant1"}}});

  const auto regionScopeId = scopeId("region");
  const auto partition1Id = partitionId("partition1");
  const auto partitionData = co_await universeBuilder_.getPartition(
      universeBuilder_.getPartitionId("partition1"));
  const auto group1 = partitionData->getGroupId("group1");

  auto latencyTablePtr = std::make_shared<
      entities::Map<entities::ScopeItemId, SortedDestinationLatencyMap>>();
  latencyTablePtr->emplace(
      scopeItem(1), SortedDestinationLatencyMap({{scopeItem(1), 0}}));

  std::vector<entities::RoutingRing> groupRoutingRings;
  groupRoutingRings.emplace_back(
      scopeItem(1),
      /*originTraffic=*/100.0,
      std::vector<std::vector<entities::ScopeItemId>>{{scopeItem(1)}});

  auto groupToRoutingRings =
      entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>();
  groupToRoutingRings.emplace(group1, groupRoutingRings);

  auto routingConfig = std::make_shared<entities::RoutingConfig>(
      std::move(groupToRoutingRings),
      latencyTablePtr,
      regionScopeId,
      partition1Id,
      std::make_shared<entities::Map<
          entities::ScopeItemId,
          std::vector<std::vector<entities::ScopeItemId>>>>());

  co_await addRoutingConfig(
      "routing_config", entities::RoutingConfigData{std::move(routingConfig)});

  universe_ = buildUniverse();

  const auto groupRoutingRing = buildRoutingRing();

  REBALANCER_EXPECT_RUNTIME_ERROR_CONTAINS(
      std::make_shared<GroupRoutingTrafficLookup>(
          groupRoutingRing, scopeItem(1), *universe_),
      "Expected total traffic from all origins to equal 1.0");

  REBALANCER_EXPECT_RUNTIME_ERROR_CONTAINS(
      std::make_shared<GroupRoutingLatencyLookup>(
          groupRoutingRing,
          thriftUtils::makeRoutingLatencyMetric(
              interface::RoutingLatencyMetric::AVG),
          *universe_),
      "Expected total traffic from all origins to equal 1.0");
}

CO_TEST_F(GroupRoutingExpressionsTest, ThrowWhenSomeOriginsAreStranded) {
  // Two origins with disjoint destination sets. The single tenant lives in
  // region1, which is in origin1's destinations but NOT origin2's. So origin1
  // contributes its full share (0.6) and origin2 is stranded (contributes 0),
  // making totalTrafficFromAllOrigins == 0.6 != 1.0, causing a throw.
  universeBuilder_.setObjectTypeName("tenant");
  universeBuilder_.setContainerTypeName("region");
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"region1", {"tenant1"}}, {"region2", {}}});
  co_await addPartition(
      "partition1",
      entities::Map<std::string, std::vector<std::string>>{
          {"group1", {"tenant1"}}});
  const auto regionScopeId = scopeId("region");
  const auto partition1Id = partitionId("partition1");
  const auto partitionData = co_await universeBuilder_.getPartition(
      universeBuilder_.getPartitionId("partition1"));
  const auto group1 = partitionData->getGroupId("group1");
  auto latencyTablePtr = std::make_shared<
      entities::Map<entities::ScopeItemId, SortedDestinationLatencyMap>>();
  latencyTablePtr->emplace(
      scopeItem(1), SortedDestinationLatencyMap({{scopeItem(1), 0}}));
  latencyTablePtr->emplace(
      scopeItem(2), SortedDestinationLatencyMap({{scopeItem(2), 0}}));
  std::vector<entities::RoutingRing> groupRoutingRings;
  groupRoutingRings.emplace_back(
      scopeItem(1),
      /*originTraffic=*/60.0,
      std::vector<std::vector<entities::ScopeItemId>>{{scopeItem(1)}});
  groupRoutingRings.emplace_back(
      scopeItem(2),
      /*originTraffic=*/40.0,
      std::vector<std::vector<entities::ScopeItemId>>{{scopeItem(2)}});
  auto groupToRoutingRings =
      entities::Map<entities::GroupId, std::vector<entities::RoutingRing>>();
  groupToRoutingRings.emplace(group1, groupRoutingRings);
  auto routingConfig = std::make_shared<entities::RoutingConfig>(
      std::move(groupToRoutingRings),
      latencyTablePtr,
      regionScopeId,
      partition1Id,
      std::make_shared<entities::Map<
          entities::ScopeItemId,
          std::vector<std::vector<entities::ScopeItemId>>>>());
  co_await addRoutingConfig(
      "routing_config", entities::RoutingConfigData{std::move(routingConfig)});
  universe_ = buildUniverse();
  const auto groupRoutingRing = buildRoutingRing();

  REBALANCER_EXPECT_RUNTIME_ERROR(
      std::make_shared<GroupRoutingTrafficLookup>(
          groupRoutingRing, scopeItem(1), *universe_),
      "Expected total traffic from all origins to equal 1.0, but found 0.6. Do the traffic values for each (origin, destination) pair reflect the fraction of total traffic from all origins to that destination?");

  REBALANCER_EXPECT_RUNTIME_ERROR(
      std::make_shared<GroupRoutingLatencyLookup>(
          groupRoutingRing,
          thriftUtils::makeRoutingLatencyMetric(
              interface::RoutingLatencyMetric::AVG),
          *universe_),
      "Expected total traffic from all origins to equal 1.0, but found 0.6. Do the traffic values for each (origin, destination) pair reflect the fraction of total traffic from all origins to that destination?");
}

} // namespace facebook::rebalancer::packer::tests
