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

#include "algopt/rebalancer/interface/tests/ProblemSolverTest.h"

#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include <folly/container/irange.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/synchronization/Baton.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

using facebook::algopt::isSolverUnavailable;
using facebook::algopt::solverName;
using facebook::algopt::testSolverPackages;
using namespace facebook::rebalancer::interface;
using namespace folly;
using namespace std;

namespace {
AssignmentSolution solveWithReasonBreakdown(bool enabled) {
  auto solver = initializeTestProblemSolver({.executorThreadCount = 1});
  solver->disableLogging();
  ManifoldBackupParams backup;
  backup.uploadPolicy() = ManifoldUploadPolicy::NEVER;
  solver->setManifoldBackupParams(backup);
  solver->setObjectName("task");
  solver->setContainerName("host");
  solver->setAssignment(
      std::vector<std::pair<std::string, std::vector<std::string>>>{
          {"h1", {"s1", "s2", "s3", "s4"}}, {"h2", {}}});
  solver->addObjectDimension(
      "cpu",
      folly::F14FastMap<std::string, double>{
          {"s1", 10}, {"s2", 10}, {"s3", 10}, {"s4", 10}});
  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"h1", 100}, {"h2", 100}});
  BalanceSpec balance;
  balance.scope() = "host";
  balance.dimension() = "cpu";
  balance.formula() = BalanceSpecFormula::LEGACY;
  balance.fixAverageToInitial() = true;
  solver->addGoal(balance);

  MoveStatsSpec moveStats;
  if (enabled) {
    moveStats.evalReasonBreakdownSpec() = EvalReasonBreakdownSpec{};
  }
  solver->enableMoveStats(moveStats);
  LocalSearchSolverSpec local;
  local.solveTime() = 10;
  local.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));
  LocalSearchStageSolverSpec stages;
  for (const auto stageId : folly::irange(2)) {
    LocalSearchStageSpec stage;
    stage.begin() = 0;
    stage.end() = 1;
    stage.solverSpec() = local;
    if (stageId == 0) {
      stage.solverSpec()->stopAfterMoves() = 1;
    }
    stages.stageSpecs()->push_back(std::move(stage));
  }
  solver->addSolver(stages);
  return solver->solve();
}
} // namespace

class ProblemSolverTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(NumThreads, ProblemSolverTest, testThreadCounts());

TEST_P(ProblemSolverTest, Basic) {
  auto executor = make_shared<CPUThreadPoolExecutor>(1);
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  runBasicTest(*solver);
  EXPECT_FALSE(
      *solver->getBundle().problem()->enableParallelBoundsComputation());
}

TEST_P(ProblemSolverTest, ParallelBoundsWithoutParallelMaterializer) {
  auto solver = initializeTestProblemSolver(
      {.useParallelMaterializer = false, .executorThreadCount = GetParam()});
  solver->disableLogging();
  ManifoldBackupParams backup;
  backup.uploadPolicy() = ManifoldUploadPolicy::NEVER;
  solver->setManifoldBackupParams(backup);
  solver->enableParallelBoundsComputation(true);

  runBasicTest(*solver);
  EXPECT_TRUE(
      *solver->getBundle().problem()->enableParallelBoundsComputation());
}

TEST_P(ProblemSolverTest, ParallelBoundsCanBeDisabled) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->disableLogging();
  ManifoldBackupParams backup;
  backup.uploadPolicy() = ManifoldUploadPolicy::NEVER;
  solver->setManifoldBackupParams(backup);
  solver->enableParallelBoundsComputation(true);
  solver->enableParallelBoundsComputation(false);

  runBasicTest(*solver);
  EXPECT_FALSE(
      *solver->getBundle().problem()->enableParallelBoundsComputation());
}

TEST_P(ProblemSolverTest, BottomUp) {
  auto executor = make_shared<CPUThreadPoolExecutor>(1);
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  runBasicTest(*solver, false);
}

TEST_P(ProblemSolverTest, Parallel) {
  auto executor = make_shared<CPUThreadPoolExecutor>(1);
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  runBasicTest(*solver, false, true);
}

TEST_P(ProblemSolverTest, MakeMoveTypes) {
  // verify if the move type spec helper correctly works. It is sufficient to
  // test on a few (> 1) move types, so this test does not needs to be
  // exhaustive
  auto single = ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec());
  EXPECT_EQ(MoveTypeSpec::Type::singleMoveTypeSpec, single.getType());
  auto swap = ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec());
  EXPECT_EQ(MoveTypeSpec::Type::swapMoveTypeSpec, swap.getType());
  auto klSearch = ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec());
  EXPECT_EQ(MoveTypeSpec::Type::klSearchMoveTypeSpec, klSearch.getType());
  // Move type specification using names will eventually be deprecated
  auto moveTypeName =
      ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED");
  EXPECT_EQ(MoveTypeSpec::Type::moveTypeName, moveTypeName.getType());
}

