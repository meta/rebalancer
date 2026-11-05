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
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/materializer/spec_builder/AvoidAssignmentsSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

class AvoidAssignmentsSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {{"host0", {"task0", "task1"}},
         {"host1", {"task2"}},
         {"host2", {"task3"}}});
    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }
};

CO_TEST_F(AvoidAssignmentsSpecBuilderTest, Constraint) {
  interface::AvoidAssignmentsSpec avoidAssignmentsSpec;
  avoidAssignmentsSpec.scope() = "host";
  avoidAssignmentsSpec.assignments() = {
      interface::makeAvoidAssignment("task0", {"host0", "host1"}),
      interface::makeAvoidAssignment("task1", {"host0", "host2"}),
      interface::makeAvoidAssignment("task2", {"host0"})};

  const AvoidAssignmentsSpecBuilder specBuilder(
      buildUniverse(), avoidAssignmentsSpec);

  EXPECT_EQ("Avoid 3 assignments on scope host", specBuilder.description());

  auto expectedSpecInfo =
      SpecParameters{.name = "", .scope = "host", .size = 3};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());

  auto constraint = co_await specBuilder.constraints(expressionBuilder());
  // Constraints are per scope-item (host); order depends on hash map iteration.
  // Identify which constraint corresponds to which host by behavior.
  std::optional<size_t> host0Ctr;
  std::optional<size_t> host1Ctr;
  std::optional<size_t> host2Ctr;
  for (const auto i : folly::irange(constraint.size())) {
    const double initVal = evaluate(constraint.at(i), deltaFromInitial({}));
    if (std::abs(initVal - 2) < 1e-8) {
      host0Ctr = i;
    } else if (
        std::abs(
            evaluate(constraint.at(i), deltaFromInitial({{"task0", "host1"}})) -
            1) < 1e-8) {
      host1Ctr = i;
    } else {
      host2Ctr = i;
    }
  }
  EXPECT_TRUE(host0Ctr.has_value());
  EXPECT_TRUE(host1Ctr.has_value());
  EXPECT_TRUE(host2Ctr.has_value());

  // value will be 2 as task0 and task1 are on host0
  EXPECT_NEAR(
      2, evaluate(constraint.at(*host0Ctr), deltaFromInitial({})), 1e-8);
  // value will be 0 as both tasks moved out of host0
  EXPECT_NEAR(
      0,
      evaluate(
          constraint.at(*host0Ctr),
          deltaFromInitial({{"task0", "host2"}, {"task1", "host1"}})),
      1e-8);

  // value will be 0 as host1 doesn't have task0
  EXPECT_NEAR(
      0, evaluate(constraint.at(*host1Ctr), deltaFromInitial({})), 1e-8);
  // value will be 1 as task0 moved to host1
  EXPECT_NEAR(
      1,
      evaluate(
          constraint.at(*host1Ctr), deltaFromInitial({{"task0", "host1"}})),
      1e-8);

  // value will be 0 as host2 doesn't have task1
  EXPECT_NEAR(
      0, evaluate(constraint.at(*host2Ctr), deltaFromInitial({})), 1e-8);
}

TEST_F(AvoidAssignmentsSpecBuilderTest, FilterBlocksAvoidedAssignments) {
  interface::AvoidAssignmentsSpec avoidAssignmentsSpec;
  avoidAssignmentsSpec.scope() = "host";
  avoidAssignmentsSpec.assignments() = {
      interface::makeAvoidAssignment("task0", {"host0", "host1"}),
      interface::makeAvoidAssignment("task1", {"host0", "host2"}),
      interface::makeAvoidAssignment("task2", {"host0"})};

  const AvoidAssignmentsSpecBuilder specBuilder(
      buildUniverse(), avoidAssignmentsSpec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // task0 and task1 start on host0, so only task2 is blocked from it.
  const std::set<InvalidPair> expectedInvalidPairs{
      {objectId("task0"), containerId("host1")},
      {objectId("task1"), containerId("host2")},
      {objectId("task2"), containerId("host0")}};
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(
    AvoidAssignmentsSpecBuilderTest,
    FilterUsesUpdatedInitialAssignmentForInitialViolations) {
  interface::AvoidAssignmentsSpec avoidAssignmentsSpec;
  avoidAssignmentsSpec.scope() = "host";
  avoidAssignmentsSpec.assignments() = {
      interface::makeAvoidAssignment("task0", {"host1"}),
      interface::makeAvoidAssignment("task3", {"host1", "host2"})};

  const AvoidAssignmentsSpecBuilder specBuilder(
      buildUniverse(), avoidAssignmentsSpec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  specBuilder.populateInvalidMoveFilter(
      filter,
      Assignment(deltaFromInitial({{"task0", "host1"}, {"task3", "host0"}})));

  // task0 starts on host1 in the updated assignment; task3 now starts on host0.
  const std::set<InvalidPair> expectedInvalidPairs{
      {objectId("task3"), containerId("host1")},
      {objectId("task3"), containerId("host2")}};
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(AvoidAssignmentsSpecBuilderTest, Goal) {
  const interface::AvoidAssignmentsSpec spec;
  AvoidAssignmentsSpecBuilder specBuilder(buildUniverse(), spec);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.goalCoro(expressionBuilder())),
      "AvoidAssignmentsSpec not supported as a goal");
}
} // namespace facebook::rebalancer::materializer::tests
