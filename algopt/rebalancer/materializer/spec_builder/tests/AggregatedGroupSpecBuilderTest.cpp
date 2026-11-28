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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/spec_builder/AggregatedGroupSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <gtest/gtest.h>

#include <vector>

namespace facebook::rebalancer::materializer::tests {

namespace interface = facebook::rebalancer::interface;

class AggregatedGroupSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {{"container1", {"obj1", "obj2", "obj3"}}, {"container2", {"obj4"}}},
        "obj",
        "container");

    co_await addPartition(
        "partition",
        {{"group1", {"obj1", "obj2", "obj4"}}, {"group2", {"obj3"}}});

    co_await addScope("scope", {{"item", {"container1", "container2"}}});

    // set up static dimension
    co_await addObjectDimension(
        "size",
        {{objectId("obj1"), 1},
         {objectId("obj2"), 2},
         {objectId("obj3"), 3},
         {objectId("obj4"), 4}},
        0);

    // set up dynamic dimension
    co_await addDynamicObjectDimension(
        "dynamicSize",
        scopeId("scope"),
        {{"item",
          makeSharedPtrEntityToValueMap(
              {{objectId("obj1"), 1},
               {objectId("obj2"), 2},
               {objectId("obj3"), 3},
               {objectId("obj4"), 4}})}},
        0);

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }
};

TEST_F(AggregatedGroupSpecBuilderTest, Description) {
  interface::AggregatedGroupSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);
  EXPECT_EQ(
      "Minimize aggregated groupe size of hosts weighted by cpu",
      specBuilder.description());
}

TEST_F(AggregatedGroupSpecBuilderTest, SpecInfo) {
  interface::AggregatedGroupSpec spec;
  spec.name() = "test";
  spec.scope() = "host";
  spec.partitionName() = "partition";
  spec.dimension() = "cpu";

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);

  auto expectedSpecInfo = SpecParameters{
      .name = "test",
      .scope = "host",
      .partition = "partition",
      .dimension = "cpu",
      .limitType = "ABSOLUTE",
      .filterAllowListSize = 0,
      .filterBlockListSize = 0};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}

CO_TEST_F(AggregatedGroupSpecBuilderTest, Goal) {
  interface::AggregatedGroupSpec spec;
  spec.scope() = "scope";
  spec.partitionName() = "partition";
  spec.dimension() = "size";
  interface::Limit coef;
  coef.scopeItemLimits() = {{"item", 0.5}};
  spec.contributions() = {{"item", coef}};
  spec.limit()->globalLimit() = 10;
  spec.withinGroupAggregationType() =
      interface::AggregatedGroupSpecAggType::SUM;
  spec.groupAggregationType() = interface::AggregatedGroupSpecAggType::MAX;
  spec.containerAggregationType() = interface::AggregatedGroupSpecAggType::SUM;

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);

  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  EXPECT_EQ(0, evaluate(goal, deltaFromInitial({})));
}

CO_TEST_F(AggregatedGroupSpecBuilderTest, Constraint) {
  interface::AggregatedGroupSpec spec;
  spec.scope() = "scope";
  spec.partitionName() = "partition";
  spec.dimension() = "size";
  interface::Limit coef;
  coef.scopeItemLimits() = {{"item", 0.5}};
  spec.contributions() = {{"item", coef}};
  spec.limit()->globalLimit() = 10;
  spec.withinGroupAggregationType() =
      interface::AggregatedGroupSpecAggType::SUM;
  spec.groupAggregationType() = interface::AggregatedGroupSpecAggType::MAX;
  spec.containerAggregationType() = interface::AggregatedGroupSpecAggType::SUM;

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);

  const auto conts = co_await specBuilder.constraints(expressionBuilder());
  EXPECT_EQ(1, conts.size());
  EXPECT_EQ(-6.5, evaluate(conts[0], deltaFromInitial({})));
}

CO_TEST_F(AggregatedGroupSpecBuilderTest, DynamicDimensionConstraint) {
  interface::AggregatedGroupSpec spec;
  spec.scope() = "scope";
  spec.partitionName() = "partition";
  spec.dimension() = "dynamicSize";
  interface::Limit coef;
  coef.scopeItemLimits() = {{"item", 0.5}};
  spec.contributions() = {{"item", coef}};
  spec.limit()->globalLimit() = 10;
  spec.withinGroupAggregationType() =
      interface::AggregatedGroupSpecAggType::SUM;
  spec.groupAggregationType() = interface::AggregatedGroupSpecAggType::MAX;
  spec.containerAggregationType() = interface::AggregatedGroupSpecAggType::SUM;

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);

  const auto conts = co_await specBuilder.constraints(expressionBuilder());
  EXPECT_EQ(1, conts.size());
  EXPECT_EQ(-6.5, evaluate(conts[0], deltaFromInitial({})));
}

CO_TEST_F(AggregatedGroupSpecBuilderTest, DynamicDimensionGoal) {
  interface::AggregatedGroupSpec spec;
  spec.scope() = "scope";
  spec.partitionName() = "partition";
  spec.dimension() = "dynamicSize";
  interface::Limit coef;
  coef.scopeItemLimits() = {{"item", 0.5}};
  spec.contributions() = {{"item", coef}};
  spec.limit()->globalLimit() = 10;
  spec.withinGroupAggregationType() =
      interface::AggregatedGroupSpecAggType::SUM;
  spec.groupAggregationType() = interface::AggregatedGroupSpecAggType::MAX;
  spec.containerAggregationType() = interface::AggregatedGroupSpecAggType::SUM;

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);

  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  EXPECT_EQ(0, evaluate(goal, deltaFromInitial({})));
}

// No object of the partition has a non-zero weight in the scope item, so the
// scope item's aggregated value is 0 and the constraint value is 0 - 10.
CO_TEST_F(AggregatedGroupSpecBuilderTest, ConstraintWithAllZeroWeights) {
  co_await addObjectDimension(
      "zeroSize",
      {{objectId("obj1"), 0},
       {objectId("obj2"), 0},
       {objectId("obj3"), 0},
       {objectId("obj4"), 0}},
      0);
  interface::AggregatedGroupSpec spec;
  spec.scope() = "scope";
  spec.partitionName() = "partition";
  spec.dimension() = "zeroSize";
  spec.limit()->globalLimit() = 10;

  const AggregatedGroupSpecBuilder specBuilder(buildUniverse(), spec);

  const auto conts = co_await specBuilder.constraints(expressionBuilder());
  CO_ASSERT_EQ(1, conts.size());
  EXPECT_EQ(-10, evaluate(conts[0], deltaFromInitial({})));
  const auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  EXPECT_EQ(0, evaluate(goal, deltaFromInitial({})));
}
} // namespace facebook::rebalancer::materializer::tests
