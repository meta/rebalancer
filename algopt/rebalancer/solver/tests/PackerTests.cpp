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

#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/StableStayed.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchSolver.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchStageSolver.h"
#include "algopt/rebalancer/solver/solvers/LPStore.h"
#include "algopt/rebalancer/solver/solvers/OptimalSolver.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#include "multifeed/hash/HeterogeneousAccess.h"

#include "gtest/gtest.h"
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <cmath>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::packer::tests {

class PackerTests : public ExpressionTestsBase {
 protected:
  // Setup universe placing object i in container (i % numContainers).
  std::shared_ptr<const entities::Universe> setupUniverse(
      const int numObjects,
      const int numContainers) {
    entities::Map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(numObjects)) {
      assignment[fmt::format("container{}", i % numContainers)].push_back(
          fmt::format("object{}", i));
    }
    setInitialAssignment(assignment);
    return buildUniverse();
  }

  std::shared_ptr<const entities::Universe> setupUniverse(
      const entities::Map<std::string, std::vector<std::string>>&
          containerToObjects) {
    setInitialAssignment(containerToObjects);
    return buildUniverse();
  }

  void basic(
      ExprPtr objective,
      double before,
      double after,
      const std::shared_ptr<const entities::Universe>& universe);

  void mipbasic(
      ExprPtr objective,
      double before,
      double after,
      const std::shared_ptr<const entities::Universe>& universe);
};

// expr is the objective, should force object 0 to move away from container 0
// object 0 must move to container 6
// Provide expected objective before/after
void PackerTests::basic(
    ExprPtr objective,
    double before,
    double after,
    const std::shared_ptr<const entities::Universe>& universe) {
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  std::vector<ExprPtr> exprs;
  for (const auto i : folly::irange(10)) {
    if (i != 6 && i != 0) {
      exprs.push_back(variable(object(0), container(i), *universe, assignment));
    }
  }

  auto p_ptr = createTestProblem(universe, {objective}, max(exprs, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(before, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));

  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  LocalSearchSolver solver(spec);
  EXPECT_EQ(true, solver.solve(p));

  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(after, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(6), p.assignment.getContainer(object(0)));

  EXPECT_EQ("object0", p.objectName(object(0)));
  EXPECT_EQ("object6", p.objectName(object(6)));
  EXPECT_EQ("object9", p.objectName(object(9)));
  EXPECT_EQ("container6", p.containerName(container(6)));
  EXPECT_EQ("container9", p.containerName(container(9)));
}

void PackerTests::mipbasic(
    ExprPtr objective,
    double before,
    double after,
    const std::shared_ptr<const entities::Universe>& universe) {
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  std::vector<ExprPtr> exprs;
  for (const auto i : folly::irange(10)) {
    if (i != 6 && i != 0) {
      exprs.push_back(variable(object(0), container(i), *universe, assignment));
    }
  }

  auto p_ptr = createTestProblem(universe, {objective}, max(exprs, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(before, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));

  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(after, result.getValue().get(0).objective);
  }

  EXPECT_EQ(container(6), p.assignment.getContainer(object(0)));

  EXPECT_EQ("object0", p.objectName(object(0)));
  EXPECT_EQ("object6", p.objectName(object(6)));
  EXPECT_EQ("object9", p.objectName(object(9)));
  EXPECT_EQ("container6", p.containerName(container(6)));
  EXPECT_EQ("container9", p.containerName(container(9)));
}

TEST_F(PackerTests, Step) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      step(variable(object(0), container(0), *universe, assignment) * 10 - 5),
      1,
      0,
      universe);
}

TEST_F(PackerTests, Square) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      square(variable(object(0), container(0), *universe, assignment) * 10 + 5),
      225,
      25,
      universe);
}

TEST_F(PackerTests, Log) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      log(variable(object(0), container(0), *universe, assignment) * 10 + 5),
      std::log(15),
      std::log(5),
      universe);
}

