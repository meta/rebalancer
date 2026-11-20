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
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/tests/MockExpression.h"
#include "algopt/rebalancer/solver/utils/AffectedByChangeInfo.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <folly/container/F14Map.h>
#include <folly/container/F14Set.h>
#include <folly/lang/Hint.h>
#include <folly/Optional.h>
#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

constexpr double kOldValue = 1.0;
constexpr double kNewValue = 2.0;

namespace facebook::rebalancer::packer::tests {
// build a graph with two roots,
// a node changes its value when
// MockExpression(kOldValue, kNewValue, testContext)
class OrchestratorTwoRootsTest : public ::testing::Test {
 private:
  std::shared_ptr<MockExpression> createNodeKeepValue() {
    return std::make_shared<MockExpression>(kOldValue, kOldValue, testContext);
  }

  std::shared_ptr<MockExpression> createNodeChangeValue() {
    return std::make_shared<MockExpression>(kOldValue, kNewValue, testContext);
  }

  std::shared_ptr<MockExpression> createTempNodeKeepValue() {
    return std::make_shared<MockExpressionWithTempNode>(
        kOldValue, kOldValue, testContext);
  }

  std::shared_ptr<MockExpression> createTempNodeChangeValue() {
    return std::make_shared<MockExpressionWithTempNode>(
        kOldValue, kNewValue, testContext);
  }

 protected:
  void SetUp() override {
    testContext = std::make_shared<TestContext>();

    A = createNodeChangeValue();

    B = createNodeChangeValue();

    // create a temp node
    C = createTempNodeChangeValue();

    D = createNodeKeepValue();

    E = createNodeChangeValue();

    F = createNodeChangeValue();

    G = createNodeChangeValue();

    H = createNodeKeepValue();

    // create a temp node
    I = createTempNodeChangeValue();

    // create a temp node
    J = createTempNodeKeepValue();

    K = createNodeChangeValue();

    L = createNodeKeepValue();

    M = createNodeChangeValue();

    // create a temp node
    N = createTempNodeKeepValue();

    A->add_child(C);
    A->add_child(D);
    A->add_child(E);
    A->add_child(F);

    B->add_child(E);
    B->add_child(F);
    B->add_child(G);

    C->add_child(H);
    C->add_child(I);
    D->add_child(J);
    E->add_child(K);
    F->add_child(K);
    F->add_child(L);

    G->add_child(M);
    G->add_child(N);

    context.changes() = ChangeSet();

    // initialize expected changed children
    // used by apply and evaluate function
    testContext->expectedChangedChildren[A.get()].insert(
        {C.get(), E.get(), F.get()});
    testContext->expectedChangedChildren[B.get()].insert(
        {E.get(), F.get(), G.get()});
    testContext->expectedChangedChildren[C.get()].insert(I.get());
    testContext->expectedChangedChildren[E.get()].insert(K.get());
    testContext->expectedChangedChildren[F.get()].insert(K.get());
    testContext->expectedChangedChildren[G.get()].insert(M.get());

    // initialize expected changed children by containers
    // used by lp function
    testContext->expectedChangedChildrenByContainers[A.get()].insert(
        {C.get(), D.get(), E.get(), F.get()});
    testContext->expectedChangedChildrenByContainers[B.get()].insert(
        {E.get(), F.get(), G.get()});
    testContext->expectedChangedChildrenByContainers[C.get()].insert(
        {H.get(), I.get()});
    testContext->expectedChangedChildrenByContainers[D.get()].insert(J.get());
    testContext->expectedChangedChildrenByContainers[E.get()].insert(K.get());
    testContext->expectedChangedChildrenByContainers[F.get()].insert(
        {K.get(), L.get()});
    testContext->expectedChangedChildrenByContainers[G.get()].insert(
        {M.get(), N.get()});

    roots.push_back(A.get());
    roots.push_back(B.get());
    // initialize orchestrator
    orch.init(roots, AffectedByChangeDecisionData(0, 0));
  }

