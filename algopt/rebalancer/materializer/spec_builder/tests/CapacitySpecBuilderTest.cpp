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
#include "algopt/rebalancer/materializer/spec_builder/CapacitySpecBuilder.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace facebook::rebalancer::materializer::tests {

namespace entities = facebook::rebalancer::entities;
namespace interface = facebook::rebalancer::interface;

class CapacitySpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {{"host0", {"task0", "task1"}},
         {"host1", {"task2"}},
         {"host2", {}},
         {"host3", {"task3", "task4", "task5"}}});

    co_await addObjectDimension(
        "cpu",
        {{task(0), 0},
         {task(1), 0.1},
         {task(2), 0.2},
         {task(3), 0.4},
         {task(4), 0.8},
         {task(5), 1.6}},
        0);

    co_await addScopeDimension(
        "cpu",
        scopeId("host"),
        {{"host0", 0}, {"host1", 1}, {"host2", 2}, {"host3", 4}},
        0);

    co_return;
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  entities::ObjectId task(int index) const {
    return objectId(fmt::format("task{}", index));
  }

  std::set<InvalidPair> collectInvalidPairsFor(
      const interface::CapacitySpec& spec) {
    if (!universe_) {
      buildUniverse();
    }
    const auto& universe = universe_;
    const CapacitySpecBuilder specBuilder(universe, spec);
    InvalidMoveFilter filter(
        universe->getObjects().getObjectIds().size(),
        universe->getContainers().getContainerIds().size());
    specBuilder.populateInvalidMoveFilter(
        filter, expressionBuilder().getUpdatedInitialAssignment());
    return collectInvalidPairs(filter);
  }

  folly::coro::Task<ExprPtr> buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition definition) {
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "cpu";
    capacitySpec.definition() = definition;

    // Only include host3.
    capacitySpec.filter()->itemsWhitelist() = {"host3"};

    // Define an absolute limit of 0 cpu units.
    capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
    capacitySpec.limit()->globalLimit() = 0;

    const CapacitySpecBuilder capacitySpecBuilder(
        buildUniverse(), capacitySpec);
    auto goalExpr = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

    // when used as a goal, we do not expect capacitySpec to return any
    // nonAcceptingContainers
    EXPECT_EQ(0, capacitySpecBuilder.nonAcceptingContainers().size());

    co_return goalExpr;
  }

  static interface::CapacitySpec makeBasicMaxCapacitySpec(
      std::string scope,
      std::string dimension,
      interface::CapacitySpecDefinition definition) {
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = std::move(scope);
    capacitySpec.dimension() = std::move(dimension);
    capacitySpec.definition() = definition;
    capacitySpec.bound() = interface::CapacitySpecBound::MAX;
    capacitySpec.limit()->type() = interface::LimitType::RELATIVE;
    capacitySpec.limit()->globalLimit() = 1.0;

    return capacitySpec;
  }
};

// VCP-style bundles of k = 3: servers are placed into reservations, servers of
// one pod form a usable bundle together, and the `eligible` dimension is 1 for
// server types the reservation accepts and 0 otherwise.
class BundledCapacitySpecBuilderTest : public SpecBuilderTestBase<> {
 protected:
  using Moves = entities::Map<std::string, std::string>;

  inline static const std::vector<std::string> kInitialEligibleInR0 = {
      "mixed0",
      "mixed1",
      "fragmentA0"};
  inline static const std::vector<std::string> kPure = {
      "pure0",
      "pure1",
      "pure2"};
  inline static const std::vector<std::string> kPureInR1 = {
      "pureR1_0",
      "pureR1_1",
      "pureR1_2"};
  // Two eligible servers and an ineligible one of the same pod.
  inline static const std::vector<std::string> kMixedCandidate = {
      "candidate0",
      "candidate1",
      "candidateIneligible"};

  folly::coro::Task<void> setUpCoro() {
    setUpUniverse(
        {// r0 holds 3 eligible servers but no complete bundle.
         {"r0", {"mixed0", "mixed1", "mixedIneligible", "fragmentA0"}},
         {"r1", kPureInR1},
         {"free",
          {"pure0",
           "pure1",
           "pure2",
           "candidate0",
           "candidate1",
           "candidate2",
           "candidateIneligible",
           "fragmentC0"}}},
        "server",
        "reservation");
    co_await addPartition(
        "pod",
        {{"mixed", {"mixed0", "mixed1", "mixedIneligible"}},
         {"fragmentA", {"fragmentA0"}},
         {"pureR1", kPureInR1},
         {"pure", kPure},
         {"candidate",
          {"candidate0", "candidate1", "candidate2", "candidateIneligible"}},
         {"fragmentC", {"fragmentC0"}}});
    co_await addPartition(
        "overlapping", {{"group0", kPure}, {"group1", {"pure2"}}});
    co_await addScope("allowance", {{"a0", {"r0", "r1"}}});
    co_await addScope("rack", {{"rack0", {"r0", "free"}}, {"rack1", {"r1"}}});
    co_await addScope("empty", {{"empty0", {}}});

    co_await addObjectDimension(
        "eligible",
        entities::Map<std::string, double>{
            {"mixedIneligible", 0}, {"candidateIneligible", 0}},
        1);
    co_await addObjectDimension(
        "units",
        entities::Map<std::string, double>{
            {"pure0", 1.5}, {"pure1", 1}, {"pure2", 1}},
        0);
    co_await addObjectDimension(
        "near_bundle",
        entities::Map<std::string, double>{
            {"pure0", 1 - 1e-12}, {"mixedIneligible", 0}},
        1);
    co_await addObjectDimension(
        "signed", entities::Map<std::string, double>{{"pure0", -1}}, 1);
    co_await addObjectDimension(
        "vector",
        std::vector<entities::Map<entities::ObjectId, double>>{{}, {}},
        {1, 1});
    for (const auto& dimension : {"eligible", "units", "near_bundle"}) {
      for (const auto& scope : {"reservation", "allowance"}) {
        co_await addScopeDimension(dimension, scopeId(scope), {}, 1);
      }
    }
  }

  void SetUp() override {
    folly::coro::blockingWait(setUpCoro());
  }

  static interface::GroupBundleUtilization makeBundleUtilization(
      std::map<std::string, double> bundleSizes) {
    interface::GroupBundleUtilization bundleUtilization;
    bundleUtilization.partitionName() = "pod";
    bundleUtilization.scope() = "reservation";
    bundleUtilization.bundleSizes() = std::move(bundleSizes);
    return bundleUtilization;
  }