TEST_P(ProblemSolverTest, MakeCPUThreadPoolExecutor) {
  // Test the static makeCPUThreadPoolExecutor method
  auto executor = ProblemSolver::makeCPUThreadPoolExecutor("TestPool", 4);
  ASSERT_NE(nullptr, executor);
  EXPECT_EQ(4, executor->numThreads());
}

TEST(ProblemSolverExecutorTest, AddDoesNotBlockBeyondDefaultQueueSize) {
  // Tasks running on this pool schedule their own continuations back onto it,
  // so a producer that parks waiting for queue capacity deadlocks the pool. A
  // bounded queue accepts kDefaultMaxQueueSize tasks and then blocks; the
  // margin covers any slack in the queue's real capacity.
  const size_t taskCount = CPUThreadPoolExecutor::kDefaultMaxQueueSize + 1024;
  const auto executor = ProblemSolver::makeCPUThreadPoolExecutor("TestPool", 1);

  // Occupy the only worker so nothing drains the queue while it is filled.
  Baton<> workerStarted;
  Baton<> releaseWorker;
  executor->add([&workerStarted, &releaseWorker] {
    workerStarted.post();
    releaseWorker.wait();
  });
  workerStarted.wait();

  std::atomic<size_t> accepted{0};
  Baton<> allAccepted;
  std::thread producer([&] {
    for ([[maybe_unused]] const auto i : folly::irange(taskCount)) {
      executor->add([] {});
      accepted.fetch_add(1, std::memory_order_relaxed);
    }
    allAccepted.post();
  });
  const bool acceptedAll = allAccepted.try_wait_for(std::chrono::seconds(30));
  const size_t acceptedOnTimeout = accepted.load(std::memory_order_relaxed);

  // Release the worker before joining: if the queue does block, the producer
  // only makes progress once the worker starts freeing slots.
  releaseWorker.post();
  producer.join();
  executor->join();

  EXPECT_TRUE(acceptedAll) << "producer blocked after " << acceptedOnTimeout
                           << " of " << taskCount << " tasks";
}

TEST(ProblemSolverReasonBreakdownTest, DefaultResponseDoesNotIncludeBreakdown) {
  const auto solution = solveWithReasonBreakdown(false);
  ASSERT_EQ(1, solution.solverSummaries()->size());
  const auto& report = solution.solverSummaries()->front();
  ASSERT_TRUE(report.evalStats().has_value());
  EXPECT_FALSE(report.evalStats()->evalReasonBreakdown().has_value());
  ASSERT_TRUE(report.stagesSummaries().has_value());
  ASSERT_EQ(2, report.stagesSummaries()->size());
  for (const auto& stage : *report.stagesSummaries()) {
    ASSERT_TRUE(stage.evalStats().has_value());
    EXPECT_FALSE(stage.evalStats()->evalReasonBreakdown().has_value());
  }
}

TEST(
    ProblemSolverReasonBreakdownTest,
    OptInResponseIncludesStageAndSolveCounts) {
  const auto solution = solveWithReasonBreakdown(true);
  ASSERT_EQ(1, solution.solverSummaries()->size());
  const auto& report = solution.solverSummaries()->front();
  ASSERT_TRUE(report.evalStats().has_value());
  ASSERT_TRUE(report.evalStats()->evalReasonBreakdown().has_value());
  ASSERT_TRUE(report.stagesSummaries().has_value());
  ASSERT_EQ(2, report.stagesSummaries()->size());
  EvalReasonBreakdown expected;
  for (const auto& stage : *report.stagesSummaries()) {
    ASSERT_TRUE(stage.evalStats().has_value());
    ASSERT_TRUE(stage.evalStats()->evalReasonBreakdown().has_value());
    const auto& breakdown = *stage.evalStats()->evalReasonBreakdown();
    for (const auto& [reason, count] :
         *breakdown.constraintToNumInvalidEvals()) {
      expected.constraintToNumInvalidEvals()[reason] += count;
    }
    for (const auto& [reason, count] : *breakdown.objectiveToNumWorseEvals()) {
      expected.objectiveToNumWorseEvals()[reason] += count;
    }
  }
  EXPECT_EQ(expected, *report.evalStats()->evalReasonBreakdown());
}

class ProblemSolverOptimalTest
    : public ::testing::TestWithParam<std::tuple<int, OptimalSolverPackage>> {
 protected:
  void SetUp() override {
    const auto [threads, solver] = GetParam();
    if (isSolverUnavailable(solver)) {
      GTEST_SKIP() << solverName(solver) << " solver not available";
    }
  }
};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    ProblemSolverOptimalTest,
    ::testing::Combine(testThreadCounts(), testSolverPackages()));

TEST_P(ProblemSolverOptimalTest, Optimal) {
  auto [threads, solver] = GetParam();
  auto problemSolver =
      initializeTestProblemSolver({.executorThreadCount = threads});
  runBasicTest(*problemSolver, true, false, solver);
}
