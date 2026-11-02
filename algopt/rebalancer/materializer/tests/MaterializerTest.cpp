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

#include "algopt/rebalancer/materializer/Materializer.h"
#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/materializer/utils/tests/SpecBuilderTestBase.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>
#include <algopt/rebalancer/solver/expressions/Expression.h>

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <gtest/gtest.h>

static std::shared_ptr<folly::CPUThreadPoolExecutor> get_executor(
    int num_threads) {
  return std::make_shared<folly::CPUThreadPoolExecutor>(
      num_threads,
      std::make_shared<folly::NamedThreadFactory>("CPUThreadPool"));
}

namespace facebook::rebalancer::materializer::tests {

namespace {
const static auto nonDefaultLpTolerances = packer::tests::LpAssertOptions{
    .lpTolerances =
        algopt::lp::Tolerances{.constraint = 1e-7, .integer = 1e-6}};
} // namespace

class MaterializerTest : public SpecBuilderTestBase<int> {
 protected:
  void SetUp() override {
    kNumThreads = GetParam();
    folly::coro::blockingWait(setUpCoro());
  }

  std::shared_ptr<folly::CPUThreadPoolExecutor> makeExecutor() const {
    return get_executor(kNumThreads);
  }

  folly::coro::Task<void> setUpCoro() {
    setUpUniverse({
        {"host0", {"task0"}},
        {"host1", {"task1"}},
        {"host2", {"task2"}},
    });

    const entities::Map<entities::ObjectId, double> cpuValues = {
        {objectId("task0"), 2},
        {objectId("task1"), 4},
        {objectId("task2"), 8},
    };
    co_await addObjectDimension("cpu", cpuValues, 0);

    {
      // goal0 evaluates to the amount of cpu placed in host0
      interface::CapacitySpec spec;
      spec.scope() = "host";
      spec.dimension() = "cpu";
      spec.filter()->itemsWhitelist() = {"host0"};
      spec.limit()->globalLimit() = 0;

      interface::GoalSpecs goal;
      goal.capacitySpec() = spec;

      co_await addGoal("goal0", goal, 0.1, 2);
    }

    co_return;
  }

  entities::ObjectId task(int index) const {
    return objectId(fmt::format("task{}", index));
  }

  folly::coro::Task<void> addTestConstraint(
      interface::ConstraintPolicy policy,
      int tuplePosIfBroken = 0) {
    // constraint0 caps host1 and host2 at 4 cpu units
    interface::CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.filter()->itemsWhitelist() = {"host1", "host2"};
    spec.limit()->globalLimit() = 4;

    interface::ConstraintSpecs constraint;
    constraint.capacitySpec() = spec;

    co_await addConstraint(
        "constraint0", constraint, policy, 100, 10000, tuplePosIfBroken);
  }

  folly::coro::Task<void> addMovesInProgressConstraint(
      const std::string &name,
      const entities::Map<std::string, std::string> &destinations) {
    interface::MovesInProgressSpec spec;
    spec.name() = name;
    for (const auto &[objectName, containerName] : destinations) {
      interface::MoveInProgress move;
      move.objName() = objectName;
      move.toContainer() = containerName;
      spec.moves()->push_back(std::move(move));
    }

    interface::ConstraintSpecs constraint;
    constraint.movesInProgressSpec() = std::move(spec);
    co_await addConstraint(name, constraint,
                           interface::ConstraintPolicy::DEFAULT, 100, 10000, 0);
  }

  static void verifyGlobalObjective(
      const std::vector<GoalInfo>& finalGoals,
      const GlobalObjective& globalObjective) {
    EXPECT_EQ(finalGoals.size(), globalObjective.size());

    for (const auto pos : folly::irange(finalGoals.size())) {
      const auto tuplePosition = static_cast<int>(pos);
      // check that globalObjective and finalGoals are using the same
      // expressions
      EXPECT_EQ(
          finalGoals.at(pos).objectiveExpr->getId(),
          globalObjective.getObjectiveAt(tuplePosition)->getId());
    }
  }

