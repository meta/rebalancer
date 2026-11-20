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
#include "algopt/rebalancer/common/log/LogCollector.h"
#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/LinearSum.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/solvers/LocalSearchSolver.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/MapUtil.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::tests {

class LocalSearchEvalApplyDisagreementTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {
 protected:
  static constexpr double kContribution = 4194400.0;
  static constexpr double kInitialValue = 79.00000000582155;

  std::shared_ptr<const entities::Universe> buildNetZeroUniverse() {
    algopt::common::thrift::PrecisionTolerances tolerances;
    tolerances.absolute() = 1e-12;
    tolerances.relative() = 1e-12;
    universeBuilder_.setPrecision(tolerances);

    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"container0", {"object0"}}, {"container1", {}}});
    return buildUniverse();
  }

  ExprPtr makeNetZeroObjective(const entities::Universe& universe) {
    const Assignment assignment(
        universe.getContainers().getInitialAssignment());
    // These constants reproduce a production case where different
    // floating-point operation orders make evaluate() see an improvement that
    // disappears on apply().
    const auto source = variable(object(0), container(0), universe, assignment);
    const auto destination =
        variable(object(0), container(1), universe, assignment);
    const auto offset = const_expr(1.0, universe);
    return std::make_shared<LinearSum>(
        universe,
        kInitialValue,
        PackerMap<std::shared_ptr<Expression>, double>{
            {source, kContribution},
            {destination, kContribution},
            {offset, -kContribution}});
  }
};

TEST_F(LocalSearchEvalApplyDisagreementTest, UndoesMoveAndFinishesSearch) {
  const auto universe = buildNetZeroUniverse();
  const auto logCollector = std::make_shared<LogCollector>();
  ProblemConfigs config;
  config.logger = logCollector;

  auto problem = packer::tests::createTestProblem(
      universe,
      /*objectiveTuple=*/{makeNetZeroObjective(*universe)},
      /*constraint=*/const_expr(0, *universe),
      /*nonAcceptingContainers=*/{},
      /*config=*/config);

  interface::LocalSearchSolverSpec spec;
  spec.stopAfterMoves() = 10;
  spec.moveTypeList() = {interface::ProblemSolver::makeMoveTypeSpec(
      interface::SingleMoveTypeSpec{})};

  LocalSearchSolver solver(std::move(spec));
  EXPECT_TRUE(solver.solve(*problem));

  auto data = logCollector->takeLoggedData();
  const auto& summaries = data.solverSummaries;
  ASSERT_EQ(1, summaries.size());
  const auto& summary = summaries.front();
  EXPECT_EQ(
      interface::EndReason::UNABLE_TO_FIND_IMPROVING_MOVES, summary.endReason);
  ASSERT_TRUE(summary.auxInfo.has_value());
  EXPECT_EQ("candidate move does not improve the objective", *summary.auxInfo);
  ASSERT_TRUE(summary.moveStats.has_value());
  EXPECT_EQ(0, *summary.moveStats->numMoves());

  EXPECT_TRUE(data.moves.empty());
  EXPECT_EQ(container(0), problem->assignment.getContainer(object(0)));
  EXPECT_DOUBLE_EQ(kInitialValue, problem->objective.getValue().get(0));

  const auto& profiles = data.localSearchProfiles;
  ASSERT_EQ(1, profiles.size());
  ASSERT_EQ(1, profiles.front().moveTypeEvents()->size());
  const auto& event = profiles.front().moveTypeEvents()->front();
  EXPECT_DOUBLE_EQ(*event.initialValue(), *event.finalValue());
}

TEST_F(
    LocalSearchEvalApplyDisagreementTest,
    ContinuesSearchAfterRejectedAppliedMove) {
  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-12;
  tolerances.relative() = 1e-12;
  universeBuilder_.setPrecision(tolerances);
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0"}},
          {"container1", {}},
          {"container2", {"object1"}},
          {"container3", {}}});
  const auto universe = buildUniverse();

  // The constraint permits only object0 -> container1 and object1 ->
  // container3. The first objective makes object0's move look best during
  // evaluation, but it is neutral after apply; the second objective improves
  // when object1 leaves container2. Search should reject the first move and
  // apply the second.
  const auto logCollector = std::make_shared<LogCollector>();
  ProblemConfigs config;
  config.logger = logCollector;

  const Assignment assignment(universe->getContainers().getInitialAssignment());
  const auto constraint =
      variable(object(0), container(2), *universe, assignment) +
      variable(object(0), container(3), *universe, assignment) +
      variable(object(1), container(0), *universe, assignment) +
      variable(object(1), container(1), *universe, assignment);
  auto problem = packer::tests::createTestProblem(
      universe,
      /*objectiveTuple=*/
      {makeNetZeroObjective(*universe),
       variable(object(1), container(2), *universe, assignment)},
      /*constraint=*/constraint,
      /*nonAcceptingContainers=*/{},
      /*config=*/config);

  interface::LocalSearchSolverSpec spec;
  spec.stopAfterMoves() = 10;
  spec.moveTypeList() = {interface::ProblemSolver::makeMoveTypeSpec(
      interface::SingleMoveTypeSpec{})};

  LocalSearchSolver solver(std::move(spec));
  EXPECT_TRUE(solver.solve(*problem));

  EXPECT_EQ(container(0), problem->assignment.getContainer(object(0)));
  EXPECT_EQ(container(3), problem->assignment.getContainer(object(1)));

  const auto moves = logCollector->takeLoggedData().moves;
  ASSERT_EQ(1, moves.size());
  ASSERT_EQ(1, moves.front().moves()->size());
  EXPECT_EQ("object1", *moves.front().moves()->front().object());
}

