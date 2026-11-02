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

#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <folly/container/irange.h>
#include <folly/String.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;

namespace facebook::rebalancer::interface::tests {

class CapacityTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(NumThreads, CapacityTest, testThreadCounts());

class CapacityOptimalTest
    : public ::testing::TestWithParam<std::tuple<int, OptimalSolverPackage>> {
 protected:
  void SetUp() override {
    auto [threads, solver] = GetParam();
    if (isSolverUnavailable(solver)) {
      GTEST_SKIP() << solverName(solver) << " solver not available";
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    AllSolvers,
    CapacityOptimalTest,
    ::testing::Combine(testThreadCounts(), testSolverPackages()));

TEST_P(CapacityTest, InitiallyBrokenCapacityConstraint) {
  // In this example there are 2 hosts and 4 tasks. Each host has capacity for
  // 2 tasks. host0 initially breaks the constraint, as there are 3 tasks in
  // it according to the initial assignment.
  // Rebalancer should detect host0 initially breaks the constraint and fix it
  // to the extent possible. In this case, moving just one task from host0 to
  // host 1 completely fixes the constraint violation.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // host0 initially has 3 tasks in it, and host1 only 1.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3"}},
      });

  // Each task needs 10 GB of memory.
  solver->addObjectDimension(
      "memory",
      folly::F14FastMap<std::string, double>{
          {"task0", 10},
          {"task1", 10},
          {"task2", 10},
          {"task3", 10},
      });

  // Each host has 20 GB of memory capacity.
  solver->addContainerDimension(
      "memory",
      folly::F14FastMap<std::string, double>{
          {"host0", 20},
          {"host1", 20},
      });

  // Enforce memory capacity on hosts.
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "memory";
  solver->addConstraint(capacitySpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  // We expect each host to contain exactly 2 tasks in the final assignment.
  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(2, tasksInHost["host0"]);
  EXPECT_EQ(2, tasksInHost["host1"]);
}

TEST_P(CapacityTest, MinAbsoluteCapacityConstraint) {
  // In this example there are 2 hosts and 4 tasks. All tasks start in host0
  // but they have a preference for being placed in host1 instead. A minimum
  // capacity constraint enforces that each host must contain at least 1 task.
  // Rebalancer should move all tasks but 1 from host0 to host1 in order to
  // optimize the preferences without breaking the capacity constraint.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // host0 initially has 4 tasks in it, and host1 has none.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2", "task3"}},
          {"host1", {}},
      });

  // Make all tasks prefer being placed in host1 rather than host0.
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.scope() = "host";
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("task0", "host1", 1),
      makeAssignmentAffinity("task1", "host1", 1),
      makeAssignmentAffinity("task2", "host1", 1),
      makeAssignmentAffinity("task3", "host1", 1),
  };

  solver->addGoal(assignmentAffinitiesSpec);

  // Note: the dimension task_count is implicit and it's defined as 1 for every
  // task (object). Since we are using absolute limits, it is irrelevant how we
  // define the same dimension on the hosts (scope items).

  // Enforce min absolute capacity on hosts: at least 1 task per host.
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "task_count";
  capacitySpec.bound() = CapacitySpecBound::MIN;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 1.0;

  capacitySpec.limit() = limit;

  solver->addConstraint(capacitySpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  // We expect host0 to contain a single task, and host1 to contain the rest.
  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(1, tasksInHost["host0"]);
  EXPECT_EQ(3, tasksInHost["host1"]);
}

TEST_P(CapacityTest, DuringCapacityConstraint) {
  // In this example there are 3 hosts and 5 tasks with the following initial
  // assignment:
  // host0: task0, task1
  // host1: task2, task3
  // host2: task4
  // There's a capacity constraint with the DURING definition, limiting the
  // capacity of hosts to no more than 2 tasks each. task1 has a preference
  // for host1, and task3 has a preference for host2.
  // While it is certainly possible to move both tasks to their preferred host
  // with an AFTER definition without violating capacity (first task3 moves to
  // host2, then task1 moves to host1), it is not possible with the DURING
  // definition: task3 moves to host2, but at that point it contributes to the
  // utilization of both host1 and host2, therefore there's no room in host1 for
  // task1 to come in.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // host0 and host1 start with 2 tasks each, host2 starts with a single task.
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2", "task3"}},
          {"host2", {"task4"}},
      });

  // task1 has a preference for host1, and task3 has a preference for host2.
  AssignmentAffinitiesSpec assignmentAffinitiesSpec;
  assignmentAffinitiesSpec.scope() = "host";
  assignmentAffinitiesSpec.affinities() = {
      makeAssignmentAffinity("task1", "host1", 1),
      makeAssignmentAffinity("task3", "host2", 1)};

  solver->addGoal(assignmentAffinitiesSpec);

  // Note: the dimension task_count is implicit and it's defined as 1 for every
  // task (object). Since we are using absolute limits, it is irrelevant how we
  // define the same dimension on the hosts (scope items).

  // Enforce DURING capacity on hosts: at most 2 tasks per host.
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "task_count";
  capacitySpec.definition() = CapacitySpecDefinition::DURING;

  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = 2.0;

  capacitySpec.limit() = limit;

  solver->addConstraint(capacitySpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  // We expect task3 to have moved to host2, but task1 to have stayed in host0.
  const std::map<std::string, std::string> expected = {
      {"task0", "host0"},
      {"task1", "host0"},
      {"task2", "host1"},
      {"task3", "host2"},
      {"task4", "host2"},
  };
  EXPECT_EQ(expected, toOrderedMap(*solution.assignment()));
}

TEST_P(CapacityTest, DefualtObjectDimensions) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0"}},
          {"host1", {"task1"}},
          {"host2", {"task2"}},
      });

  solver->addObjectDimension("memory", std::map<std::string, double>(), 10);
  CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "memory";
  spec.definition() = CapacitySpecDefinition::DURING_AND_AFTER;
  solver->addConstraint(spec);

  // Local search with a limit of zero moves, since we are interested in
  // testing materialization
  LocalSearchSolverSpec localSearchSpec;
  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));

  localSearchSpec.stopAfterMoves() = 0;
  solver->addSolver(localSearchSpec);
  solver->solve();
}