TEST_F(PackerTests, Power) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      power(
          variable(object(0), container(0), *universe, assignment) * 10 + 5,
          0.5),
      pow(15, 0.5),
      pow(5, 0.5),
      universe);
}

TEST_F(PackerTests, Ceil) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      ceil(variable(object(0), container(0), *universe, assignment) * 10 - 2.9),
      8,
      -2,
      universe);
}

TEST_F(PackerTests, MultipleMoves) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      variable(object(0), container(0), *universe, assignment) * 2 +
          variable(object(1), container(1), *universe, assignment) * 3 +
          variable(object(3), container(3), *universe, assignment) * 4 +
          variable(object(0), container(6), *universe, assignment) + 2,
      11,
      3,
      universe);
}

TEST_F(PackerTests, Lookup) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  basic(
      object_lookup(
          makeObjectVector({{object(0), 10}, {object(1), 5}}, *universe),
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}),
          assignment),
      10,
      0,
      universe);
}

TEST_F(PackerTests, BrokenConstraintStats) {
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto objective =
      step(variable(object(0), container(0), *universe, assignment) * 10 - 5);
  std::vector<ExprPtr> exprs;
  for (const auto i : folly::irange(10)) {
    if (i != 6) {
      exprs.push_back(variable(object(0), container(i), *universe, assignment));
    }
  }

  ProblemConfigs config;
  config.moveStatsSpec.trackContainers() = true;

  auto p_ptr = createTestProblem(
      universe, {objective}, max(exprs, *universe), {}, config);
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  EXPECT_FALSE(evaluator.evaluate({}).isValid());
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));

  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  LocalSearchSolver solver(spec);
  EXPECT_EQ(true, solver.solve(p));

  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(0, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(6), p.assignment.getContainer(object(0)));

  EXPECT_EQ("object0", p.objectName(object(0)));
  EXPECT_EQ("object6", p.objectName(object(6)));
  EXPECT_EQ("object9", p.objectName(object(9)));
  EXPECT_EQ("container6", p.containerName(container(6)));
  EXPECT_EQ("container9", p.containerName(container(9)));
}

TEST_F(PackerTests, ObjectDeduplication) {
  const auto universe = setupUniverse({
      {"container0", {"object0", "object1"}},
      {"container1", {"object2", "object3"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto expr = rebalancer::max(
      {variable(object(0), container(1), *universe, assignment),
       variable(object(1), container(1), *universe, assignment),
       variable(object(2), container(0), *universe, assignment),
       variable(object(3), container(0), *universe, assignment)},
      *universe);

  auto p_ptr = createTestProblem(universe, {expr}, expr);
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  EXPECT_TRUE(evaluator.evaluate({}).isValid());
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(2)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(3)));

  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  LocalSearchSolver solver(spec);
  EXPECT_EQ(true, solver.solve(p));

  EXPECT_TRUE(evaluator.evaluate({}).isValid());
  EXPECT_EQ(0, solver.totalEvals);
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(2)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(3)));
}

TEST_F(PackerTests, NonObjectDeduplication) {
  const auto universe = setupUniverse({
      {"container0", {"object0", "object1"}},
      {"container1", {"object2", "object3"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto objective = rebalancer::max(
      {variable(object(0), container(0), *universe, assignment),
       variable(object(1), container(1), *universe, assignment),
       variable(object(2), container(1), *universe, assignment),
       variable(object(3), container(0), *universe, assignment)},
      *universe);
  auto constraint = rebalancer::max(
      {variable(object(0), container(1), *universe, assignment),
       variable(object(1), container(1), *universe, assignment),
       variable(object(2), container(0), *universe, assignment),
       variable(object(3), container(0), *universe, assignment)},
      *universe);

  auto p_ptr = createTestProblem(universe, {objective}, constraint);
  auto& p = *p_ptr;

  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(2)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(3)));

  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  LocalSearchSolver solver(spec);
  EXPECT_EQ(true, solver.solve(p));

  EXPECT_EQ(16, solver.totalEvals);
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(2)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(3)));
}

