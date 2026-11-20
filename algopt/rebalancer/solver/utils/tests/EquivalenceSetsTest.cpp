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

#include "algopt/rebalancer/algopt_common/CompressedIdMap.h"
#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/ObjectDimension.h"
#include "algopt/rebalancer/entities/Objects.h"
#include "algopt/rebalancer/entities/ObjectScalarDimension.h"
#include "algopt/rebalancer/entities/ObjectValueTypes.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Scope.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/ObjectToContainerAssignmentUtils.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>

#include <initializer_list>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <utility>
#include <vector>
using namespace std;
using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::packer::tests {

using entities::ContainerId;
using entities::EquivalenceSetId;
using entities::ObjectId;

namespace {
constexpr int kObjectCount = 200;

std::shared_ptr<const entities::Universe> makeTestUniverse() {
  entities::tests::UniverseBuilderTestUtils builder("object", "container");
  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  initialAssignment["container0"] = {};
  initialAssignment["container0"].reserve(kObjectCount);
  for (const auto i : folly::irange(kObjectCount)) {
    initialAssignment["container0"].push_back(fmt::format("object{}", i));
  }
  builder.setInitialAssignment(initialAssignment);
  return builder.buildUniverse();
}

set<set<ObjectId>> get_sets(EquivalenceSets& equivalenceSets) {
  set<set<ObjectId>> sets;
  for (const auto i : folly::irange(equivalenceSets.size())) {
    auto& packer_set = equivalenceSets.getSet(EquivalenceSetId(i));
    sets.insert(set<ObjectId>(packer_set.begin(), packer_set.end()));
  }
  EXPECT_EQ(equivalenceSets.size(), sets.size());
  return sets;
}

ObjectToContainerAssignmentUtils getAssignmentUtils(
    std::vector<PackerSet<entities::ContainerId>> containerGroups = {}) {
  ObjectToContainerAssignmentUtils assignmentUtils;
  for (auto& containerGroup : containerGroups) {
    assignmentUtils.addStableContainerGroup(
        std::make_shared<const PackerSet<entities::ContainerId>>(
            std::move(containerGroup)));
  }
  return assignmentUtils;
}

} // namespace

class EquivalenceSetsAssignmentTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {};

TEST(EquivalenceSetsTest, Simple) {
  const auto universe = makeTestUniverse();
  EquivalenceSets equivalenceSets(*universe);

  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(101), 1},
           {object(102), 1},
           {object(103), 2},
           {object(104), 2}}));
  EXPECT_EQ(
      set<set<ObjectId>>(
          {{object(101), object(102)}, {object(103), object(104)}}),
      get_sets(equivalenceSets));

  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(103), 1},
           {object(104), 1},
           {object(105), 2},
           {object(106), 2}}));
  EXPECT_EQ(
      set<set<ObjectId>>(
          {{object(101), object(102)},
           {object(103), object(104)},
           {object(105), object(106)}}),
      get_sets(equivalenceSets));

  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(102), 1},
           {object(103), 1},
           {object(107), 2},
           {object(108), 2}}));
  EXPECT_EQ(
      set<set<ObjectId>>(
          {{object(101)},
           {object(102)},
           {object(103)},
           {object(104)},
           {object(105), object(106)},
           {object(107), object(108)}}),
      get_sets(equivalenceSets));

  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(106), 1}, {object(107), 1}, {object(108), 1}}));
  EXPECT_EQ(
      set<set<ObjectId>>(
          {{object(101)},
           {object(102)},
           {object(103)},
           {object(104)},
           {object(105)},
           {object(106)},
           {object(107), object(108)}}),
      get_sets(equivalenceSets));
}

