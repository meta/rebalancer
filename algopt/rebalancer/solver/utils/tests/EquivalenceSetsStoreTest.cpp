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

#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/builders/ConstraintsBuilder.h"
#include "algopt/rebalancer/entities/builders/GoalsBuilder.h"
#include "algopt/rebalancer/entities/Constraint.h"
#include "algopt/rebalancer/entities/Goal.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/materializer/Materializer.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"
#include "algopt/rebalancer/treeprof/Profiler.h"
#include <algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSetsStore.h>

#include "multifeed/hash/HeterogeneousAccess.h"

#include "gtest/gtest.h"
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/F14Set.h>
#include <folly/container/HeterogeneousAccess.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/lang/Hint.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/String.h>
#include <folly/tracing/AsyncStack.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <coroutine>
#include <memory>
#include <optional>
#include <ostream>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

namespace materializer = facebook::rebalancer::materializer;

const folly::F14FastSet<std::string> kEmptySet;

class EquivalenceSetsStoreTest : public ExpressionTestsBase {
 protected:
  void SetUp() override {
    folly::coro::blockingWait(setUpUniverse());
  }

  folly::coro::Task<void> setUpUniverse() {
    // Setup initial assignment
    // only one container (id = 0) that contains n objects
    constexpr int n = 16;
    PackerMap<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(n)) {
      assignment["container0"].push_back(fmt::format("object{}", i));
    }
    setInitialAssignment(assignment);

    // create a dimension that has nonzero values for odd objects
    entities::Map<std::string, double> objectNameToValue;
    for (const auto i : folly::irange(n)) {
      objectNameToValue[fmt::format("object{}", i)] = i % 2;
    }
    co_await addObjectDimension(
        "binary", objectNameToValue, /*defaultValue=*/0);

    // this constraint should split all objects into two equivalence sets (odd
    // and even)
    co_await addCapacityConstraint("binary", "binary_constraint", n / 2);
    co_await addCapacityGoal("binary", "binary_goal", n / 2);
  }

  // The capacity of each container in dimension `dimName` is at most
  // `globalLimit`.
  folly::coro::Task<void> addCapacityConstraint(
      const std::string& dimName,
      const std::string& constraintName,
      int globalLimit) {
    interface::CapacitySpec spec;
    spec.scope() = "container";
    spec.dimension() = dimName;
    spec.limit()->globalLimit() = globalLimit;

    interface::ConstraintSpecs constraint;
    constraint.capacitySpec() = spec;

    const auto innerCtr = universeBuilder_.makeConstraintId(constraintName);
    co_await universeBuilder_.addConstraint(
        innerCtr,
        entities::ConstraintData{
            .constraint = std::make_unique<entities::Constraint>(
                std::move(constraint),
                interface::ConstraintPolicy::HARD,
                /*invalidCost=*/100,
                /*invalidState=*/10000,
                /*tuplePosIfBroken=*/0)});
  }

  // Add an objective to get utilization of dimension `dimName` in each
  // container as close as possible to `globalLimit`.
  folly::coro::Task<void> addCapacityGoal(
      const std::string& dimName,
      const std::string& goalName,
      int globalLimit) {
    interface::CapacitySpec spec;
    spec.scope() = "container";
    spec.dimension() = dimName;
    spec.limit()->globalLimit() = globalLimit;

    interface::GoalSpecs goal;
    goal.capacitySpec() = spec;

    const auto goalId = universeBuilder_.makeGoalId(goalName);
    co_await universeBuilder_.addGoal(
        goalId,
        entities::GoalData{
            .goal = std::make_unique<entities::Goal>(
                std::move(goal),
                /*weight=*/1,
                /*tupleIndex=*/0)});
  }

  // creates a partition with n/k groups, each with k objects
  folly::coro::Task<void>
  addPartitionModK(const std::string& partitionName, int n, int k) {
    entities::Map<std::string, std::vector<std::string>> groupToObjects;
    for (const auto i : folly::irange(n)) {
      groupToObjects[fmt::format("group_{}", i / k)].push_back(
          fmt::format("object{}", i));
    }
    co_await addPartition(partitionName, groupToObjects);
  }

  Problem makeProblem() {
    universe_ = buildUniverse();
    constexpr int numThreads = 2;
    auto executor = std::make_shared<folly::CPUThreadPoolExecutor>(numThreads);
    auto materializedProblem = materializer::Materializer::materialize(
        std::make_shared<algopt::treeprof::ExecutorWrapper>(executor),
        universe_,
        /*continuousExpressions=*/false);
    auto configs = ProblemConfigs{.useDynamicObjectOrdering = true};
    return Problem(universe_, materializedProblem, configs);
  }

  // Given a partitioning of objects into groups and a set of constraints,
  // split the groups further into equivalent sets such that two objects are
  // equivalent if and only if they are in the same group and are also
  // "equivalent" for each goal and constraint
  static EquivalenceSets splitObjectPartitionByConstraintList(
      Problem& problem,
      const std::string& partitionName,
      const std::vector<std::string>& constraintNames,
      const std::vector<std::string>& goalNames) {
    problem.getEquivalenceSetsStore().initialize(
        partitionName, constraintNames, goalNames);
    return problem.getEquivalenceSetsStore().get();
  }

  std::shared_ptr<const entities::Universe> universe_;
};

