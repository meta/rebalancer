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

#include "algopt/rebalancer/solver/expressions/ObjectPartitionMoveLimit.h"
#include "algopt/rebalancer/interface/UniverseProblemBuilder.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"

#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

#include <limits>

namespace facebook::rebalancer::packer::tests {

class ObjectPartitionMoveLimitTest : public ExpressionTestsBase {
 protected:
  const inline static std::string kPartitionName = "partition1";
  const inline static std::string kDimensionName = "dimension1";

  folly::coro::Task<void> setUpTestUniverse(
      const entities::Map<std::string, std::vector<std::string>>& assignment,
      const entities::Map<std::string, std::vector<std::string>>& groupToObjs,
      const entities::Map<std::string, double>& objWeights,
      const std::optional<
          entities::Map<std::string, entities::Map<std::string, double>>>&
          dynamicWeights = std::nullopt) {
    setInitialAssignment(assignment);
    co_await addPartition(kPartitionName, groupToObjs);

    if (dynamicWeights.has_value()) {
      co_await addDynamicObjectDimension(
          kDimensionName, scopeId("container"), *dynamicWeights, 1.0);
    } else {
      co_await addObjectDimension(kDimensionName, objWeights, 1.0);
    }
  }

  // Create the expression after universe is built
  ExprPtr makeObjectPartitionMoveLimit(
      const entities::Universe& universe,
      const PackerMap<entities::GroupId, double>& groupLimits,
      const entities::Set<entities::ContainerId>& srcNotAffecting = {},
      const entities::Set<entities::ContainerId>& dstNotAffecting = {}) {
    return std::make_shared<ObjectPartitionMoveLimit>(ObjectPartitionMoveLimit(
        universe,
        getInitialAssignment(universe),
        partitionId(),
        dimensionId(),
        groupLimits,
        srcNotAffecting,
        dstNotAffecting));
  }

  static Assignment getInitialAssignment(const entities::Universe& universe) {
    return Assignment(universe.getContainers().getInitialAssignment());
  }

  entities::GroupId group(const int i) const {
    return groupId(partitionId(), fmt::format("group{}", i));
  }

  entities::PartitionId partitionId() const {
    return ExpressionTestsBase::partitionId(kPartitionName);
  }

  entities::DimensionId dimensionId() const {
    return ExpressionTestsBase::dimensionId(kDimensionName);
  }
};

CO_TEST_F(
    ObjectPartitionMoveLimitTest,
    EquivalenceSetsObjectPartitionMoveLimit) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object3"}},
       {"container1", {"object0", "object1", "object2"}}},
      /*groupToObjs=*/
      {{"group1", {"object0", "object1"}}, {"group2", {"object2", "object3"}}},
      /*objWeights=*/
      {{"object0", 1}, {"object1", 2}, {"object2", 1}, {"object3", 1}});

  buildUniverse();
  const auto& universe = getUniverse();
  const PackerMap<entities::GroupId, double> groupLimits = {
      {group(1), 0}, {group(2), 0}};
  const auto expr = makeObjectPartitionMoveLimit(universe, groupLimits);

  EquivalenceSets equivalenceSets(universe);
  updateEquivalenceSets(equivalenceSets, *expr);

  // 4 objects belongs to different sets
  // 3 is by itself, as only 3 on container 0 initially
  // 2 is different from 0 and 1, as 2 is on different group
  // 0 and 1 are different, as they have different weight
  EXPECT_EQ(equivalenceSets.size(), 4);
}

