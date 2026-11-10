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
#include "algopt/rebalancer/solver/moves/SwapMoveType.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h>
#include <algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h>

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/logging/Init.h>
#include <folly/String.h>
#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace facebook::rebalancer::interface::tests {

class GroupDiversityTest
    : public ::testing::TestWithParam<
          std::tuple<int, SolverAlgoType, OptimalSolverPackage>> {
 protected:
  static int getThreadCount() {
    return std::get<0>(GetParam());
  }
  static SolverAlgoType getSolverAlgoType() {
    return std::get<1>(GetParam());
  }
  static OptimalSolverPackage getSolverPackage() {
    return std::get<2>(GetParam());
  }

  void SetUp() override {
    if (getSolverAlgoType() == OPTIMAL &&
        facebook::algopt::isSolverUnavailable(getSolverPackage())) {
      GTEST_SKIP() << facebook::algopt::solverName(getSolverPackage())
                   << " solver not available";
    }
  }

  // Registers the parameterized solver on `solver`. For LOCALSEARCH the
  // provided local-search spec is used (defaulting to the standard move-type
  // set); for OPTIMAL the spec is ignored and an exact MIP solver is added.
  static void addConfiguredSolver(
      ProblemSolver& solver,
      std::optional<LocalSearchSolverSpec> localSearchSpec = std::nullopt) {
    switch (getSolverAlgoType()) {
      case LOCALSEARCH:
        solver.addSolver(
            std::move(localSearchSpec)
                .value_or(makeDefaultLocalSearchSolver()));
        break;
      case OPTIMAL: {
        OptimalSolverSpec optimalSpec;
        optimalSpec.solverPackage() = getSolverPackage();
        solver.addSolver(optimalSpec);
        break;
      }
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    LocalSearch,
    GroupDiversityTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(LOCALSEARCH),
        ::testing::Values(OptimalSolverPackage::GUROBI)));

INSTANTIATE_TEST_CASE_P(
    Optimal,
    GroupDiversityTest,
    ::testing::Combine(
        testThreadCounts(),
        ::testing::Values(OPTIMAL),
        facebook::algopt::testSolverPackages()));

TEST_P(GroupDiversityTest, Basic) {
  // TODO: Investigate why this test fails only in OSS with HiGHS.
  if (getSolverAlgoType() == OPTIMAL &&
      getSolverPackage() == OptimalSolverPackage::HIGHS) {
    GTEST_SKIP() << "Temporarily disabled: test fails only in OSS with HiGHS";
  }

  // In this test we model the problem of assigning servers to reservations. All
  // servers are initially unassigned. We add a "group diversity" spec to
  // incentivize each reservation to get servers from a minimum number of
  // different regions.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("server");
  solver->setContainerName("reservation");

  // There are 8 servers initially unassigned.
  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"unassigned",
           {"server0",
            "server1",
            "server2",
            "server3",
            "server4",
            "server5",
            "server6",
            "server7"}},
          {"reservation0", {}},
          {"reservation1", {}}});

  // There are 4 different regions with 2 servers each.
  std::map<std::string, std::string> serverToRegion = {
      {"server0", "region0"},
      {"server1", "region0"},
      {"server2", "region1"},
      {"server3", "region1"},
      {"server4", "region2"},
      {"server5", "region2"},
      {"server6", "region3"},
      {"server7", "region3"}};
  solver->addPartition("region", serverToRegion);

  {
    // Incentivize reservation0 to get servers from at least 2 different
    // regions, and reservation1 to get servers from at least 3 different
    // regions.
    GroupDiversitySpec spec;
    spec.scope() = "reservation";
    spec.partition() = "region";
    spec.dimension() = "server_count";
    spec.limit()->globalLimit() = 2;
    spec.limit()->scopeItemLimits() = {{"reservation1", 3}};
    solver->addGoal(spec);
  }

  addConfiguredSolver(*solver);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, int> reservationToServerCount;
  for (auto& [_, reservation] : assignment) {
    ++reservationToServerCount[reservation];
  }

  std::map<std::string, std::set<std::string>> reservationToRegions;
  for (auto& [server, reservation] : assignment) {
    auto& region = serverToRegion.at(server);
    reservationToRegions[reservation].insert(region);
  }

  EXPECT_NEAR(5.0, *solution.initialObjective()->value(), 1e-8);
  EXPECT_NEAR(0.0, *solution.finalObjective()->value(), 1e-8);

  // Check specific final solution only for local search
  if (getSolverAlgoType() == LOCALSEARCH) {
    EXPECT_EQ(3, reservationToServerCount.at("unassigned"));
    EXPECT_EQ(2, reservationToServerCount.at("reservation0"));
    EXPECT_EQ(3, reservationToServerCount.at("reservation1"));

    EXPECT_EQ(2, reservationToRegions.at("reservation0").size());
    EXPECT_EQ(3, reservationToRegions.at("reservation1").size());
  }
}