  static interface::CapacitySpec makeSpec(
      interface::CapacitySpecBound bound,
      double limit,
      interface::GroupBundleUtilization bundleUtilization) {
    interface::CapacitySpec spec;
    spec.scope() = "reservation";
    spec.dimension() = "eligible";
    spec.bound() = bound;
    spec.limit()->type() = interface::LimitType::ABSOLUTE;
    spec.limit()->globalLimit() = limit;
    spec.filter()->itemsWhitelist() = {"r0"};
    spec.groupBundleUtilization() = std::move(bundleUtilization);
    return spec;
  }

  // Spec on r0 with a bundle size of 3.
  static interface::CapacitySpec makeR0Spec(
      interface::CapacitySpecBound bound,
      double limit) {
    return makeSpec(bound, limit, makeBundleUtilization({{"r0", 3}}));
  }

  // A MAX bound of zero evaluates to the usable utilization.
  static interface::CapacitySpec makeUsableUtilSpec() {
    return makeR0Spec(interface::CapacitySpecBound::MAX, 0);
  }

  static Moves moveTo(
      const std::string& reservation,
      const std::vector<std::string>& servers,
      Moves moves = {}) {
    for (const auto& server : servers) {
      moves[server] = reservation;
    }
    return moves;
  }

  void constructSpecBuilder(const interface::CapacitySpec& spec) {
    expressionBuilder();
    [[maybe_unused]] const CapacitySpecBuilder specBuilder(
        getUniversePtr(), spec);
  }

  folly::coro::Task<ConstraintInfo> buildConstraint(
      const interface::CapacitySpec& spec) {
    auto& builder = expressionBuilder();
    const CapacitySpecBuilder specBuilder(getUniversePtr(), spec);
    auto constraints = co_await specBuilder.constraints(builder);
    EXPECT_EQ(1, constraints.size());
    co_return constraints.at(0);
  }

  // Evaluates the constraint, with both the local search and the MIP
  // evaluators, after applying the moves to the initial assignment.
  double evaluateAfter(const ConstraintInfo& constraint, const Moves& moves) {
    return evaluate(constraint, deltaFromInitial(moves));
  }
};

CO_TEST_F(BundledCapacitySpecBuilderTest, CountsOnlyCompletePerGroupBundles) {
  const auto usableUtil = co_await buildConstraint(makeUsableUtilSpec());

  EXPECT_EQ(0, evaluateAfter(usableUtil, moveTo("r0", {"pure0", "pure1"})));
  EXPECT_EQ(
      0,
      evaluateAfter(
          usableUtil, moveTo("r0", {"pure0", "pure1", "fragmentC0"})));
  EXPECT_EQ(3, evaluateAfter(usableUtil, moveTo("r0", kPure)));
  EXPECT_EQ(
      6,
      evaluateAfter(usableUtil, moveTo("r0", kPure, moveTo("r0", kPureInR1))));

  // A zero-dimension server cannot complete its pod's bundle.
  EXPECT_EQ(0, evaluateAfter(usableUtil, moveTo("r0", kMixedCandidate)));
  EXPECT_EQ(
      3,
      evaluateAfter(
          usableUtil,
          moveTo("r0", {"candidate2"}, moveTo("r0", kMixedCandidate))));
}

CO_TEST_F(
    BundledCapacitySpecBuilderTest,
    NonIntegralUtilCountsOnlyCompleteBundles) {
  auto spec = makeUsableUtilSpec();
  spec.dimension() = "units";
  const auto usableUtil = co_await buildConstraint(spec);

  // 2.5 and 3.5 units of the pure pod.
  EXPECT_EQ(0, evaluateAfter(usableUtil, moveTo("r0", {"pure0", "pure1"})));
  EXPECT_EQ(3, evaluateAfter(usableUtil, moveTo("r0", kPure)));

  spec.groupBundleUtilization()->bundleSizes() = {{"r0", 1.75}};
  const auto fractionalBundles = co_await buildConstraint(spec);
  EXPECT_DOUBLE_EQ(
      1.75, evaluateAfter(fractionalBundles, moveTo("r0", {"pure0", "pure1"})));
  EXPECT_DOUBLE_EQ(3.5, evaluateAfter(fractionalBundles, moveTo("r0", kPure)));
}

CO_TEST_F(
    BundledCapacitySpecBuilderTest,
    FloorHonorsPrecisionAtBundleBoundary) {
  auto spec = makeUsableUtilSpec();
  spec.dimension() = "near_bundle";
  const auto usableUtil = co_await buildConstraint(spec);

  EXPECT_EQ(3, evaluateAfter(usableUtil, moveTo("r0", kPure)));
}

CO_TEST_F(BundledCapacitySpecBuilderTest, MinCountsOnlyCompleteBundles) {
  const auto constraint = co_await buildConstraint(
      makeR0Spec(interface::CapacitySpecBound::MIN, 3));

  EXPECT_EQ(3, evaluateAfter(constraint, {}));
  EXPECT_EQ(3, evaluateAfter(constraint, moveTo("free", kInitialEligibleInR0)));
  EXPECT_EQ(3, evaluateAfter(constraint, moveTo("r0", kMixedCandidate)));
  EXPECT_EQ(
      0,
      evaluateAfter(
          constraint,
          moveTo("r0", kPure, moveTo("free", kInitialEligibleInR0))));
}

CO_TEST_F(BundledCapacitySpecBuilderTest, MaxDoesNotGrandfatherInitialRawUtil) {
  const auto constraint = co_await buildConstraint(
      makeR0Spec(interface::CapacitySpecBound::MAX, 1));
  const auto replacement =
      moveTo("r0", kPure, moveTo("free", kInitialEligibleInR0));

  EXPECT_EQ(2, evaluateAfter(constraint, replacement));
}

TEST_F(
    BundledCapacitySpecBuilderTest,
    InvalidMoveFilterSkipsBundledUtilization) {
  expressionBuilder();
  const CapacitySpecBuilder specBuilder(getUniversePtr(), makeUsableUtilSpec());
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_TRUE(filter.empty());
}

CO_TEST_F(BundledCapacitySpecBuilderTest, MixedScopeItemMinCountsRawItemsRaw) {
  auto spec = makeSpec(
      interface::CapacitySpecBound::MIN, 6, makeBundleUtilization({{"r0", 3}}));
  spec.scope() = "allowance";
  spec.filter()->itemsWhitelist() = {"a0"};
  const auto mixedMin = co_await buildConstraint(spec);

  // r0 has no complete bundle; unconfigured r1 contributes its 3 raw servers.
  EXPECT_EQ(3, evaluateAfter(mixedMin, {}));
  EXPECT_EQ(4, evaluateAfter(mixedMin, moveTo("free", {"pureR1_0"})));
  EXPECT_EQ(3, evaluateAfter(mixedMin, moveTo("free", {"fragmentA0"})));
  EXPECT_EQ(6, evaluateAfter(mixedMin, moveTo("free", kPureInR1)));
  EXPECT_EQ(
      2,
      evaluateAfter(
          mixedMin,
          moveTo("r1", {"fragmentC0"}, moveTo("free", {"fragmentA0"}))));
  EXPECT_EQ(
      0,
      evaluateAfter(
          mixedMin, moveTo("r1", kPure, moveTo("free", kInitialEligibleInR0))));
  EXPECT_EQ(0, evaluateAfter(mixedMin, moveTo("r0", kPure)));
}