TEST_F(EquivalenceSetsAssignmentTest, getObjectToContainer) {
  setInitialAssignment({
      {"container1", {"object101", "object103"}},
      {"container2", {"object102"}},
      {"container3", {"object104"}},
  });
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);

  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(101), 1},
           {object(102), 1},
           {object(103), 2},
           {object(104), 2}}));
  EXPECT_EQ(
      set<set<ObjectId>>(
          {{object(101), object(102)}, {object(103), object(104)}}),
      get_sets(equivalenceSets));
  ObjectToContainerAssignmentUtils assignmentUtils =
      getAssignmentUtils({{container(1)}, {container(2)}, {container(3)}});

  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  // 101 -> container 1
  equivSetToContainerToObjectCount[equivalenceSets.at(object(101))]
                                  [container(1)] = 1;
  // 102 -> container 3
  equivSetToContainerToObjectCount[equivalenceSets.at(object(101))]
                                  [container(3)] = 1;
  // 104 -> container 3
  equivSetToContainerToObjectCount[equivalenceSets.at(object(103))]
                                  [container(3)] = 1;
  // 103 -> container 2
  equivSetToContainerToObjectCount[equivalenceSets.at(object(103))]
                                  [container(2)] = 1;
  auto finalAssignment = assignmentUtils.getObjectToContainer(
      equivalenceSets, equivSetToContainerToObjectCount, initialAssignment);
  const PackerMap<ObjectId, ContainerId> expected = {
      {object(101), container(1)},
      {object(103), container(2)},
      {object(102), container(3)},
      {object(104), container(3)}};
  EXPECT_EQ(finalAssignment, expected);
}

TEST_F(EquivalenceSetsAssignmentTest, equivalenceSetShuffling) {
  entities::Map<std::string, std::vector<std::string>> containerToObjects = {
      {"container0", {}}, {"container1", {}}};
  auto& objectNames = containerToObjects.at("container0");
  objectNames.reserve(100);
  for (const auto i : folly::irange(100)) {
    objectNames.push_back(fmt::format("object{}", i));
  }
  setInitialAssignment(containerToObjects);
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);

  PackerMap<ObjectId, int> merging;
  for (const auto obj : universe->getObjects().getObjectIds()) {
    merging[obj] = 1;
  }
  equivalenceSets.mappingMerge(merging);

  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(0))]
                                  [container(0)] = 50;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(0))]
                                  [container(1)] = 50;

  auto generateAssignment = [&](const std::optional<std::string>& seed =
                                    std::nullopt) {
    return getAssignmentUtils().getObjectToContainer(
        equivalenceSets,
        equivSetToContainerToObjectCount,
        initialAssignment,
        seed);
  };

  auto checkValid = [&](const auto& result) {
    PackerMap<ContainerId, int> totals;
    for (const auto& [_, containerId] : result) {
      totals[containerId]++;
    }
    EXPECT_EQ(totals.size(), 2);
    EXPECT_EQ(totals.at(container(0)), 50);
    EXPECT_EQ(totals.at(container(1)), 50);
  };

  checkValid(generateAssignment());
  checkValid(generateAssignment("seed1"));
  checkValid(generateAssignment("seed2"));

  EXPECT_EQ(generateAssignment(), generateAssignment());
  EXPECT_EQ(generateAssignment("seed1"), generateAssignment("seed1"));
  EXPECT_EQ(generateAssignment("seed2"), generateAssignment("seed2"));

  EXPECT_NE(generateAssignment(), generateAssignment("seed1"));
  EXPECT_NE(generateAssignment("seed1"), generateAssignment("seed2"));
  EXPECT_NE(generateAssignment("seed2"), generateAssignment());
}

TEST_F(EquivalenceSetsAssignmentTest, TestMinimumMovementAssignment) {
  setInitialAssignment({
      {"container1", {"object1", "object2", "object3"}},
      {"container2", {"object4", "object5"}},
  });
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(1), 1},
           {object(2), 1},
           {object(3), 1},
           {object(4), 1},
           {object(5), 2}}));
  auto assignmentUtils = getAssignmentUtils({{container(1)}, {container(2)}});
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());

  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(1))] = {
      {container(1), 2}, {container(2), 2}};
  equivSetToContainerToObjectCount[equivalenceSets.at(object(5))] = {
      {container(1), 1}, {container(2), 0}};
  auto res = assignmentUtils.getObjectToContainer(
      equivalenceSets, equivSetToContainerToObjectCount, initialAssignment);
  EXPECT_EQ(res.at(object(5)), container(1));
}