  static void verifyLabeledObjectives(
      const entities::Map<entities::GoalId, ExprPtr>& userGoals,
      const entities::Map<entities::ConstraintId, ExprPtr>& softConstraints,
      const std::vector<GoalInfo>& finalGoals,
      const GlobalLabeledObjectives& labeledObjectives,
      const entities::Universe& universe) {
    PackerMap<int, std::vector<ExprPtr>> tupleIndexToGoals;
    for (auto& [goalId, expr] : userGoals) {
      auto& goal = universe.getGoals().getGoal(goalId);
      tupleIndexToGoals[goal.getTupleIndex()].push_back(expr);
    }

    for (auto& [id, expr] : softConstraints) {
      auto tuplePos =
          universe.getConstraints().getConstraint(id).getTupleIndex();
      tupleIndexToGoals[tuplePos].push_back(expr);
    }

    for (const auto pos : folly::irange(labeledObjectives.size())) {
      const auto tuplePosition = static_cast<int>(pos);
      auto& labeledObjsAtPos = labeledObjectives.getObjectiveAt(tuplePosition);

      // check that all the userGoal and softConstraints are added to
      // labeledObjectives
      EXPECT_EQ(
          tupleIndexToGoals[tuplePosition].size(),
          labeledObjsAtPos.getExpressions().size());

      int nLabeledObjs = 0;
      for (auto& labeledObj : labeledObjsAtPos.getExpressions()) {
        auto& objExpr = labeledObj->expression;

        // check that the expressionIds used by userGoals and softConstraints
        // are the same as that in labeledObjectives
        EXPECT_EQ(
            objExpr->getId(),
            tupleIndexToGoals.at(tuplePosition).at(nLabeledObjs)->getId());

        nLabeledObjs++;
      }

      // check that the root of labeledObjsAtPos is the same as the expression
      // in finalGoals.at(pos), which in turn implies it is the same as
      // globalObjecive.at(pos)
      EXPECT_EQ(
          labeledObjsAtPos.getRoot()->getId(),
          finalGoals.at(pos).objectiveExpr->getId());
    }
  }

  static void verifyLabeledConstraints(
      const entities::Map<entities::ConstraintId, ExprPtr>& constraints,
      ExprPtr finalConstraint,
      const LabeledConstraints& labeledConstraints) {
    // ensure that root of labeledConstraints is the same as the
    // finalConstraint
    EXPECT_EQ(finalConstraint->getId(), labeledConstraints.getRoot()->getId());

    auto& labeledConstraintExprs = labeledConstraints.getExpressions();
    int nConstraints = 0;
    for (auto& [id, constraint] : constraints) {
      auto& labeledExpr = labeledConstraintExprs.at(nConstraints);

      // ensure that the expressions in constraints is the same as the one in
      // labeledConstraints
      EXPECT_EQ(constraint->getId(), labeledExpr->expression->getId());
      ++nConstraints;
    }
  }

  static std::shared_ptr<const MaterializedProblem> getMaterializedProblem(
      std::shared_ptr<const entities::Universe> universe,
      int numThreads,
      bool continuousExpressions = false,
      bool useSeparatedLocalSearchPenaltyObjective = false,
      bool enableInvalidMoveFilter = true) {
    const std::shared_ptr<folly::CPUThreadPoolExecutor> executor =
        get_executor(numThreads);

    auto wrappedExecutor =
        std::make_shared<algopt::treeprof::ExecutorWrapper>(executor);
    return Materializer::materialize(
        std::move(wrappedExecutor),
        std::move(universe),
        continuousExpressions,
        std::make_shared<LogCollector>(),
        /*shouldCollectMetrics=*/false,
        enableInvalidMoveFilter,
        useSeparatedLocalSearchPenaltyObjective);
  }