CO_TEST_F(
    ObjectPartitionMoveLimitTest,
    ObjectPartitionMoveLimitApplyAndEvaluate) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object0"}},
       {"container1", {"object1", "object2", "object3"}},
       {"container2", {}}},
      /*groupToObjs=*/
      {{"group1", {"object0", "object1"}}, {"group2", {"object2", "object3"}}},
      /*objWeights=*/{});

  buildUniverse();
  const auto& universe = getUniverse();

  const PackerMap<entities::GroupId, double> groupLimits = {
      {group(1), 0}, {group(2), 0}};
  const auto objPartitionMoveLimitExpr =
      makeObjectPartitionMoveLimit(universe, groupLimits);

  // initially, expect the value of the expr to be 0.0 as there are no moves
  auto assignment = getInitialAssignment(universe);
  EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));

  //
  {
    auto changes = ObjectToNewContainer{{object(0), container(2)}};

    EXPECT_EQ(1.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        1.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(1.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  // undoing the change above; expect both evaluate() and partialApply() to
  // return 0
  {
    auto changes = ObjectToNewContainer{{object(0), container(0)}};

    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{
        {object(1), container(2)},
        {object(2), container(0)},
        {object(3), container(2)}};

    EXPECT_EQ(2.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        2.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(2.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    // the following undoes the changes in the previous evaluation w.r.t.
    // object(2) and object(3)
    auto changes = ObjectToNewContainer{
        {object(2), container(1)}, {object(3), container(1)}};

    EXPECT_EQ(1.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        1.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(1.0, apply(objPartitionMoveLimitExpr, assignment));
  }
}

CO_TEST_F(
    ObjectPartitionMoveLimitTest,
    ObjectPartitionMoveLimitWithSourceAndDestinationsNotAffectingLimit) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object0", "object3"}},
       {"container1", {"object1", "object2", "object5"}},
       {"container2", {}},
       {"container3", {"object4"}}},
      /*groupToObjs=*/
      {{"group1", {"object0", "object1", "object2"}},
       {"group2", {"object3", "object5"}},
       {"group3", {"object4"}}},
      /*objWeights=*/{});

  buildUniverse();
  const auto& universe = getUniverse();

  const PackerMap<entities::GroupId, double> groupLimits = {
      {group(1), 0}, {group(2), 1}, {group(3), 0}};
  const auto objPartitionMoveLimitExpr = makeObjectPartitionMoveLimit(
      universe,
      groupLimits,
      /*srcNotAffecting=*/{container(0)},
      /*dstNotAffecting=*/{container(3)});

  auto assignment = getInitialAssignment(universe);
  EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));

  {
    auto changes = ObjectToNewContainer{{object(0), container(2)}};

    // container(0) is specified as a source container that does not affect the
    // limit. Therefore, the value should be 0.0
    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{{object(0), container(0)}};

    // undo the previous set of moves. We expect the value to remain at 0.
    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{{object(4), container(0)}};

    // container(3) is specified as a destination container that does not affect
    // the limit and container(0) is specified as a source container that does
    // not affect the limit. However, the move is from container(3) to
    // container(0), so it does contribute to the limit.
    EXPECT_EQ(1.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        1.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(1.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{{object(4), container(3)}};

    // undoing the previous move should evaluate to 0.0
    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{
        {object(1), container(3)},
        {object(5), container(2)},
        {object(3), container(2)}};

    // Changes here affect group(2) which has a limit of 1. However, the first
    // move is from container (1) to container(3), while the last move is from
    // container(0) to container(2). Therefore, only one move contributes to the
    // limit and hence the violation is 0.
    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{
        {object(1), container(3)}, {object(2), container(2)}};

    EXPECT_EQ(1.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        1.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(1.0, apply(objPartitionMoveLimitExpr, assignment));
  }
}

CO_TEST_F(
    ObjectPartitionMoveLimitTest,
    ObjectPartitionMoveLimitWithSameSourceAndDestinationNotAffectingLimit) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {}},
       {"container1", {"object1", "object2"}},
       {"container2", {}}},
      /*groupToObjs=*/{{"group1", {"object1", "object2"}}},
      /*objWeights=*/{});

  buildUniverse();
  const auto& universe = getUniverse();
  const PackerMap<entities::GroupId, double> groupLimits = {{group(1), 0}};

  const auto objPartitionMoveLimitExpr = makeObjectPartitionMoveLimit(
      universe,
      groupLimits,
      /*srcNotAffecting=*/{container(0)},
      /*dstNotAffecting=*/{container(0)});

  auto assignment = getInitialAssignment(universe);
  EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));

  {
    auto changes = ObjectToNewContainer{
        {object(2), container(0)},
        {object(2), container(2)},
        {object(2), container(1)},
    };

    // expect the value to be zero because all the changes together actually do
    // not affect the initial assignment
    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));
  }

  {
    auto changes = ObjectToNewContainer{{object(2), container(0)}};

    // expect the value to be zero because container(0) is a destination that
    // does not affect the limit
    EXPECT_EQ(0.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        0.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(0.0, apply(objPartitionMoveLimitExpr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{{object(2), container(2)}};

    // expect the value to be 1.0 because although the move from container(0)
    // does not contribute to the limit, note that we are looking at the number
    // of relevant moves from the initial assignment (in which object(2) is in
    // container(1))
    EXPECT_EQ(1.0, evaluate(objPartitionMoveLimitExpr, changes, assignment));
    // partialApply
    EXPECT_EQ(
        1.0, applyChanges(objPartitionMoveLimitExpr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(1.0, apply(objPartitionMoveLimitExpr, assignment));
  }
}

CO_TEST_F(ObjectPartitionMoveLimitTest, BoundsTest) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object0"}},
       {"container1", {"object1", "object2"}},
       {"container2", {}}},
      /*groupToObjs=*/
      {{"group0", {"object0", "object1"}}, {"group1", {"object2"}}},
      /*objWeights=*/{{"object0", 1}, {"object1", 3}, {"object2", 3}});

  buildUniverse();
  const auto& universe = getUniverse();
  const PackerMap<entities::GroupId, double> groupLimits = {{group(1), 0}};
  const auto expr = makeObjectPartitionMoveLimit(universe, groupLimits);

  // Bounds
  EXPECT_EQ(0.0, lower_bound(*expr));
  // for group0, sum of weights = 1 + 3 = 4
  // for group1, sum of weights = 3
  // upper_bound = max of these values = 4
  EXPECT_EQ(4.0, upper_bound(*expr));
}

CO_TEST_F(ObjectPartitionMoveLimitTest, DynamicDimension) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object0"}},
       {"container1", {"object1", "object2", "object3", "object5"}},
       {"container2", {"object4"}}},
      /*groupToObjs=*/
      {{"group0", {"object0", "object4"}},
       {"group1", {"object1", "object2", "object3", "object5"}}},
      /*objWeights=*/{},
      /*dynamicWeights=*/
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"container0",
           {{"object0", 1},
            {"object1", 1},
            {"object2", 1},
            {"object3", 1},
            {"object4", 1},
            {"object5", 6}}},
          {"container1",
           {{"object0", 5},
            {"object1", 6},
            {"object2", 6},
            {"object3", 1},
            {"object4", 5},
            {"object5", 1}}},
          {"container2",
           {{"object0", 1},
            {"object1", 2},
            {"object2", 2},
            {"object3", 1},
            {"object4", 1},
            {"object5", 6}}}});

  buildUniverse();
  const auto& universe = getUniverse();
  const PackerMap<entities::GroupId, double> groupLimits = {
      {group(0), 0}, {group(1), 0}};
  const auto expr = makeObjectPartitionMoveLimit(universe, groupLimits);

  auto assignment = getInitialAssignment(universe);

  // Initially, we expect apply to return 0 because there are no moves
  EXPECT_EQ(0.0, apply(expr, assignment));

  {
    auto changes = ObjectToNewContainer{{object(0), container(1)}};

    // object0 has a weight of 5 for container1
    EXPECT_EQ(5.0, evaluate(expr, changes, assignment));
    // partialApply
    EXPECT_EQ(5.0, applyChanges(expr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(5.0, apply(expr, assignment));
  }

  {
    auto changes = ObjectToNewContainer{{object(1), container(0)}};

    // object1 has a weight of 1 for destination container (=container0), but
    // the weight for source container (=container1) is 6. Because we use the
    // maximum value between source and destination weights, we expect evaluate
    // to return  6.0
    EXPECT_EQ(6.0, evaluate(expr, changes, assignment));
    // partialApply
    EXPECT_EQ(6.0, applyChanges(expr, changes, assignment));

    // update assignment and verify fullApply
    assignment = getModifiedAssignment(assignment, changes);
    EXPECT_EQ(6.0, apply(expr, assignment));
  }
}

CO_TEST_F(ObjectPartitionMoveLimitTest, DynamicDimensionEquivalenceSetsTest) {
  // object1 and object2 have the same weights for all containers and have the
  // same initial assignment.
  // object0 and object4 have same weights for all containers but have different
  // initial assignments.
  // object1 and object3 have the same initial assignment but have different
  // weights for all containers.
  // object5 has different weights than object1 and object2, but with
  // combination of containers, it has the same weight.
  // object6 has same source container as object4, different weights, but
  // we have container2 in sourceContainersIdsNotAffectingLimit
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object0"}},
       {"container1", {"object1", "object2", "object3", "object5"}},
       {"container2", {"object4", "object6"}}},
      /*groupToObjs=*/
      {{"group0", {"object0", "object4", "object6"}},
       {"group1", {"object1", "object2", "object3", "object5"}}},
      /*objWeights=*/{},
      /*dynamicWeights=*/
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"container0",
           {{"object0", 1},
            {"object1", 1},
            {"object2", 1},
            {"object3", 1},
            {"object4", 1},
            {"object5", 6}}},
          {"container1",
           {{"object0", 5},
            {"object1", 6},
            {"object2", 6},
            {"object3", 1},
            {"object4", 5},
            {"object5", 1}}},
          {"container2",
           {{"object0", 1},
            {"object1", 2},
            {"object2", 2},
            {"object3", 1},
            {"object4", 1},
            {"object5", 6}}}});

  buildUniverse();
  const auto& universe = getUniverse();

  const PackerMap<entities::GroupId, double> groupLimits = {
      {group(0), 0}, {group(1), 0}};
  const auto expr = makeObjectPartitionMoveLimit(
      universe,
      groupLimits,
      /*srcNotAffecting=*/{container(2)});

  // Equivalence sets
  EquivalenceSets equivalenceSets(universe);
  updateEquivalenceSets(equivalenceSets, *expr);

  // we expect 4 sets - {{1,2,5}, {4,6}, {3}, {0}}
  EXPECT_EQ(equivalenceSets.size(), 4);

  // Bounds
  EXPECT_EQ(0.0, lower_bound(*expr));
  // upper_bound is the sum of max weights for each object,container combination
  EXPECT_EQ(19.0, upper_bound(*expr));
}

