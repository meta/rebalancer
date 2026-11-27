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
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/spec_builder/SumOfMaxSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

class SumOfMaxSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {{"host0", {"task0", "task1"}},
         {"host1", {"task2", "task3"}},
         {"host2", {"task4", "task5"}}});

    co_await addPartition(
        "job",
        {{"job1", {"task0", "task3", "task2"}},
         {"job2", {"task1", "task4", "task5"}}});

    const entities::Map<entities::ObjectId, double> cpuValues = {
        {objectId("task0"), 10},
        {objectId("task1"), 20},
        {objectId("task2"), 30},
        {objectId("task3"), 40},
        {objectId("task4"), 50},
        {objectId("task5"), 60}};
    co_await addObjectDimension("cpu", cpuValues, 0);

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }
};

static interface::SumOfMaxSpec createSumOfMaxSpec() {
  interface::SumOfMaxSpec spec;
  spec.name() = "test";
  spec.dimension() = "cpu";
  spec.partitionName() = "job";
  spec.scope() = "host";
  spec.filter() = interface::Filter();
  return spec;
}

CO_TEST_F(SumOfMaxSpecBuilderTest, Goal) {
  const SumOfMaxSpecBuilder specBuilder(buildUniverse(), createSumOfMaxSpec());

  EXPECT_EQ("SumOfMax(job, cpu) for scope host", specBuilder.description());
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  EXPECT_NEAR(200, evaluate(goal, deltaFromInitial({})), 1e-8);

  auto expectedSpecInfo = SpecParameters{
      .name = "test",
      .scope = "host",
      .partition = "job",
      .dimension = "cpu",
      .filterAllowListSize = 0,
      .filterBlockListSize = 0};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}

TEST_F(SumOfMaxSpecBuilderTest, Constraint) {
  SumOfMaxSpecBuilder specBuilder(buildUniverse(), createSumOfMaxSpec());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.constraints(expressionBuilder())),
      "SumOfMaxSpec not supported as a constraint");
}

class SumOfMaxSpecBuilderEmptyPartitionTest : public SpecBuilderTestBase<> {
 protected:
  void SetUp() override {
    // The partition has no groups; task0 belongs to none.
    setUpUniverse({{"host0", {}}, {"host1", {"task0"}}});
    folly::coro::blockingWait(addPartition("msb", {}));
    folly::coro::blockingWait(addObjectDimension(
        "cpu", entities::Map<entities::ObjectId, double>{}, 10));
  }
};

TEST_F(SumOfMaxSpecBuilderEmptyPartitionTest, PartitionWithoutGroups) {
  auto spec = createSumOfMaxSpec();
  spec.partitionName() = "msb";
  spec.filter()->itemsWhitelist() = std::vector<std::string>{"host0", "host1"};
  const SumOfMaxSpecBuilder specBuilder(buildUniverse(), spec);

  // No group has any utilization, so every scope item contributes 0.
  const auto goal =
      folly::coro::blockingWait(specBuilder.goalCoro(expressionBuilder()));
  EXPECT_EQ(0, evaluate(goal, deltaFromInitial({})));
  EXPECT_EQ(0, evaluate(goal, deltaFromInitial({{"task0", "host0"}})));
}

} // namespace facebook::rebalancer::materializer::tests
