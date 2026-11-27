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

#include <folly/container/irange.h>
#include <folly/Portability.h>
#include <gtest/gtest.h>

#include <map>
#include <string>

using namespace facebook::rebalancer::interface;

namespace {
void setMaxFreeLimit(MinimizeContainersSpec& spec, int32_t limit) {
  MinimizeContainersTarget target;
  target.set_maxFreeLimit(limit);
  spec.target() = std::move(target);
}
} // namespace

class MinimizeContainersTest : public ::testing::TestWithParam<int> {
  void SetUp() override {}

 public:
  static std::unique_ptr<ProblemSolver> createScenario(int threadCount) {
    // 10 hosts
    // 16 tasks
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = threadCount});
    solver->setObjectName("task");
    solver->setContainerName("host");

    std::map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(10)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }

    // initialize all task(i) in host(i)
    for (const auto j : folly::irange(2)) {
      for (const auto i : folly::irange(8)) {
        auto host = fmt::format("host{}", i);
        auto task = fmt::format("task{}", j * 8 + i);
        assignment[host].push_back(task);
      }
    }
    solver->setAssignment(assignment);

    solver->addSolver(makeDefaultLocalSearchSolver());
    return solver;
  }

  static std::unique_ptr<ProblemSolver> createScenario2(int threadCount) {
    // 10 hosts
    // 8 tasks
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = threadCount});
    solver->setObjectName("task");
    solver->setContainerName("host");

    std::unordered_map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(10)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }

    // initialize all task(i) in host(i)
    int count = 0;
    for (const auto i : folly::irange(8)) {
      for (const auto _ : folly::irange(i + 1)) {
        auto host = fmt::format("host{}", i);
        auto task = fmt::format("task{}", count++);
        assignment[host].push_back(task);
      }
    }
    solver->setAssignment(assignment);
    solver->addSolver(makeDefaultLocalSearchSolver());

    return solver;
  }

  static std::unique_ptr<ProblemSolver> createLargeScenario(int threadCount) {
    // 100 hosts
    // 101 tasks
    // 1 task per host except host 99 which will have 2
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = threadCount});
    solver->setObjectName("task");
    solver->setContainerName("host");

    folly::F14FastMap<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(100)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }

    // initialize all task(i) in host(i)
    for (const auto i : folly::irange(100)) {
      auto host = fmt::format("host{}", i);
      auto task = fmt::format("task{}", i);
      assignment[host].push_back(task);
    }

    auto host = fmt::format("host{}", 99);
    auto task = fmt::format("task{}", 100);
    assignment[host].push_back(task);

    solver->setAssignment(assignment);

    solver->addSolver(makeDefaultLocalSearchSolver());
    return solver;
  }

  static std::unique_ptr<ProblemSolver> createScenarioWithContainersNotInScope(
      int threadCount) {
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = threadCount});
    solver->setObjectName("task");
    solver->setContainerName("host");

    std::unordered_map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(12)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }

    // initialize all task(i) in host(i)
    for (const auto j : folly::irange(2)) {
      for (const auto i : folly::irange(8)) {
        auto host = fmt::format("host{}", i);
        auto task = fmt::format("task{}", j * 8 + i);
        assignment[host].push_back(task);
      }
    }

    // host 11 will contain tasks
    assignment["host11"].emplace_back("task100");

    solver->setAssignment(assignment);

    // host 10 and 11 is not in scope
    std::map<std::string, std::string> hostToRack;
    hostToRack["host0"] = "rack0";
    hostToRack["host1"] = "rack0";
    hostToRack["host2"] = "rack1";
    hostToRack["host3"] = "rack1";
    hostToRack["host4"] = "rack2";
    hostToRack["host5"] = "rack2";
    hostToRack["host6"] = "rack3";
    hostToRack["host7"] = "rack3";
    hostToRack["host8"] = "rack4";
    hostToRack["host9"] = "rack4";

    solver->addScope("rack", hostToRack);

    solver->addSolver(makeDefaultLocalSearchSolver());
    return solver;
  }

  static std::unique_ptr<ProblemSolver>
  createScenarioWithDynamicObjectDimensions(int threadCount) {
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = threadCount});
    solver->setObjectName("task");
    solver->setContainerName("host");

    std::unordered_map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(10)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }

    // initialize all task(i) in host(i)
    for (const auto j : folly::irange(2)) {
      for (const auto i : folly::irange(8)) {
        auto host = fmt::format("host{}", i);
        auto task = fmt::format("task{}", j * 8 + i);
        assignment[host].push_back(task);
      }
    }
    solver->setAssignment(assignment);

    folly::F14FastMap<std::string, std::map<std::string, double>>
        dynamicObjectDimension;
    for (const auto i : folly::irange(10)) {
      auto host = fmt::format("host{}", i);
      for (const auto j : folly::irange(16)) {
        auto task = fmt::format("task{}", j);
        dynamicObjectDimension[host][task] = (i + 1) * (j + 1);
      }
    }
    solver->addDynamicObjectDimension(
        "weight", "host", dynamicObjectDimension, 1.0);

    solver->addSolver(makeDefaultLocalSearchSolver());
    return solver;
  }

  static std::unique_ptr<ProblemSolver> createNegativeObjectDimensionScenario(
      int threadCount) {
    // 10 hosts
    // 16 tasks
    auto solver =
        initializeTestProblemSolver({.executorThreadCount = threadCount});
    solver->setObjectName("task");
    solver->setContainerName("host");

    std::map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(10)) {
      auto host = fmt::format("host{}", i);
      assignment[host] = {};
    }

    // initialize all task(i) in host(i)
    for (const auto j : folly::irange(2)) {
      for (const auto i : folly::irange(8)) {
        auto host = fmt::format("host{}", i);
        auto task = fmt::format("task{}", j * 8 + i);
        assignment[host].push_back(task);
      }
    }
    solver->setAssignment(assignment);

    std::unordered_map<std::string, double> objectDimension;
    for (const auto i : folly::irange(15)) {
      auto task = fmt::format("task{}", i);
      objectDimension[task] = -1;
    }
    solver->addObjectDimension("weight", objectDimension);

    solver->addSolver(makeDefaultLocalSearchSolver());
    return solver;
  }
};