TEST_P(CapacityTest, AggregatedGroupSpec) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setContainerName("container");
  solver->setObjectName("obj");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"container1", {"obj1", "obj2", "obj3"}}, {"container2", {"obj4"}}});
  solver->addObjectDimension(
      "size",
      std::unordered_map<std::string, double>{
          {"obj1", 1}, {"obj2", 2}, {"obj3", 3}, {"obj4", 4}});
  solver->addPartition(
      "partition",
      std::vector<std::pair<std::string, std::string>>{
          {"obj1", "group1"},
          {"obj2", "group1"},
          {"obj3", "group2"},
          {"obj4", "group1"}});
  solver->addScope(
      "scope",
      std::unordered_map<std::string, std::string>{
          {"container1", "item"}, {"container2", "item"}});

  AggregatedGroupSpec spec;
  spec.scope() = "scope";
  spec.partitionName() = "partition";
  spec.dimension() = "size";
  Limit coef;
  coef.scopeItemLimits() = {{"item", 0.8}};
  spec.contributions() = {{"item", coef}};
  spec.limit()->globalLimit() = 10;
  spec.withinGroupAggregationType() = AggregatedGroupSpecAggType::SUM;
  spec.groupAggregationType() = AggregatedGroupSpecAggType::MAX;
  spec.containerAggregationType() = AggregatedGroupSpecAggType::SUM;

  solver->addConstraint(spec);
  // Local search with a limit of zero moves, since we are interested in
  // testing materialization
  LocalSearchSolverSpec localSearchSpec;
  localSearchSpec.stopAfterMoves() = 0;
  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solver->addSolver(localSearchSpec);
  auto solution = solver->solve();
  auto ans = *solution.assignment();
  const std::map<std::string, std::string> expected = {
      {"obj1", "container1"},
      {"obj2", "container1"},
      {"obj3", "container1"},
      {"obj4", "container2"}};
  EXPECT_EQ(expected, toOrderedMap(*solution.assignment()));
}

TEST_P(CapacityOptimalTest, StableStayed) {
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1", "task2"}}, {"unassigned", {}}});

  // add memory dimension to split equivalence sets
  solver->addObjectDimension(
      "weight",
      std::map<std::string, double>{
          {"task0", 10},
          {"task1", 15},
          {"task2", 20},
      });

  // Incentivize host0 to release one task each
  CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.definition() = CapacitySpecDefinition::AFTER;
  spec.bound() = CapacitySpecBound::MAX;
  spec.limit()->globalLimit() = 2;
  spec.filter()->itemsWhitelist() = {"host0"};
  solver->addConstraint(spec);
  ManifoldBackupParams params;
  params.uploadPolicy() = ManifoldUploadPolicy::NEVER;
  solver->setManifoldBackupParams(params);

  // enforce that host0 gives up task of smallest weight
  CapacitySpec validMoveSpec;
  validMoveSpec.scope() = "host";
  validMoveSpec.dimension() = "weight";
  validMoveSpec.definition() = CapacitySpecDefinition::OLD;
  validMoveSpec.bound() = CapacitySpecBound::MAX;
  validMoveSpec.limit()->globalLimit() = 10;
  solver->addConstraint(validMoveSpec);

  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);
  solver->enableStableAsMuchAsPossible();

  auto solution = solver->solve();
  EXPECT_EQ("unassigned", solution.assignment()->at("task0"));
  EXPECT_EQ("host0", solution.assignment()->at("task1"));
  EXPECT_EQ("host0", solution.assignment()->at("task2"));
}

TEST_P(CapacityOptimalTest, StableStayedWithDefaultValues) {
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task3", "task6", "task9", "task12"}},
          {"host1", {"task1", "task4", "task7", "task10", "task13"}},
          {"host2", {"task2", "task5", "task8", "task11", "task14"}},
          {"unassigned", {}}});

  // tasks0-11 all are not moveable
  solver->addObjectDimension(
      "is_fixed",
      std::vector<std::pair<std::string, double>>{
          {"task0", 1},
          {"task1", 1},
          {"task2", 1},
          {"task3", 1},
          {"task4", 1},
          {"task5", 1},
          {"task6", 1},
          {"task7", 1},
          {"task8", 1},
          {"task9", 1},
          {"task10", 1},
          {"task11", 1},
      });

  // Incentivize hosts to release one task each
  CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.definition() = CapacitySpecDefinition::AFTER;
  spec.bound() = CapacitySpecBound::MIN;
  spec.limit()->globalLimit() = 4;
  spec.filter()->itemsWhitelist() = {"host0", "host1", "host2"};
  solver->addConstraint(spec);
  spec.bound() = CapacitySpecBound::MAX;
  solver->addConstraint(spec);
  ManifoldBackupParams params;
  params.uploadPolicy() = ManifoldUploadPolicy::NEVER;
  solver->setManifoldBackupParams(params);

  // enforce that hosts can only give up tasks that are not fixed
  CapacitySpec validMoveSpec;
  validMoveSpec.scope() = "host";
  validMoveSpec.dimension() = "is_fixed";
  validMoveSpec.definition() = CapacitySpecDefinition::OLD;
  validMoveSpec.bound() = CapacitySpecBound::MAX;
  validMoveSpec.limit()->globalLimit() = 0;
  solver->addConstraint(validMoveSpec);

  OptimalSolverSpec optimalSpec;
  optimalSpec.solverPackage() = solverPkg;
  solver->addSolver(optimalSpec);
  solver->enableStableAsMuchAsPossible();
  auto solution = solver->solve();

  for (const auto& [task, host] : *solution.assignment()) {
    if (task == "task12" || task == "task13" || task == "task14") {
      // all moveable tasks move to unassigned
      EXPECT_EQ("unassigned", host);
    } else {
      // all fixed tasks stay at their location
      EXPECT_EQ(solution.initialAssignment()->at(task), host);
    }
  }
}