TEST_F(EquivalenceSetsAssignmentTest, TestMinimumMovementAssignment2) {
  setInitialAssignment({
      {"container1", {"object2", "object3"}},
      {"container2", {"object1", "object4", "object5", "object6"}},
      {"container3", {"object7", "object8", "object9"}},
      {"container4", {"object10"}},
  });
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(1), 1},
           {object(2), 1},
           {object(3), 2},
           {object(4), 1},
           {object(5), 1},
           {object(6), 1},
           {object(7), 2},
           {object(8), 2},
           {object(9), 1},
           {object(10), 3}}));
  auto assignmentUtils =
      getAssignmentUtils({{container(1)}, {container(2)}, {container(3)}});
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());

  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(1))] = {
      {container(1), 3},
      {container(2), 3},
      {container(3), 0},
      {container(4), 0}};
  equivSetToContainerToObjectCount[equivalenceSets.at(object(3))] = {
      {container(1), 0},
      {container(2), 0},
      {container(3), 2},
      {container(4), 1}};
  equivSetToContainerToObjectCount[equivalenceSets.at(object(10))] = {
      {container(1), 0},
      {container(2), 0},
      {container(3), 1},
      {container(4), 0}};
  auto res = assignmentUtils.getObjectToContainer(
      equivalenceSets, equivSetToContainerToObjectCount, initialAssignment);
  // Objects in set 2 {3,7,8} and set 3 {10} have deterministic assignments
  EXPECT_EQ(res.at(object(3)), container(4));
  EXPECT_EQ(res.at(object(7)), container(3));
  EXPECT_EQ(res.at(object(8)), container(3));
  EXPECT_EQ(res.at(object(10)), container(3));

  // Objects in set 1 {1,2,4,5,6,9}: need 3 in container(1) and 3 in
  // container(2). Exact assignment depends on hash map iteration for tied
  // objects, so verify counts instead.
  PackerMap<ContainerId, int> set1Counts;
  for (const auto obj :
       {object(1), object(2), object(4), object(5), object(6), object(9)}) {
    set1Counts[res.at(obj)]++;
  }
  EXPECT_EQ(3, set1Counts[container(1)]);
  EXPECT_EQ(3, set1Counts[container(2)]);
  // object(2) was initially on container(1), should stay to minimize moves
  EXPECT_EQ(res.at(object(2)), container(1));
  // objects {4,5} were initially on container(2), should stay
  EXPECT_EQ(res.at(object(4)), container(2));
  EXPECT_EQ(res.at(object(5)), container(2));
}

TEST(EquivalenceSetsTest, getPreferredContainers) {
  {
    // we only prefer objects initially on 1
    // stable group (2, 3) does not indicate preference
    // when choosing objects for 1
    auto assignmentUtils =
        getAssignmentUtils({{container(1)}, {container(2), container(3)}});
    const std::vector<ContainerId> expected = {container(1)};
    EXPECT_EQ(expected, assignmentUtils.getPreferredContainers(container(1)));
  }
  {
    // 1 is part of (1, 3)
    // when choosing objects for 1, need to consider 3 as well
    auto assignmentUtils = getAssignmentUtils({{container(1), container(3)}});
    const std::vector<ContainerId> expected = {container(1), container(3)};
    EXPECT_EQ(expected, assignmentUtils.getPreferredContainers(container(1)));
  }
  {
    auto assignmentUtils = getAssignmentUtils(
        {{container(1), container(3), container(4)},
         {container(1), container(3), container(5), container(6)}});
    const std::vector<ContainerId> expected = {
        container(1), container(3), container(4), container(5), container(6)};
    EXPECT_EQ(expected, assignmentUtils.getPreferredContainers(container(1)));
  }
  {
    auto assignmentUtils = getAssignmentUtils(
        {{container(2), container(3)}, {container(2), container(4)}});
    const std::vector<ContainerId> expected = {
        container(2), container(3), container(4)};
    EXPECT_EQ(expected, assignmentUtils.getPreferredContainers(container(2)));
  }
  {
    auto assignmentUtils = getAssignmentUtils(
        {{container(1), container(3), container(4)},
         {container(1), container(3), container(5), container(6)},
         {container(1), container(3), container(6)}});
    const std::vector<ContainerId> expected = {
        container(1), container(3), container(6), container(4), container(5)};
    EXPECT_EQ(expected, assignmentUtils.getPreferredContainers(container(1)));
  }
}

