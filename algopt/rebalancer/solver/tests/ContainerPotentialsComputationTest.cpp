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
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/ContainerPotential.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/Precision.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>

#include <coroutine>
#include <initializer_list>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {
namespace {

Precision createPrecision() {
  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}
} // namespace

class ContainerPotentialsComputationTest : public ExpressionTestsBase {
 protected:
  constexpr static int nObjects = 12;
  constexpr static int nContainers = 4;

  void SetUp() override {
    folly::coro::blockingWait(setUpUniverse());
  }

  folly::coro::Task<void> setUpUniverse() {
    // Create initial assignment with 12 objects and 4 containers
    // Container i has objects where (objectId mod nContainers) = i
    entities::Map<std::string, std::vector<std::string>> initialAssignment;

    for (const auto i : folly::irange(nContainers)) {
      initialAssignment[fmt::format("container{}", i)] = {};
    }

    for (const auto objectId : folly::irange(nObjects)) {
      const int containerId = objectId % nContainers;
      initialAssignment[fmt::format("container{}", containerId)].push_back(
          fmt::format("object{}", objectId));
    }

    setInitialAssignment(initialAssignment);
    universe_ = buildUniverse();

    co_return;
  }

  void createProblem(ExprPtr objective, ExprPtr constraint);
  void createSimpleExample(int nobjects, int nContainers);
  void createMovesEvaluator();
  ExprPtr getDefaultObjective();
  LocalSearchObjectiveValue makeGlobalValue(double value) const;

  std::unique_ptr<Problem> problem_;
  std::shared_ptr<const entities::Universe> universe_;
  std::unique_ptr<MovesEvaluator> movesEvaluator_;
};

void ContainerPotentialsComputationTest::createProblem(
    ExprPtr objective,
    ExprPtr constraint) {
  problem_ = createTestProblem(universe_, {objective}, constraint);
}

void ContainerPotentialsComputationTest::createMovesEvaluator() {
  movesEvaluator_ = std::make_unique<MovesEvaluator>(
      MovesEvaluator(*problem_, 0, problem_->objective.size(), "Stage 1"));
}

ExprPtr ContainerPotentialsComputationTest::getDefaultObjective() {
  // define objective to be the sum_{objects} variable(object, container) *
  // dimensionValue of object
  const Assignment initialAssignment(
      universe_->getContainers().getInitialAssignment());
  ExprPtr objective = const_expr(0, *universe_);
  for (const auto objectId : folly::irange(nObjects)) {
    for (const auto containerId : folly::irange(nContainers)) {
      objective += variable(
                       object(objectId),
                       container(containerId),
                       *universe_,
                       initialAssignment) *
          objectId;
    }
  }

  return objective;
}

LocalSearchObjectiveValue ContainerPotentialsComputationTest::makeGlobalValue(
    double value) const {
  GlobalObjective::Builder builder;
  builder.setObjective(0, const_expr(value, *universe_));
  return LocalSearchObjective(
             builder.build(*universe_), GlobalObjective(*universe_), *universe_)
      .getValue();
}

TEST_F(ContainerPotentialsComputationTest, BasicComputeAllPotentials) {
  const auto precision = createPrecision();

  {
    // create a problem and movesEvaluator
    auto objective = getDefaultObjective();
    auto dummyConstraint = const_expr(0, *universe_);
    createProblem(objective, dummyConstraint);
    createMovesEvaluator();
  }

  // compute the ContainerPotential of all the containers
  auto computedPotentials = movesEvaluator_->computeContainerPotentials();

  /*
    In this example, the contributionToGoal of different containers w.r.t.
    to the initial assignment are as follows:

    container0: 0 + 4 + 8
        (since the objects in this container w.r.t. the initialAssignment_ are
        objects 0, 4, and 8, with dimension values 0, 4, and 8, respectively).
        Similarly for the other containers.
    container1: 1 + 5 + 9
    container2: 2 +  6 + 10
    container3: 3 + 7 + 11
  */

  PackerMap<entities::ContainerId, double> contributions = {
      {container(0), 12},
      {container(1), 15},
      {container(2), 18},
      {container(3), 21}};
  PackerMap<entities::ContainerId, ContainerPotential> expectedPotentials;
  for (const auto containerId : folly::irange(nContainers)) {
    auto contributionToGoal =
        makeGlobalValue(contributions.at(container(containerId)));

    // all the containers have 3 objects; so containerPotential is
    // (contributionToGoal, 3)
    expectedPotentials.emplace(
        container(containerId),
        ContainerPotential(contributionToGoal, 3, precision));
  }

  EXPECT_EQ(computedPotentials.size(), nContainers);

  for (auto& [containerId, computedPotential] : computedPotentials) {
    auto& expectedPotential = expectedPotentials.at(containerId);
    const int compareComputedAndExpected = ContainerPotential::precisionCompare(
        computedPotential, expectedPotential, precision);

    // make sure that both the computedPotential and expectedPotential are
    // equal. If their comparison above returns 0, then they are equal
    EXPECT_EQ(compareComputedAndExpected, 0);
  }
}