INSTANTIATE_TEST_CASE_P(
    ThreadCounts,
    MinimizeContainersTest,
    testThreadCounts());

static std::map<std::string, int> genHostCounts(
    const AssignmentSolution& solution) {
  std::map<std::string, int> hostCount;
  for (const auto& it : *solution.assignment()) {
    hostCount[it.second]++;
  }
  return hostCount;
}

static CapacitySpec createCapacityConstraint(
    const std::string& dimension,
    double limitRatio,
    const std::string& scope = "host") {
  CapacitySpec capacitySpec;
  Limit limit;
  limit.type() = LimitType::ABSOLUTE;
  limit.globalLimit() = limitRatio;

  capacitySpec.scope() = scope;
  capacitySpec.dimension() = dimension;
  capacitySpec.definition() = CapacitySpecDefinition::AFTER;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  capacitySpec.limit() = limit;

  return capacitySpec;
}

TEST_P(MinimizeContainersTest, MinimizeContainersBasic) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2", "task3"}},
      });

  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  solver->addGoal(spec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // ensure all tasks move to single host
  auto hostCount = genHostCounts(solution);
  EXPECT_TRUE(hostCount["host0"] == 4 || hostCount["host1"] == 4);
}

TEST_P(MinimizeContainersTest, MinimizeContainersWithCapacity) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2", "task3"}},
          {"host2", {"task4", "task5"}}});

  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  solver->addGoal(spec);

  // max 3 tasks per host
  auto capacitySpec = createCapacityConstraint("task_count", 3);
  solver->addConstraint(capacitySpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // ensure only one host is empty and other 3 hosts have 3 tasks
  const std::vector<std::string> containers = {"host0", "host1", "host2"};
  auto hostCount = genHostCounts(solution);
  for (const auto& container : containers) {
    const int count = hostCount[container];
    EXPECT_TRUE(count == 3 || count == 0);
  }
}