CO_TEST_F(ObjectPartitionMoveLimitTest, DynamicDimensionBoundsTest) {
  co_await setUpTestUniverse(
      /*assignment=*/
      {{"container0", {"object0"}},
       {"container1", {"object1"}},
       {"container2", {"object2", "object3"}}},
      /*groupToObjs=*/
      {{"group0", {"object0", "object3"}}, {"group1", {"object1", "object2"}}},
      /*objWeights=*/{},
      /*dynamicWeights=*/
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"container0",
           {{"object0", 1}, {"object1", 1}, {"object2", 1}, {"object3", 8}}},
          {"container1",
           {{"object0", 5}, {"object1", 6}, {"object2", 6}, {"object3", 1}}},
          {"container2",
           {{"object0", 1}, {"object1", 2}, {"object2", 2}, {"object3", 1}}}});

  buildUniverse();
  const auto& universe = getUniverse();

  const PackerMap<entities::GroupId, double> groupLimits = {
      {group(0), 0}, {group(1), 0}};

  {
    const auto expr = makeObjectPartitionMoveLimit(universe, groupLimits);

    EXPECT_EQ(0.0, lower_bound(*expr));
    EXPECT_EQ(13.0, upper_bound(*expr));
  }

  // Verify that upper bound computation honors
  // sourceContainersIdsNotAffectingLimit
  {
    const auto expr = makeObjectPartitionMoveLimit(
        universe,
        groupLimits,
        /*srcNotAffecting=*/{container(2)});

    EXPECT_EQ(0.0, lower_bound(*expr));
    EXPECT_EQ(6.0, upper_bound(*expr));
  }

  // Verify that upper bound computation honors
  // destinationContainersIdsNotAffectingLimit
  {
    const auto expr = makeObjectPartitionMoveLimit(
        universe,
        groupLimits,
        /*srcNotAffecting=*/{},
        /*dstNotAffecting=*/{container(0), container(1), container(2)});

    EXPECT_EQ(0.0, lower_bound(*expr));
    EXPECT_EQ(0.0, upper_bound(*expr));
  }
}