TEST_F(
    EquivalenceSetsStoreTest,
    SetAndGetAllGoalsAndConstraintsWithExclusions) {
  auto problem = makeProblem();

  // Initializing equivalence_sets_store with no exclusions results in creating
  // most granular equivalence sets. In this case, there are 2 equivalence sets,
  // one for each dimension value.
  problem.getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/kEmptySet,
      /*excludeGoalNames=*/kEmptySet);
  EXPECT_EQ(problem.getEquivalenceSetsStore().get().size(), 2);

  // Initializing equivalence_sets_store by excluding dimensions in both
  // constraint and goal results in one equivalence set containing all objects,
  // since all objects differ only in the one dimension value.
  problem.getEquivalenceSetsStore().clear();
  problem.getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/{"binary_constraint"},
      /*excludeGoalNames=*/{"binary_goal"});
  EXPECT_EQ(problem.getEquivalenceSetsStore().get().size(), 1);

  // Initializing equivalence_sets_store by excluding dimensions in
  // either goal or constraint results in two equivalence sets, since both
  // the goal and constraint have a dimension (`binary`) that splits the objects
  // into two distinct values.
  problem.getEquivalenceSetsStore().clear();
  problem.getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/kEmptySet,
      /*excludeGoalNames=*/{"binary_goal"});
  EXPECT_EQ(problem.getEquivalenceSetsStore().get().size(), 2);

  problem.getEquivalenceSetsStore().clear();
  problem.getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/{"binary_constraint"},
      /*excludeGoalNames=*/{});
  EXPECT_EQ(problem.getEquivalenceSetsStore().get().size(), 2);
}

TEST_F(EquivalenceSetsStoreTest, SetAndGetAllGoalsAndConstraints) {
  auto problem = makeProblem();

  // test set for all goals and constraints
  problem.getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/kEmptySet, /*excludeGoalNames=*/kEmptySet);
  EXPECT_EQ(2, problem.getEquivalenceSetsStore().get().size());
}

CO_TEST_F(EquivalenceSetsStoreTest, SetAndGetWithPartition) {
  // create partition, each group has 4 objects
  co_await addPartitionModK("mod_4", 16, 4);

  auto problem = makeProblem();

  // test set with partition, goals and constraints
  problem.getEquivalenceSetsStore().initialize(
      "mod_4", {"binary_constraint"}, {"binary_goal"});
  EXPECT_EQ(8, problem.getEquivalenceSetsStore().get().size());

  // test clear
  problem.getEquivalenceSetsStore().clear();
  // subsequent calls to get() after clear() should re-initialize with default
  // equivalence sets
  EXPECT_EQ(2, problem.getEquivalenceSetsStore().get().size());
}