TEST_P(MinimizeContainersTest, MinimizeContainersWithCost) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2", "task3"}},
          {"host2", {"task4", "task5"}}});

  std::map<std::string, double> containerCosts = {
      {"host0", 100}, {"host1", 1}, {"host2", 1}};

  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.containerCosts() = std::move(containerCosts);

  solver->addGoal(spec);

  // max 3 tasks per host
  auto capacitySpec = createCapacityConstraint("task_count", 3);
  solver->addConstraint(capacitySpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  EXPECT_EQ(0, hostCount["host0"]);
  EXPECT_EQ(3, hostCount["host1"]);
  EXPECT_EQ(3, hostCount["host1"]);
}

TEST_P(MinimizeContainersTest, MinimizeContainersWithMaxFreeLimit) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single
  auto solver = createScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 1},
      {"host1", 2},
      {"host2", 3},
      {"host3", 4},
      {"host4", 5},
      {"host5", 6},
      {"host6", 7},
      {"host7", 8},
      {"host8", 9},
      {"host9", 10},
  };
  setMaxFreeLimit(spec, 5);

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();

  // We should have max 5 empty hosts
  EXPECT_EQ(5, sumEmptyScopes);
  EXPECT_EQ(0, hostCount["host8"]);
  EXPECT_EQ(0, hostCount["host9"]);
}

TEST_P(MinimizeContainersTest, MinimizeContainersWithMaxFreeLimitScenario2) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single
  auto solver = createScenario2(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::NEW;

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();
  EXPECT_EQ(5, sumEmptyScopes);
  // empty buckets
  EXPECT_EQ(0, hostCount["host0"]);
  EXPECT_EQ(0, hostCount["host1"]);
  EXPECT_EQ(0, hostCount["host2"]);
  EXPECT_EQ(0, hostCount["host8"]);
  EXPECT_EQ(0, hostCount["host9"]);

  // non empty buckets
  EXPECT_LT(0, hostCount["host3"]);
  EXPECT_LT(0, hostCount["host4"]);
  EXPECT_LT(0, hostCount["host5"]);
  EXPECT_LT(0, hostCount["host6"]);
  EXPECT_LT(0, hostCount["host7"]);
}

TEST_P(
    MinimizeContainersTest,
    MinimizeContainersWithMaxFreeLimitCorrectEmptyScopes) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  // Setting maxFreeLimit to 4 will leave you with at least 6 filled scopes

  auto solver = createScenario2(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 4);
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  solver->addGoal(spec);
  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();

  EXPECT_EQ(4, sumEmptyScopes);
  // empty buckets
  EXPECT_EQ(0, hostCount["host0"]);
  EXPECT_EQ(0, hostCount["host1"]);
  EXPECT_EQ(0, hostCount["host8"]);
  EXPECT_EQ(0, hostCount["host9"]);

  // non empty buckets
  EXPECT_LT(0, hostCount["host2"]);
  EXPECT_LT(0, hostCount["host3"]);
  EXPECT_LT(0, hostCount["host4"]);
  EXPECT_LT(0, hostCount["host5"]);
  EXPECT_LT(0, hostCount["host6"]);
  EXPECT_LT(0, hostCount["host7"]);
}