TEST_P(CapacityTest, SingleRandomStratifiedMoveTypeBasic) {
  // Same as the InitiallyBrokenCapacityConstraint test but with
  // SingleRandomStratifiedMoveType where it samples everything
  // expect no moves as we are now using the destination container as the hot
  // container
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3"}},
      });

  solver->addObjectDimension(
      "memory",
      folly::F14FastMap<std::string, double>{
          {"task0", 10},
          {"task1", 10},
          {"task2", 10},
          {"task3", 10},
      });

  // Each host has 20 GB of memory capacity.
  solver->addContainerDimension(
      "memory",
      folly::F14FastMap<std::string, double>{
          {"host0", 20},
          {"host1", 20},
      });

  const std::unordered_map<std::string, std::vector<std::string>>
      similarObjectsList = {
          {"group1", {"task0", "task1", "task2"}}, {"group2", {"task3"}}};
  solver->addPartition("group", similarObjectsList);

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "memory";
  solver->addConstraint(capacitySpec);

  LocalSearchSolverSpec localSearchSpec;
  SingleRandomObjectStratifiedMoveTypeSpec
      singleRandomObjectStratifiedMoveTypeSpec;

  GroupList groupList;
  groupList.partitionName() = "group";
  ObjectsFromGroupsSpec objectsFromGroupsSpec;
  objectsFromGroupsSpec.groupList() = groupList;
  ObjectsToExploreOptions objectsToExploreOptions;
  objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

  singleRandomObjectStratifiedMoveTypeSpec.objectsToExploreOptions() =
      objectsToExploreOptions;
  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 4;
  singleRandomObjectStratifiedMoveTypeSpec.stratifiedSampleSize() = sampleSize;

  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(
          singleRandomObjectStratifiedMoveTypeSpec));
  localSearchSpec.exploreMovesFromContainersNotInObjective() = false;

  solver->addSolver(localSearchSpec);

  auto solution = solver->solve();
  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(3, tasksInHost["host0"]);
  EXPECT_EQ(1, tasksInHost["host1"]);
}

TEST_P(CapacityTest, SingleRandomStratifiedMoveTypeWithSampleSize) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3", "task4", "task5"}},
          {"host2", {"task6", "task7", "task8"}},
          {"host3", {"task9", "task10", "task11"}}});

  folly::F14FastMap<std::string, double> memoryUsageTask;
  for (const auto i : folly::irange(12)) {
    memoryUsageTask[fmt::format("task{}", i)] = i + 1;
  }

  solver->addObjectDimension("memory", memoryUsageTask, 1 /* defaultValue */);

  folly::F14FastMap<std::string, double> memoryCapacityHost;
  for (const auto i : folly::irange(4)) {
    memoryCapacityHost[fmt::format("host{}", i)] = 40;
  }

  solver->addContainerDimension("memory", memoryCapacityHost);

  const std::unordered_map<std::string, std::vector<std::string>>
      similarObjectsList = {
          {"group1", {"task0", "task1", "task2"}},
          {"group2", {"task3"}},
          {"group3", {"task4"}},
          {"group4", {"task5"}},
          {"group5", {"task6", "task7", "task8"}},
          {"group6", {"task9", "task10", "task11"}}};
  solver->addPartition("group", similarObjectsList);

  CapacitySpec minCapacitySpec;
  minCapacitySpec.scope() = "host";
  minCapacitySpec.filter()->itemsWhitelist() = {"host1"};
  minCapacitySpec.dimension() = "memory";
  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.globalLimit() = 0.5;
  minCapacitySpec.limit() = limit;
  minCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  minCapacitySpec.bound() = CapacitySpecBound::MIN;

  solver->addGoal(minCapacitySpec);
  solver->addGoalBoundary();

  CapacitySpec maxCapacitySpec;
  maxCapacitySpec.scope() = "host";
  maxCapacitySpec.dimension() = "memory";
  maxCapacitySpec.filter()->itemsWhitelist() = {"host1"};
  Limit limit2;
  limit2.type() = LimitType::RELATIVE;
  limit2.globalLimit() = 0.5;
  maxCapacitySpec.limit() = limit2;
  maxCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  maxCapacitySpec.bound() = CapacitySpecBound::MAX;

  solver->addGoal(maxCapacitySpec);

  LocalSearchSolverSpec localSearchSpec;
  SingleRandomObjectStratifiedMoveTypeSpec
      singleRandomObjectStratifiedMoveTypeSpec;

  GroupList groupList;
  groupList.partitionName() = "group";
  ObjectsFromGroupsSpec objectsFromGroupsSpec;
  objectsFromGroupsSpec.groupList() = groupList;
  ObjectsToExploreOptions objectsToExploreOptions;
  objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

  singleRandomObjectStratifiedMoveTypeSpec.objectsToExploreOptions() =
      objectsToExploreOptions;
  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 12;
  singleRandomObjectStratifiedMoveTypeSpec.stratifiedSampleSize() = sampleSize;

  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(
          singleRandomObjectStratifiedMoveTypeSpec));

  std::vector<LocalSearchStageSpec> stages;
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage1";
    stage.begin() = 0;
    stage.end() = 1;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage2";
    stage.begin() = 1;
    stage.end() = 2;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs() = stages;
  stageSolverSpec.exploreMovesFromContainersNotInObjective() = false;

  solver->addSolver(stageSolverSpec);
  auto solution = solver->solve();

  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(3, tasksInHost["host0"]);
  EXPECT_EQ(4, tasksInHost["host1"]);
  EXPECT_EQ(2, tasksInHost["host2"]);
  EXPECT_EQ(3, tasksInHost["host3"]);
}