TEST_F(PackerTests, MipExprVar) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse({
      {"container0", {"object0", "object1"}},
      {"container1", {"object2", "object3"}},
  });

  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto v1 = variable(object(0), container(1), *universe, assignment);
  auto v2 = variable(object(1), container(1), *universe, assignment);
  auto v3 = variable(object(2), container(0), *universe, assignment);
  auto v4 = variable(object(3), container(0), *universe, assignment);

  auto expr = rebalancer::max({v1, v2, v3, v4}, *universe);

  auto p_ptr = createTestProblem(universe, {expr}, expr);
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));
  EXPECT_TRUE(evaluator.evaluate({}).isValid());
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(2)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(3)));
}

TEST_F(PackerTests, MipExprLinearSum) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  /*
   *                min(LinearSum)
   *                /    |    \
   *              8     -10     5
   *            /        |       \
   *          v1         v2       v3
   *
   *        (o0c0)      (o1c0)    (o0c1)
   *
   *    2 objects, 2 containers
   *    o0c0 = 0; o0c1 = 1; o1c0 = 1
   */
  const auto universe = setupUniverse({
      {"container0", {"object0"}},
      {"container1", {"object1"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto o0c0 = variable(object(0), container(0), *universe, assignment);
  auto o0c1 = variable(object(0), container(1), *universe, assignment);
  auto o1c0 = variable(object(1), container(0), *universe, assignment);
  auto objective = 8 * o0c0 - 10 * o1c0 + 5 * o0c1;
  auto p_ptr =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));
  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(-5, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(0)));
}