  int kNumThreads{1};
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    MaterializerTest,
    interface::testThreadCounts());

CO_TEST_P(MaterializerTest, InvalidMoveFilterEnabledByDefault) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->globalLimit() = 0;

  interface::ConstraintSpecs constraint;
  constraint.capacitySpec() = std::move(spec);
  co_await addConstraint(
      "constraint0",
      constraint,
      interface::ConstraintPolicy::HARD,
      100,
      10000,
      0);

  const auto materialized =
      getMaterializedProblem(buildUniverse(), MaterializerTest::GetParam());

  EXPECT_NE(nullptr, materialized->invalidMoveFilter);
  if (materialized->invalidMoveFilter) {
    EXPECT_TRUE(materialized->invalidMoveFilter->isMarkedInvalid(
        task(0), containerId("host1")));
    EXPECT_FALSE(materialized->invalidMoveFilter->isMarkedInvalid(
        task(0), containerId("host0")));
  }
}

CO_TEST_P(MaterializerTest, InvalidMoveFilterDisabledByRollout) {
  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.bound() = interface::CapacitySpecBound::MAX;
  spec.definition() = interface::CapacitySpecDefinition::DURING;
  spec.limit()->globalLimit() = 0;

  interface::ConstraintSpecs constraint;
  constraint.capacitySpec() = std::move(spec);
  co_await addConstraint(
      "constraint0",
      constraint,
      interface::ConstraintPolicy::HARD,
      100,
      10000,
      0);

  const auto materialized = getMaterializedProblem(
      buildUniverse(),
      MaterializerTest::GetParam(),
      /*continuousExpressions=*/false,
      /*useSeparatedLocalSearchPenaltyObjective=*/false,
      /*enableInvalidMoveFilter=*/false);

  EXPECT_EQ(nullptr, materialized->invalidMoveFilter);
}