TEST_P(CapacityTest, SingleRandomStratifiedMoveTypeMultiSize) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3", "task4", "task5"}},
          {"host2", {"task6", "task7", "task8"}},
          {"host3", {"task9", "task10", "task11"}},
          {"host4", {}}});

  folly::F14FastMap<std::string, double> memoryUsageTask;
  for (const auto i : folly::irange(12)) {
    memoryUsageTask[fmt::format("task{}", i)] = i + 1;
  }

  solver->addObjectDimension("memory", memoryUsageTask, 1 /* defaultValue */);

  folly::F14FastMap<std::string, double> memoryCapacityHost;
  for (const auto i : folly::irange(5)) {
    memoryCapacityHost[fmt::format("host{}", i)] = 40;
  }

  solver->addContainerDimension("memory", memoryCapacityHost);

  const std::unordered_map<std::string, std::vector<std::string>>
      similarObjectsList = {
          {"group1", {"task0", "task1", "task2"}},
          {"group2", {"task3"}},
          {"group3", {"task4"}},
          {"group4", {"task5"}},
          {"group5", {"task6", "task7", "task8"}},
          {"group6", {"task9", "task10", "task11"}}};
  solver->addPartition("group", similarObjectsList);

  CapacitySpec minCapacitySpec;
  minCapacitySpec.scope() = "host";
  minCapacitySpec.filter()->itemsWhitelist() = {"host4"};
  minCapacitySpec.dimension() = "memory";
  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.globalLimit() = 0.5;
  minCapacitySpec.limit() = limit;
  minCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  minCapacitySpec.bound() = CapacitySpecBound::MIN;

  solver->addGoal(minCapacitySpec);
  solver->addGoalBoundary();

  CapacitySpec maxCapacitySpec;
  maxCapacitySpec.scope() = "host";
  maxCapacitySpec.dimension() = "memory";
  maxCapacitySpec.filter()->itemsWhitelist() = {"host4"};
  Limit limit2;
  limit2.type() = LimitType::RELATIVE;
  limit2.globalLimit() = 0.5;
  maxCapacitySpec.limit() = limit2;
  maxCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  maxCapacitySpec.bound() = CapacitySpecBound::MAX;

  solver->addGoal(maxCapacitySpec);

  LocalSearchSolverSpec localSearchSpec;
  SingleRandomObjectStratifiedMoveTypeSpec
      singleRandomObjectStratifiedMoveTypeSpec;

  GroupList groupList;
  groupList.partitionName() = "group";
  ObjectsFromGroupsSpec objectsFromGroupsSpec;
  objectsFromGroupsSpec.groupList() = groupList;
  ObjectsToExploreOptions objectsToExploreOptions;
  objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

  singleRandomObjectStratifiedMoveTypeSpec.objectsToExploreOptions() =
      objectsToExploreOptions;

  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 12;
  singleRandomObjectStratifiedMoveTypeSpec.stratifiedSampleSize() = sampleSize;

  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(
          singleRandomObjectStratifiedMoveTypeSpec));

  std::vector<LocalSearchStageSpec> stages;
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage1";
    stage.begin() = 0;
    stage.end() = 1;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage2";
    stage.begin() = 1;
    stage.end() = 2;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs() = stages;
  stageSolverSpec.exploreMovesFromContainersNotInObjective() = false;

  solver->addSolver(stageSolverSpec);
  auto solution = solver->solve();

  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(3, tasksInHost["host0"]);
  EXPECT_EQ(3, tasksInHost["host1"]);
  EXPECT_EQ(2, tasksInHost["host2"]);
  EXPECT_EQ(2, tasksInHost["host3"]);
  EXPECT_EQ(2, tasksInHost["host4"]);
}

TEST_P(CapacityTest, SingleRandomStratifiedMoveTypeWithMultipleWhiteList) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3", "task4", "task5"}},
          {"host2", {"task6", "task7", "task8"}},
          {"host3", {"task9", "task10", "task11"}},
          {"host4", {}}});

  folly::F14FastMap<std::string, double> memoryUsageTask;
  for (const auto i : folly::irange(12)) {
    memoryUsageTask[fmt::format("task{}", i)] = i + 1;
  }

  solver->addObjectDimension("memory", memoryUsageTask, 1 /* defaultValue */);

  folly::F14FastMap<std::string, double> memoryCapacityHost;
  for (const auto i : folly::irange(5)) {
    memoryCapacityHost[fmt::format("host{}", i)] = 40;
  }

  solver->addContainerDimension("memory", memoryCapacityHost);

  const std::unordered_map<std::string, std::vector<std::string>>
      similarObjectsList = {
          {"group1", {"task0", "task1", "task2"}},
          {"group2", {"task3"}},
          {"group3", {"task4"}},
          {"group4", {"task5"}},
          {"group5", {"task6", "task7", "task8"}},
          {"group6", {"task9", "task10", "task11"}}};
  solver->addPartition("group", similarObjectsList);

  CapacitySpec minCapacitySpec;
  minCapacitySpec.scope() = "host";
  minCapacitySpec.filter()->itemsWhitelist() = {"host4", "host1"};
  minCapacitySpec.dimension() = "memory";
  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.globalLimit() = 0.5;
  minCapacitySpec.limit() = limit;
  minCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  minCapacitySpec.bound() = CapacitySpecBound::MIN;

  solver->addGoal(minCapacitySpec);
  solver->addGoalBoundary();

  CapacitySpec maxCapacitySpec;
  maxCapacitySpec.scope() = "host";
  maxCapacitySpec.dimension() = "memory";
  maxCapacitySpec.filter()->itemsWhitelist() = {"host4", "host1"};
  Limit limit2;
  limit2.type() = LimitType::RELATIVE;
  limit2.globalLimit() = 0.5;
  maxCapacitySpec.limit() = limit2;
  maxCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  maxCapacitySpec.bound() = CapacitySpecBound::MAX;

  solver->addGoal(maxCapacitySpec);

  LocalSearchSolverSpec localSearchSpec;
  SingleRandomObjectStratifiedMoveTypeSpec
      singleRandomObjectStratifiedMoveTypeSpec;

  GroupList groupList;
  groupList.partitionName() = "group";
  ObjectsFromGroupsSpec objectsFromGroupsSpec;
  objectsFromGroupsSpec.groupList() = groupList;
  ObjectsToExploreOptions objectsToExploreOptions;
  objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);
  singleRandomObjectStratifiedMoveTypeSpec.objectsToExploreOptions() =
      objectsToExploreOptions;
  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 12;
  singleRandomObjectStratifiedMoveTypeSpec.stratifiedSampleSize() = sampleSize;

  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(
          singleRandomObjectStratifiedMoveTypeSpec));

  std::vector<LocalSearchStageSpec> stages;
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage1";
    stage.begin() = 0;
    stage.end() = 1;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage2";
    stage.begin() = 1;
    stage.end() = 2;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs() = stages;
  stageSolverSpec.exploreMovesFromContainersNotInObjective() = false;

  solver->addSolver(stageSolverSpec);
  auto solution = solver->solve();

  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(3, tasksInHost["host0"]);
  EXPECT_EQ(4, tasksInHost["host1"]);
  EXPECT_EQ(1, tasksInHost["host2"]);
  EXPECT_EQ(2, tasksInHost["host3"]);
  EXPECT_EQ(2, tasksInHost["host4"]);
}