TEST_F(PackerTests, MipExprMax) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  /*
   *                min(LinearSum)
   *                /    |    \
   *              8     -10     5
   *            /        |       \
   *          MAX       MAX      MAX
   *        (v1,v3)   (v2,v4)   (v3, v4)
   *
   *        v1          v2         v3           v4
   *        (o0c0)      (o1c0)    (o0c1)       (o1c1)
   *        1           1          0           0
   *
   *    2 objects, 2 containers
   *    1 * 8 - 10 * 1 + 5 * 0 = -2
   *    v3, v4 = 0
   *    v1, v2 = 1
   */
  const auto universe = setupUniverse({
      {"container0", {"object0"}},
      {"container1", {"object1"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto o0c0 = variable(object(0), container(0), *universe, assignment);
  auto o1c0 = variable(object(1), container(0), *universe, assignment);
  auto o0c1 = variable(object(0), container(1), *universe, assignment);
  auto o1c1 = variable(object(1), container(1), *universe, assignment);
  auto max1 = rebalancer::max(o0c0, o0c1);
  auto max2 = rebalancer::max(o1c0, o1c1);
  auto max3 = rebalancer::max(o0c1, o1c1);
  auto objective = 8 * max1 - 10 * max2 + 5 * max3;
  auto p_ptr =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);

  Context context;
  EXPECT_TRUE(max1->is_binary(context));
  EXPECT_TRUE(max2->is_binary(context));
  EXPECT_TRUE(max3->is_binary(context));
  EXPECT_EQ(true, solver.solve(p));
  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(-2, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(0), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
}

TEST_F(PackerTests, MipExprBinaryMax) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  /* max1 is a binary max of binary variables
   * max2 is a binary max of max1 and a binary variable
   */
  const auto universe = setupUniverse({
      {"container0", {"object0", "object1", "object2"}},
      {"container1", {"object3"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto o0c0 = variable(object(0), container(0), *universe, assignment);
  auto o1c0 = variable(object(1), container(0), *universe, assignment);
  auto o0c1 = variable(object(2), container(0), *universe, assignment);
  auto o1c1 = variable(object(3), container(0), *universe, assignment);
  auto max1 = rebalancer::max({o0c0, o0c1, o1c0}, *universe);
  auto max2 = rebalancer::max(max1, o1c1);
  auto objective = max2;
  auto p_ptr =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);

  Context context;
  EXPECT_TRUE(max1->is_binary(context));
  EXPECT_TRUE(max2->is_binary(context));
  EXPECT_EQ(true, solver.solve(p));
  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(0, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(1), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(1)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(2)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(3)));
}

TEST_F(PackerTests, MipExprBinaryMin) {
  const auto universe = setupUniverse({
      {"container0", {"object0", "object1"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto o0c0 = variable(object(0), container(0), *universe, assignment);
  auto o1c0 = variable(object(1), container(0), *universe, assignment);
  auto expr = rebalancer::min(o0c0, o1c0); // -max(-v1, -v2)
  auto p_ptr = createTestProblem(universe, {const_expr(0, *universe)}, expr);
  auto& p = *p_ptr;

  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();

  const PackerSet<entities::ContainerId> dynamicContainers = {container(0)};
  LpContext context(
      dynamicContainers,
      p.getDynamicEquivalentSets(dynamicContainers),
      p.getOrchestrator().getDynamicChildren(dynamicContainers));

  EXPECT_TRUE(expr->is_binary(context));
  p.lp_store.reset(
      facebook::algopt::getAvailableMIPSolver().value(), false, context);
  p.getOrchestrator().buildLp({expr}, {}, p, context, solverT);
  // max_0 + assign_0_0 >= 1
  // max_0 + assign_0_0 >= 1
  // max_0 + 2*assign_0_0 <= 2
  EXPECT_EQ(3, p.lp_store.getConstraintCount());
}

TEST_F(PackerTests, MipStep) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  mipbasic(
      step(variable(object(0), container(0), *universe, assignment) * 10 - 5),
      1,
      0,
      universe);
}

TEST_F(PackerTests, MipSquare) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  mipbasic(
      square(variable(object(0), container(0), *universe, assignment) * 10 + 5),
      225,
      25,
      universe);
}

TEST_F(PackerTests, MipCeil) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  mipbasic(
      ceil(variable(object(0), container(0), *universe, assignment) * 10 - 2.9),
      8,
      -2,
      universe);
}

TEST_F(PackerTests, MipFloor) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  mipbasic(
      floor(
          variable(object(0), container(0), *universe, assignment) * 10 - 2.9),
      7,
      -3,
      universe);
}

TEST_F(PackerTests, MipRectangle) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  mipbasic(
      rectangle(
          variable(object(0), container(0), *universe, assignment) * 10 - 10,
          -1,
          1),
      1,
      0,
      universe);
}

TEST_F(PackerTests, MipLookup) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse(/*numObjects=*/10, /*numContainers=*/10);
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  mipbasic(
      object_lookup(
          makeObjectVector({{object(0), 10}, {object(1), 5}}, *universe),
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(0)}),
          assignment),
      10,
      0,
      universe);
}