TEST_P(GroupDiversityTest, WithOveralappingGroups) {
  // In this problem, there are 4 types of labels (label1, label2, label3,
  // label4); these are the group names. Each object has certain labels
  // associated with it and can associated with multiple labels.
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("ball");
  solver->setContainerName("bin");

  // There are 4 balls that are all initially unassigned.
  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"unassigned", {"ball1", "ball2", "ball3", "ball4"}},
          {"bin0", {}},
          {"bin1", {}}});

  const std::map<std::string, std::vector<std::string>> colorsToBalls = {
      {"color1", {"ball1", "ball2"}},
      {"color2", {"ball1", "ball2"}},
      {"color3", {"ball1", "ball2"}},
      {"color4", {"ball3", "ball4"}},
      {"color5", {"ball3", "ball4"}},
      {"color6", {"ball3", "ball4"}},
  };

  solver->addPartition("label", colorsToBalls);

  {
    GroupDiversitySpec spec;
    spec.scope() = "bin";
    spec.partition() = "label";
    spec.dimension() = "ball_count";
    spec.limit()->globalLimit() = 3;
    spec.bound() = GroupDiversityBound::MAX;
    solver->addGoal(spec);
  }
  {
    // drain unassigned
    ToFreeSpec spec;
    spec.containers() = {"unassigned"};

    solver->addConstraint(spec);
  }

  // use just SINGLE moves
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  addConfiguredSolver(*solver, solverSpec);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  // expect that final objective is optimal
  EXPECT_NEAR(0.0, *solution.finalObjective()->value(), 1e-8);

  // Check specific final solution only for local search
  if (getSolverAlgoType() == LOCALSEARCH) {
    folly::F14FastMap<std::string, std::set<std::string>> binToBalls;
    for (auto& [ball, bin] : assignment) {
      binToBalls[bin].insert(ball);
    }

    EXPECT_EQ(2, binToBalls["bin0"].size());
    EXPECT_EQ(2, binToBalls["bin1"].size());

    // we expect that both ball1 and ball2 are in the same container, and ball3
    // and ball4 are also in the same container
    EXPECT_EQ(assignment["ball1"], assignment["ball2"]);
    EXPECT_EQ(assignment["ball3"], assignment["ball4"]);
  }
}

static std::unique_ptr<ProblemSolver> setupStackingTestcase(
    int threadCount,
    std::map<std::string, std::string>& partToServer,
    std::optional<std::map<std::string, std::vector<std::string>>>
        assignmentOverride = std::nullopt,
    bool useAsSoftConstraint = false) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount});
  solver->setObjectName("server_part");
  solver->setContainerName("reservation");

  if (assignmentOverride) {
    solver->setAssignment(*assignmentOverride);
  } else {
    // default assignment
    // 3 servers, each with 4 parts. 4 parts are initially unassigned
    solver->setAssignment(
        folly::F14FastMap<std::string, std::vector<std::string>>{
            {"unassigned",
             {"server0_part3",
              "server1_part3",
              "server2_part2",
              "server2_part3"}},
            {"reservation0",
             {"server0_part0",
              "server0_part1",
              "server0_part2",
              "server1_part0",
              "server1_part1",
              "server1_part2",
              "server2_part0",
              "server2_part1"}}});
  }

  // each server has four parts
  partToServer = {
      {"server0_part0", "server0"},
      {"server0_part1", "server0"},
      {"server0_part2", "server0"},
      {"server0_part3", "server0"},
      {"server1_part0", "server1"},
      {"server1_part1", "server1"},
      {"server1_part2", "server1"},
      {"server1_part3", "server1"},
      {"server2_part0", "server2"},
      {"server2_part1", "server2"},
      {"server2_part2", "server2"},
      {"server2_part3", "server2"}};

  solver->addPartition("server", partToServer);

  {
    // Reservation0 needs 8 server parts which it already has
    CapacitySpec capacitySpec;
    capacitySpec.scope() = "reservation";
    capacitySpec.dimension() = "server_part_count";
    capacitySpec.bound() = CapacitySpecBound::MIN;
    capacitySpec.limit()->globalLimit() = 8;
    capacitySpec.filter()->itemsWhitelist() = {"reservation0"};
    solver->addConstraint(capacitySpec);
  }

  {
    // Incentivize reservation0 to not use more than 2 servers. Initially, it
    // uses parts from all 3 servers, after solve it should use parts from 2
    // servers. Moreover, it should release parts of server2 because they are
    // fewer in count
    GroupDiversitySpec spec;
    spec.scope() = "reservation";
    spec.partition() = "server";
    spec.dimension() = "server_part_count";
    spec.bound() = GroupDiversityBound::MAX;
    spec.limit()->globalLimit() = 2;
    spec.filter()->itemsWhitelist() = {"reservation0"};
    if (useAsSoftConstraint) {
      solver->addConstraint(
          spec,
          ConstraintPolicy::SOFT,
          /*invalidCost=*/1,
          /*invalidState=*/0);
    } else {
      solver->addGoal(spec);
    }
  }
  return solver;
}