TEST_F(ContainerPotentialsComputationTest, BasicUpdatePotentials) {
  // This is very similar to the test case with just one change: object4 is
  // moved from container0 to container3
  const auto precision = createPrecision();

  {
    // create a problem and moves evaluator
    auto objective = getDefaultObjective();
    auto dummyConstraint = const_expr(0, *universe_);

    // modify the objective by adding an extra term 20 * variable(4, 3), which
    // penalizes moving object4 from container0 to container3
    auto modifiedObjective = objective +
        20 *
            variable(
                object(4),
                container(3),
                *universe_,
                Assignment(universe_->getContainers().getInitialAssignment()));
    createProblem(modifiedObjective, dummyConstraint);
    createMovesEvaluator();
  }

  // create a single change to initialAssignment_. Move object 4 from container0
  // to container3. This move makes the objective worse because of a high
  // penalty term "20 * variable(4, 3)" in modifiedObjective
  ChangeSet changeSet;
  changeSet.insert(Change(object(4), container(0), -1));
  changeSet.insert(Change(object(4), container(3), 1));

  // apply the changes
  auto [before, after] = problem_->apply(changeSet);
  const auto appliedMoveResult = MoveResult::makeValid(
      MoveSet::fromChangeSet(changeSet), std::move(before), std::move(after));

  // compute the *change* in containerPotentials
  auto computedChanges =
      movesEvaluator_->updateContainerPotentialsAfterMove(appliedMoveResult);

  /*
    Note that this example is just the one in the test case
    BasicComputeAllPotentials, but where there is an additional change---one of
    moving object4 from container0 to container3.

    Therefore, when we call the updateContainerPotentialsAfterMove(), only the
    change in potentials of container0 and container3 are calculated.

    Change in potential of container0 is ((-4), -1), where (-4) is the decrease
    in container0's contributionToGoal and -1 is the decrease in the number of
    objects

    Similarly, change in potential of container3 is ((24), 1), where (24) is the
    increase in container3's contributionToGoal because of moving object4 into
    it and 1 is the increase in its number of objects. The increase in
    conributionToGoal by 24 happens since there are terms (objectDimValue_(4) *
    variable(4, 3) + 20 * variable(4, 3)) in modifiedObjective
  */
  PackerMap<entities::ContainerId, ContainerPotential> expectedChanges;
  ContainerPotential changeForSource =
      ContainerPotential(makeGlobalValue(-4), -1, precision);
  ContainerPotential changeForDestination =
      ContainerPotential(makeGlobalValue(24), 1, precision);
  expectedChanges.emplace(container(0), changeForSource);
  expectedChanges.emplace(container(3), changeForDestination);

  EXPECT_EQ(computedChanges.size(), expectedChanges.size());

  for (auto& [containerId, computedChange] : computedChanges) {
    auto& expectedChange = expectedChanges.at(containerId);
    const int compareComputedAndExpected = ContainerPotential::precisionCompare(
        computedChange, expectedChange, precision);

    // make sure that both the computedChange and expectedChange are equal. If
    // their comparison above returns 0, then they are equal
    EXPECT_EQ(compareComputedAndExpected, 0);
  }
}

} // namespace facebook::rebalancer::packer::tests
