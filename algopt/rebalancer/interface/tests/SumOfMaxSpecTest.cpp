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

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <unordered_map>

using namespace facebook::rebalancer::interface;

class SumOfMaxTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(NumThreads, SumOfMaxTest, testThreadCounts());

TEST_P(SumOfMaxTest, BasicTest) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");

  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {"task0", "task1"}},
          {"host1", {"task2", "task3"}},
          {"host2", {"task4", "task5"}}});

  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::vector<std::string>>{
          {"job0", {"task0", "task1", "task3"}},
          {"job2", {"task4"}},
          {"job3", {"task5"}}});

  solver->addObjectDimension(
      "cpu",
      std::map<std::string, double>{
          {"task1", 20},
          {"task2", 30},
          {"task3", 40},
          {"task4", 50},
          {"task5", 60}});

  SumOfMaxSpec spec;
  spec.name() = "test";
  spec.dimension() = "cpu";
  spec.partitionName() = "job";
  spec.scope() = "host";
  spec.filter() = Filter();
  solver->addGoal(spec);

  MinimizeMovementSpec minimizeMovementSpec;
  minimizeMovementSpec.scope() = "host";
  minimizeMovementSpec.dimension() = "task_count";
  solver->addGoal(minimizeMovementSpec);

  solver->addSolver(makeDefaultLocalSearchSolver());

  // Get a solution from Rebalancer.
  auto solution = solver->solve();
  const auto& assignment = *solution.assignment();

  // Optimal SumOfMax = 60: achieved when all tasks with non-zero job cpu are
  // co-located on one host so their contributions collapse into a single max:
  //   job0: task1(20) + task3(40) = 60
  //   job2: task4(50) = 50
  //   job3: task5(60) = 60
  // → max(60, 50, 60) = 60 on one host, 0 on all others.
  // This co-location is necessary: any of these four tasks on a separate host
  // would add >= 20 to the sum, exceeding the optimum of 60.
  // task0 (cpu=0 in job0) and task2 (not in any job) don't affect the
  // objective.
  const std::string& heavyTaskHost = assignment.at("task1");
  EXPECT_EQ(heavyTaskHost, assignment.at("task3"));
  EXPECT_EQ(heavyTaskHost, assignment.at("task4"));
  EXPECT_EQ(heavyTaskHost, assignment.at("task5"));
}

TEST_P(SumOfMaxTest, PartitionWithoutGroups) {
  // Like a RAS shard whose request has reservations but no servers: the
  // partition built from the shard's servers has no groups.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::unordered_map<std::string, std::vector<std::string>>{
          {"host0", {}}, {"host1", {}}});
  solver->addPartition(
      "msb", std::unordered_map<std::string, std::vector<std::string>>{});

  SumOfMaxSpec spec;
  spec.name() = "test";
  spec.dimension() = "task_count";
  spec.partitionName() = "msb";
  spec.scope() = "host";
  spec.filter()->itemsWhitelist() = std::vector<std::string>{"host0"};
  solver->addGoal(spec);
  solver->addSolver(makeDefaultLocalSearchSolver());

  auto solution = solver->solve();
  EXPECT_EQ(0, *solution.finalObjective()->value());
}