TEST_P(GroupDiversityTest, stackHighOnServersNoProgressWithSingleMoves) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "local search specific test";
  }

  std::map<std::string, std::string> partToServer;
  auto solver = setupStackingTestcase(getThreadCount(), partToServer);
  // use just SINGLE moves
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  addConfiguredSolver(*solver, solverSpec);

  auto solution = solver->solve();
  auto numMoves = solution.movesSummary().value().size();
  EXPECT_EQ(0, numMoves);
}

static void verifyStackHighOnServersPossibleWithSwap(
    int threadCount,
    bool useAsSoftConstraint) {
  std::map<std::string, std::string> partToServer;
  auto solver = setupStackingTestcase(
      threadCount,
      partToServer,
      /*assignmentOverride=*/std::nullopt,
      useAsSoftConstraint);
  // use just SWAP moves
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  solver->addSolver(solverSpec);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, std::set<std::string>> containerToServers;
  for (auto& [part, container] : assignment) {
    auto& server = partToServer.at(part);
    containerToServers[container].insert(server);
  }

  for (auto& [container, servers] : containerToServers) {
    XLOG(INFO) << container << ": " << folly::join(",", servers);
  }

  auto numSwapMoves = solution.movesSummary().value().size();
  XLOG(INFO) << "Solver made " << numSwapMoves << " SWAP moves";
  EXPECT_EQ(2, numSwapMoves);

  EXPECT_EQ(2, containerToServers["reservation0"].size());
  EXPECT_EQ(1, containerToServers["unassigned"].size());
  EXPECT_TRUE(containerToServers["unassigned"].contains("server2"));
  EXPECT_TRUE(containerToServers["reservation0"].contains("server0"));
  EXPECT_TRUE(containerToServers["reservation0"].contains("server1"));
}

TEST_P(GroupDiversityTest, StackHighOnServersPossibleWithSwapAsGoal) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "local search specific test";
  }

  verifyStackHighOnServersPossibleWithSwap(
      getThreadCount(), /*useAsSoftConstraint=*/false);
}

TEST_P(GroupDiversityTest, StackHighOnServersPossibleWithSwapAsConstraint) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "local search specific test";
  }

  verifyStackHighOnServersPossibleWithSwap(
      getThreadCount(), /*useAsSoftConstraint=*/true);
}

TEST_P(GroupDiversityTest, stackHighAllGroupsEqual) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "local search specific test";
  }

  std::map<std::string, std::string> partToServer;
  std::map<std::string, std::vector<std::string>> assignmentOverride = {
      {"unassigned", {"server0_part3", "server1_part3", "server2_part3"}},
      {"reservation0",
       {
           "server0_part0",
           "server0_part1",
           "server0_part2",
           "server1_part0",
           "server1_part1",
           "server1_part2",
           "server2_part0",
           "server2_part1",
           "server2_part2",
       }}};

  auto solver =
      setupStackingTestcase(getThreadCount(), partToServer, assignmentOverride);
  // use SINGLE, SWAP moves to get rid of excess capacity
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  addConfiguredSolver(*solver, solverSpec);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, std::set<std::string>> containerToServers;
  for (auto& [part, container] : assignment) {
    auto& server = partToServer.at(part);
    containerToServers[container].insert(server);
  }

  for (auto& [container, servers] : containerToServers) {
    XLOG(INFO) << container << ": " << folly::join(",", servers);
  }

  auto numTotalMoves = solution.movesSummary().value().size();
  XLOG(INFO) << "Solver made " << numTotalMoves << " total moves";
  EXPECT_EQ(3, numTotalMoves);

  EXPECT_EQ(2, containerToServers["reservation0"].size());
  EXPECT_EQ(1, containerToServers["unassigned"].size());
}