TEST_F(EquivalenceSetsAssignmentTest, addStableContainerGroup) {
  setInitialAssignment({
      {"container1", {"object101"}},
      {"container2", {"object102"}},
      {"container3", {"object103"}},
      {"container4", {"object104"}},
  });
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);

  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(101), 1},
           {object(102), 1},
           {object(103), 1},
           {object(104), 1}}));
  EXPECT_EQ(
      set<set<ObjectId>>(
          {{object(101), object(102), object(103), object(104)}}),
      get_sets(equivalenceSets));

  // we want 102, 104, to stay in (2, 4)
  // if possible
  auto assignmentUtils = getAssignmentUtils(
      {{container(4), container(2)},
       {container(1)},
       {container(2)},
       {container(3)},
       {container(4)}});

  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(101))]
                                  [container(2)] = 2;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(101))]
                                  [container(3)] = 2;
  auto finalAssignment = assignmentUtils.getObjectToContainer(
      equivalenceSets, equivSetToContainerToObjectCount, initialAssignment);
  const PackerMap<ObjectId, ContainerId> expected = {
      {object(101), container(3)},
      {object(102), container(2)},
      {object(103), container(3)},
      {object(104), container(2)}};
  EXPECT_EQ(finalAssignment, expected);
}

TEST_F(EquivalenceSetsAssignmentTest, stableContainerCompetition) {
  setInitialAssignment({
      {"container1", {"object1"}},
      {"container2", {"object2"}},
      {"container3", {"object3"}},
  });
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(1), 1}, {object(2), 1}, {object(3), 1}}));
  EXPECT_EQ(
      set<set<ObjectId>>({{object(1), object(2), object(3)}}),
      get_sets(equivalenceSets));

  // we would like container 2 to be as stable as possile
  // (1, 2) to be as stable as possile
  // therefore when after solve
  // 1:2, 2:1, 3:0
  // 2 should keep its object and 1 gets it from 3
  auto assignmentUtils =
      getAssignmentUtils({{container(1)}, {container(2)}, {container(2)}});

  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  // we want to move 3 from c3 to c1
  equivSetToContainerToObjectCount[equivalenceSets.at(object(1))]
                                  [container(1)] = 2;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(1))]
                                  [container(2)] = 1;
  auto finalAssignment = assignmentUtils.getObjectToContainer(
      equivalenceSets, equivSetToContainerToObjectCount, initialAssignment);
  const PackerMap<ObjectId, ContainerId> expected = {
      {object(1), container(1)},
      {object(2), container(2)},
      {object(3), container(1)}};
  EXPECT_EQ(finalAssignment, expected);
}

TEST_F(EquivalenceSetsAssignmentTest, overlappingStableContainerCompetition) {
  setInitialAssignment({
      {"container1", {"object1"}},
      {"container2", {"object2"}},
      {"container3", {"object3"}},
      {"container4", {"object4"}},
  });
  const auto universe = buildUniverse();
  EquivalenceSets equivalenceSets(*universe);
  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(1), 1}, {object(2), 1}, {object(3), 1}, {object(4), 1}}));
  EXPECT_EQ(
      set<set<ObjectId>>({{object(1), object(2), object(3), object(4)}}),
      get_sets(equivalenceSets));

  auto assignmentUtils = getAssignmentUtils(
      {{container(1), container(3), container(4)},
       {container(2), container(3)},
       {container(1)},
       {container(2)},
       {container(3)},
       {container(4)}});

  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());
  PackerMap<EquivalenceSetId, PackerMap<ContainerId, int>>
      equivSetToContainerToObjectCount;
  // 1 and 2 are competing for 3
  // we should prioritize 3 for 2, since 1 has another choice 4
  equivSetToContainerToObjectCount[equivalenceSets.at(object(1))]
                                  [container(1)] = 2;
  equivSetToContainerToObjectCount[equivalenceSets.at(object(1))]
                                  [container(2)] = 2;
  auto finalAssignment = assignmentUtils.getObjectToContainer(
      equivalenceSets, equivSetToContainerToObjectCount, initialAssignment);
  const PackerMap<ObjectId, ContainerId> expected = {
      {object(1), container(1)},
      {object(2), container(2)},
      {object(3), container(2)},
      {object(4), container(1)}};
  EXPECT_EQ(finalAssignment, expected);
}