CO_TEST_F(ObjectPartitionMoveLimitTest, SparseRowsWithDifferentDefaultCosts) {
  entities::Map<std::string, std::vector<std::string>> assignment;
  entities::Map<std::string, std::vector<std::string>> groups;
  entities::Map<std::string, entities::Map<std::string, double>> values;
  for (int c = 0; c < 8; ++c) {
    assignment[fmt::format("container{}", c)] = {};
  }
  for (int o = 0; o < 32; ++o) {
    const auto object = fmt::format("object{}", o);
    assignment["container0"].push_back(object);
    groups["group0"].push_back(object);
  }
  values["container0"]["object0"] = 3.0;
  values["container0"]["object1"] = 2.0;
  for (int c = 1; c < 8; ++c) {
    values[fmt::format("container{}", c)]["object1"] = 3.0;
    values[fmt::format("container{}", c)]["object2"] = 3.0;
  }
  co_await setUpTestUniverse(assignment, groups, {}, values);
  buildUniverse();
  const auto& universe = getUniverse();
  const auto expression = makeObjectPartitionMoveLimit(universe, {});
  EquivalenceSets sets(universe);
  updateEquivalenceSets(sets, *expression);
  sets.finalize();
  EXPECT_EQ(sets.size(), 2);
  EXPECT_EQ(sets.at(object(0)), sets.at(object(1)));
  EXPECT_EQ(sets.at(object(1)), sets.at(object(2)));
  EXPECT_NE(sets.at(object(2)), sets.at(object(3)));

  entities::Set<entities::ContainerId> allDestinations;
  for (const auto destination : universe.getContainers().getContainerIds()) {
    allDestinations.insert(destination);
  }
  for (const bool ignoreSource : {false, true}) {
    const auto ignored = makeObjectPartitionMoveLimit(
        universe, {},
        ignoreSource ? entities::Set<entities::ContainerId>{container(0)}
                     : entities::Set<entities::ContainerId>{},
        ignoreSource ? entities::Set<entities::ContainerId>{}
                     : allDestinations);
    EquivalenceSets ignoredSets(universe);
    updateEquivalenceSets(ignoredSets, *ignored);
    ignoredSets.finalize();
    EXPECT_EQ(ignoredSets.size(), 1);
  }
}