TEST_P(GroupDiversityTest, stackHighDynamicDimensions) {
  if (getSolverAlgoType() == OPTIMAL) {
    GTEST_SKIP() << "local search specific test";
  }

  auto solver =
      initializeTestProblemSolver({.executorThreadCount = getThreadCount()});
  solver->setObjectName("server_part");
  solver->setContainerName("reservation");

  // each server has 16GB memory and we have two shape types: M3 and M12
  // Reservation r0 needs 2 M3s and reservation r1 needs 1 M12
  // initially r0 has 3 M3 allotments (server parts) from server0 and
  // r1 has 1 M12 allotment from server1
  // In other words: reserved allotments on server0 = 3 M3 = 9 GB
  // reserved allotments on server1 = 1 M12 = 12GB
  // To reduce fragmentation, we should move 1M3 from server0 to server1
  // That way server1 is  almost fully packed
  // # allotments per server = 16 / min(M3, M3) = 16/3 = 5
  solver->setAssignment(
      folly::F14FastMap<std::string, std::vector<std::string>>{
          {"unassigned",
           {"server0_0",
            "server0_1",
            "server1_0",
            "server1_1",
            "server1_2",
            "server1_3"}},
          {"r0", {"server0_2", "server0_3", "server0_4"}},
          {"r1", {"server1_4"}},
      });

  // each server has four parts
  folly::F14FastMap<std::string, std::string> partToServer;
  folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
      scopeItemToObjectToValue;
  for (const auto i : folly::irange(2)) {
    for (const auto j : folly::irange(5)) {
      const auto allotment = fmt::format("server{}_{}", i, j);
      partToServer.emplace(allotment, fmt::format("server{}", i));
      // r0 is asking for M3 shapes
      scopeItemToObjectToValue["r0"][allotment] = 3;
      // r1 is asking for M12 shapes
      scopeItemToObjectToValue["r1"][allotment] = 12;
    }
  }
  solver->addPartition("server", partToServer);
  // create a dynamic dimension 'memory'
  solver->addDynamicObjectDimension(
      "memory", "reservation", scopeItemToObjectToValue);

  folly::F14FastMap<std::string, std::string> containerToScopeItem;
  containerToScopeItem.emplace("r0", "onlyScopeItem");
  containerToScopeItem.emplace("r1", "onlyScopeItem");
  solver->addScope("all_reservation_containers", containerToScopeItem);

  CapacitySpec capacitySpec;
  capacitySpec.name() = "meet resource requirements";
  capacitySpec.scope() = "reservation";
  capacitySpec.dimension() = "memory";
  capacitySpec.bound() = CapacitySpecBound::MIN;
  capacitySpec.limit()->scopeItemLimits() = {
      {"r0", 6} /*2 M3 allotments*/, {"r1", 12} /*1 M12 allotment*/};
  capacitySpec.filter()->itemsBlacklist() = {"unassigned"};
  solver->addConstraint(capacitySpec);

  // enforce that total memory usage of a server is at most 16 GB
  GroupCountSpec resourceLimit;
  resourceLimit.name() = "do not exceed memory";
  resourceLimit.scope() = "all_reservation_containers";
  resourceLimit.partitionName() = "server";
  resourceLimit.dimension() = "memory";
  resourceLimit.limit()->globalLimit() = 16;
  solver->addConstraint(resourceLimit);

  // minimize the total number of servers used
  GroupDiversitySpec spec;
  spec.scope() = "all_reservation_containers";
  spec.partition() = "server";
  spec.dimension() = "memory";
  spec.bound() = GroupDiversityBound::MAX;
  spec.limit()->globalLimit() = 0;
  solver->addGoal(spec);

  LocalSearchSolverSpec solverSpec;
  // single move needed to release extra M3 on server0
  SingleMoveTypeSpec singleMoveSpec;
  // SWAP move needed to swap one M3 allotments on server0 with another M3
  // allotments on server1. Make it fixed dest to only allow swaps with
  // unassigned container
  SwapMoveTypeSpec swapSpec;
  facebook::rebalancer::SwapMoveType::makeFixedDest(
      swapSpec, "reservation", "unassigned");
  facebook::rebalancer::SwapMoveType::makeGreedy(swapSpec, true, false);
  // add move types
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(singleMoveSpec));
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(swapSpec));
  addConfiguredSolver(*solver, solverSpec);

  auto solution = solver->solve();
  auto& assignment = *solution.assignment();

  std::map<std::string, std::map<std::string, int>>
      containerToServerToPartCount;
  for (auto& [part, container] : assignment) {
    auto& server = partToServer.at(part);
    containerToServerToPartCount[container][server]++;
    XLOG(INFO) << fmt::format("{} -> {}", part, container);
  }

  // after solve, server1 is nearly packed 15 GB
  // M3 allotment takes 3GB
  EXPECT_EQ(1, containerToServerToPartCount["r0"]["server1"]);
  // M12 allotment takes 12 GB
  EXPECT_EQ(1, containerToServerToPartCount["r1"]["server1"]);
  // remaining M3 allotment of r0 stayes on server0
  EXPECT_EQ(1, containerToServerToPartCount["r0"]["server0"]);
}

} // namespace facebook::rebalancer::interface::tests