TEST(EquivalenceSetsTest, finalize) {
  const auto universe = makeTestUniverse();
  EquivalenceSets equivalenceSets(*universe);

  // Touch only 4 of the kObjectCount objects.
  equivalenceSets.mappingMerge(
      PackerMap<ObjectId, int>(
          {{object(0), 1}, {object(1), 1}, {object(2), 2}, {object(3), 2}}));

  // Before finalize: only the touched objects appear in eq sets; the rest are
  // unassigned and don't show up.
  EXPECT_EQ(
      set<set<ObjectId>>({{object(0), object(1)}, {object(2), object(3)}}),
      get_sets(equivalenceSets));

  equivalenceSets.finalize();

  // After finalize: every still-unseen object is bundled into one new eq set.
  std::set<ObjectId> remaining;
  for (const auto i : folly::irange(4, kObjectCount)) {
    remaining.insert(object(i));
  }
  EXPECT_EQ(
      std::set<std::set<ObjectId>>(
          {{object(0), object(1)}, {object(2), object(3)}, remaining}),
      get_sets(equivalenceSets));
}

class EquivalenceSetsIdOverloadsTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {};

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergePartitionMatchesPerGroup) {
  // Verifies that mappingMerge(P) matches a manual loop calling
  // mappingMerge(P, g) per group. The setup intentionally has multi-group
  // memberships (obj0 in {g1, g2}, obj3 in {g2, g3}) so the test exercises
  // the overlap-split property: objects sharing some groups but differing on
  // others end up in different eq sets.
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2", "obj3"}}});
  co_await addPartition(
      "partition",
      {{"g1", {"obj0", "obj1"}},
       {"g2", {"obj0", "obj2", "obj3"}},
       {"g3", {"obj3"}}});
  const auto universe = buildUniverse();
  const auto pId = partitionId("partition");

  EquivalenceSets viaPartitionId(*universe);
  viaPartitionId.mappingMerge(pId);

  EquivalenceSets viaPerGroup(*universe);
  for (const auto gId : universe->getPartition(pId).getGroupIds()) {
    viaPerGroup.mappingMerge(pId, gId);
  }

  // Each object has a unique group-membership signature, so all four end up
  // in their own eq set.
  const std::set<std::set<ObjectId>> expected = {
      {object("obj0")}, {object("obj1")}, {object("obj2")}, {object("obj3")}};
  EXPECT_EQ(get_sets(viaPartitionId), expected);
  EXPECT_EQ(get_sets(viaPerGroup), expected);
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergePartitionBackedObjectValuesUsesPartitionGroups) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2", "obj3", "obj4"}}});
  co_await addPartition(
      "partition",
      {{"g0", {"obj0", "obj1"}},
       {"g1", {"obj2"}},
       {"g2", {"obj3"}},
       {"g3", {"obj4"}}});
  const auto universe = buildUniverse();
  const auto pId = partitionId("partition");
  const auto g0 = groupId(pId, "g0");
  const auto g1 = groupId(pId, "g1");
  const auto g2 = groupId(pId, "g2");
  const auto g3 = groupId(pId, "g3");
  auto compactPartition = std::make_shared<const Partition>(
      entities::Map<GroupId, vector<ObjectId>>{
          {g0, {object("obj0"), object("obj1")}},
          {g1, {object("obj2")}},
          {g2, {object("obj3")}},
          {g3, {object("obj4")}}});

  const entities::ObjectValues values(
      std::make_shared<const entities::Map<GroupId, double>>(
          entities::Map<GroupId, double>{{g0, 7.0}, {g1, 7.0}, {g2, 9.0}}),
      compactPartition,
      /*defaultValue=*/0.0,
      universe->getNumObjects(),
      pId);

  EquivalenceSets viaValues(*universe);
  viaValues.mappingMerge(static_cast<ExprId>(77), values);

  EquivalenceSets viaPartitionGroups(*universe);
  viaPartitionGroups.mappingMerge(pId, g0);
  viaPartitionGroups.mappingMerge(pId, g1);
  viaPartitionGroups.mappingMerge(pId, g2);

  EXPECT_EQ(get_sets(viaPartitionGroups), get_sets(viaValues));
  EXPECT_EQ(
      (std::set<std::set<ObjectId>>{
          {object("obj0"), object("obj1")},
          {object("obj2")},
          {object("obj3")}}),
      get_sets(viaValues));
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergeStaticDimensionMatchesValues) {
  // Verifies that mappingMerge(D) for a static dim matches passing the dim's
  // ObjectValues directly into the overload.
  // Also exercises finalize(): obj4 and obj5 hold the default value (so they
  // are absent from non-default values) and must get bundled into a single
  // new eq set by finalize().
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2", "obj3", "obj4", "obj5"}}});
  co_await addObjectDimension(
      "static_dim",
      entities::Map<std::string, double>{
          {"obj0", 1.0}, {"obj1", 1.0}, {"obj2", 2.0}, {"obj3", 2.0}},
      0.0);
  const auto universe = buildUniverse();
  const auto dId = dimensionId("static_dim");

  EquivalenceSets viaDimId(*universe);
  viaDimId.mappingMerge(dId);

  EquivalenceSets viaValues(*universe);
  viaValues.mappingMerge(
      universe->getObjects().getDimension(dId).only().values());

  EquivalenceSets viaMap(*universe);
  viaMap.mappingMerge(
      *universe->getObjects().getDimension(dId).only().values().asMapOrNull());

  // Before finalize: mappingMerge alone leaves obj4/obj5 (default-valued)
  // unassigned, so they don't appear in any eq set.
  const std::set<std::set<ObjectId>> expectedBeforeFinalize = {
      {object("obj0"), object("obj1")}, {object("obj2"), object("obj3")}};
  EXPECT_EQ(get_sets(viaDimId), expectedBeforeFinalize);
  EXPECT_EQ(get_sets(viaValues), expectedBeforeFinalize);
  EXPECT_EQ(get_sets(viaMap), expectedBeforeFinalize);

  viaDimId.finalize();
  viaValues.finalize();
  viaMap.finalize();

  // After finalize: obj4/obj5 get bundled into one new eq set.
  const std::set<std::set<ObjectId>> expectedAfterFinalize = {
      {object("obj0"), object("obj1")},
      {object("obj2"), object("obj3")},
      {object("obj4"), object("obj5")}};
  EXPECT_EQ(get_sets(viaDimId), expectedAfterFinalize);
  EXPECT_EQ(get_sets(viaValues), expectedAfterFinalize);
  EXPECT_EQ(get_sets(viaMap), expectedAfterFinalize);

  for (const auto objectId : universe->getObjects().getObjectIds()) {
    EXPECT_EQ(viaMap.at(objectId), viaValues.at(objectId));
    EXPECT_EQ(viaMap.at(objectId), viaDimId.at(objectId));
  }
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergeExprIdObjectValuesMatchesTemplate) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2", "obj3"}}});
  co_await addObjectDimension(
      "dim",
      entities::Map<std::string, double>{
          {"obj0", 1.0}, {"obj1", 1.0}, {"obj2", 2.0}, {"obj3", 2.0}},
      0.0);
  const auto universe = buildUniverse();
  const auto dId = dimensionId("dim");

  EquivalenceSets viaTemplate(*universe);
  viaTemplate.mappingMerge(
      static_cast<ExprId>(99),
      *universe->getObjects().getDimension(dId).only().values().asMapOrNull());

  EquivalenceSets viaStorage(*universe);
  viaStorage.mappingMerge(
      static_cast<ExprId>(99),
      universe->getObjects().getDimension(dId).only().values());

  viaTemplate.finalize();
  viaStorage.finalize();
  EXPECT_EQ(get_sets(viaTemplate), get_sets(viaStorage));
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergeExprIdObjectValuesUsesPartitionGroupSlice) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2"}}});
  co_await addPartition(
      "partition", {{"g0", {"obj0", "obj1"}}, {"g1", {"obj2"}}});
  const auto universe = buildUniverse();
  const auto pId = partitionId("partition");
  const auto g0 = groupId(pId, "g0");
  const auto g1 = groupId(pId, "g1");
  auto compactPartition = std::make_shared<const Partition>(
      entities::Map<GroupId, vector<ObjectId>>{
          {g0, {object("obj0"), object("obj1")}}, {g1, {object("obj2")}}});

  const entities::ObjectValues values(
      std::make_shared<const entities::Map<GroupId, double>>(
          entities::Map<GroupId, double>{{g0, 5.0}}),
      compactPartition,
      /*defaultValue=*/0.0,
      universe->getNumObjects(),
      pId);
  const auto group0Values = values.sliceGroup(*compactPartition, g0);

  EquivalenceSets viaValues(*universe);
  viaValues.mappingMerge(static_cast<ExprId>(99), group0Values);

  EquivalenceSets viaPartitionGroup(*universe);
  viaPartitionGroup.mappingMerge(pId, g0);

  EXPECT_EQ(get_sets(viaPartitionGroup), get_sets(viaValues));

  const auto zeroGroupValues = values.sliceGroup(*compactPartition, g1);
  EquivalenceSets zeroGroupEqSets(*universe);
  zeroGroupEqSets.mappingMerge(static_cast<ExprId>(100), zeroGroupValues);
  EXPECT_TRUE(get_sets(zeroGroupEqSets).empty());
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergeDynamicDimensionMatchesPerScopeItem) {
  // Verifies that mappingMerge(D) for a dynamic dim matches the manual loop
  // over Scope::getScopeItemIds() calling mappingMerge(D, sid) per item.
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1"}}, {"container1", {"obj2", "obj3"}}});
  co_await addScope(
      "rack", {{"rack0", {"container0"}}, {"rack1", {"container1"}}});
  co_await addDynamicObjectDimension(
      "dyn_dim",
      scopeId("rack"),
      entities::Map<std::string, entities::Map<std::string, double>>{
          {"rack0", {{"obj0", 1.0}, {"obj1", 2.0}}},
          {"rack1", {{"obj2", 3.0}, {"obj3", 4.0}}}},
      0.0);
  const auto universe = buildUniverse();
  const auto dId = dimensionId("dyn_dim");
  const auto sId = scopeId("rack");

  EquivalenceSets viaDimId(*universe);
  viaDimId.mappingMerge(dId);

  EquivalenceSets viaPerScopeItem(*universe);
  for (const auto sItemId : universe->getScope(sId).getScopeItemIds()) {
    viaPerScopeItem.mappingMerge(dId, sItemId);
  }

  // Every object has a unique value at exactly one scope item, so each ends
  // up in its own eq set.
  const std::set<std::set<ObjectId>> expected = {
      {object("obj0")}, {object("obj1")}, {object("obj2")}, {object("obj3")}};
  EXPECT_EQ(get_sets(viaDimId), expected);
  EXPECT_EQ(get_sets(viaPerScopeItem), expected);
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergeDimensionScopeItemHandlesStatic) {
  // (D, sid) on a static dim: the scope item is ignored for static scalars,
  // so the merge is by static value and produces the same partitioning as the
  // no-arg static path.
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2"}}});
  co_await addObjectDimension(
      "static_dim",
      entities::Map<std::string, double>{
          {"obj0", 1.0}, {"obj1", 1.0}, {"obj2", 2.0}},
      0.0);
  co_await addScope("rack", {{"rack0", {"container0"}}});
  const auto universe = buildUniverse();
  const auto dId = dimensionId("static_dim");

  EquivalenceSets eqSets(*universe);
  eqSets.mappingMerge(dId, scopeItemId("rack", "rack0"));

  const std::set<std::set<ObjectId>> expected = {
      {object("obj0"), object("obj1")}, {object("obj2")}};
  EXPECT_EQ(get_sets(eqSets), expected);
}