CO_TEST_F(
    BundledCapacitySpecBuilderTest,
    NestedScopeBundlesOnlyConfiguredItems) {
  auto spec = makeUsableUtilSpec();
  spec.scope() = "allowance";
  spec.filter()->itemsWhitelist() = {"a0"};
  const auto partiallyBundled = co_await buildConstraint(spec);

  // Unconfigured r1 stays raw; incomplete bundles in configured r0 count zero.
  EXPECT_EQ(3, evaluateAfter(partiallyBundled, {}));
  EXPECT_EQ(4, evaluateAfter(partiallyBundled, moveTo("r1", {"fragmentC0"})));
  EXPECT_EQ(3, evaluateAfter(partiallyBundled, moveTo("r0", {"fragmentC0"})));
  EXPECT_EQ(6, evaluateAfter(partiallyBundled, moveTo("r0", kPure)));

  spec.groupBundleUtilization()->bundleSizes() = {{"r0", 3}, {"r1", 3}};
  const auto fullyBundled = co_await buildConstraint(spec);
  EXPECT_EQ(3, evaluateAfter(fullyBundled, moveTo("r1", {"fragmentC0"})));
  // Fragments of one pod split across bundle items never combine.
  EXPECT_EQ(
      3,
      evaluateAfter(
          fullyBundled,
          moveTo("r1", {"pure0", "pure1"}, moveTo("r0", {"pure2"}))));

  spec.groupBundleUtilization()->bundleSizes() = {{"free", 3}};
  const auto outOfScopeBundle = co_await buildConstraint(spec);
  EXPECT_EQ(6, evaluateAfter(outOfScopeBundle, {}));
}

TEST_F(BundledCapacitySpecBuilderTest, RejectsUnsupportedInputs) {
  const auto expectRejected = [&](const std::string& scope,
                                  const std::string& item,
                                  const std::string& partition,
                                  const std::string& message) {
    auto bundleUtilization = makeBundleUtilization({{item, 3}});
    bundleUtilization.scope() = scope;
    bundleUtilization.partitionName() = partition;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        constructSpecBuilder(makeSpec(
            interface::CapacitySpecBound::MAX,
            0,
            std::move(bundleUtilization))),
        message);
  };
  expectRejected(
      "rack",
      "rack0",
      "pod",
      "GroupBundleUtilization requires rack rack0 to be contained in a single item of scope reservation");
  expectRejected(
      "empty",
      "empty0",
      "pod",
      "GroupBundleUtilization requires empty empty0 to contain at least one container");
  expectRejected(
      "reservation",
      "r0",
      "overlapping",
      "GroupBundleUtilization requires a disjoint partition, but partition overlapping has overlapping groups");

  for (const std::string dimension : {"signed", "vector"}) {
    auto spec = makeUsableUtilSpec();
    spec.dimension() = dimension;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        constructSpecBuilder(spec),
        fmt::format(
            "GroupBundleUtilization requires a static scalar dimension without negative values, but got dimension {}",
            dimension));
  }
}

CO_TEST_F(CapacitySpecBuilderTest, Basic) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";

  // The effective limits are:
  // host0: 0 cpu units
  // host1: 0.1 cpu units
  // host2: 0.2 cpu units
  // host3: 2 cpu units
  capacitySpec.limit()->type() = interface::LimitType::RELATIVE;
  capacitySpec.limit()->globalLimit() = 0.1;
  capacitySpec.limit()->scopeItemLimits() = {{"host3", 0.5}};

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto& exprBuilder = expressionBuilder();
  auto goal = co_await capacitySpecBuilder.goalCoro(exprBuilder);
  auto constraint = co_await capacitySpecBuilder.constraints(exprBuilder);

  // In the initial assignment has the following distribution of cpu units:
  // host0: 0.1 cpu units, limit exceeded by 0.1.
  // host1: 0.2 cpu units, limit exceeded by 0.1.
  // host2: 0 cpu units, limit exceeded by 0.
  // host3: 2.8 cpu units, limit exceeded by 0.8.

  // The total excess is 1 cpu unit. The excess gets divided by the average
  // host capacity, which is 1.75 cpu units, due to normalization.
  EXPECT_NEAR(1 / 1.75, evaluate(goal, deltaFromInitial({})), 1e-8);

  // Test the components of the constraint individually.
  std::vector<double> componentValues;
  componentValues.reserve(constraint.size());
  for (auto& component : constraint) {
    componentValues.push_back(evaluate(component, deltaFromInitial({})));
  }
  sort(componentValues.begin(), componentValues.end());

  // The individual components of the constraint are negative when the limit is
  // not reached. The expectation is that a non-positive constraint is
  // satisfied, and a positive constraint is broken.
  EXPECT_NEAR(-0.2 / 1.75, componentValues.at(0), 1e-8);
  EXPECT_NEAR(0.1 / 1.75, componentValues.at(1), 1e-8);
  EXPECT_NEAR(0.1 / 1.75, componentValues.at(2), 1e-8);
  EXPECT_NEAR(0.8 / 1.75, componentValues.at(3), 1e-8);

  EXPECT_EQ(0, capacitySpecBuilder.nonAcceptingContainers().size());
}