CO_TEST_P(MaterializerTest, DefaultConstraintPolicy) {
  co_await addTestConstraint(
      interface::ConstraintPolicy::DEFAULT, 0 /* tuplePosIfBroken */);
  const auto universe = buildUniverse();
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;

  EXPECT_EQ(3, materialized.finalGoals.size());

  // The first item in the goals tuple is the soft component of constraint0,
  // which corresponds to host2 initially exceeding capacity by 4 cpu units.
  // The penalty is 10000 for being broken (invalidState) plus 400 (violation
  // amount times invalidCost).
  EXPECT_NEAR(
      10400.0,
      evaluate(
          materialized.finalGoals.at(0).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // The second item in the goals tuple is zero as there aren't any goals in
  // this position.
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.finalGoals.at(1).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // The third item in the goals tuple is the user-defined goal0. It evaluates
  // to the amount of cpu placed in host0, which is 2 for the initial
  // assignment, times the goal weight 0.1.
  EXPECT_NEAR(
      0.2,
      evaluate(
          materialized.finalGoals.at(2).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // verify that globalObjective uses the same expressions as those in
  // finalGoals
  verifyGlobalObjective(materialized.finalGoals, materialized.globalObjective);

  // verify that labeledObjectives uses the same expressions as in userGoals and
  // softConstraints
  verifyLabeledObjectives(
      materialized.userGoals,
      materialized.softConstraints,
      materialized.finalGoals,
      materialized.labeledObjectives,
      *universe);

  // The constraint is never broken for the initial assignment.
  EXPECT_NEAR(
      0.0, evaluate(materialized.finalConstraint, deltaFromInitial({})), 1e-8);

  // Breaking an initially satisfied component (host1) breaks the constraint.
  EXPECT_NEAR(
      1.0,
      evaluate(
          materialized.finalConstraint, deltaFromInitial({{"task0", "host1"}})),
      1e-8);

  // Breaking an initially broken component (host2) beyond its initial
  // violation also breaks the constraint.
  EXPECT_NEAR(
      1.0,
      evaluate(
          materialized.finalConstraint, deltaFromInitial({{"task0", "host2"}})),
      1e-8);

  // Test raw user-defined goals.
  auto goal0 = materialized.userGoals.at(universe->getGoalId("goal0"));
  EXPECT_NEAR(0.2, evaluate(goal0, deltaFromInitial({})), 1e-8);

  // Test raw user-defined constraints.
  auto constraint0 = universe->getConstraintId("constraint0");
  EXPECT_NEAR(
      4.0,
      evaluate(
          materialized.userConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      4.0,
      evaluate(materialized.userConstraintSum, deltaFromInitial({})),
      1e-8);

  // The soft component of constraint0 matches the violation amount.
  EXPECT_NEAR(
      10400.0,
      evaluate(
          materialized.softConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);

  // The hard component is satisfied for the initial assignment.
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.hardConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);

  // The hard component is broken when violation amount increases beyond the
  // initial.
  EXPECT_NEAR(
      1.0,
      evaluate(
          materialized.hardConstraints.at(constraint0),
          deltaFromInitial({{"task0", "host1"}})),
      1e-8);

  // verify that the expressions in labeledHardConstraints are the same as the
  // one in hardConstraints
  verifyLabeledConstraints(
      materialized.hardConstraints,
      materialized.finalConstraint,
      materialized.labeledHardConstraints);

  // verify that the expressions in labeledSoftConstraints are the same as the
  // one in userConstraints
  verifyLabeledConstraints(
      materialized.userConstraints,
      materialized.userConstraintSum,
      materialized.labeledUserConstraints);

  // since similarContainers defintion is not specified,
  // materialized.similarContainers should be std::nullopt
  EXPECT_EQ(false, materialized.similarContainers.has_value());
}

CO_TEST_P(MaterializerTest, MaterializesGlobalAndPenaltyObjectives) {
  co_await addPartition(
      "job", {{"job0", {"task0", "task1"}}, {"job1", {"task2"}}});
  interface::ColocateGroupsSpec spec;
  spec.name() = "colocate";
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limits()->globalLimit() = 1;
  interface::ConstraintSpecs constraint;
  constraint.colocateGroupsSpec() = spec;
  co_await addConstraint(
      "constraint0",
      constraint,
      interface::ConstraintPolicy::SOFT,
      /*invalidCost=*/100,
      /*invalidState=*/10000,
      /*tupleIndex=*/1);

  const auto universe = buildUniverse();
  const auto materialized = getMaterializedProblem(
      universe,
      MaterializerTest::GetParam(),
      /*continuousExpressions=*/true,
      /*useSeparatedLocalSearchPenaltyObjective=*/true);

  // This case has a separated penalty only from the SOFT constraint. It
  // verifies that such a penalty still builds both the Penalty objective and
  // its labels without relying on a user-goal penalty.
  EXPECT_TRUE(materialized->userPenalties.empty());
  EXPECT_EQ(1, materialized->softConstraintPenalties.size());
  EXPECT_EQ(3, materialized->globalObjective.size());
  EXPECT_EQ(3, materialized->penaltyObjective.size());
  // job0 is present on two hosts, so its violation is 1. Its normalized
  // utilization is 0.5 on each host, giving a continuous penalty of
  // 2 * (0.5 - 0.1 * 0.5^2) = 0.95.
  EXPECT_NEAR(
      10100,
      evaluate(
          materialized->globalObjective.getObjectiveAt(1),
          deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      95,
      evaluate(
          materialized->penaltyObjective.getObjectiveAt(1),
          deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      0.2,
      evaluate(
          materialized->globalObjective.getObjectiveAt(2),
          deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      0,
      evaluate(
          materialized->penaltyObjective.getObjectiveAt(2),
          deltaFromInitial({})),
      1e-8);
  EXPECT_EQ(
      materialized->finalGoals.at(1).penaltyExpr,
      materialized->penaltyObjective.getObjectiveAt(1));
  EXPECT_EQ(nullptr, materialized->finalGoals.at(2).penaltyExpr);
  EXPECT_EQ(
      materialized->globalObjective.getObjectiveAt(1)->getId(),
      materialized->labeledObjectives.getObjectiveAt(1).getRoot()->getId());
  EXPECT_EQ(3, materialized->labeledPenalties.size());
  EXPECT_EQ(
      materialized->penaltyObjective.getObjectiveAt(1)->getId(),
      materialized->labeledPenalties.getObjectiveAt(1).getRoot()->getId());
  const auto& labeledPenaltyExpressions =
      materialized->labeledPenalties.getObjectiveAt(1).getExpressions();
  EXPECT_EQ(1, labeledPenaltyExpressions.size());
  if (!labeledPenaltyExpressions.empty()) {
    EXPECT_EQ("constraint0", labeledPenaltyExpressions.front()->name);
    EXPECT_NEAR(
        evaluate(
            materialized->finalGoals.at(1).penaltyExpr, deltaFromInitial({})),
        evaluate(
            labeledPenaltyExpressions.front()->expression,
            deltaFromInitial({})),
        1e-8);
  }
  verifyGlobalObjective(
      materialized->finalGoals, materialized->globalObjective);
}

CO_TEST_P(MaterializerTest, OmitsPenaltyObjectiveWhenNoPenaltyExists) {
  const auto universe = buildUniverse();
  const auto materialized = getMaterializedProblem(
      universe,
      MaterializerTest::GetParam(),
      /*continuousExpressions=*/true,
      /*useSeparatedLocalSearchPenaltyObjective=*/true);

  EXPECT_EQ(3, materialized->globalObjective.size());
  EXPECT_EQ(0, materialized->penaltyObjective.size());
  co_return;
}

CO_TEST_P(MaterializerTest, HardConstraintPolicy) {
  co_await addTestConstraint(interface::ConstraintPolicy::HARD);
  const auto universe = buildUniverse();
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;
  EXPECT_EQ(3, materialized.finalGoals.size());
  EXPECT_EQ(0, materialized.penaltyObjective.size());
  for (const auto& goalInfo : materialized.finalGoals) {
    EXPECT_EQ(nullptr, goalInfo.penaltyExpr);
  }

  // The first item in the goals tuple doesn't contain any goals nor softened
  // constraints.
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.finalGoals.at(0).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // The second item in the goals tuple is zero as there aren't any goals in
  // this position.
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.finalGoals.at(1).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // The third item in the goals tuple is the user-defined goal0. It evaluates
  // to the amount of cpu placed in host0, which is 2 for the initial
  // assignment, times the goal weight 0.1.
  EXPECT_NEAR(
      0.2,
      evaluate(
          materialized.finalGoals.at(2).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // verify that globalObjective uses the same expressions as those in
  // finalGoals
  verifyGlobalObjective(materialized.finalGoals, materialized.globalObjective);

  // verify that labeledObjectives uses the same expressions as in userGoals and
  // softConstraints
  verifyLabeledObjectives(
      materialized.userGoals,
      materialized.softConstraints,
      materialized.finalGoals,
      materialized.labeledObjectives,
      *universe);

  // Because the constraint policy is "hard", the initially broken constraint0
  // doesn't get softened and it will be broken for the initial assignment.
  EXPECT_NEAR(
      1.0, evaluate(materialized.finalConstraint, deltaFromInitial({})), 1e-8);

  // Test raw user-defined goals. Unaffected by the policy.
  auto goal0 = materialized.userGoals.at(universe->getGoalId("goal0"));
  EXPECT_NEAR(0.2, evaluate(goal0, deltaFromInitial({})), 1e-8);

  // Test raw user-defined constraints. Unaffected by the policy.
  auto constraint0 = universe->getConstraintId("constraint0");
  EXPECT_NEAR(
      4.0,
      evaluate(
          materialized.userConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      4.0,
      evaluate(materialized.userConstraintSum, deltaFromInitial({})),
      1e-8);

  // The soft component of constraint0 does not exist because the policy is
  // "hard".
  EXPECT_EQ(0, materialized.softConstraints.count(constraint0));

  // verify that the expressions in labeledHardConstraints are the same as the
  // one in hardConstraints
  verifyLabeledConstraints(
      materialized.hardConstraints,
      materialized.finalConstraint,
      materialized.labeledHardConstraints);

  // verify that the expressions in labeledSoftConstraints are the same as the
  // one in userConstraints
  verifyLabeledConstraints(
      materialized.userConstraints,
      materialized.userConstraintSum,
      materialized.labeledUserConstraints);

  // The hard component is broken for the initial assignment.
  EXPECT_NEAR(
      1.0,
      evaluate(
          materialized.hardConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);
}

CO_TEST_P(MaterializerTest, SoftConstraintPolicy) {
  co_await addTestConstraint(
      interface::ConstraintPolicy::SOFT, 1 /* tuplePosIfBroken */);
  const auto universe = buildUniverse();
  const std::shared_ptr<folly::CPUThreadPoolExecutor> executor;
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;
  EXPECT_EQ(3, materialized.finalGoals.size());

  // The broken constraint is added to tupel index 1, and there is no goal in
  // tuple index 0. Therefore, expect the value at goal tuple 0 to be 0.
  EXPECT_NEAR(
      0,
      evaluate(
          materialized.finalGoals.at(0).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // The second item in the goals tuple is the soft component of constraint0,
  // which corresponds to host2 initially exceeding capacity by 4 cpu units.
  // The penalty is 10000 for being broken (invalidState) plus 400 (violation
  // amount times invalidCost).
  EXPECT_NEAR(
      10400.0,
      evaluate(
          materialized.finalGoals.at(1).objectiveExpr,
          deltaFromInitial({}),
          nonDefaultLpTolerances),
      1e-8);

  // Increasing the violation beyond initial increased the goal value at tuple
  // index 1 accordingly.
  EXPECT_NEAR(
      10600.0,
      evaluate(
          materialized.finalGoals.at(1).objectiveExpr,
          deltaFromInitial({{"task0", "host2"}}),
          nonDefaultLpTolerances),
      1e-8);

  // Breaking a new component of constraint0 that wasn't initially broken
  // increases the goal's value by invalidState plus the overall violation
  // increase times invalidCost.
  EXPECT_NEAR(
      20600.0,
      evaluate(
          materialized.finalGoals.at(1).objectiveExpr,
          deltaFromInitial({{"task0", "host1"}})),
      1e-8);

  // The third item in the goals tuple is the user-defined goal0. It evaluates
  // to the amount of cpu placed in host0, which is 2 for the initial
  // assignment, times the goal weight 0.1.
  EXPECT_NEAR(
      0.2,
      evaluate(
          materialized.finalGoals.at(2).objectiveExpr, deltaFromInitial({})),
      1e-8);

  // verify that globalObjective uses the same expressions as those in
  // finalGoals
  verifyGlobalObjective(materialized.finalGoals, materialized.globalObjective);

  // verify that labeledObjectives uses the same expressions as in userGoals and
  // softConstraints
  verifyLabeledObjectives(
      materialized.userGoals,
      materialized.softConstraints,
      materialized.finalGoals,
      materialized.labeledObjectives,
      *universe);

  // The constraint is never broken because the policy is "soft".
  EXPECT_NEAR(
      0.0, evaluate(materialized.finalConstraint, deltaFromInitial({})), 1e-8);
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.finalConstraint, deltaFromInitial({{"task0", "host1"}})),
      1e-8);
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.finalConstraint, deltaFromInitial({{"task0", "host2"}})),
      1e-8);

  // Test raw user-defined goals. Unaffected by the policy.
  auto goal0 = materialized.userGoals.at(universe->getGoalId("goal0"));
  EXPECT_NEAR(0.2, evaluate(goal0, deltaFromInitial({})), 1e-8);

  // Test raw user-defined constraints. Unaffected by the policy.
  auto constraint0 = universe->getConstraintId("constraint0");
  EXPECT_NEAR(
      4.0,
      evaluate(
          materialized.userConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      4.0,
      evaluate(materialized.userConstraintSum, deltaFromInitial({})),
      1e-8);

  // The soft component of constraint0 matches the violation amount times
  // invalidCost plus number of broken components times invalidState.
  EXPECT_NEAR(
      10400.0,
      evaluate(
          materialized.softConstraints.at(constraint0),
          deltaFromInitial({}),
          nonDefaultLpTolerances),
      1e-8);

  // The hard component is satisfied for any assignment.
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.hardConstraints.at(constraint0), deltaFromInitial({})),
      1e-8);
  EXPECT_NEAR(
      0.0,
      evaluate(
          materialized.hardConstraints.at(constraint0),
          deltaFromInitial({{"task0", "host1"}})),
      1e-8);

  // verify that the expressions in labeledHardConstraints are the same as the
  // one in hardConstraints
  verifyLabeledConstraints(
      materialized.hardConstraints,
      materialized.finalConstraint,
      materialized.labeledHardConstraints);

  // verify that the expressions in labeledSoftConstraints are the same as the
  // one in userConstraints
  verifyLabeledConstraints(
      materialized.userConstraints,
      materialized.userConstraintSum,
      materialized.labeledUserConstraints);
}

CO_TEST_P(MaterializerTest, UpdatesInInitialAssignment) {
  /* Test spec updates initial assignment */
  std::vector<interface::MoveInProgress> moves;
  {
    interface::MoveInProgress move;
    move.objName() = "task0";
    move.toContainer() = "host2";
    moves.push_back(move);
  }
  {
    interface::MoveInProgress move;
    move.objName() = "task1";
    move.toContainer() = "host0";
    moves.push_back(move);
  }

  interface::MovesInProgressSpec spec;
  spec.name() = "test";
  spec.moves() = std::move(moves);

  interface::ConstraintSpecs constraint;
  constraint.movesInProgressSpec() = spec;

  co_await addConstraint(
      "constraint0",
      constraint,
      interface::ConstraintPolicy::DEFAULT,
      100,
      10000,
      0);

  const auto universe = buildUniverse();
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;

  entities::Map<entities::ContainerId, std::set<entities::ObjectId>>
      expectedAssignment = {
          {containerId("host2"), {task(0), task(2)}},
          {containerId("host0"), {task(1)}}};

  for (auto [containerId, objects] : materialized.updatedInitialAssignment) {
    EXPECT_EQ(
        expectedAssignment[containerId],
        std::set<entities::ObjectId>(objects.begin(), objects.end()));
  }
}

CO_TEST_P(MaterializerTest, ConflictingUpdatesInInitialAssignment) {
  co_await addMovesInProgressConstraint("constraint0", {{"task0", "host1"}});
  co_await addMovesInProgressConstraint("constraint1", {{"task0", "host2"}});

  const auto universe = buildUniverse();
  REBALANCER_EXPECT_RUNTIME_ERROR_CONTAINS(
      getMaterializedProblem(universe, MaterializerTest::GetParam()),
      "Object can be fixed to only one container");
}

CO_TEST_P(MaterializerTest, CompatibleUpdatesInInitialAssignment) {
  co_await addMovesInProgressConstraint(
      "constraint0", {{"task0", "host2"}, {"task1", "host0"}});
  co_await addMovesInProgressConstraint(
      "constraint1", {{"task0", "host2"}, {"task2", "host1"}});

  const auto universe = buildUniverse();
  const auto materialized =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  const auto &assignment = materialized->updatedInitialAssignment;

  ASSERT_EQ(3, assignment.size());
  EXPECT_EQ(std::vector<entities::ObjectId>{task(1)},
            assignment.at(containerId("host0")));
  EXPECT_EQ(std::vector<entities::ObjectId>{task(2)},
            assignment.at(containerId("host1")));
  EXPECT_EQ(std::vector<entities::ObjectId>{task(0)},
            assignment.at(containerId("host2")));
}

CO_TEST_P(MaterializerTest, FixedObjects) {
  interface::AvoidMovingSpec spec;
  spec.objects() = {"task1", "task2"};

  interface::ConstraintSpecs constraint;
  constraint.avoidMovingSpec() = spec;

  co_await addConstraint(
      "constraint0",
      constraint,
      interface::ConstraintPolicy::DEFAULT,
      100,
      10000,
      0);

  const auto universe = buildUniverse();
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;
  EXPECT_EQ(2, materialized.fixedObjects.size());
  EXPECT_EQ(1, materialized.fixedObjects.count(task(1)));
  EXPECT_EQ(1, materialized.fixedObjects.count(task(2)));
}

CO_TEST_P(MaterializerTest, FixedContainers) {
  {
    interface::AvoidMovingSpec spec;
    spec.objects() = {"task1", "task2"};

    interface::ConstraintSpecs constraint;
    constraint.avoidMovingSpec() = spec;

    co_await addConstraint(
        "constraint0",
        constraint,
        interface::ConstraintPolicy::DEFAULT,
        100,
        10000,
        0);
  }

  {
    interface::NonAcceptingSpec spec;
    spec.scope() = "host";
    spec.items() = {"host0", "host1"};

    interface::ConstraintSpecs constraint;
    constraint.nonAcceptingSpec() = spec;

    co_await addConstraint(
        "constraint1",
        constraint,
        interface::ConstraintPolicy::DEFAULT,
        100,
        10000,
        0);
  }

  const auto universe = buildUniverse();
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;

  EXPECT_EQ(1, materialized.fixedContainers.size());
  EXPECT_TRUE(materialized.fixedContainers.contains(containerId("host1")));
}

CO_TEST_P(MaterializerTest, FixedContainersBasedOnMultipleSpecs) {
  {
    interface::AvoidMovingSpec spec;
    spec.objects() = {"task1", "task2"};

    interface::ConstraintSpecs constraint;
    constraint.avoidMovingSpec() = std::move(spec);

    co_await addConstraint(
        "constraint0",
        constraint,
        interface::ConstraintPolicy::DEFAULT,
        100,
        10000,
        0);
  }

  {
    interface::NonAcceptingSpec spec;
    spec.scope() = "host";
    spec.items() = {"host0", "host1"};

    interface::ConstraintSpecs constraint;
    constraint.nonAcceptingSpec() = std::move(spec);

    co_await addConstraint(
        "constraint1",
        constraint,
        interface::ConstraintPolicy::DEFAULT,
        100,
        10000,
        0);
  }

  {
    interface::CapacitySpec spec;
    spec.scope() = "host";
    spec.dimension() = "task_count";
    spec.bound() = interface::CapacitySpecBound::MAX;
    spec.limit()->globalLimit() = 0;
    spec.definition() = interface::CapacitySpecDefinition::DURING_AND_AFTER;

    interface::ConstraintSpecs constraint;
    constraint.capacitySpec() = std::move(spec);

    co_await addConstraint(
        "constraint2",
        constraint,
        interface::ConstraintPolicy::DEFAULT,
        100,
        10000,
        0);
  }

  const auto universe = buildUniverse();
  auto materializedPtr =
      getMaterializedProblem(universe, MaterializerTest::GetParam());
  auto& materialized = *materializedPtr;

  // host1 is nonAccepting based on nonAcceptingSpec, while host2 is
  // nonAccepting because of capacitySpec. Since all the initial objects in
  // host1 and host2 are fixed, they are also fixed
  EXPECT_EQ(2, materialized.fixedContainers.size());
  EXPECT_TRUE(materialized.fixedContainers.contains(containerId("host1")));
  EXPECT_TRUE(materialized.fixedContainers.contains(containerId("host2")));
}

} // namespace facebook::rebalancer::materializer::tests