TEST_P(MinimizeContainersTest, MinimizeContainersWithoutMaxFreeLimit) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  // without maxFreeLimit being set, default is set to 0, expect task to go to 1
  // bin
  auto solver = createScenario2(GetParam());

  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  solver->addGoal(spec);
  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();

  EXPECT_EQ(9, sumEmptyScopes);
  // empty buckets
  EXPECT_EQ(0, hostCount["host0"]);
  EXPECT_EQ(0, hostCount["host1"]);
  EXPECT_EQ(0, hostCount["host2"]);
  EXPECT_EQ(0, hostCount["host3"]);
  EXPECT_EQ(0, hostCount["host4"]);
  EXPECT_EQ(0, hostCount["host5"]);
  EXPECT_EQ(0, hostCount["host6"]);
  EXPECT_EQ(0, hostCount["host8"]);
  EXPECT_EQ(0, hostCount["host9"]);

  // non empty buckets
  EXPECT_LT(0, hostCount["host7"]);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitAllButTwo) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  auto solver = createLargeScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";

  // all but 2 hosts should be empty
  setMaxFreeLimit(spec, 98);
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  solver->addGoal(spec);

  auto solution = solver->solve();
  auto assignment = *solution.assignment();
  auto hostCount = genHostCounts(solution);
  const int nEmptyScopItems = 100 - hostCount.size();

  EXPECT_EQ(98, nEmptyScopItems);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitWithCosts) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  auto solver = createScenario2(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 10000},
      {"host1", 20000},
      {"host2", 30000},
      {"host3", 40000},
      {"host4", 50000},
      {"host5", 60000},
      {"host6", 70000},
      {"host7", 80000},
      {"host8", 1},
      {"host9", 1},
  };

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();
  EXPECT_EQ(5, sumEmptyScopes);

  // you will need to empty at least 3 buckets with the greatest cost
  EXPECT_EQ(0, hostCount["host5"]);
  EXPECT_EQ(0, hostCount["host6"]);
  EXPECT_EQ(0, hostCount["host7"]);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitWithCostsAndCapacity) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  auto solver = createScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 10000},
      {"host1", 20000},
      {"host2", 30000},
      {"host3", 40000},
      {"host4", 50000},
      {"host5", 60000},
      {"host6", 70000},
      {"host7", 80000},
      {"host8", 1},
      {"host9", 1},
  };

  solver->addGoal(spec);

  auto capacitySpec = createCapacityConstraint("task_count", 0);
  capacitySpec.filter()->itemsBlacklist() = {"host8", "host9"};
  solver->addGoal(capacitySpec, 1, 2);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();

  // Because the capacity constraint is applied, all hosts must have a capacity
  // of 0. The only hosts that are exempt from this are host8 and host9
  EXPECT_LE(8, sumEmptyScopes);

  EXPECT_EQ(0, hostCount["host0"]);
  EXPECT_EQ(0, hostCount["host1"]);
  EXPECT_EQ(0, hostCount["host2"]);
  EXPECT_EQ(0, hostCount["host3"]);
  EXPECT_EQ(0, hostCount["host4"]);
  EXPECT_EQ(0, hostCount["host5"]);
  EXPECT_EQ(0, hostCount["host6"]);
  EXPECT_EQ(0, hostCount["host7"]);

  EXPECT_LT(0, hostCount["host8"] + hostCount["host9"]);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitWithCostsAndCapacity2) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  auto solver = createScenario2(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 1},
      {"host1", 2},
      {"host2", 3},
      {"host3", 4},
      {"host4", 5},
      {"host5", 6},
      {"host6", 7},
      {"host7", 8},
  };

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();
  EXPECT_EQ(5, sumEmptyScopes);

  // There is no guarantee which buckets will be emptied when cost and number of
  // object are different for each scope item
}

TEST_P(MinimizeContainersTest, MaxFreeLimitWithCostsAndCapacity3) {
  // Due to how we pick hottest containers currently,
  // the current test does not work with move type single

  auto solver = createScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 1},
      {"host1", 2},
      {"host2", 3},
      {"host3", 4},
      {"host4", 5},
      {"host5", 6},
      {"host6", 7},
      {"host7", 8},
  };

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();
  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();
  EXPECT_EQ(5, sumEmptyScopes);

  // you will need to empty at least 3 buckets with the greatest cost
  EXPECT_EQ(0, hostCount["host5"]);
  EXPECT_EQ(0, hostCount["host6"]);
  EXPECT_EQ(0, hostCount["host7"]);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitWithContainersOutOfScope) {
  auto solver = createScenarioWithContainersNotInScope(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "rack";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 2);
  spec.formula() = MinimizeContainerSpecFormula::NEW;

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();

  auto assignment = *solution.assignment();

  std::map<std::string, std::string> hostToRack;
  hostToRack["host0"] = "rack0";
  hostToRack["host1"] = "rack0";
  hostToRack["host2"] = "rack1";
  hostToRack["host3"] = "rack1";
  hostToRack["host4"] = "rack2";
  hostToRack["host5"] = "rack2";
  hostToRack["host6"] = "rack3";
  hostToRack["host7"] = "rack3";
  hostToRack["host8"] = "rack4";
  hostToRack["host9"] = "rack4";

  std::map<std::string, int> rackCount;
  for (const auto& it : *solution.assignment()) {
    if (hostToRack.find(it.second) != hostToRack.end()) {
      rackCount[hostToRack[it.second]]++;
    }
  }
  const int sumEmptyScopes = 5 - rackCount.size();

  EXPECT_EQ(2, sumEmptyScopes);
}