CO_TEST_F(CapacitySpecBuilderTest, RelativeLimit) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";

  // Only include host3.
  capacitySpec.filter()->itemsWhitelist() = {"host3"};

  // Define a relative limit of 0.5. Since host3's capacity is 4, this
  // translates to an effective limit of 2 cpu units.
  capacitySpec.limit()->type() = interface::LimitType::RELATIVE;
  capacitySpec.limit()->globalLimit() = 0.5;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto goal = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

  // In the initial assignment there are 2.8 cpu units allocated to host 3.
  // Since the effective limit is 2, it's being exceeded by 0.8 cpu units. The
  // capacity spec normalizes the formula by dividing the exceeded amount by the
  // average capacity, which is 4 in this case since only host3 is included.
  EXPECT_NEAR(0.2, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, AbsoluteLimit) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";

  // Only include host3.
  capacitySpec.filter()->itemsWhitelist() = {"host3"};

  // Define an absolute limit of 0.5 cpu units.
  capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 0.5;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto goal = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

  // In the initial assignment there are 2.8 cpu units allocated to host 3.
  // Since the effective limit is 0.5, it's being exceeded by 2.3 cpu units. The
  // capacity spec normalizes the formula by dividing the exceeded amount by the
  // average capacity, which is 4 in this case since only host3 is included.
  EXPECT_NEAR(0.575, evaluate(goal, deltaFromInitial({})), 1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, AfterDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::AFTER);

  // In the initial assignment, host3 contains 2.8 cpu units, which exceeds the
  // limit of 0. The excess gets normalized by the average capacity, which is 4.
  EXPECT_NEAR(0.7, evaluate(goal, deltaFromInitial({})), 1e-8);

  // After task5 moves out of host3 and task2 moves in, host3 contains 1.4 cpu
  // units, which gets divided by 4 due to normalization.
  EXPECT_NEAR(
      0.35,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, DuringDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::DURING);

  // In the initial assignment, host3 contains 2.8 cpu units, which exceeds the
  // limit of 0. The excess gets normalized by the average capacity, which is 4.
  EXPECT_NEAR(0.7, evaluate(goal, deltaFromInitial({})), 1e-8);

  // After task5 moves out of host3 and task2 moves in, host3 contains 1.4 cpu
  // units. The during definition considers any objects that moved out as if
  // they were still in the initial container, so the effective utilization
  // is 3. It gets divided by 4 due to normalization.
  EXPECT_NEAR(
      0.75,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, DuringAndAfterDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::DURING_AND_AFTER);

  // The "during and after" definition is equivalent to computing and adding
  // "during" and "after" separately.
  EXPECT_NEAR(1.4, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      1.1,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, NewDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::NEW);

  // The "new" definition only looks at objects that have moved into the scope
  // item from the initial assignment. The "new" utilization with the initial
  // assignment is zero, and the limit of zero is not exceeded.
  EXPECT_NEAR(0.0, evaluate(goal, deltaFromInitial({})), 1e-8);

  // Since task2 moves to host3, the "new" utilization of host3 becomes 0.2,
  // which exceeds the limit of zero by the same amount and gets normalized by
  // the average host capacity.
  EXPECT_NEAR(
      0.2 / 4,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, OldDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::OLD);

  // The "old" definition only looks at objects that were in the scope item
  // initially but have moved somewhere else. The "old" utilization with the
  // initial assignment is zero, and the limit of zero is not exceeded.
  EXPECT_NEAR(0.0, evaluate(goal, deltaFromInitial({})), 1e-8);

  // Since task5 moves out of host3, the "old" utilization of host3 becomes 1.6,
  // which exceeds the limit of zero by the same amount and gets normalized by
  // the average host capacity.
  EXPECT_NEAR(
      1.6 / 4,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, DoubleDuringDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::DOUBLE_DURING);

  // The "double during" definition is equivalent to computing and adding
  // "during" and "old" separately.
  EXPECT_NEAR(0.7, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      1.15,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, DoubleDuringAndAfterDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER);

  // The "double during and after" definition is equivalent to computing and
  // adding "double during" and "after" separately.
  EXPECT_NEAR(1.4, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      1.5,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, MovedDataDefinition) {
  auto goal = co_await buildGoalForDefinitionTestCoro(
      interface::CapacitySpecDefinition::MOVED_DATA);
  // The "moved_data" definition only looks at objects that have moved away or
  // into the scope item from the initial assignment. The "moved_data"
  // utilization with the initial assignment is zero, and the limit of zero is
  // not exceeded.
  EXPECT_NEAR(0.0, evaluate(goal, deltaFromInitial({})), 1e-8);

  // Since task2 moves to host3, and task 5 moves away from host3, so
  // the "moved_data" utilization of host3 becomes 0.2 + 1.6 = 1.8,
  // which exceeds the limit of zero by the same amount and gets normalized by
  // the average host capacity.
  EXPECT_NEAR(
      1.8 / 4,
      evaluate(
          goal, deltaFromInitial({{"task2", "host3"}, {"task5", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, MinBound) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";
  capacitySpec.definition() = interface::CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = interface::CapacitySpecBound::MIN;

  // Only include host2.
  capacitySpec.filter()->itemsWhitelist() = {"host2"};

  // Define an absolute limit of 1.5 cpu units.
  capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 1.5;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto goal = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

  // In the initial assignment, host2 contains zero cpu units, and falls short
  // of the min limit by 1.5 cpu units. The shortage is normalized by the
  // average host capacity, which is 2 since only host2 is included.
  EXPECT_NEAR(1.5 / 2, evaluate(goal, deltaFromInitial({})), 1e-8);

  // If we move task3 to host2, then it contains 0.4 cpu units and falls short
  // of the min limit by only 1.1 cpu units.
  EXPECT_NEAR(
      1.1 / 2, evaluate(goal, deltaFromInitial({{"task3", "host2"}})), 1e-8);

  // If we move task5 instead, then host2 has 1.6 cpu units, exceeding the min
  // limit of 1.5 cpu units by 0.1, and there's no shortage.
  EXPECT_NEAR(
      0.0, evaluate(goal, deltaFromInitial({{"task5", "host2"}})), 1e-8);

  EXPECT_EQ(0, capacitySpecBuilder.nonAcceptingContainers().size());
}

CO_TEST_F(CapacitySpecBuilderTest, DuringWithInitialViolations) {
  co_await addObjectDimension("memory", {{task(2), 1.0}}, 0.6);

  const CapacitySpecBuilder capacitySpecBuilder(
      buildUniverse(),
      makeBasicMaxCapacitySpec(
          "host",
          "memory",
          interface::CapacitySpecDefinition::DURING_AND_AFTER));
  auto constraints =
      co_await capacitySpecBuilder.constraints(expressionBuilder());

  // 4 DURING constraints w.r.t. each host
  // constraints w.r.t. host0 and host3 are initially broken, so 2 more AFTER
  // constraints
  EXPECT_EQ(6, constraints.size());

  // Expect host0, host1, and host3 to be non-accepting. host0 and host3 have
  // initial during violations, whereas host1 is at the during limit
  auto nonAcceptingContainers = capacitySpecBuilder.nonAcceptingContainers();
  EXPECT_EQ(3, nonAcceptingContainers.size());
  EXPECT_TRUE(nonAcceptingContainers.contains(containerId("host0")));
  EXPECT_TRUE(nonAcceptingContainers.contains(containerId("host1")));
  EXPECT_TRUE(nonAcceptingContainers.contains(containerId("host3")));
}

CO_TEST_F(CapacitySpecBuilderTest, DuringWithInitialViolationsWithRack) {
  co_await addObjectDimension("memory", {{task(2), 1.0}}, 0.6);
  co_await addScope(
      "rack",
      {
          {"rack0", {"host0", "host2"}},
          {"rack1", {"host1"}},
          {"rack2", {"host3"}},
      });
  co_await addScopeDimension(
      "memory", scopeId("rack"), {{"rack0", 2}, {"rack1", 0.5}}, 0.0);

  const CapacitySpecBuilder capacitySpecBuilder(
      buildUniverse(),
      makeBasicMaxCapacitySpec(
          "rack", "memory", interface::CapacitySpecDefinition::DURING));
  auto constraints =
      co_await capacitySpecBuilder.constraints(expressionBuilder());

  // 3 DURING constraints w.r.t. each scope item
  EXPECT_EQ(3, constraints.size());

  auto nonAcceptingContainers = capacitySpecBuilder.nonAcceptingContainers();
  // Expect host1 and host3 to be non-accepting. Note that despite initial
  // during violations, host0 and host2 cannot be marked as non-accepting
  // since moves can happen between them without changing during utilizations
  EXPECT_EQ(2, nonAcceptingContainers.size());
  EXPECT_TRUE(nonAcceptingContainers.contains(containerId("host1")));
  EXPECT_TRUE(nonAcceptingContainers.contains(containerId("host3")));
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    DuringWithInitialViolationsDynamicDimensions) {
  co_await addScope(
      "rack",
      {
          {"rack0", {"host0", "host2"}},
          {"rack1", {"host1"}},
          {"rack2", {"host3"}},
      });
  co_await addScopeDimension(
      "memory", scopeId("rack"), {{"rack0", 2}, {"rack1", 0.5}}, 0.0);

  // each object has 0.6 memory dimension value, expect host0 in rack1 which has
  // a value of 0
  co_await addDynamicObjectDimension(
      "memory",
      scopeId("rack"),
      {{"rack1", makeSharedPtrEntityToValueMap({{task(0), 0}})}},
      0.6);

  const CapacitySpecBuilder capacitySpecBuilder(
      buildUniverse(),
      makeBasicMaxCapacitySpec(
          "rack", "memory", interface::CapacitySpecDefinition::DURING));
  auto constraints =
      co_await capacitySpecBuilder.constraints(expressionBuilder());

  // 3 DURING constraints w.r.t. each scope item
  EXPECT_EQ(3, constraints.size());

  auto nonAcceptingContainers = capacitySpecBuilder.nonAcceptingContainers();
  // expect only host3 to be non-accepting. Note that despite initial
  // during violations, host0 and host2 cannot be marked as non-accepting
  // since moves can happen between them without changing during utilizations
  // host1 cannot be marked as non-accepting since it has a task0 has a value of
  // 0 in it
  EXPECT_EQ(1, nonAcceptingContainers.size());
  EXPECT_TRUE(nonAcceptingContainers.contains(containerId("host3")));
}

CO_TEST_F(CapacitySpecBuilderTest, MinBoundZeroAllowed) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";
  capacitySpec.definition() = interface::CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = interface::CapacitySpecBound::MIN;
  capacitySpec.zeroAllowed() = true;

  // Only include host2.
  capacitySpec.filter()->itemsWhitelist() = {"host2"};

  // Define an absolute limit of 1.5 cpu units.
  capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 1.5;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto goal = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

  // When zero is allowed, there's no penalty when the host contains zero cpu
  // units. All other cases remain the same.
  EXPECT_NEAR(0.0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      1.1 / 2, evaluate(goal, deltaFromInitial({{"task3", "host2"}})), 1e-8);
  EXPECT_NEAR(
      0.0, evaluate(goal, deltaFromInitial({{"task5", "host2"}})), 1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, ZeroAverageCapacity) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";
  capacitySpec.definition() = interface::CapacitySpecDefinition::AFTER;

  // Only include host0, which has a capacity of zero.
  capacitySpec.filter()->itemsWhitelist() = {"host0"};

  // Define an absolute limit of zero cpu units.
  capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 0;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto goal = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

  // Since host0 contains 0.1 cpu units and the limit is zero, it exceeds the
  // limit by 0.1. Excess amount is typically normalized by dividing it by the
  // average host capacity. Since the average host capacity is zero in this
  // case, it doesn't get normalized.
  EXPECT_NEAR(0.1, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      1.7, evaluate(goal, deltaFromInitial({{"task5", "host0"}})), 1e-8);
}

TEST_F(CapacitySpecBuilderTest, Description) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";
  capacitySpec.definition() =
      interface::CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER;
  capacitySpec.limit()->type() = interface::LimitType::RELATIVE;
  capacitySpec.limit()->globalLimit() = 1.5;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);

  EXPECT_EQ(
      "during + old + after(cpu) <= 1.5 for scope host",
      capacitySpecBuilder.description());
}

TEST_F(CapacitySpecBuilderTest, SpecInfo) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "cpu";
  capacitySpec.definition() =
      interface::CapacitySpecDefinition::DOUBLE_DURING_AND_AFTER;
  capacitySpec.limit()->type() = interface::LimitType::RELATIVE;
  capacitySpec.zeroAllowed() = 1;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);

  auto expectedSpecInfo = SpecParameters{
      .name = "",
      .scope = "host",
      .dimension = "cpu",
      .definition = "DOUBLE_DURING_AND_AFTER",
      .boundType = "MAX",
      .limitType = "RELATIVE",
      .zeroAllowed = "yes",
      .filterAllowListSize = 0,
      .filterBlockListSize = 0};
  EXPECT_EQ(expectedSpecInfo, capacitySpecBuilder.getSpecInfo());
}