TEST_P(CapacityTest, SingleRandomStratifiedMoveTypeWithMultipleDimensions) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task3", "task4", "task5"}},
          {"host2", {"task6", "task7", "task8"}},
          {"host3", {"task9", "task10", "task11"}},
          {"host4", {}}});

  folly::F14FastMap<std::string, double> memoryUsageTask;
  for (const auto i : folly::irange(12)) {
    memoryUsageTask[fmt::format("task{}", i)] = i + 1;
  }

  solver->addObjectDimension("memory", memoryUsageTask, 1 /* defaultValue */);

  folly::F14FastMap<std::string, double> memoryCapacityHost;
  for (const auto i : folly::irange(5)) {
    memoryCapacityHost[fmt::format("host{}", i)] = 40;
  }

  solver->addContainerDimension("memory", memoryCapacityHost);

  const std::unordered_map<std::string, std::vector<std::string>>
      similarObjectsList = {
          {"group1", {"task0", "task1", "task2"}},
          {"group2", {"task3"}},
          {"group3", {"task4"}},
          {"group4", {"task5"}},
          {"group5", {"task6", "task7", "task8"}},
          {"group6", {"task9", "task10", "task11"}}};
  solver->addPartition("group", similarObjectsList);

  folly::F14FastMap<std::string, double> cpuUsageTask;
  for (int i = 11; i >= 0; i--) {
    cpuUsageTask[fmt::format("task{}", i)] = i + 1;
  }

  solver->addObjectDimension("cpu", cpuUsageTask, 1 /* defaultValue */);

  folly::F14FastMap<std::string, double> cpuCapacityHost;
  for (const auto i : folly::irange(5)) {
    cpuCapacityHost[fmt::format("host{}", i)] = 50;
  }

  solver->addContainerDimension("cpu", cpuCapacityHost);

  CapacitySpec minCapacitySpec;
  minCapacitySpec.scope() = "host";
  minCapacitySpec.filter()->itemsWhitelist() = {"host4", "host1"};
  minCapacitySpec.dimension() = "memory";
  Limit limit;
  limit.type() = LimitType::RELATIVE;
  limit.globalLimit() = 0.5;
  minCapacitySpec.limit() = limit;
  minCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  minCapacitySpec.bound() = CapacitySpecBound::MIN;

  solver->addGoal(minCapacitySpec);

  CapacitySpec minCapacitySpecCPU;
  minCapacitySpecCPU.scope() = "host";
  minCapacitySpecCPU.filter()->itemsWhitelist() = {"host4", "host1"};
  minCapacitySpecCPU.dimension() = "cpu";
  Limit limitCPU;
  limitCPU.type() = LimitType::RELATIVE;
  limitCPU.globalLimit() = 0.9;
  minCapacitySpecCPU.limit() = limitCPU;
  minCapacitySpecCPU.definition() = CapacitySpecDefinition::AFTER;
  minCapacitySpecCPU.bound() = CapacitySpecBound::MIN;

  solver->addGoal(minCapacitySpecCPU);
  solver->addGoalBoundary();

  CapacitySpec maxCapacitySpec;
  maxCapacitySpec.scope() = "host";
  maxCapacitySpec.dimension() = "memory";
  maxCapacitySpec.filter()->itemsWhitelist() = {"host4", "host1"};
  Limit limit2;
  limit2.type() = LimitType::RELATIVE;
  limit2.globalLimit() = 0.5;
  maxCapacitySpec.limit() = limit2;
  maxCapacitySpec.definition() = CapacitySpecDefinition::AFTER;
  maxCapacitySpec.bound() = CapacitySpecBound::MAX;

  solver->addGoal(maxCapacitySpec);

  CapacitySpec maxCapacitySpecCPU;
  maxCapacitySpecCPU.scope() = "host";
  maxCapacitySpecCPU.dimension() = "cpu";
  maxCapacitySpecCPU.filter()->itemsWhitelist() = {"host4", "host1"};
  Limit limitCPU2;
  limitCPU2.type() = LimitType::RELATIVE;
  limitCPU2.globalLimit() = 0.9;
  maxCapacitySpecCPU.limit() = limitCPU2;
  maxCapacitySpecCPU.definition() = CapacitySpecDefinition::AFTER;
  maxCapacitySpecCPU.bound() = CapacitySpecBound::MAX;

  solver->addGoal(maxCapacitySpecCPU);

  LocalSearchSolverSpec localSearchSpec;
  SingleRandomObjectStratifiedMoveTypeSpec
      singleRandomObjectStratifiedMoveTypeSpec;
  GroupList groupList;
  groupList.partitionName() = "group";
  ObjectsFromGroupsSpec objectsFromGroupsSpec;
  objectsFromGroupsSpec.groupList() = groupList;
  ObjectsToExploreOptions objectsToExploreOptions;
  objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);
  singleRandomObjectStratifiedMoveTypeSpec.objectsToExploreOptions() =
      objectsToExploreOptions;
  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 12;
  singleRandomObjectStratifiedMoveTypeSpec.stratifiedSampleSize() = sampleSize;

  localSearchSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(
          singleRandomObjectStratifiedMoveTypeSpec));

  std::vector<LocalSearchStageSpec> stages;
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage1";
    stage.begin() = 0;
    stage.end() = 1;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }
  {
    LocalSearchStageSpec stage;
    stage.name() = "stage2";
    stage.begin() = 1;
    stage.end() = 2;
    stage.solverSpec() = localSearchSpec;
    stages.push_back(stage);
  }

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs() = stages;
  stageSolverSpec.exploreMovesFromContainersNotInObjective() = false;

  solver->addSolver(stageSolverSpec);
  auto solution = solver->solve();

  std::map<std::string, int> tasksInHost;
  for (auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(0, tasksInHost["host0"]);
  EXPECT_EQ(8, tasksInHost["host1"]);
  EXPECT_EQ(0, tasksInHost["host2"]);
  EXPECT_EQ(0, tasksInHost["host3"]);
  EXPECT_EQ(4, tasksInHost["host4"]);
}