  std::shared_ptr<TestContext> testContext;

  std::shared_ptr<MockExpression> A;

  std::shared_ptr<MockExpression> B;

  std::shared_ptr<MockExpression> C;

  std::shared_ptr<MockExpression> D;

  std::shared_ptr<MockExpression> E;

  std::shared_ptr<MockExpression> F;

  std::shared_ptr<MockExpression> G;

  std::shared_ptr<MockExpression> H;

  std::shared_ptr<MockExpression> I;

  std::shared_ptr<MockExpression> J;

  std::shared_ptr<MockExpression> K;

  std::shared_ptr<MockExpression> L;

  std::shared_ptr<MockExpression> M;

  std::shared_ptr<MockExpression> N;

  Orchestrator orch;
  std::vector<Expression*> roots;
  Assignment assignment;
  Context context;
};

TEST(OrchestratorTest, SingleRootApplyTest) {
  auto testContext = std::make_shared<TestContext>();

  auto A = std::make_shared<MockExpression>(kOldValue, kOldValue, testContext);

  auto B = std::make_shared<MockExpression>(kOldValue, kOldValue, testContext);

  auto C = std::make_shared<MockExpression>(kOldValue, kNewValue, testContext);

  A->add_child(B);
  A->add_child(C);
  testContext->expectedChangedChildren[A.get()].insert(C.get());

  Orchestrator orch;
  std::vector<Expression*> roots;
  roots.push_back(A.get());
  orch.init(roots, AffectedByChangeDecisionData(0, 0));
  const Assignment assignment;
  const ChangeSet changes;
  Context context;
  context.changes() = changes;
  // partial apply the changes
  orch.apply(context, assignment);
  EXPECT_TRUE(testContext->alreadyEvaluated.count(A.get()));
}

TEST_F(OrchestratorTwoRootsTest, TwoRootsApplyTest) {
  testContext->clear();
  // partial apply the changes
  orch.apply(context, assignment);
  // root A and B are already evaluated
  EXPECT_TRUE(testContext->alreadyEvaluated.count(A.get()));
  EXPECT_TRUE(testContext->alreadyEvaluated.count(B.get()));
  // all mock leaf nodes are evaluated
  // node D is the only non-leaf node that
  // does not affected by changed values of leaf nodes
  EXPECT_FALSE(testContext->alreadyEvaluated.count(D.get()));
}

TEST_F(OrchestratorTwoRootsTest, TwoRootsEvaluateTest) {
  testContext->clear();
  // evaluate until priority queue reaching or passing root A
  orch.evaluate(A.get(), context);
  EXPECT_TRUE(testContext->alreadyEvaluated.count(A.get()));
  // root B did not evaluated yet
  EXPECT_FALSE(testContext->alreadyEvaluated.count(B.get()));
  // evaluate until priority queue reaching or passing node B
  orch.evaluate(B.get(), context);
  EXPECT_TRUE(testContext->alreadyEvaluated.count(B.get()));
  // did not evaluate node D, because its child J didn't change value
  EXPECT_FALSE(testContext->alreadyEvaluated.count(D.get()));
}

class OrchestratorAssignmentTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {};

TEST_F(OrchestratorAssignmentTest, DynamicChildrenTest) {
  // This test is to verify that if some nodes are affected by objects, the
  // still dynamic children are computed correctly
  Orchestrator orchestrator;
  setInitialAssignment({
      {"container1", {"object1", "object2"}},
      {"container2", {}},
      {"container3", {}},
      {"container4", {}},
      {"container5", {}},
  });
  const auto universe = buildUniverse();

  // create two lookup expressions
  const auto containerIds = universe->getContainers().getContainerIds();
  const auto allContainers = std::make_shared<PackerSet<entities::ContainerId>>(
      containerIds.begin(), containerIds.end());

  const auto numTotalObjects = universe->getNumObjects();
  const int numTotalContainers = allContainers->size();

  // only one object with non-zero value; therefore, the AffectedByChangeType
  // for lookup1 will be OBJECTS_ONLY
  auto objectVector1 = makeObjectVector(
      PackerMap<entities::ObjectId, double>{{object(1), 1}},
      0,
      numTotalObjects,
      *universe);
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto objectLookup1 =
      object_lookup(objectVector1, allContainers, initialAssignment);

  // all objects have non-zero value; therefore, the AffectedByChangType for
  // lookup2 will be CONTAINERS_ONLY
  auto objectVector2 = makeObjectVector(
      PackerMap<entities::ObjectId, double>{{object(1), 1}},
      1,
      numTotalObjects,
      *universe);
  auto objectLookup2 =
      object_lookup(objectVector2, allContainers, initialAssignment);

  auto sum = objectLookup1 + objectLookup2;

  const std::vector<Expression*> exprs = {sum.get()};
  const AffectedByChangeDecisionData data(numTotalObjects, numTotalContainers);
  orchestrator.init(exprs, data);

  // basic check to see the AffectedByChangeType of each lookup
  EXPECT_EQ(
      objectLookup1->isAffectedByChange(data)->getType(),
      AffectedByChangeType::OBJECTS_ONLY);
  EXPECT_EQ(
      objectLookup2->isAffectedByChange(data)->getType(),
      AffectedByChangeType::CONTAINERS_ONLY);

  auto parentToDynamicChildren =
      orchestrator.getDynamicChildren(*allContainers);
  EXPECT_EQ(parentToDynamicChildren.size(), 1);

  // we expect both lookups to be considered dynamic. If leaves with
  // AffectedByChangeType::OBJECT_ONLY are not processed correctly, this will
  // fail
  EXPECT_EQ(parentToDynamicChildren.at(sum.get()).size(), 2);
}

TEST_F(OrchestratorAssignmentTest, MultiRootEvaluate) {
  // it is tempting to use only the height of a node as the priority when
  // evaluating (where nodes at smaller heights have higher priority). This
  // works when there is only one root, but fails when there are multiple roots
  // in the Orchestartor.
  setInitialAssignment({
      {"container0", {}},
      {"container1", {"object0"}},
      {"container2", {}},
  });
  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  auto v0 = variable(object(0), container(0), *universe, initialAssignment);
  auto v1 = variable(object(0), container(1), *universe, initialAssignment);
  auto v2 = variable(object(0), container(2), *universe, initialAssignment);

  ExprPtr sum1 = const_expr(0, *universe);
  inplace_add(sum1, max(0, v0 + v1));

  sum1->description = "sum1";

  auto sum2 = 2 * (v0 + v2);
  sum2->description = "sum2";

  // perform initial fully apply
  {
    Context context;
    const auto evaluator = TopToBottomEvaluator(context);
    sum1->fullApply(evaluator, initialAssignment);
    sum2->fullApply(evaluator, initialAssignment);
    EXPECT_NEAR(1, sum1->value, 1e-9);
    EXPECT_NEAR(0, sum2->value, 1e-9);
  }

  Orchestrator orchestrator;
  orchestrator.init(
      {sum1.get(), sum2.get()},
      AffectedByChangeDecisionData(/*_numTotalObjects=*/1,
                                   /*_numTotalContainers=*/3));

  {
    Context context;
    ChangeSet changeSet;
    changeSet.insert(Change(object(0), container(1), -1));
    changeSet.insert(Change(object(0), container(2), 1));

    context.changes() = std::move(changeSet);

    EXPECT_NEAR(0, orchestrator.evaluate(sum1.get(), context), 1e-9);
    // if we just use height, sum2 will not be evaluated correctly
    EXPECT_NEAR(2, orchestrator.evaluate(sum2.get(), context), 1e-9);
  }
}

} // namespace facebook::rebalancer::packer::tests