CO_TEST_F(CapacitySpecBuilderTest, DuringAndAfterOptimization) {
  const CapacitySpecBuilder capacitySpecBuilder(
      buildUniverse(),
      makeBasicMaxCapacitySpec(
          "host", "cpu", interface::CapacitySpecDefinition::DURING_AND_AFTER));
  auto constraints =
      co_await capacitySpecBuilder.constraints(expressionBuilder());
  // 1 DURING constraint per host = 4 in total
  // DURING for host0 is initially broken => 1 AFTER constraint
  // DURING for remaining hosts is not initially broken => AFTER is redundant
  // Total = 5 constraints
  EXPECT_EQ(5, constraints.size());

  // note that although DURING constraint w.r.t host0 is initially broken, it is
  // not marked as non-accepting since task0 has dimension value 0
  auto nonAcceptingContainers = capacitySpecBuilder.nonAcceptingContainers();
  EXPECT_EQ(0, nonAcceptingContainers.size());
}

CO_TEST_F(CapacitySpecBuilderTest, MovedData) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "task_count";
  capacitySpec.definition() = interface::CapacitySpecDefinition::MOVED_DATA;
  capacitySpec.limit()->type() = interface::LimitType::RELATIVE;
  capacitySpec.limit()->globalLimit() = 1;

  const CapacitySpecBuilder capacitySpecBuilder(buildUniverse(), capacitySpec);
  auto goal = co_await capacitySpecBuilder.goalCoro(expressionBuilder());

  EXPECT_NEAR(0.0, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      0.0, evaluate(goal, deltaFromInitial({{"task0", "host1"}})), 1e-8);
  EXPECT_NEAR(
      0.0, evaluate(goal, deltaFromInitial({{"task2", "host0"}})), 1e-8);
  EXPECT_NEAR(
      1.0,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task3", "host0"}})),
      1e-8);
  EXPECT_NEAR(
      2.0,
      evaluate(
          goal, deltaFromInitial({{"task0", "host1"}, {"task2", "host0"}})),
      1e-8);
}