TEST_P(CapacityOptimalTest, capUtilization) {
  // The requirement here is to fulfill requested capacity of a scopeItem while
  // capping the utilization of a nested scopeItem to a given upperbound.
  //
  //  Task is to assign "RRU"(objects) to "reservations" (bins)
  //  -- each rru contributes an "RCU" and "storage" (dimensions)
  //  -- rrus are grouped into "regions" (partition)
  // Constraints:
  //  Capacity constraints on "RCU" and "storage" dimensions
  //  Caveat: "storage" from a "region" becomes useless when it is over a
  //  certain limit
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("rru");
  solver->setContainerName("reservation");

  // rru_i_j belongs to region_i
  // rru to region mapping
  std::map<std::string, std::string> rruToRegion;
  std::vector<std::string> rrus;
  for (const auto i : folly::irange(6)) {
    auto region = fmt::format("region_{}", i);
    for (const auto j : folly::irange(3)) {
      auto rru = fmt::format("rru_{}_{}", i, j);
      rruToRegion[rru] = region;
      rrus.push_back(rru);
    }
  }
  // initially all rrus are unassigned
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"reservation0", {}}, {"unassigned", rrus}});
  solver->addPartition("region", rruToRegion);

  // Add RCU and storage dimensions
  solver->addObjectDimension("RCU", std::map<std::string, double>{}, 10);
  solver->addObjectDimension("storage", std::map<std::string, double>{}, 5);

  // Add requirements
  // 1. At least 30 RCU in region1, 20 in region2
  GroupCountSpec minRCU;
  minRCU.scope() = "reservation";
  minRCU.dimension() = "RCU";
  minRCU.name() = "ensure min RCU allocated";
  minRCU.partitionName() = "region";
  minRCU.bound() = GroupCountSpecBound::MIN;
  minRCU.limit()->type() = LimitType::ABSOLUTE;
  minRCU.limit()->globalLimit() = 0;
  minRCU.limit()->groupLimits() = {{"region_0", 30}, {"region_1", 20}};
  minRCU.filter()->itemsWhitelist() = {"reservation0"};
  solver->addConstraint(minRCU);

  // 2. At least 30 storage from any region for reservation0
  CapacitySpec minStorage;
  minStorage.bound() = CapacitySpecBound::MIN;
  minStorage.scope() = "reservation";
  minStorage.name() = "honor global storage limits";
  minStorage.dimension() = "storage";
  minStorage.limit()->type() = LimitType::ABSOLUTE;
  minStorage.limit()->globalLimit() = 35;
  minStorage.filter()->itemsWhitelist() = {"reservation0"};
  GroupUtilizationBound storageCap;
  // storage constribution of each region is capped to 5 (default)
  // and 10 (for region_5)
  storageCap.partitionName() = "region";
  storageCap.perGroupValues()->globalLimit() = 0;
  storageCap.perGroupValues()->isDefaultLimitUnbounded() = false;
  storageCap.perGroupValues()->scopeItemLimits()["reservation0"] = 5;
  storageCap.perGroupValues()
      ->scopeItemToGroupLimits()["reservation0"]["region_5"] = 10;
  minStorage.utilizationBound().emplace();
  minStorage.utilizationBound()->set_groupUtilizationBound(storageCap);
  solver->addConstraint(minStorage);

  auto maxStorage = minStorage;
  maxStorage.bound() = CapacitySpecBound::MAX;
  maxStorage.name() = "minimize total storage of reservation0";
  solver->addGoal(maxStorage);

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.solverPackage() = solverPkg;
  optimalSolverSpec.solveTime() = 10;
  solver->addSolver(optimalSolverSpec);

  auto solution = solver->solve();
  std::map<std::string, int> regionToRRUCount;
  for (auto& [rru, reservation] : *solution.assignment()) {
    if (reservation == "reservation0") {
      regionToRRUCount[rruToRegion[rru]]++;
    }
  }
  // need 3 rrus from region_0 and 2 from region_1 to meet the minRCU demand
  // need at least 1 rru from each region to meet the minStorage demand
  EXPECT_EQ(3, regionToRRUCount["region_0"]);
  EXPECT_EQ(2, regionToRRUCount["region_1"]);
  EXPECT_EQ(1, regionToRRUCount["region_2"]);
  EXPECT_EQ(1, regionToRRUCount["region_3"]);
  EXPECT_EQ(1, regionToRRUCount["region_4"]);
  EXPECT_EQ(2, regionToRRUCount["region_5"]);
}

TEST_P(CapacityOptimalTest, FixedMoveCostForGroups) {
  // In some cases, we may want to consider a "fixed" movement cost of a group
  // of objects regardless of how many objects are moved. One application of
  // this is in the context of gang scheduling, where a group of tasks are
  // scheduled together and the cost of moving a single task is the same as
  // moving the entire group. In other words, moving one task or two tasks
  // incurs the same cost. This can be modeled using utilization cap.
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("task");
  solver->setContainerName("container");
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"container1", {"task1_1"}},
          {"container2", {"task1_2"}},
          {"container3", {"task1_3"}},
          {"container4", {"task2_1"}},
          {"unassigned", {"task3_1", "task3_2"}}});

  std::map<std::string, std::vector<std::string>> jobToTasks = {
      {"job1", {"task1_1", "task1_2", "task1_3"}},
      {"job2", {"task2_1"}},
      {"job3", {"task3_1", "task3_2"}}};
  std::map<std::string, std::string> tasksToJobs;
  for (auto& [job, tasks] : jobToTasks) {
    for (auto& task : tasks) {
      tasksToJobs[task] = job;
    }
  }
  solver->addPartition("job", jobToTasks);

  ToFreeSpec toFreeSpec;
  toFreeSpec.name() = "Ensure that all tasks are assigned";
  toFreeSpec.containers() = {"unassigned"};
  solver->addConstraint(toFreeSpec);

  // movement cost of a task is equal to the number of tasks in the same job
  auto job1Size = jobToTasks.at("job1").size();
  auto job2Size = jobToTasks.at("job2").size();
  auto job3Size = jobToTasks.at("job3").size();
  const std::map<std::string, double> movementCost = {
      {"task1_1", job1Size},
      {"task1_2", job1Size},
      {"task1_3", job1Size},
      {"task2_1", job2Size},
      {"task3_1", job3Size},
      {"task3_2", job3Size}};
  solver->addObjectDimension("movementCost", movementCost, 0);

  // all jobs except job3 need 2 gpus
  solver->addObjectDimension(
      "gpu", std::map<std::string, double>{{"task3_1", 4}, {"task3_2", 4}}, 2);

  // every container has 4 gpus
  CapacitySpec capacitySpec;
  capacitySpec.name() = "do not exceed gpu capacity";
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "gpu";
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 4;
  solver->addConstraint(capacitySpec);

  // add a new "trivial" scope with all assignable containers
  solver->addScope(
      "assignable",
      std::map<std::string, std::vector<std::string>>(
          {{"onlyScopeItem",
            {"container1", "container2", "container3", "container4"}}}));

  CapacitySpec minimizeMovementCost;
  minimizeMovementCost.bound() = CapacitySpecBound::MAX;
  minimizeMovementCost.definition() = CapacitySpecDefinition::NEW;
  minimizeMovementCost.scope() = "assignable";
  minimizeMovementCost.name() = "minimize movement cost of jobs";
  minimizeMovementCost.dimension() = "movementCost";
  minimizeMovementCost.limit()->type() = LimitType::ABSOLUTE;
  minimizeMovementCost.limit()->globalLimit() = 0;
  GroupUtilizationBound movementCostCap;
  movementCostCap.partitionName() = "job";
  // without the following change, the solver can move more jobs than needed
  movementCostCap.aggregationScope() = "container";
  movementCostCap.perGroupValues()->groupLimits() = {
      {"job1", job1Size}, {"job2", job2Size}, {"job3", job3Size}};
  movementCostCap.perGroupValues()->isDefaultLimitUnbounded() = false;
  minimizeMovementCost.utilizationBound().emplace();
  minimizeMovementCost.utilizationBound()->set_groupUtilizationBound(
      movementCostCap);
  solver->addGoal(minimizeMovementCost);

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.solverPackage() = solverPkg;
  optimalSolverSpec.solveTime() = 10;
  solver->addSolver(optimalSolverSpec);

  auto solution = solver->solve();
  folly::F14FastSet<std::string> movedJobs;
  for (auto& [task, dstContainer] : *solution.assignment()) {
    const auto& srcContainer = solution.initialAssignment()->at(task);
    if (srcContainer != dstContainer) {
      XLOG(INFO) << fmt::format(
          "Task {} moved from {} to {}", task, srcContainer, dstContainer);
      movedJobs.insert(tasksToJobs[task]);
    }
  }
  // The key invariant: only job1 and job3 should be disturbed, not job2.
  // Moving any tasks from job2 would add its capped cost (1) to the objective,
  // making it suboptimal. The exact number of tasks moved within job1 is not
  // checked: the GroupUtilizationBound makes moving 1 or 3 tasks from job1
  // equally costly, so solvers may choose either.
  EXPECT_EQ(2, movedJobs.size());
  XLOG(INFO) << fmt::format("Moved jobs: {}", folly::join(", ", movedJobs));
}

