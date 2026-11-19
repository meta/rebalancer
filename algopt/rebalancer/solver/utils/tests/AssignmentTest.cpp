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

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#include "multifeed/hash/QuickHashSet.h"

#include "gtest/gtest.h"
#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>

#include <algorithm>
#include <memory>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace facebook::rebalancer::entities;
namespace facebook::rebalancer::packer::tests {

template <class C>
set<entities::ObjectId> makeSet(const C& collection) {
  return set<entities::ObjectId>(collection.begin(), collection.end());
}

static set<entities::ObjectId> makeSet(
    std::vector<entities::ObjectId> collection) {
  return set<entities::ObjectId>(collection.begin(), collection.end());
}

class AssignmentTest : public ExpressionTestsBase {
 protected:
  entities::ObjectId task(int i) {
    return object(fmt::format("task{}", i));
  }

  entities::ContainerId host(int i) {
    return container(fmt::format("host{}", i));
  }
};

TEST_F(AssignmentTest, EmptyObjects) {
  const Assignment assignment;
  const auto objects = assignment.getObjects();
  EXPECT_TRUE(objects.empty());
  EXPECT_EQ(0, objects.size());
}

TEST_F(AssignmentTest, DynamicObjects) {
  const entities::Map<std::string, std::vector<std::string>> initialAssignment =
      {{
          {"container100", {"object1000", "object1001"}},
          {"container101", {}},
          {"container102", {"object1002", "object1003"}},
      }};
  setInitialAssignment(initialAssignment);
  const auto universe = buildUniverse();
  Assignment assignment(
      universe->getContainers().getInitialAssignment(),
      {},
      {object(1000), object(1003)});

  const auto objects = assignment.getObjects();
  const auto expectedObjects =
      makeSet({object(1000), object(1001), object(1002), object(1003)});
  EXPECT_EQ(expectedObjects, makeSet(objects));
  EXPECT_EQ(expectedObjects.size(), objects.size());

  // Get all objects.
  EXPECT_EQ(
      makeSet({object(1000), object(1001)}),
      makeSet(assignment.getObjects(container(100))));
  EXPECT_EQ(makeSet({}), makeSet(assignment.getObjects(container(101))));
  EXPECT_EQ(
      makeSet({object(1002), object(1003)}),
      makeSet(assignment.getObjects(container(102))));

  // Get dynamic objects.
  EXPECT_EQ(
      makeSet({object(1001)}),
      makeSet(assignment.getDynamicObjects(container(100))));
  EXPECT_EQ(makeSet({}), makeSet(assignment.getDynamicObjects(container(101))));
  EXPECT_EQ(
      makeSet({object(1002)}),
      makeSet(assignment.getDynamicObjects(container(102))));

  // Update assignment.
  assignment.moveTo(object(1001), container(101));
  EXPECT_EQ(expectedObjects, makeSet(objects));
}

TEST_F(AssignmentTest, EquivalenceIndexGetterRejectsOtherKeys) {
  const entities::Map<std::string, std::vector<std::string>> initialAssignment =
      {{{"host0", {"task0", "task1"}}}};
  setInitialAssignment(initialAssignment);
  const auto universe = buildUniverse();
  Assignment assignment(universe->getContainers().getInitialAssignment());
  EquivalenceSets sets(*universe, "sets");
  sets.finalize();
  EquivalenceSets otherSets(*universe, "other_sets");
  otherSets.finalize();

  EXPECT_THROW(
      assignment.getObjectsIndexedByEquivSets(sets), std::runtime_error);
  assignment.buildIndexByEquivalentSets(sets);
  EXPECT_NO_THROW(assignment.getObjectsIndexedByEquivSets(sets));
  EXPECT_THROW(
      assignment.getObjectsIndexedByEquivSets(otherSets), std::runtime_error);
}

TEST_F(AssignmentTest, EquivalenceIndexPutsTopScoredObjectOfEachSetFirst) {
  const entities::Map<std::string, std::vector<std::string>> initialAssignment =
      {{{"host0", {"task0", "task1", "task2", "task3"}},
        {"host1", {"task4", "task5"}}}};
  setInitialAssignment(initialAssignment);
  const auto universe = buildUniverse();
  const PackerMap<entities::ObjectId, double> taskToScore = {
      {task(0), 1.0},
      {task(1), 4.0},
      {task(2), 3.0},
      {task(3), 2.0},
      {task(5), 5.0}};
  Assignment assignment(
      universe->getContainers().getInitialAssignment(),
      std::make_shared<const ObjectScores>(entities::ObjectValues(
          std::make_shared<const entities::ObjectIdToDoubleMap>(
              taskToScore,
              /*defaultValue=*/0.0,
              /*totalSize=*/universe->getNumObjects()))),
      /*fixedObjects=*/{},
      /*useDynamicObjectOrdering=*/false);
  EquivalenceSets sets(*universe);
  sets.mappingMerge(
      PackerMap<entities::ObjectId, int>{
          {task(0), 0},
          {task(1), 1},
          {task(2), 0},
          {task(3), 1},
          {task(4), 2},
          {task(5), 0}});
  assignment.buildIndexByEquivalentSets(sets);
  const auto firstObjects = [&]() {
    return makeSet(assignment.getDistinctObjectsFromEquivSetIdx(host(0), sets));
  };

  EXPECT_EQ(makeSet({task(2), task(1)}), firstObjects());
  // An object that arrives after the index is built goes by its score.
  assignment.moveTo(task(5), host(0));
  EXPECT_EQ(makeSet({task(5), task(1)}), firstObjects());
  assignment.moveTo(task(1), host(1));
  EXPECT_EQ(makeSet({task(5), task(3)}), firstObjects());
}

TEST_F(AssignmentTest, DistinctObjectsOfContainerWithoutObjectsAreEmpty) {
  const entities::Map<std::string, std::vector<std::string>> initialAssignment =
      {{{"host0", {"task0", "task1"}}}};
  setInitialAssignment(initialAssignment);
  const auto universe = buildUniverse();
  Assignment assignment(universe->getContainers().getInitialAssignment());
  EquivalenceSets sets(*universe);
  sets.finalize();
  assignment.buildIndexByEquivalentSets(sets);

  EXPECT_TRUE(
      assignment
          .getDistinctObjectsFromEquivSetIdx(entities::ContainerId(1000), sets)
          .empty());
}

CO_TEST_F(AssignmentTest, AllEquivalenceIndexesFollowSetsWithAnotherKey) {
  const entities::Map<std::string, std::vector<std::string>> initialAssignment =
      {{{"host0", {"task0", "task1"}}}};
  setInitialAssignment(initialAssignment);
  const entities::Map<std::string, std::vector<std::string>> groupToObjects = {
      {{"all", {"task0", "task1"}}}};
  co_await addPartition("all", groupToObjects);
  const auto universe = buildUniverse();
  Assignment assignment(universe->getContainers().getInitialAssignment());
  const auto allPartitionId = partitionId("all");
  EquivalenceSets oneSet(*universe, "one_set");
  oneSet.finalize();
  // A key stands for the sets' content, so sets with oneSet's key keep the
  // indexes built from oneSet.
  EquivalenceSets twoSetsWithOneSetsKey(*universe, "one_set");
  twoSetsWithOneSetsKey.mappingMerge(
      PackerMap<entities::ObjectId, int>{{task(0), 0}, {task(1), 1}});
  EquivalenceSets twoSets(*universe, "two_sets");
  twoSets.mappingMerge(
      PackerMap<entities::ObjectId, int>{{task(0), 0}, {task(1), 1}});
  const auto numIndexedSets = [&]() {
    return assignment.getObjectsIndexedByEquivSetsAndPartition(allPartitionId)
        .getContainerObjects(host(0))
        .size();
  };

  assignment.buildIndexByEquivalentSetsAndPartition(
      *universe, oneSet, allPartitionId);
  EXPECT_EQ(1, numIndexedSets());
  assignment.buildAllIndexesByEquivalentSets(*universe, twoSetsWithOneSetsKey);
  EXPECT_EQ(1, numIndexedSets());
  assignment.buildAllIndexesByEquivalentSets(*universe, twoSets);
  EXPECT_EQ(2, numIndexedSets());
  EXPECT_EQ(
      2,
      assignment.getObjectsIndexedByEquivSets(twoSets)
          .getContainerObjects(host(0))
          .size());
}

CO_TEST_F(AssignmentTest, IndexedObjects) {
  const entities::Map<std::string, std::vector<std::string>> initialAssignment =
      {{
          {"host0", {"task0", "task1", "task2"}},
          {"host1", {"task6"}},
          {"host2", {"task3", "task4", "task5"}},
      }};

  setInitialAssignment(initialAssignment);

  // Also create an odd/even group as well as an equivalent set based on odd
  // even split. Same test will check for both index by equivalent sets and
  // index by groups.
  const entities::Map<std::string, std::vector<std::string>> groupToObjects = {
      {"even", {"task0", "task2", "task4", "task6"}},
      {"odd", {"task1", "task3", "task5"}}};
  co_await addPartition("odd_even", groupToObjects);

  const auto universe = buildUniverse();

  std::vector<entities::ObjectId> objects;
  for (const auto i : folly::irange(7)) {
    objects.push_back(task(i));
  }

  Assignment assignment(
      universe->getContainers().getInitialAssignment(), {}, {objects.at(6)});

  const auto oddEvenPartitionId = partitionId("odd_even");
  const auto evenGroup = groupId(oddEvenPartitionId, "even");
  const auto oddGroup = groupId(oddEvenPartitionId, "odd");

  // Build equivalence sets based on odd/even split
  EquivalenceSets equivalenceSets(*universe);
  PackerMap<entities::ObjectId, double> objectToEquivSet;
  for (const auto i : folly::irange(7)) {
    objectToEquivSet[objects.at(i)] = i % 2;
  }
  // all odd objects are equivalent, all even objects are equivalent
  equivalenceSets.mappingMerge(objectToEquivSet);
  EquivalenceSetId even(0);
  EquivalenceSetId odd(1);
  assignment.buildIndexByEquivalentSets(equivalenceSets);
  assignment.buildIndexByPartition(*universe, oddEvenPartitionId);
  assignment.buildIndexByEquivalentSetsAndPartition(
      *universe, equivalenceSets, oddEvenPartitionId);

  auto checkExpectedMakeup = [&](entities::ContainerId container,
                                 std::vector<entities::ObjectId> expectedOdd,
                                 std::vector<entities::ObjectId> expectedEven) {
    auto indexedByEquivSet =
        assignment.getObjectsIndexedByEquivSets(equivalenceSets)
            .getContainerObjects(container);
    auto indexedByGroupId =
        assignment.getObjectsIndexedByPartition(oddEvenPartitionId)
            .getContainerObjects(container);
    auto indexedByEquivSetAndGroupId =
        assignment.getObjectsIndexedByEquivSetsAndPartition(oddEvenPartitionId)
            .getContainerObjects(container);
    const int numExpectedIndices =
        (expectedOdd.empty() ? 0 : 1) + (expectedEven.empty() ? 0 : 1);
    auto debugStr =
        fmt::format("container: {}", universe->getEntityName(container));
    EXPECT_EQ(numExpectedIndices, indexedByEquivSet.size()) << debugStr;
    EXPECT_EQ(numExpectedIndices, indexedByGroupId.size()) << debugStr;
    EXPECT_EQ(numExpectedIndices, indexedByEquivSetAndGroupId.size())
        << debugStr;

    // ensure getDistinctObjectsInContainer returns one object from each set
    auto expectedDistinctObjs = assignment.getDistinctObjectsFromEquivSetIdx(
        container, equivalenceSets);
    if (!expectedOdd.empty()) {
      int oddObjCount = 0;
      for (auto oddObj : expectedOdd) {
        if (std::count(
                expectedDistinctObjs.begin(),
                expectedDistinctObjs.end(),
                oddObj) == 1) {
          oddObjCount++;
        }
      }
      EXPECT_EQ(oddObjCount, 1) << debugStr;
    }
    if (!expectedEven.empty()) {
      int evenObjCount = 0;
      for (auto evenObj : expectedEven) {
        if (std::count(
                expectedDistinctObjs.begin(),
                expectedDistinctObjs.end(),
                evenObj) == 1) {
          evenObjCount++;
        }
      }
      EXPECT_EQ(evenObjCount, 1) << debugStr;
    }

    for (auto objectId : expectedOdd) {
      EXPECT_TRUE(indexedByEquivSet.at(odd).contains(objectId));
      EXPECT_TRUE(indexedByGroupId.at(oddGroup).contains(objectId));
      EXPECT_TRUE(
          indexedByEquivSetAndGroupId.at(odd).at(oddGroup).contains(objectId));
    }
    for (auto objectId : expectedEven) {
      EXPECT_TRUE(indexedByEquivSet.at(even).contains(objectId));
      EXPECT_TRUE(indexedByGroupId.at(evenGroup).contains(objectId));
      EXPECT_TRUE(indexedByEquivSetAndGroupId.at(even).at(evenGroup).contains(
          objectId));
    }
  };

  const auto c0 = host(0);
  const auto c1 = host(1);
  const auto c2 = host(2);
  // c0 has both odd {1} and even {0,2} objects
  checkExpectedMakeup(c0, {objects.at(1)}, {objects.at(0), objects.at(2)});
  // no dynamic objects in c1
  checkExpectedMakeup(c1, {}, {});
  // c2 has both odd {3, 5} and even {4} objects
  checkExpectedMakeup(c2, {objects.at(3), objects.at(5)}, {objects.at(4)});

  // Make some changes to the assignment
  assignment.moveTo(objects.at(1), c1);
  checkExpectedMakeup(c0, {}, {objects.at(0), objects.at(2)});
  checkExpectedMakeup(c1, {objects.at(1)}, {});
  checkExpectedMakeup(c2, {objects.at(3), objects.at(5)}, {objects.at(4)});

  assignment.moveTo(objects.at(4), c0);
  assignment.moveTo(objects.at(3), c1);
  checkExpectedMakeup(c0, {}, {objects.at(0), objects.at(2), objects.at(4)});
  checkExpectedMakeup(c1, {objects.at(1), objects.at(3)}, {});
  checkExpectedMakeup(c2, {objects.at(5)}, {});
}

} // namespace facebook::rebalancer::packer::tests
