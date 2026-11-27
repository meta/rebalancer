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
#include "algopt/rebalancer/materializer/spec_builder/MinimizeContainersSpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::materializer::tests {

namespace {
void setMaxFreeLimit(interface::MinimizeContainersSpec& spec, int32_t limit) {
  interface::MinimizeContainersTarget target;
  target.set_maxFreeLimit(limit);
  spec.target() = std::move(target);
}
} // namespace

class MinimizeContainersSpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  static folly::coro::Task<void> setUpCoro() {
    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  void setUpDefaultUniverse() {
    setUpUniverse({
        {"host0", {"task0", "task1"}},
        {"host1", {"task2", "task3"}},
    });
  }

  void createScenario() {
    // 10 hosts, 2 tasks per host
    entities::Map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(10)) {
      assignment[fmt::format("host{}", (i))] = {};
    }
    for (const auto j : folly::irange(2)) {
      for (const auto i : folly::irange(8)) {
        auto host = fmt::format("host{}", i);
        auto task = fmt::format("task{}", j * 8 + i);
        assignment[host].push_back(task);
      }
    }

    setUpUniverse(assignment);
  }

  void createScenario2() {
    // 10 hosts, 2 tasks per host
    entities::Map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(10)) {
      assignment[fmt::format("host{}", (i))] = {};
    }
    for (const auto i : folly::irange(16)) {
      auto task = fmt::format("task{}", i);
      assignment[fmt::format("host{}", 0)].push_back(task);
    }

    setUpUniverse(assignment);
  }
};

TEST_F(MinimizeContainersSpecBuilderTest, Constraint) {
  setUpDefaultUniverse();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, false);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.constraints(expressionBuilder())),
      "MinimizeContainersSpec not supported as a constraint");
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, GoalBasic) {
  setUpDefaultUniverse();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, false);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // initially goal will not be satisfied
  EXPECT_NEAR(2, evaluate(goal, deltaFromInitial({})), 1e-8);

  // one host is empty hence value will be 1
  EXPECT_NEAR(
      1,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task1", "host1"}})),
      1e-8);
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, GoalContinous) {
  setUpDefaultUniverse();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.formula() = interface::MinimizeContainerSpecFormula::LEGACY;

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  // initially goal will not be satisfied
  // = ((-1) (2 ^ 1.25) + (2 ^ 1.25) ) / 5 = -0.9513656
  EXPECT_NEAR(-0.9513656, evaluate(goal, deltaFromInitial({})), 1e-7);
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, GoalWithCapacity) {
  setUpDefaultUniverse();

  std::map<std::string, double> containerCosts = {{"host0", 100}, {"host1", 1}};

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.containerCosts() = std::move(containerCosts);

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, false);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());
  EXPECT_NEAR(101, evaluate(goal, deltaFromInitial({})), 1e-7);

  // host1 has lower penatly
  EXPECT_NEAR(
      1,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task1", "host1"}})),
      1e-8);
}

TEST_F(MinimizeContainersSpecBuilderTest, SpecInfo) {
  setUpDefaultUniverse();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, false);

  auto expectedSpecInfo = SpecParameters{
      .name = "",
      .scope = "host",
      .dimension = "task_count",
      .filterAllowListSize = 0,
      .filterBlockListSize = 0};
  EXPECT_EQ(expectedSpecInfo, specBuilder.getSpecInfo());
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, maxFreeLimit) {
  createScenario();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = interface::MinimizeContainerSpecFormula::NEW;

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  packer::tests::LpAssertOptions assertOptions = {
      .exceptionForLpExpr =
          "At least one of the operands must be a binary variable"};
  EXPECT_NEAR(
      1.5796936900910941,
      evaluate(goal, deltaFromInitial({}), assertOptions),
      1e-7);
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, maxFreeLimit2) {
  createScenario2();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = interface::MinimizeContainerSpecFormula::NEW;

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  packer::tests::LpAssertOptions assertOptions = {
      .exceptionForLpExpr =
          "At least one of the operands must be a binary variable"};
  EXPECT_NEAR(0, evaluate(goal, deltaFromInitial({}), assertOptions), 1e-7);
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, maxFreeLimit3) {
  createScenario();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = interface::MinimizeContainerSpecFormula::NEW;

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  packer::tests::LpAssertOptions assertOptions = {
      .exceptionForLpExpr =
          "At least one of the operands must be a binary variable"};
  EXPECT_NEAR(
      1.5796211302855405,
      evaluate(goal, deltaFromInitial({{"task15", "host1"}}), assertOptions),
      1e-7);
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, maxFreeLimit4) {
  createScenario();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = interface::MinimizeContainerSpecFormula::NEW;

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, true);

  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  packer::tests::LpAssertOptions assertOptions = {
      .exceptionForLpExpr =
          "At least one of the operands must be a binary variable"};

  EXPECT_NEAR(
      1.57932932654772,
      evaluate(
          goal,
          deltaFromInitial({{"task6", "host1"}, {"task14", "host1"}}),
          assertOptions),
      1e-7);
}