TEST_P(CapacityOptimalTest, ReproNumericIssue) {
  auto [threads, solverPkg] = GetParam();
  if (solverPkg != OptimalSolverPackage::GUROBI) {
    GTEST_SKIP() << "Gurobi-specific numeric reproduction";
  }
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("server");
  solver->setContainerName("reservation");
  // assume only 1 reservation r0
  std::map<std::string, std::vector<std::string>> containerToServers = {
      {"unassigned", {}}, {"r0", {}}};
  // r0 initilly had 49 servers
  for (const auto i : folly::irange(49)) {
    containerToServers["r0"].emplace_back(fmt::format("s{}", i));
  }
  solver->setAssignment(containerToServers);
  // Add an rru dimension
  solver->addObjectDimension(
      "rru", std::map<std::string, double>{}, 1.4894894894894894);
  // Add a capacity spec
  CapacitySpec capacitySpec;
  capacitySpec.scope() = "reservation";
  capacitySpec.dimension() = "rru";
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.filter()->itemsWhitelist() = {"r0"};

  // Add lowerbound constraint that requires at least 47 servers
  // 1.4894894894894894 * 47 = 70.006006006
  // Make the bound so that it is within 3e-6 of the limit
  capacitySpec.name() = "numerically sensitive capacity spec LB";
  capacitySpec.bound() = CapacitySpecBound::MIN;
  capacitySpec.limit()->globalLimit() = 70.006009009009;

  solver->addConstraint(capacitySpec);

  // Add lowerbound constraint that requires at most 48 servers
  // 1.4894894894894894 * 48 = 71.4954954955
  // Make the bound so that it is within 3e-6 of the limit
  capacitySpec.name() = "numerically sensitive capacity spec UB";
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit()->globalLimit() = 70.006009009009;
  solver->addConstraint(capacitySpec);

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.solverPackage() = OptimalSolverPackage::GUROBI;
  optimalSolverSpec.solveTime() = 10;
  optimalSolverSpec.xpressArgs() = {
      {"GRB_FeasibilityTol", 1e-6},
      {"GRB_IntFeasTol", 1e-5},
      {"GRB_Presolve", 0}};
  solver->addSolver(optimalSolverSpec);

  auto solution = solver->solve();

  for (auto& [server, reservation] : *solution.assignment()) {
    if (reservation == "unassigned") {
      XLOG(INFO) << fmt::format("Server {} moved to unassigned", server);
    }
  }
}