TEST_F(PackerTests, MipExprBinary) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse({
      {"container0", {"object0"}},
      {"container1", {"object1"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto objective = product(
      variable(object(0), container(1), *universe, assignment),
      variable(object(1), container(0), *universe, assignment));
  auto p_ptr =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));
  {
    auto result = evaluator.evaluate({});
    EXPECT_TRUE(result.isValid());
    EXPECT_EQ(0, result.getValue().get(0).objective);
  }
  EXPECT_EQ(container(0), p.assignment.getContainer(object(0)));
  EXPECT_EQ(container(1), p.assignment.getContainer(object(1)));
}

TEST_F(PackerTests, MipExprSwaps) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse({
      {"container1", {"object1"}},
      {"container2", {"object2"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto v1 = variable(object(1), container(1), *universe, assignment);
  auto v2 = variable(object(2), container(1), *universe, assignment);
  auto sw =
      swaps({{object(1), container(1)}, {object(2), container(2)}}, *universe);

  auto objective = 0 - v1 - v2;
  auto constraint = 1 - sw;
  auto p_ptr = createTestProblem(universe, {objective}, constraint);
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));
  EXPECT_EQ(-1, evaluator.evaluate({}).getValue().get(0).objective);
}

TEST_F(PackerTests, MipQuotient) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse({
      {"container1", {"object1"}},
      {"container2", {"object2"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto x11 = variable(object(1), container(1), *universe, assignment);
  auto x12 = variable(object(1), container(2), *universe, assignment);
  auto x21 = variable(object(2), container(1), *universe, assignment);
  auto x22 = variable(object(2), container(2), *universe, assignment);
  auto sw =
      swaps({{object(1), container(1)}, {object(2), container(2)}}, *universe);

  auto objective = quotient(x11 + x12, x11 + x12 + x21 + x22) +
      quotient(x21 + x12, x11 + x12 + x21 + x22);
  auto p_ptr =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));
  EXPECT_EQ(0.5, evaluator.evaluate({}).getValue().get(0).objective);
}

CO_TEST_F(PackerTests, MipExprObjParLookup) {
  REBALANCER_CO_SKIP_IF_NO_MIP_SOLVER();
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object1"}},
          {"container1", {"object2"}},
          {"container2", {"object3", "object4", "object5"}},
      });
  co_await addPartition(
      "partition",
      {{"group0", {"object1", "object2", "object3"}},
       {"group1", {"object4"}},
       {"group2", {"object5"}}});
  co_await addScope("scope1", {{"scopeItem0", {"container0"}}});

  const auto universe = buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  const auto partId = partitionId("partition");
  const auto scope = scopeId("scope1");
  const auto scopeItem0 = scopeItemId(scope, "scopeItem0");
  const auto objectCountDimensionId = dimensionId("object_count");

  // Limits, g0=3, g1=1.5, g2=3
  auto obj_part = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partId),
      objectCountDimensionId,
      {{groupId(partId, "group0"), 1},
       {groupId(partId, "group1"), 1.5},
       {groupId(partId, "group2"), 3}});

  auto objective = object_partition_lookup(
      obj_part,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1), container(2)}),
      scope,
      scopeItem0,
      assignment);
  auto p_ptr =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(true, solver.solve(p));
  EXPECT_EQ(0, evaluator.evaluate({}).getValue().get(0).objective);
  EXPECT_EQ(
      true,
      (int(p.assignment.getContainer(object(1)) == container(0)) +
       int(p.assignment.getContainer(object(2)) == container(0)) +
       int(p.assignment.getContainer(object(3)) == container(0))) >= 2);
}

CO_TEST_F(PackerTests, ObjPartitionLookupBrokenConstraint) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object4"}},
          {"container1", {"object1"}},
          {"container2", {"object2", "object3", "object5"}},
      });
  co_await addPartition(
      "partition",
      {{"group0", {"object1", "object2", "object3"}},
       {"group1", {"object4"}},
       {"group2", {"object5"}}});
  co_await addScope("scope1", {{"scopeItem0", {"container0"}}});

  const auto universe = buildUniverse();

  const auto partId = partitionId("partition");
  const auto scope = scopeId("scope1");
  const auto scopeItem0 = scopeItemId(scope, "scopeItem0");
  const auto objectCountDimensionId = dimensionId("object_count");

  auto obj_part = object_partition(
      std::make_shared<const PartitionInfo>(*universe, partId),
      objectCountDimensionId,
      /*groupLimits=*/{});
  // let us say containers are allowed to have just one group (group 0)
  // the constraint is initially broken
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto constraint = std::dynamic_pointer_cast<ObjectPartitionLookupDefault>(
      object_partition_lookup(
          obj_part,
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container(1), container(2)}),
          scope,
          scopeItem0,
          assignment,
          {{groupId(partId, "group0"), -1}},
          {},
          std::nullopt,
          ObjectPartitionLookupPenaltyTransform::IDENTITY,
          1));

  Context ctx;
  constraint->fullApply(TopToBottomEvaluator(ctx), assignment);
  // object 5 in container 2 is penalized, object 4 is not in the list of
  // scope items so no penalty
  auto problem =
      createTestProblem(universe, {constraint}, constraint, {}, {}, false);
  EXPECT_EQ(1, constraint->value) << constraint->digest(*problem);

  auto loosened_constraint =
      std::dynamic_pointer_cast<ObjectPartitionLookupDefault>(
          constraint->get_do_not_make_worse_copy(assignment));
  loosened_constraint->fullApply(TopToBottomEvaluator(ctx), assignment);
  // loosened constraint has zero penalty
  auto loosened_problem = createTestProblem(
      universe, {loosened_constraint}, loosened_constraint, {}, {}, false);
  EXPECT_EQ(0, loosened_constraint->value)
      << loosened_constraint->digest(*loosened_problem);

  // when we move object 4 into container 1 we incur penalty
  ctx.clear();
  loosened_constraint->fullApply(
      TopToBottomEvaluator(ctx),
      Assignment(
          {{container(1), {object(1), object(4)}},
           {container(2), {object(2), object(3), object(5)}}}));
  EXPECT_EQ(1, loosened_constraint->value)
      << loosened_constraint->digest(*loosened_problem);
}