// minUsedLimit=U and maxFreeLimit=(N-U) are duals for a fixed scope of N items,
// so both must produce the same goal. createScenario has N=10 hosts, so
// minUsedLimit=7 must match maxFreeLimit=3.
CO_TEST_F(MinimizeContainersSpecBuilderTest, minUsedLimitDualToMaxFreeLimit) {
  createScenario();
  const auto universe = buildUniverse();

  interface::MinimizeContainersSpec freeSpec;
  freeSpec.scope() = "host";
  freeSpec.dimension() = "task_count";
  freeSpec.formula() = interface::MinimizeContainerSpecFormula::NEW;
  setMaxFreeLimit(freeSpec, 3);

  interface::MinimizeContainersSpec usedSpec;
  usedSpec.scope() = "host";
  usedSpec.dimension() = "task_count";
  usedSpec.formula() = interface::MinimizeContainerSpecFormula::NEW;
  interface::MinimizeContainersTarget target;
  target.set_minUsedLimit(7);
  usedSpec.target() = target;

  const MinimizeContainersSpecBuilder freeBuilder(universe, freeSpec, true);
  const MinimizeContainersSpecBuilder usedBuilder(universe, usedSpec, true);

  const auto freeGoal = co_await freeBuilder.goalCoro(expressionBuilder());
  const auto usedGoal = co_await usedBuilder.goalCoro(expressionBuilder());

  const packer::tests::LpAssertOptions assertOptions = {
      .exceptionForLpExpr =
          "At least one of the operands must be a binary variable"};
  const auto delta =
      deltaFromInitial({{"task6", "host1"}, {"task14", "host1"}});
  EXPECT_NEAR(
      evaluate(freeGoal, delta, assertOptions),
      evaluate(usedGoal, delta, assertOptions),
      1e-9);
}

TEST_F(
    MinimizeContainersSpecBuilderTest,
    minUsedLimitUnsupportedInLegacyFormula) {
  createScenario();

  interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.formula() = interface::MinimizeContainerSpecFormula::LEGACY;
  interface::MinimizeContainersTarget target;
  target.set_minUsedLimit(7);
  spec.target() = target;

  const MinimizeContainersSpecBuilder specBuilder(buildUniverse(), spec, true);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.goalCoro(expressionBuilder())),
      "Custom stopping condition (maxFreeLimit/minUsedLimit) not supported in "
      "minimize containers goal in LEGACY formula but is supported in NEW formula");
}

CO_TEST_F(MinimizeContainersSpecBuilderTest, ContinuousFormulasWithZeroCost) {
  // host9 starts empty
  createScenario();
  const auto universe = buildUniverse();

  for (const auto formula :
       {interface::MinimizeContainerSpecFormula::NEW,
        interface::MinimizeContainerSpecFormula::LEGACY}) {
    interface::MinimizeContainersSpec spec;
    spec.scope() = "host";
    spec.dimension() = "task_count";
    spec.formula() = formula;
    spec.containerCosts() = {{"host9", 0.0}};

    const MinimizeContainersSpecBuilder specBuilder(
        universe, spec, /*continuousExpressions=*/true);
    REBALANCER_EXPECT_RUNTIME_ERROR(
        folly::coro::blockingWait(specBuilder.goalCoro(expressionBuilder())),
        "Non-positive containerCosts are not supported when using MinimizeContainerSpec with a continuous formula; 'host9' has cost 0");

    // the discrete (optimal solver) formula supports it: host9 does not count
    const MinimizeContainersSpecBuilder discreteBuilder(
        universe, spec, /*continuousExpressions=*/false);
    const auto discreteGoal =
        co_await discreteBuilder.goalCoro(expressionBuilder());
    EXPECT_NEAR(8, evaluate(discreteGoal, deltaFromInitial({})), 1e-8);
  }
}
} // namespace facebook::rebalancer::materializer::tests