TEST_P(CapacityTest, DynamicDimensionDeadlockTest) {
  // This test reproduces the deadlock issue from nested blockingWait
  // when using dynamic dimensions with multiple capacity specs.
  // The deadlock occurs when:
  // 1. Materializer::materialize() calls blockingWait(collectAll(...)) on
  // executor
  // 2. Multiple constraint tasks run on executor threads
  // 3. Each calls getAbsoluteUtilAfterDynamic() which has a nested
  //    blockingWait on the SAME executor
  // 4. All executor threads block -> DEADLOCK
  constexpr int numHosts = 100;
  constexpr int numTasks = 1000;
  constexpr int numDynamicDimensions =
      10; // 10 capacity specs with dynamic dims

  auto solver = interface::initializeTestProblemSolver(
      interface::TestProblemSolverParams{
          .executorThreadCount = numDynamicDimensions / 2});
  solver->setObjectName("task");
  solver->setContainerName("host");

  // Create initial assignment: tasks distributed across hosts
  folly::F14FastMap<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(numHosts)) {
    initialAssignment[fmt::format("host{}", i)] = {};
  }
  for (const auto i : folly::irange(numTasks)) {
    auto host = fmt::format("host{}", i % numHosts);
    initialAssignment[host].push_back(fmt::format("task{}", i));
  }
  solver->setAssignment(initialAssignment);

  // Add dynamic object dimensions: value depends on (task, host) pair
  // This triggers getAbsoluteUtilAfterDynamic() during materialization
  for (const auto d : folly::irange(numDynamicDimensions)) {
    folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
        hostToTaskToValue;
    for (const auto h : folly::irange(numHosts)) {
      folly::F14FastMap<std::string, double> taskValues;
      for (const auto t : folly::irange(numTasks)) {
        // Dynamic: value depends on which host the task is on
        taskValues[fmt::format("task{}", t)] = (t + h + d) % 10 + 1;
      }
      hostToTaskToValue[fmt::format("host{}", h)] = std::move(taskValues);
    }
    solver->addDynamicObjectDimension(
        fmt::format("dynamic_dim{}", d), "host", hostToTaskToValue, 1.0);

    // Add container dimension for capacity limit
    folly::F14FastMap<std::string, double> hostCapacity;
    for (const auto h : folly::irange(numHosts)) {
      hostCapacity[fmt::format("host{}", h)] = 100.0;
    }
    solver->addContainerDimension(
        fmt::format("dynamic_dim{}", d), hostCapacity);
  }

  // Add capacity constraints for each dynamic dimension
  // Each constraint will trigger materialization that calls
  // getAbsoluteUtilAfterDynamic
  for (const auto d : folly::irange(numDynamicDimensions)) {
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = fmt::format("dynamic_dim{}", d);
    capacitySpec.bound() = interface::CapacitySpecBound::MAX;
    capacitySpec.definition() = interface::CapacitySpecDefinition::AFTER;

    interface::Limit limit;
    limit.type() = interface::LimitType::RELATIVE;
    limit.globalLimit() = 1.0;
    capacitySpec.limit() = std::move(limit);

    solver->addConstraint(std::move(capacitySpec));
  }

  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleFastMoveTypeSpec()));
  solverSpec.stopAfterMoves() = 1;

  solver->addSolver(solverSpec);

  // This will hang if there is a deadlock
  const auto solution = solver->solve();

  // just checking that the solver didn't hang
  EXPECT_FALSE(solution.assignment()->empty());
}

TEST_P(CapacityTest, DuringInvalidMoveFilterAllowsWithinScopeItemBalance) {
  // Regression test for the DURING invalid-move filter exempting objects that
  // are already initially assigned to a scope item. rack0 spans host0 and host1
  // and has a zero DURING limit, so the invalid-move filter is populated for
  // it. Both tasks start on host0; a balance goal wants one task per host -- a
  // move *within* rack0, which does not change rack0's DURING utilization.
  // Without the exemption the filter blocks the initially-assigned tasks from
  // every rack0 container, so the balance move is pruned and the tasks stay
  // stacked on host0.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {}},
      });

  solver->addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{{"task0", 1}, {"task1", 1}});
  solver->addContainerDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{{"host0", 10}, {"host1", 10}});

  solver->addScope(
      "rack",
      std::unordered_map<std::string, std::string>{
          {"host0", "rack0"}, {"host1", "rack0"}});

  CapacitySpec capacitySpec;
  capacitySpec.scope() = "rack";
  capacitySpec.dimension() = "cpu";
  capacitySpec.definition() = CapacitySpecDefinition::DURING;
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = 0;
  solver->addConstraint(capacitySpec);

  BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.formula() = BalanceSpecFormula::IDEAL;
  solver->addGoal(balanceSpec);

  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec()));
  solver->addSolver(solverSpec);

  const auto solution = solver->solve();

  std::map<std::string, int> tasksInHost;
  for (const auto& [_task, host] : *solution.assignment()) {
    ++tasksInHost[host];
  }
  EXPECT_EQ(1, tasksInHost["host0"]);
  EXPECT_EQ(1, tasksInHost["host1"]);
}

TEST_P(CapacityOptimalTest, BundledCapacityReplacesIncompleteBundles) {
  auto [threads, solverPkg] = GetParam();
  auto solver = initializeTestProblemSolver({.executorThreadCount = threads});
  solver->setObjectName("server");
  solver->setContainerName("reservation");
  solver->setAssignment(
      std::map<std::string, std::vector<std::string>>{
          {"r0", {"fragmentA0", "fragmentA1", "fragmentB0"}},
          {"free", {"pure0", "pure1", "pure2"}}});
  solver->addPartition(
      "pod",
      std::map<std::string, std::vector<std::string>>{
          {"fragmentA", {"fragmentA0", "fragmentA1"}},
          {"fragmentB", {"fragmentB0"}},
          {"pure", {"pure0", "pure1", "pure2"}}});
  solver->addObjectDimension("eligible", std::map<std::string, double>{}, 1);
  solver->addObjectDimension("cost", std::map<std::string, double>{}, 1);

  const auto makeBundledSpec = [](CapacitySpecBound bound) {
    GroupBundleUtilization bundles;
    bundles.partitionName() = "pod";
    bundles.scope() = "reservation";
    bundles.bundleSizes() = {{"r0", 3}};

    CapacitySpec spec;
    spec.scope() = "reservation";
    spec.dimension() = "eligible";
    spec.bound() = bound;
    spec.limit()->type() = LimitType::ABSOLUTE;
    spec.limit()->globalLimit() = bound == CapacitySpecBound::MAX ? 3 : 2;
    spec.filter()->itemsWhitelist() = {"r0"};
    spec.groupBundleUtilization() = std::move(bundles);
    return spec;
  };
  solver->addConstraint(makeBundledSpec(CapacitySpecBound::MIN));
  solver->addConstraint(makeBundledSpec(CapacitySpecBound::MAX));

  CapacitySpec minimizeServers;
  minimizeServers.scope() = "reservation";
  minimizeServers.dimension() = "cost";
  minimizeServers.limit()->type() = LimitType::ABSOLUTE;
  minimizeServers.limit()->globalLimit() = 0;
  minimizeServers.filter()->itemsWhitelist() = {"r0"};
  solver->addGoal(minimizeServers);

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.solverPackage() = solverPkg;
  optimalSolverSpec.solveTime() = 10;
  solver->addSolver(optimalSolverSpec);

  const auto solution = solver->solve();
  for (const auto* server : {"pure0", "pure1", "pure2"}) {
    EXPECT_EQ(solution.assignment()->at(server), "r0");
  }
  for (const auto* server : {"fragmentA0", "fragmentA1", "fragmentB0"}) {
    EXPECT_EQ(solution.assignment()->at(server), "free");
  }
}

} // namespace facebook::rebalancer::interface::tests