CO_TEST_F(EquivalenceSetsStoreTest, cachedAccess) {
  // Profiler is used to measure the time taken to build equivalence sets
  const algopt::treeprof::Profiler profiler("EquivalenceSetsStoreTest");

  // create partition, each group has 4 objects
  co_await addPartitionModK("mod_4", 16, 4);
  auto problem = makeProblem();

  double buildTimeSoFar = 0;
  auto checkAndUpdateBuildTime = [&](double newBuildTime, bool equal = false) {
    if (equal) {
      EXPECT_EQ(buildTimeSoFar, newBuildTime);
    } else {
      EXPECT_GT(newBuildTime, buildTimeSoFar);
      buildTimeSoFar = newBuildTime;
    }
  };
  auto& equivSetStore = problem.getEquivalenceSetsStore();
  equivSetStore.clear();
  // initialize with partition, goals and constraints
  equivSetStore.initialize("mod_4", {"binary_constraint"}, {"binary_goal"});
  EXPECT_EQ(8, problem.getEquivalenceSetsStore().get().size());
  checkAndUpdateBuildTime(
      problem.getEquivalenceSetsStore().getTotalBuildTime());

  // re-initialize with all goals and constraints
  problem.getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/kEmptySet, /*excludeGoalNames=*/kEmptySet);
  EXPECT_EQ(2, problem.getEquivalenceSetsStore().get().size());
  checkAndUpdateBuildTime(
      problem.getEquivalenceSetsStore().getTotalBuildTime());

  // re-initialize with partition, goals and constraints
  // this should not recompute equivalence sets and fetch from cache
  equivSetStore.initialize("mod_4", {"binary_constraint"}, {"binary_goal"});
  EXPECT_EQ(8, problem.getEquivalenceSetsStore().get().size());
  checkAndUpdateBuildTime(
      problem.getEquivalenceSetsStore().getTotalBuildTime(), /*equal=*/true);

  // re-initialize with all goals and constraints
  // this should not recompute equivalence sets and fetch from cache
  equivSetStore.initialize(
      /*excludeConstraintNames=*/kEmptySet, /*excludeGoalNames=*/kEmptySet);
  EXPECT_EQ(2, problem.getEquivalenceSetsStore().get().size());
  checkAndUpdateBuildTime(
      problem.getEquivalenceSetsStore().getTotalBuildTime(), /*equal=*/true);

  equivSetStore.clear();
  // subsequent calls to get() after clear() should re-initialize with default
  EXPECT_EQ(2, problem.getEquivalenceSetsStore().get().size());
  // expect the re-initialization to take some time
  checkAndUpdateBuildTime(
      problem.getEquivalenceSetsStore().getTotalBuildTime());
}

CO_TEST_F(EquivalenceSetsStoreTest, splitEquivalenceSets) {
  // create a global partition, all objects in one group
  co_await addPartitionModK("global", 16, 16);
  // create another partition, each group has 4 objects
  co_await addPartitionModK("mod_4", 16, 4);

  auto problem = makeProblem();

  // 1. Test with global partition
  auto singletonEquivSet =
      splitObjectPartitionByConstraintList(problem, "global", {}, {});
  EXPECT_EQ(1, singletonEquivSet.size());
  auto twoEquivSets = splitObjectPartitionByConstraintList(
      problem, "global", {"binary_constraint"}, {});
  EXPECT_EQ(2, twoEquivSets.size());

  // 2. Test with mod_4 partition
  auto fourEquivSets =
      splitObjectPartitionByConstraintList(problem, "mod_4", {}, {});
  EXPECT_EQ(4, fourEquivSets.size());
  auto eightEquivSets = splitObjectPartitionByConstraintList(
      problem, "mod_4", {"binary_constraint"}, {});
  EXPECT_EQ(8, eightEquivSets.size());

  // Verify that the objects are indeed split correctly by binary constraint
  for (auto idx : eightEquivSets.ids()) {
    auto equivSet = eightEquivSets.getSet(idx);
    EXPECT_EQ(2, equivSet.size());
    auto firstObjectId = *equivSet.begin();
    auto setIdxInFourEquivSets = fourEquivSets.at(firstObjectId);
    std::vector<std::string> objectsInSet;
    for (auto objectId : equivSet) {
      // all objects that belong to the same set in "granular" equivalence class
      // must belong to same set in "coarser" equivalence class
      EXPECT_EQ(setIdxInFourEquivSets, fourEquivSets.at(objectId));
      objectsInSet.push_back(universe_->getEntityName(objectId));
    }
    XLOG(INFO) << fmt::format(
        "Set {} contains objects: {}", idx, folly::join(", ", objectsInSet));
  }

  // 3. Test with both constraints and goals
  auto twoEquivSetsAsGoal = splitObjectPartitionByConstraintList(
      problem, "global", {}, {"binary_goal"});
  EXPECT_EQ(2, twoEquivSetsAsGoal.size());
  auto eightEquivSetsAsGoal = splitObjectPartitionByConstraintList(
      problem, "mod_4", {}, {"binary_goal"});
  EXPECT_EQ(8, eightEquivSetsAsGoal.size());
  auto eightEquivSetsAsGoalAndCtr = splitObjectPartitionByConstraintList(
      problem, "mod_4", {"binary_constraint"}, {"binary_goal"});
  EXPECT_EQ(8, eightEquivSetsAsGoalAndCtr.size());
}