CO_TEST_F(CapacitySpecBuilderTest, LegacyFormula) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::RELATIVE;
  spec.limit()->globalLimit() = 0.5;
  spec.filter()->itemsBlacklist() = {"host0"};
  spec.useLegacyFormula() = true;

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  auto goal = co_await specBuilder.goalCoro(expressionBuilder());

  EXPECT_NEAR(0.2, evaluate(goal, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      0.2, evaluate(goal, deltaFromInitial({{"task1", "host1"}})), 1e-8);
  EXPECT_NEAR(
      0.225, evaluate(goal, deltaFromInitial({{"task1", "host3"}})), 1e-8);
  EXPECT_NEAR(
      0.2, evaluate(goal, deltaFromInitial({{"task3", "host1"}})), 1e-8);
  EXPECT_NEAR(
      0.5, evaluate(goal, deltaFromInitial({{"task4", "host1"}})), 1e-8);
  EXPECT_NEAR(
      1.3, evaluate(goal, deltaFromInitial({{"task5", "host1"}})), 1e-8);
  EXPECT_NEAR(
      0.0, evaluate(goal, deltaFromInitial({{"task4", "host2"}})), 1e-8);
  EXPECT_NEAR(
      0.3, evaluate(goal, deltaFromInitial({{"task5", "host2"}})), 1e-8);
}

TEST_F(CapacitySpecBuilderTest, LegacyFormulaZeroCapacity) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::RELATIVE;
  spec.limit()->globalLimit() = 1;
  spec.useLegacyFormula() = true;

  CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      folly::coro::blockingWait(specBuilder.goalCoro(expressionBuilder())),
      "host host0 has zero capacity which is not compatible with legacy formula");
}

TEST_F(CapacitySpecBuilderTest, PreFilterBlocksZeroLimitContainers) {
  // host0 has capacity 0 → objects with positive dim value should be blocked
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 10;
  spec.limit()->scopeItemLimits() = {{"host0", 0}};

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // host0 has limit=0, AFTER definition. Initial util at host0 = 0.1 (task1).
  // Threshold = 0.1: objects with cpu > 0.1 are blocked from host0.
  // task0 (0) not blocked, task1 (0.1) not blocked (0.1 <= 0.1 threshold).
  // task2-5 (0.2, 0.4, 0.8, 1.6) blocked.
  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : folly::irange(2, 6)) {
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }

  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

TEST_F(CapacitySpecBuilderTest, PreFilterNoOpForMinBound) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MIN;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_TRUE(filter.empty());
}

TEST_F(CapacitySpecBuilderTest, FilterNoOpForContextDependentDefinitions) {
  const auto universe = buildUniverse();
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();

  for (auto def :
       {interface::CapacitySpecDefinition::OLD,
        interface::CapacitySpecDefinition::MOVED_DATA}) {
    interface::CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.bound() = interface::CapacitySpecBound::MAX;
    spec.definition() = def;
    spec.limit()->type() = interface::LimitType::ABSOLUTE;
    spec.limit()->globalLimit() = 0;

    const CapacitySpecBuilder specBuilder(universe, spec);
    InvalidMoveFilter filter(numObjects, numContainers);
    specBuilder.populateInvalidMoveFilter(
        filter, expressionBuilder().getUpdatedInitialAssignment());

    EXPECT_TRUE(filter.empty()) << "Expected no-op for definition "
                                << apache::thrift::util::enumNameSafe(def);
  }
}