TEST_P(MinimizeContainersTest, UseContinuousMaxFreeLimitFormulaIsFalse) {
  auto solver = createScenario2(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::LEGACY;

  solver->addGoal(spec);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->solve(),
      "Custom stopping condition (maxFreeLimit/minUsedLimit) not supported in minimize containers goal in LEGACY formula but is supported in NEW formula");
}

TEST_P(MinimizeContainersTest, DeprecatedMaxFreeLimitFieldRejected) {
  auto solver = createScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  spec.maxFreeLimit() = 5;
  FOLLY_POP_WARNING

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "The field 'maxFreeLimit' in MinimizeContainersSpec is deprecated; use `MinimizeContainersSpec.target` instead");
}

TEST_P(
    MinimizeContainersTest,
    DISABLED_MaxFreeLimitWithDynamicObjectDimensions) {
  auto solver = createScenarioWithDynamicObjectDimensions(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "weight";
  setMaxFreeLimit(spec, 5);
  spec.formula() = MinimizeContainerSpecFormula::NEW;

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  solver->persistToManifold();

  auto assignment = *solution.assignment();
  // ensure host0 has no tasks as its expensive to keep tasks there.
  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();
  EXPECT_EQ(5, sumEmptyScopes);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitGreaterThanScopeItems) {
  auto solver = createScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 1},
      {"host1", 2},
      {"host2", 3},
      {"host3", 4},
      {"host4", 5},
      {"host5", 6},
      {"host6", 7},
      {"host7", 8},
      {"host8", 9},
      {"host9", 10},
  };
  setMaxFreeLimit(spec, 20);

  solver->addGoal(spec);

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  auto assignment = *solution.assignment();

  auto hostCount = genHostCounts(solution);
  const int sumEmptyScopes = 10 - hostCount.size();

  // we should have 9 empty scopes as maxFreeLimit > number of scope items
  EXPECT_EQ(9, sumEmptyScopes);

  // host0 should have all tasks as it the cost is the lowest
  EXPECT_LT(0, hostCount["host0"]);

  EXPECT_EQ(0, hostCount["host1"]);
  EXPECT_EQ(0, hostCount["host2"]);
  EXPECT_EQ(0, hostCount["host3"]);
  EXPECT_EQ(0, hostCount["host4"]);
  EXPECT_EQ(0, hostCount["host5"]);
  EXPECT_EQ(0, hostCount["host6"]);
  EXPECT_EQ(0, hostCount["host7"]);
  EXPECT_EQ(0, hostCount["host8"]);
  EXPECT_EQ(0, hostCount["host9"]);
}

TEST_P(MinimizeContainersTest, MaxFreeLimitWhenAllDimensionsNegative) {
  auto solver = createNegativeObjectDimensionScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "weight";
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  spec.containerCosts() = {
      {"host0", 1},
      {"host1", 2},
      {"host2", 3},
      {"host3", 4},
      {"host4", 5},
      {"host5", 6},
      {"host6", 7},
      {"host7", 8},
      {"host8", 9},
      {"host9", 10},
  };
  setMaxFreeLimit(spec, 20);

  solver->addGoal(spec);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->solve(),
      "Negative dimension values are not supported when using MinimizeContainerSpec with NEW formula");
}

TEST_P(MinimizeContainersTest, ZeroContainerCostWithLocalSearch) {
  auto solver = createScenario(GetParam());
  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "task_count";
  spec.formula() = MinimizeContainerSpecFormula::NEW;
  // host9 starts empty
  spec.containerCosts() = {{"host9", 0.0}};
  solver->addGoal(spec);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->solve(),
      "Non-positive containerCosts are not supported when using MinimizeContainerSpec with a continuous formula; 'host9' has cost 0");
}