CO_TEST_F(
    EquivalenceSetsIdOverloadsTest,
    MappingMergeVectorValuedStaticDimensionMergesPerScalar) {
  // Vector-valued static dim with two scalar components. Each object has a
  // unique (s0, s1) signature, so all end up in singletons even though no
  // single scalar would split them all apart.
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container0", {"obj0", "obj1", "obj2", "obj3"}}});
  const auto obj0 = object("obj0");
  const auto obj1 = object("obj1");
  const auto obj2 = object("obj2");
  const auto obj3 = object("obj3");
  co_await addObjectDimension(
      "vec_dim",
      std::vector<entities::Map<entities::ObjectId, double>>{
          {{obj0, 1.0}, {obj1, 1.0}, {obj2, 2.0}, {obj3, 2.0}},
          {{obj0, 1.0}, {obj2, 1.0}, {obj1, 2.0}, {obj3, 2.0}}},
      std::vector<double>{0.0, 0.0});
  const auto universe = buildUniverse();
  const auto dId = dimensionId("vec_dim");

  EquivalenceSets eqSets(*universe);
  eqSets.mappingMerge(dId);

  const std::set<std::set<ObjectId>> expected = {
      {obj0}, {obj1}, {obj2}, {obj3}};
  EXPECT_EQ(get_sets(eqSets), expected);
}

} // namespace facebook::rebalancer::packer::tests