CO_TEST_F(EquivalenceSetsStoreTest, customEquivalenceSetConfig) {
  // Setup: We will incrementally add more entities (goals/ constraints/
  // partitions) to the following config which should grow the equivalence sets.
  // Next we remove these entities which should shrink the equivalence sets.
  interface::CustomEquivalenceSetConfig equivSetConfig;
  // create a global partition, all objects in one group
  co_await addPartitionModK("global", 16, 16);
  // create another partition, each group has 4 objects
  // grows equivalence set to 4
  co_await addPartitionModK("mod_4", 16, 4);

  auto problem = makeProblem();
  equivSetConfig.partitionNames() = {"global", "mod_4"};
  problem.getEquivalenceSetsStore().initialize(equivSetConfig);
  EXPECT_EQ(4, problem.getEquivalenceSetsStore().get().size());

  // add `binary_constraint`, should grow the sets to 8
  equivSetConfig.constraintSelectionConfig()->stringsToFilter() = {
      "binary_constraint"};
  equivSetConfig.constraintSelectionConfig()->filterType() =
      algopt::common::thrift::FilterType::ALLOWLIST;
  problem.getEquivalenceSetsStore().initialize(equivSetConfig);
  EXPECT_EQ(8, problem.getEquivalenceSetsStore().get().size());

  // Adding this extra goal to split on has no impact on number of equivalence
  // sets as it creates same sets as `binary_constraint`
  equivSetConfig.goalSelectionConfig()->stringsToFilter() = {"binary_goal"};
  equivSetConfig.goalSelectionConfig()->filterType() =
      algopt::common::thrift::FilterType::ALLOWLIST;
  problem.getEquivalenceSetsStore().initialize(equivSetConfig);
  EXPECT_EQ(8, problem.getEquivalenceSetsStore().get().size());

  // after excluding binary_constraint, we still have 8 equivalence sets due to
  // binary_goal
  equivSetConfig.constraintSelectionConfig()->filterType() =
      algopt::common::thrift::FilterType::BLOCKLIST;
  problem.getEquivalenceSetsStore().initialize(equivSetConfig);
  EXPECT_EQ(8, problem.getEquivalenceSetsStore().get().size());

  // Exluding binary_goal brings this down to 4
  equivSetConfig.goalSelectionConfig()->stringsToFilter() = {"binary_goal"};
  equivSetConfig.goalSelectionConfig()->filterType() =
      algopt::common::thrift::FilterType::BLOCKLIST;
  problem.getEquivalenceSetsStore().initialize(equivSetConfig);
  EXPECT_EQ(4, problem.getEquivalenceSetsStore().get().size());

  // excluding the partitions brings it down to zero
  equivSetConfig.partitionNames() = {};
  problem.getEquivalenceSetsStore().initialize(equivSetConfig);
  EXPECT_EQ(1, problem.getEquivalenceSetsStore().get().size());
}

TEST_F(EquivalenceSetsStoreTest, AssignmentIndexFollowsInitializedSets) {
  auto problem = makeProblem();
  auto& store = problem.getEquivalenceSetsStore();
  const auto numDistinctObjects = [&]() {
    problem.assignment.buildAllIndexesByEquivalentSets(
        problem.getUniverse(), store.get());
    return problem.assignment.getObjectsIndexedByEquivSets(store.get())
        .getContainerObjects(container(0))
        .size();
  };

  // the default sets split the objects into odd and even
  EXPECT_EQ(2, numDistinctObjects());

  store.initialize(
      /*excludeConstraintNames=*/{"binary_constraint"},
      /*excludeGoalNames=*/{"binary_goal"});
  EXPECT_EQ(1, numDistinctObjects());

  // back to the cached default sets
  store.initialize(
      /*excludeConstraintNames=*/kEmptySet, /*excludeGoalNames=*/kEmptySet);
  EXPECT_EQ(2, numDistinctObjects());
}

} // namespace facebook::rebalancer::packer::tests