TEST_F(PackerTests, StableStayedMultipleSolves) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto universe = setupUniverse({
      {"container1", {"object1"}},
      {"container2", {"object2"}},
      {"container3", {}},
  });

  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto ov = makeObjectVector({{object(1), 1}, {object(2), 1}}, *universe);

  auto after_1 = object_lookup(
      ov,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1)}),
      assignment);
  auto after_2 = object_lookup(
      ov,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(2)}),
      assignment);
  auto after_3 = object_lookup(
      ov,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(3)}),
      assignment);

  auto stayed_1 = stable_stayed(
      makeObjectVector({{object(1), 1}}, *universe),
      ov,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(1)}),
      *universe,
      assignment);
  auto stayed_2 = stable_stayed(
      makeObjectVector({{object(2), 1}}, *universe),
      ov,
      std::make_shared<PackerSet<entities::ContainerId>>(
          PackerSet<entities::ContainerId>{container(2)}),
      *universe,
      assignment);

  // Don't allow new objects on containers 1 and 2
  // Ensure container 3 only has at most 1 object
  auto constraints = rebalancer::max(
      {after_1 - stayed_1, after_2 - stayed_2, after_3 - 1}, *universe);

  // We're going to push for 1 object on container 3 and 1 object on
  // container 2. The bug this test is for, is to ensure container
  // 2 takes on object 2 and not object 1.
  auto objective = 4.0 - 2 * after_3 + -1 * after_2;

  auto p_ptr = createTestProblem(universe, {objective}, constraints);
  auto& p = *p_ptr;
  const MovesEvaluator evaluator(p, 0, p.objective.size(), "Stage 1");

  auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();
  solverT.skipInitialAssignmentHint() = true;
  OptimalSolver solver(solverT);
  EXPECT_EQ(3, evaluator.evaluate({}).getValue().get(0).objective);

  solver.solve(p, {container(2), container(3)});
  EXPECT_EQ(2, evaluator.evaluate({}).getValue().get(0).objective);
  EXPECT_EQ(p.assignment.getContainer(object(1)), container(1));
  EXPECT_EQ(p.assignment.getContainer(object(2)), container(3));

  solver.solve(p, {container(1), container(2)});
  EXPECT_EQ(1, evaluator.evaluate({}).getValue().get(0).objective);
  EXPECT_EQ(p.assignment.getContainer(object(1)), container(3));
  EXPECT_EQ(p.assignment.getContainer(object(2)), container(2));
}