CO_TEST_F(ObjectPartitionMoveLimitTest, DenseRowsPreserveFiltersAndPartitions) {
  co_await setUpTestUniverse(
      {{"container0", {"object0", "object1", "object2", "object3"}},
       {"container1", {"object4"}},
       {"container2", {"object5"}}},
      {{"group0", {"object0", "object1", "object2", "object3", "object4"}},
       {"group1", {"object3"}}},
      {},
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"container0", {{"object0", 2}}},
          {"container1", {{"object1", 2}, {"object2", 3}, {"object3", 2}}},
          {"container2",
           {{"object0", 3}, {"object1", 3}, {"object2", 3}, {"object3", 3}}}});
  buildUniverse();
  const auto& universe = getUniverse();
  for (const bool filtered : {false, true}) {
    const auto expression = makeObjectPartitionMoveLimit(
        universe, {}, {},
        filtered ? entities::Set<entities::ContainerId>{container(1)}
                 : entities::Set<entities::ContainerId>{});
    EquivalenceSets sets(universe);
    updateEquivalenceSets(sets, *expression);
    sets.finalize();
    EXPECT_EQ(sets.size(), filtered ? 4 : 5);
    EXPECT_EQ(sets.at(object(0)), sets.at(object(1)));
    EXPECT_EQ(sets.at(object(0)) == sets.at(object(2)), filtered);
    EXPECT_NE(sets.at(object(0)), sets.at(object(3)));
    EXPECT_NE(sets.at(object(0)), sets.at(object(4)));
    EXPECT_NE(sets.at(object(0)), sets.at(object(5)));
  }
}