TEST_F(CapacitySpecBuilderTest, FilterWorksForNewDefinition) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::NEW;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.5;

  // Unlike DURING, the tasks a host starts with do not lower its limit, so only
  // task4 (cpu 0.8) and task5 (cpu 1.6) are blocked from the other hosts.
  const std::set<InvalidPair> expected{
      {task(4), containerId("host0")},
      {task(4), containerId("host1")},
      {task(4), containerId("host2")},
      {task(5), containerId("host0")},
      {task(5), containerId("host1")},
      {task(5), containerId("host2")},
  };
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterWorksForDuringDefinitions) {
  const auto universe = buildUniverse();
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();

  // ABSOLUTE limit 0: every host is already at or above its DURING limit, so
  // each non-zero task is blocked from every host it is not already on. task0
  // (cpu 0) is never blocked. DURING and DURING_AND_AFTER share the filter.
  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : {2, 3, 4, 5}) { // host0 initially holds task0, task1
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }
  for (const auto i : {1, 3, 4, 5}) { // host1 initially holds task2
    expectedInvalidPairs.emplace(task(i), containerId("host1"));
  }
  for (const auto i : {1, 2, 3, 4, 5}) { // host2 is initially empty
    expectedInvalidPairs.emplace(task(i), containerId("host2"));
  }
  for (const auto i : {1, 2}) { // host3 initially holds task3, task4, task5
    expectedInvalidPairs.emplace(task(i), containerId("host3"));
  }

  for (auto def :
       {interface::CapacitySpecDefinition::DURING,
        interface::CapacitySpecDefinition::DURING_AND_AFTER}) {
    interface::CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.bound() = interface::CapacitySpecBound::MAX;
    spec.definition() = def;
    spec.limit()->type() = interface::LimitType::ABSOLUTE;
    spec.limit()->globalLimit() = 0;

    const CapacitySpecBuilder specBuilder(universe, spec);
    InvalidMoveFilter filter(numObjects, numContainers);
    specBuilder.populateInvalidMoveFilter(
        filter, expressionBuilder().getUpdatedInitialAssignment());

    EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter))
        << "Unexpected filter pairs for definition "
        << apache::thrift::util::enumNameSafe(def);
  }
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    FilterDuringZeroLimitHonorsFeasibilityTolerance) {
  co_await addObjectDimension(
      "near_zero",
      {{task(1), 0.00000000001}, {task(2), 0.2}, {task(3), 0.00000000015}},
      0);
  co_await addScopeDimension(
      "near_zero",
      scopeId("host"),
      {{"host0", 1}, {"host1", 1}, {"host2", 1}, {"host3", 1}},
      0);

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "near_zero";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;
  spec.filter()->itemsWhitelist() = {"host2"};

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  const std::set<InvalidPair> expected{
      {task(2), containerId("host2")},
      {task(3), containerId("host2")},
  };
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

TEST_F(CapacitySpecBuilderTest, FilterSkipsLegacyZeroCapacityScopeItem) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;
  spec.filter()->itemsWhitelist() = {"host0"};
  spec.useLegacyFormula() = true;

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  EXPECT_TRUE(filter.empty());
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    FilterLegacyNormalizationAffectsFeasibilityTolerance) {
  co_await addObjectDimension("tiny_capacity", {{task(1), 0.000000001}}, 0);
  co_await addScopeDimension(
      "tiny_capacity",
      scopeId("host"),
      {{"host0", 0.000001},
       {"host1", 0.000001},
       {"host2", 0.000001},
       {"host3", 0.000001}},
      0);

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "tiny_capacity";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;
  spec.filter()->itemsWhitelist() = {"host2"};
  spec.useLegacyFormula() = true;

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  InvalidMoveFilter filter(
      universe_->getObjects().getObjectIds().size(),
      universe_->getContainers().getContainerIds().size());
  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  const std::set<InvalidPair> expected{{task(1), containerId("host2")}};
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

TEST_F(CapacitySpecBuilderTest, FilterAfterUsesUpdatedInitialAssignment) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 10;
  spec.limit()->scopeItemLimits() = {{"host0", 0}};

  const auto universe = buildUniverse();
  const CapacitySpecBuilder specBuilder(universe, spec);
  const auto numObjects = universe->getObjects().getObjectIds().size();
  const auto numContainers = universe->getContainers().getContainerIds().size();

  InvalidMoveFilter originalFilter(numObjects, numContainers);
  specBuilder.populateInvalidMoveFilter(
      originalFilter, expressionBuilder().getUpdatedInitialAssignment());
  const std::set<InvalidPair> expectedOriginal{
      {task(2), containerId("host0")},
      {task(3), containerId("host0")},
      {task(4), containerId("host0")},
      {task(5), containerId("host0")},
  };
  EXPECT_EQ(expectedOriginal, collectInvalidPairs(originalFilter));

  InvalidMoveFilter updatedFilter(numObjects, numContainers);
  const Assignment updatedInitialAssignment(
      deltaFromInitial({{"task5", "host0"}}));
  specBuilder.populateInvalidMoveFilter(
      updatedFilter, updatedInitialAssignment);
  EXPECT_TRUE(updatedFilter.empty());
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    FilterDuringExemptsOriginalAndUpdatedScopeMembers) {
  co_await addScope(
      "rack",
      {{"rack0", {"host0", "host2"}},
       {"rack1", {"host1"}},
       {"rack2", {"host3"}}});

  interface::CapacitySpec spec;
  spec.scope() = "rack";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;
  spec.filter()->itemsWhitelist() = {"rack0"};

  const auto universe = buildUniverse();
  const CapacitySpecBuilder specBuilder(universe, spec);
  InvalidMoveFilter filter(
      universe->getObjects().getObjectIds().size(),
      universe->getContainers().getContainerIds().size());
  const Assignment updatedInitialAssignment(
      deltaFromInitial({{"task2", "host2"}}));
  specBuilder.populateInvalidMoveFilter(filter, updatedInitialAssignment);

  const std::set<InvalidPair> expected{
      {task(3), containerId("host0")},
      {task(4), containerId("host0")},
      {task(5), containerId("host0")},
      {task(3), containerId("host2")},
      {task(4), containerId("host2")},
      {task(5), containerId("host2")},
  };
  EXPECT_EQ(expected, collectInvalidPairs(filter));
}

TEST_F(CapacitySpecBuilderTest, PreFilterBlocksRelativeLimitWithZeroCapacity) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::RELATIVE;
  spec.limit()->globalLimit() = 1.0;

  // AFTER's threshold to block is max(limit, initialUtil). host0:
  // limit 1.0*0=0, util 0.1 => bound 0.1, blocks task2-5. host1: limit 1.0*1=1,
  // util 0.2 => bound 1, blocks task5 (cpu 1.6). host2/host3 bounds (2, 4)
  // exceed every object.
  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : folly::irange(2, 6)) {
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }
  expectedInvalidPairs.emplace(task(5), containerId("host1"));
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairsFor(spec));
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    FilterSortedPrefixesHandleNaNAndInfiniteObjectValues) {
  co_await addObjectDimension(
      "nan_cpu",
      {{task(1), std::numeric_limits<double>::infinity()},
       {task(2), 0.2},
       {task(3), 0.4},
       {task(4), 0.8},
       {task(5), 1.6}},
      std::numeric_limits<double>::quiet_NaN());
  co_await addScopeDimension(
      "nan_cpu",
      scopeId("host"),
      {{"host0", 1}, {"host1", 1}, {"host2", 1}, {"host3", 1}},
      0);

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "nan_cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.3;
  spec.limit()->scopeItemLimits() = {
      {"host1", 0.5}, {"host2", 1.0}, {"host3", 2.0}};

  const std::set<InvalidPair> expected{
      {task(1), containerId("host1")},
      {task(1), containerId("host2")},
      {task(1), containerId("host3")},
      {task(3), containerId("host0")},
      {task(4), containerId("host0")},
      {task(4), containerId("host1")},
      {task(5), containerId("host0")},
      {task(5), containerId("host1")},
      {task(5), containerId("host2")},
  };
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    FilterSkipsNonPositiveNormalizationCoefficient) {
  co_await addObjectDimension(
      "negative_capacity",
      {{task(0), 0},
       {task(1), 0.1},
       {task(2), 0.2},
       {task(3), 0.4},
       {task(4), 0.8},
       {task(5), 1.6}},
      0);
  co_await addScopeDimension(
      "negative_capacity",
      scopeId("host"),
      {{"host0", -1}, {"host1", 1}, {"host2", 2}, {"host3", 4}},
      0);

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "negative_capacity";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.useLegacyFormula() = true;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.3;
  spec.limit()->scopeItemLimits() = {{"host0", 2.0}, {"host3", 4.0}};

  const std::set<InvalidPair> expected{
      {task(3), containerId("host1")},
      {task(4), containerId("host1")},
      {task(5), containerId("host1")},
      {task(3), containerId("host2")},
      {task(4), containerId("host2")},
      {task(5), containerId("host2")},
  };
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

CO_TEST_F(
    CapacitySpecBuilderTest,
    FilterOrdersIncrementalThresholdsByExactInvalidPrefix) {
  const auto tolerance = algopt::kEpsilon;
  co_await addObjectDimension(
      "fine_cpu",
      {{task(0), 1},
       {task(1), 0.4 + 0.75 * tolerance},
       {task(2), 0},
       {task(3), 0},
       {task(4), 0},
       {task(5), 0}},
      0);
  co_await addScopeDimension(
      "fine_cpu",
      scopeId("host"),
      {{"host0", 0}, {"host1", 1}, {"host2", 2}, {"host3", 4}},
      0);

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "fine_cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.useLegacyFormula() = true;
  spec.filter()->itemsWhitelist() = {"host1", "host2", "host3"};
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.4 - 0.5 * tolerance;
  spec.limit()->scopeItemLimits() = {
      {"host2", 0.4 - tolerance}, {"host3", 0.0}};

  const std::set<InvalidPair> expected{
      {task(0), containerId("host1")},
      {task(1), containerId("host1")},
      {task(0), containerId("host2")},
      {task(0), containerId("host3")},
      {task(1), containerId("host3")},
  };
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterAfterUsesInitialUtilForBrokenScopeItem) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;
  spec.filter()->itemsWhitelist() = {"host0"};

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // host0 has ABSOLUTE limit 0 and initial util = 0.1 (task1 has cpu=0.1).
  // For AFTER definition, threshold = initial util = 0.1.
  // Objects with cpu > 0.1 are blocked: task2 (0.2), task3 (0.4), task4 (0.8),
  // task5 (1.6). task0 (0) and task1 (0.1) are not blocked.
  std::set<InvalidPair> expectedInvalidPairs;
  for (const auto i : {2, 3, 4, 5}) {
    expectedInvalidPairs.emplace(task(i), containerId("host0"));
  }
  EXPECT_EQ(expectedInvalidPairs, collectInvalidPairs(filter));
}