TEST_F(PackerTests, NegativeConstraintOptimization) {
  const auto universe = setupUniverse({
      {"container0", {"object0", "object1"}},
  });
  const Assignment assignment(universe->getContainers().getInitialAssignment());

  auto o0c0 = variable(object(0), container(0), *universe, assignment);
  auto o1c0 = variable(object(1), container(0), *universe, assignment);
  auto expr = -1 * o0c0 - o1c0; // will never be positive
  auto p_ptr = createTestProblem(universe, {const_expr(0, *universe)}, expr);
  auto& p = *p_ptr;

  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  const auto solverT = facebook::algopt::makeAvailableOptimalSolverSpec();

  const PackerSet<entities::ContainerId> dynamicContainers = {container(0)};
  LpContext context(
      dynamicContainers,
      p.getDynamicEquivalentSets(dynamicContainers),
      p.getOrchestrator().getDynamicChildren(dynamicContainers));

  p.lp_store.reset(
      facebook::algopt::getAvailableMIPSolver().value(), false, context);
  p.getOrchestrator().buildLp({expr}, {}, p, context, solverT);
  EXPECT_EQ(0, p.lp_store.getConstraintCount());
}

TEST_F(PackerTests, StageMoveLimitStageConfig) {
  const LocalSearchStageSolverSpec globalConfig;
  LocalSearchStageSpec stageConfig;
  const std::vector<StageSummary> stageSummaries;

  // no config
  {
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 0, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, std::nullopt);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // config is negative
  {
    stageConfig.solverSpec()->stopAfterMoves() = -1;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 0, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, std::nullopt);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // config is positive
  {
    stageConfig.solverSpec()->stopAfterMoves() = 6;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 0, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 6);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }
}

TEST_F(PackerTests, StageMoveLimitGlobalConfig) {
  LocalSearchStageSolverSpec globalConfig;
  LocalSearchStageSpec stageConfig;
  const std::vector<StageSummary> stageSummaries;

  // config is negative
  {
    globalConfig.stopAfterMoves() = -1;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 0, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, std::nullopt);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // global config positive, no stage config
  {
    globalConfig.stopAfterMoves() = 6;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 1, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 5);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // stage config smaller than global config
  {
    stageConfig.solverSpec()->stopAfterMoves() = 4;
    globalConfig.stopAfterMoves() = 7;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 1, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 4);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // global config - total moves is smaller than stage config
  {
    stageConfig.solverSpec()->stopAfterMoves() = 4;
    globalConfig.stopAfterMoves() = 7;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 5, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 2); // 7 - 5
    EXPECT_EQ(isMaxMovesTillStage, false);
  }
}

TEST_F(PackerTests, StageMoveLimitTillStage) {
  const LocalSearchStageSolverSpec globalConfig;
  LocalSearchStageSpec stageConfig;
  const std::vector<StageSummary> stageSummaries;
  // tillStage is negative
  {
    stageConfig.stopAfterMovesTillStage() = -1;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 0, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, std::nullopt);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // stageConfig is smaller than tillStage
  {
    stageConfig.solverSpec()->stopAfterMoves() = 3;
    stageConfig.stopAfterMovesTillStage() = 4;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 0, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 3);
    EXPECT_EQ(isMaxMovesTillStage, false);
  }

  // stageConfig is equal to tillStage - totalMoves
  {
    stageConfig.solverSpec()->stopAfterMoves() = 3;
    stageConfig.stopAfterMovesTillStage() = 4;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 1, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 3);
    EXPECT_EQ(isMaxMovesTillStage, true);
  }

  // stageConfig is smaller than tillStage - totalMoves
  {
    stageConfig.solverSpec()->stopAfterMoves() = 3;
    stageConfig.stopAfterMovesTillStage() = 4;
    auto [stageMaxMoves, isMaxMovesTillStage] =
        getStageMoveLimit(globalConfig, stageConfig, 2, 0, stageSummaries);
    EXPECT_EQ(stageMaxMoves, 2); // 4 - 2
    EXPECT_EQ(isMaxMovesTillStage, true);
  }
}

} // namespace facebook::rebalancer::packer::tests