TEST_F(
    LocalSearchEvalApplyDisagreementTest,
    SkipsRemainingMovesAfterRejectedAppliedMove) {
  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-12;
  tolerances.relative() = 1e-12;
  universeBuilder_.setPrecision(tolerances);
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}},
          {"container1", {}},
          {"container2", {}}});
  const auto universe = buildUniverse();

  // IMPORTANT: This test documents a potentially suboptimal behavior. After
  // the best-looking move is rejected post-apply, search skips the remaining
  // candidates from that move type, even though moving object1 would satisfy
  // the constraints and improve objective2.
  const auto logCollector = std::make_shared<LogCollector>();
  ProblemConfigs config;
  config.logger = logCollector;

  const Assignment assignment(universe->getContainers().getInitialAssignment());
  const auto constraint =
      variable(object(0), container(2), *universe, assignment) +
      variable(object(1), container(1), *universe, assignment);
  auto problem = packer::tests::createTestProblem(
      universe,
      /*objectiveTuple=*/
      {makeNetZeroObjective(*universe),
       variable(object(1), container(0), *universe, assignment)},
      /*constraint=*/constraint,
      /*nonAcceptingContainers=*/{},
      /*config=*/config);

  interface::LocalSearchSolverSpec spec;
  spec.stopAfterMoves() = 10;
  spec.moveTypeList() = {interface::ProblemSolver::makeMoveTypeSpec(
      interface::SingleMoveTypeSpec{})};

  LocalSearchSolver solver(std::move(spec));
  EXPECT_TRUE(solver.solve(*problem));

  EXPECT_EQ(container(0), problem->assignment.getContainer(object(0)));
  EXPECT_EQ(container(0), problem->assignment.getContainer(object(1)));

  const auto moves = logCollector->takeLoggedData().moves;
  EXPECT_TRUE(moves.empty());
}

TEST_F(
    LocalSearchEvalApplyDisagreementTest,
    TriesNextMoveTypeAfterRejectedAppliedMove) {
  algopt::common::thrift::PrecisionTolerances tolerances;
  tolerances.absolute() = 1e-12;
  tolerances.relative() = 1e-12;
  universeBuilder_.setPrecision(tolerances);
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"object0", "object1"}},
          {"container1", {}},
          {"container2", {}}});
  const auto universe = buildUniverse();

  // SingleFast first selects object0's move, which is neutral after apply.
  // Single then considers the same hot container and moves object1, which
  // satisfies the constraints and improves the objective.
  const auto logCollector = std::make_shared<LogCollector>();
  ProblemConfigs config;
  config.logger = logCollector;

  const Assignment assignment(universe->getContainers().getInitialAssignment());
  const auto constraint =
      variable(object(0), container(2), *universe, assignment) +
      variable(object(1), container(1), *universe, assignment);
  auto problem = packer::tests::createTestProblem(
      universe,
      /*objectiveTuple=*/
      {makeNetZeroObjective(*universe) +
       variable(object(1), container(0), *universe, assignment)},
      /*constraint=*/constraint,
      /*nonAcceptingContainers=*/{},
      /*config=*/config);

  interface::LocalSearchSolverSpec spec;
  spec.stopAfterMoves() = 10;
  spec.moveTypeList() = {
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleFastMoveTypeSpec{}),
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleMoveTypeSpec{})};

  LocalSearchSolver solver(std::move(spec));
  EXPECT_TRUE(solver.solve(*problem));

  EXPECT_EQ(container(0), problem->assignment.getContainer(object(0)));
  EXPECT_EQ(container(2), problem->assignment.getContainer(object(1)));

  const auto moves = logCollector->takeLoggedData().moves;
  ASSERT_EQ(1, moves.size());
  ASSERT_EQ(1, moves.front().moves()->size());
  EXPECT_EQ("object1", *moves.front().moves()->front().object());
}

} // namespace facebook::rebalancer::tests