CO_TEST_F(CapacitySpecBuilderTest, FilterDuringRackWithPerScopeItemLimits) {
  // Two multi-container racks in one DURING pass, mixing a non-zero and a zero
  // limit via a scopeItemLimits override.
  co_await addScope(
      "rack", {{"rack0", {"host0", "host2"}}, {"rack1", {"host1", "host3"}}});

  interface::CapacitySpec spec;
  spec.scope() = "rack";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.5;
  spec.limit()->scopeItemLimits() = {{"rack1", 0}};

  // rack0 has initial util 0.1 and headroom 0.4, so task4 and task5 exceed it.
  // rack1's zero limit blocks its only external non-zero object, task1.
  // Objects initially in a rack are exempt.
  std::set<InvalidPair> expected;
  for (const auto t : {4, 5}) {
    expected.emplace(task(t), containerId("host0"));
    expected.emplace(task(t), containerId("host2"));
  }
  expected.emplace(task(1), containerId("host1")); // rack1 zero limit
  expected.emplace(task(1), containerId("host3"));
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterDuringNonZeroAbsoluteLimit) {
  // With limit 0.5, the remaining headroom is host0=0.4, host1=0.3,
  // host2=0.5, and host3=0. Existing objects are exempt on their initial host.
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.5;

  std::set<InvalidPair> expected;
  for (const auto t : {4, 5}) {
    expected.emplace(task(t), containerId("host0"));
  }
  for (const auto t : {3, 4, 5}) {
    expected.emplace(task(t), containerId("host1"));
  }
  for (const auto t : {4, 5}) {
    expected.emplace(task(t), containerId("host2"));
  }
  for (const auto t : {1, 2}) {
    expected.emplace(task(t), containerId("host3"));
  }
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterDuringNegativeLimitClampsToZero) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = -0.5;
  spec.filter()->itemsWhitelist() = {"host2"};

  std::set<InvalidPair> expected;
  for (const auto t : folly::irange(1, 6)) {
    expected.emplace(task(t), containerId("host2"));
  }
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterDuringRelativeLimit) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->type() = interface::LimitType::RELATIVE;
  spec.limit()->globalLimit() = 0.5;

  // RELATIVE limit 0.5 gives headroom host0=0, host1=0.3, host2=1.0,
  // and host3=0 after accounting for initial utilization.
  std::set<InvalidPair> expected;
  for (const auto t : {2, 3, 4, 5}) {
    expected.emplace(task(t), containerId("host0"));
  }
  for (const auto t : {3, 4, 5}) {
    expected.emplace(task(t), containerId("host1"));
  }
  for (const auto t : {5}) {
    expected.emplace(task(t), containerId("host2"));
  }
  for (const auto t : {1, 2}) {
    expected.emplace(task(t), containerId("host3"));
  }
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterAfterNonZeroAbsoluteLimit) {
  // AFTER blocks an object only when it alone exceeds a scope item's bound
  // max(limit, initialUtil); everything else can move out. With ABSOLUTE limit
  // 0.5 only task4 (0.8) and task5 (1.6) exceed it, and only on hosts where
  // they are not already assigned. host3's initial util 2.8 raises its bound
  // above every object, so nothing is blocked there.
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0.5;

  std::set<InvalidPair> expected;
  for (const auto& host : {"host0", "host1", "host2"}) {
    expected.emplace(task(4), containerId(host));
    expected.emplace(task(5), containerId(host));
  }
  EXPECT_EQ(expected, collectInvalidPairsFor(spec));
}

TEST_F(CapacitySpecBuilderTest, FilterAfterNoOpWhenGlobalLimitFitsAllObjects) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 10;

  EXPECT_TRUE(collectInvalidPairsFor(spec).empty());
}

CO_TEST_F(CapacitySpecBuilderTest, FilterNoOpForNegativeDimensions) {
  co_await addObjectDimension(
      "weight", {{task(0), -1.0}, {task(1), 2.0}, {task(2), 3.0}}, 0);

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "weight";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::AFTER;
  spec.limit()->type() = interface::LimitType::ABSOLUTE;
  spec.limit()->globalLimit() = 0;

  const CapacitySpecBuilder specBuilder(buildUniverse(), spec);
  const auto numObjects = universe_->getObjects().getObjectIds().size();
  const auto numContainers =
      universe_->getContainers().getContainerIds().size();
  InvalidMoveFilter filter(numObjects, numContainers);

  specBuilder.populateInvalidMoveFilter(
      filter, expressionBuilder().getUpdatedInitialAssignment());

  // Dimension has negative values → filter is skipped entirely
  EXPECT_TRUE(filter.empty());
}

} // namespace facebook::rebalancer::materializer::tests