TEST_F(ObjectPartitionMoveLimitTest, GroupBackedCostsMergeEqualRows) {
  interface::UniverseProblemBuilder builder(nullptr);
  builder.setObjectName("object");
  builder.setContainerName("container");
  builder.setGroupBackedDynamicDimensions(true);
  builder.setAssignment(entities::Map<std::string, std::vector<std::string>>{
      {"container0", {"object0", "object1", "object2", "object3", "object4"}},
      {"container1", {"object5"}},
      {"container2", {}}});
  builder.addPartition("cost",
                       entities::Map<std::string, std::vector<std::string>>{
                           {"a", {"object0", "object1", "object5"}},
                           {"b", {"object2"}},
                           {"c", {"object3"}}});
  builder.addPartition("migration",
                       entities::Map<std::string, std::vector<std::string>>{
                           {"all",
                            {"object0", "object1", "object2", "object3",
                             "object4", "object5"}}});
  builder.addDynamicObjectDimension(
      "cost", "container", "cost",
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"container0", {{"a", 2}}},
          {"container1", {{"a", 2}, {"b", 2}, {"c", 3}}},
          {"container2", {{"a", 3}, {"b", 3}, {"c", 2}}}},
      1.0);
  const auto universe = builder.build();
  const auto dimension = universe->getDimensionId("cost");
  EXPECT_EQ(nullptr, universe->getObjects()
                         .getDimension(dimension)
                         .only()
                         .values(universe->getScopeItemId(
                             universe->getScopeId("container"), "container1"))
                         .asMapOrNull());
  ObjectPartitionMoveLimit expression(
      *universe, Assignment(universe->getContainers().getInitialAssignment()),
      universe->getPartitionId("migration"), dimension, {}, {}, {});
  EquivalenceSets sets(*universe);
  updateEquivalenceSets(sets, expression);
  sets.finalize();
  EXPECT_EQ(sets.size(), 4);
  EXPECT_EQ(sets.at(universe->getObjectId("object0")),
            sets.at(universe->getObjectId("object1")));
  EXPECT_EQ(sets.at(universe->getObjectId("object1")),
            sets.at(universe->getObjectId("object2")));
}

CO_TEST_F(ObjectPartitionMoveLimitTest, NonFiniteSourceCostsPreserveGroups) {
  co_await setUpTestUniverse(
      {{"container0", {"object0", "object1", "object2", "object3"}},
       {"container1", {}}},
      {{"group0", {"object0", "object1", "object2", "object3"}}}, {},
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"container0",
           {{"object0", std::numeric_limits<double>::infinity()},
            {"object1", std::numeric_limits<double>::infinity()},
            {"object2", std::numeric_limits<double>::quiet_NaN()},
            {"object3", std::numeric_limits<double>::quiet_NaN()}}}});
  buildUniverse();
  const auto& universe = getUniverse();
  const auto expression = makeObjectPartitionMoveLimit(universe, {});
  EquivalenceSets sets(universe);
  updateEquivalenceSets(sets, *expression);
  sets.finalize();
  EXPECT_EQ(sets.size(), 3);
  EXPECT_EQ(sets.at(object(0)), sets.at(object(1)));
  EXPECT_NE(sets.at(object(2)), sets.at(object(3)));
}

} // namespace facebook::rebalancer::packer::tests
