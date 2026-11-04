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

#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/ObjectsToExploreGenerator.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class ObjectsToExploreGeneratorTest : public MoveTestBase {
 protected:
  const std::string kPartitionName = "group";

  ObjectsToExploreGeneratorTest() : MoveTestBase("object", "server") {}

  folly::coro::Task<void> setUpProblem() {
    setInitialAssignment({
        {"server1", {"object1", "object2", "object3", "object4"}},
        {"server2", {"object5", "object6", "object7", "object8"}},
        {"server3", {"object9", "object10", "object11", "object12"}},
        {"server4", {"object13", "object14", "object15", "object16"}},
        {"server5", {"object17", "object18", "object19", "object20"}},
    });

    co_await addPartition(
        kPartitionName,
        {
            {"group1", {"object1", "object6", "object11", "object16"}},
            {"group2", {"object2", "object7", "object12", "object17"}},
            {"group3", {"object3", "object8", "object13", "object18"}},
            {"group4", {"object4", "object9", "object14", "object19"}},
            {"group5", {"object5", "object10", "object15", "object20"}},
        });

    const auto universe = buildUniverse();
    createProblem({const_expr(0, *universe)}, const_expr(0, *universe));
  }

  folly::coro::Task<void> setUpProblemForBundleGeneration() {
    numObjects_ = 30;
    numGroups_ = 5;

    std::vector<std::string> unassignedObjects;
    unassignedObjects.reserve(numObjects_);
    for (const auto i : folly::irange(numObjects_)) {
      unassignedObjects.push_back(fmt::format("object{}", i));
    }

    setInitialAssignment({{"server0", unassignedObjects}});

    entities::Map<std::string, std::vector<std::string>> groups;
    const int objectsPerGroup = numObjects_ / numGroups_;
    for (const auto i : folly::irange(numObjects_)) {
      const int groupIdx = i / objectsPerGroup;
      groups[fmt::format("group{}", groupIdx)].push_back(
          fmt::format("object{}", i));
    }
    co_await addPartition(kPartitionName, groups);

    const auto universe = buildUniverse();

    for (const auto i : folly::irange(numGroups_)) {
      groupIds_.push_back(
          groupId(partitionId(kPartitionName), fmt::format("group{}", i)));
    }

    createProblem({const_expr(0, *universe)}, const_expr(0, *universe));
  }

  entities::GroupId CheckAndGetGroupId(
      const ObjectBundle& bundle,
      const std::string& partitionName) {
    const auto& universe = getUniverse();
    const auto& partition =
        universe.getPartition(universe.getPartitionId(partitionName));
    const entities::Map<entities::ObjectId, std::vector<entities::GroupId>>&
        objectIdToGroupIds = partition.getObjectIdToGroupIds();

    EXPECT_FALSE(bundle.empty());
    const entities::ObjectId firstObject = bundle[0];
    for (const auto i : folly::irange(1, (int)bundle.size())) {
      const entities::ObjectId objectId = bundle[i];
      EXPECT_EQ(objectIdToGroupIds.at(objectId).size(), 1);
      EXPECT_EQ(
          objectIdToGroupIds.at(objectId)[0],
          objectIdToGroupIds.at(firstObject)[0]);
    }
    return objectIdToGroupIds.at(firstObject)[0];
  }

  int numObjects_ = 0;
  int numGroups_ = 0;
  std::vector<entities::GroupId> groupIds_;
};

CO_TEST_F(ObjectsToExploreGeneratorTest, getObjectsToExplore) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto hotContainer = container(4);

  EquivalenceSets equivalenceSets(getUniverse());
  const PackerMap<entities::ObjectId, int> mapping({
      {object(1), 1},  {object(2), 2},  {object(3), 3},  {object(4), 3},
      {object(5), 1},  {object(6), 2},  {object(7), 1},  {object(8), 3},
      {object(9), 1},  {object(10), 2}, {object(11), 1}, {object(12), 3},
      {object(13), 1}, {object(14), 2}, {object(15), 1}, {object(16), 3},
      {object(17), 1}, {object(18), 2}, {object(19), 1}, {object(20), 3},
  });
  equivalenceSets.mappingMerge(mapping);

  auto objectsReferenceList = objectsGenerator.getObjectsToExplore(
      partitionName,
      hotContainer,
      problem.initial_assignment,
      equivalenceSets,
      getMoveStatsAggregator());
  EXPECT_EQ(objectsReferenceList.size(), 5);
  std::vector<int> expectedObjectsSize = {2, 3, 2, 2, 3};

  for (const auto i : folly::irange(objectsReferenceList.size())) {
    auto objects = objectsReferenceList[i].get();
    EXPECT_EQ(objects.size(), expectedObjectsSize[i]);
    for (auto& object : objects) {
      // assert that the object is not in the hot container
      EXPECT_NE(problem.initial_assignment.getContainer(object), hotContainer);
    }
  }
}

CO_TEST_F(
    ObjectsToExploreGeneratorTest,
    allObjectsHaveSameMappingForEquivalentSets) {
  co_await setUpProblem();
  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto hotContainer = container(4);

  EquivalenceSets equivalenceSets(getUniverse());
  const PackerMap<entities::ObjectId, int> mapping({
      {object(1), 1},  {object(2), 1},  {object(3), 1},  {object(4), 1},
      {object(5), 1},  {object(6), 1},  {object(7), 1},  {object(8), 1},
      {object(9), 1},  {object(10), 1}, {object(11), 1}, {object(12), 1},
      {object(13), 1}, {object(14), 1}, {object(15), 1}, {object(16), 1},
      {object(17), 1}, {object(18), 1}, {object(19), 1}, {object(20), 1},
  });
  equivalenceSets.mappingMerge(mapping);

  auto objectsReferenceList = objectsGenerator.getObjectsToExplore(
      partitionName,
      hotContainer,
      problem.initial_assignment,
      equivalenceSets,
      getMoveStatsAggregator());
  EXPECT_EQ(objectsReferenceList.size(), 5);
  // all objects have same mapping, so all objects are in the same equivalent
  // set thus, for each group, we only have 1 object
  std::vector<int> expectedObjectsSize = {1, 1, 1, 1, 1};

  for (const auto i : folly::irange(objectsReferenceList.size())) {
    auto objects = objectsReferenceList[i].get();
    EXPECT_EQ(objects.size(), expectedObjectsSize[i]);
    for (auto& object : objects) {
      // assert that the object is not in the hot container
      EXPECT_NE(problem.initial_assignment.getContainer(object), hotContainer);
    }
  }
}

CO_TEST_F(
    ObjectsToExploreGeneratorTest,
    allObjectBundlesHaveSameMappingForEquivalentSets) {
  co_await setUpProblemForBundleGeneration();

  EquivalenceSets equivalenceSets(getUniverse());
  PackerMap<entities::ObjectId, int> mapping;
  for (const auto i : folly::irange(numObjects_)) {
    mapping[object(i)] = 1;
  }
  equivalenceSets.mappingMerge(mapping);

  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto srcContainer = container(0);

  auto objectBundles = objectsGenerator.getObjectBundlesToExplore(
      partitionName,
      srcContainer,
      /*bundleSize=*/2,
      /*groupBundleSizeOverrides=*/{},
      problem.initial_assignment,
      equivalenceSets,
      /*createGreedyHeterogenousBundles=*/false);

  EXPECT_EQ(objectBundles.size(), 5);

  std::set<entities::GroupId> seenGroupIds;
  for (const ObjectBundle& bundle : objectBundles) {
    EXPECT_EQ(bundle.size(), 2);
    const entities::GroupId groupIdVal =
        CheckAndGetGroupId(bundle, partitionName);
    EXPECT_TRUE(seenGroupIds.insert(groupIdVal).second);
  }
}

CO_TEST_F(ObjectsToExploreGeneratorTest, getObjectBundlesToExplore) {
  co_await setUpProblemForBundleGeneration();

  EquivalenceSets equivalenceSets(getUniverse());
  PackerMap<entities::ObjectId, int> mapping;
  const int objectsPerGroup = numObjects_ / numGroups_;
  const int objectsPerEquivSet = objectsPerGroup / 2;
  for (const auto i : folly::irange(numObjects_)) {
    mapping[object(i)] = i / objectsPerEquivSet;
  }
  equivalenceSets.mappingMerge(mapping);

  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto srcContainer = container(0);

  auto objectBundles = objectsGenerator.getObjectBundlesToExplore(
      partitionName,
      srcContainer,
      /*bundleSize=*/2,
      /*groupBundleSizeOverrides=*/{},
      problem.initial_assignment,
      equivalenceSets,
      /*createGreedyHeterogenousBundles=*/false);

  constexpr int expetcedBundlesPerGroup = 2;
  EXPECT_EQ(objectBundles.size(), 5 * expetcedBundlesPerGroup);

  std::map<entities::GroupId, int> bundlesPerGroup;

  for (const ObjectBundle& bundle : objectBundles) {
    EXPECT_EQ(bundle.size(), 2);
    const entities::GroupId groupIdVal =
        CheckAndGetGroupId(bundle, partitionName);
    bundlesPerGroup[groupIdVal]++;
  }
  EXPECT_EQ(bundlesPerGroup.size(), 5);
  for (const auto& [_, bundleCount] : bundlesPerGroup) {
    EXPECT_EQ(bundleCount, expetcedBundlesPerGroup);
  }
}

CO_TEST_F(ObjectsToExploreGeneratorTest, bundleSizeOverride) {
  co_await setUpProblemForBundleGeneration();

  EquivalenceSets equivalenceSets(getUniverse());
  PackerMap<entities::ObjectId, int> mapping;
  const int objectsPerGroup = numObjects_ / numGroups_;
  const int objectsPerEquivSet = objectsPerGroup / 2;
  for (const auto i : folly::irange(numObjects_)) {
    mapping[object(i)] = i / objectsPerEquivSet;
  }
  equivalenceSets.mappingMerge(mapping);

  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto srcContainer = container(0);

  folly::F14FastMap<entities::GroupId, int> groupBundleSizeOverrides;
  groupBundleSizeOverrides[groupIds_.back()] = 3;

  auto objectBundles = objectsGenerator.getObjectBundlesToExplore(
      partitionName,
      srcContainer,
      /*bundleSize=*/2,
      groupBundleSizeOverrides,
      problem.initial_assignment,
      equivalenceSets,
      /*createGreedyHeterogenousBundles=*/false);

  constexpr int expetcedBundlesPerGroup = 2;
  EXPECT_EQ(objectBundles.size(), 5 * expetcedBundlesPerGroup);

  std::map<entities::GroupId, int> bundlesPerGroup;

  for (const ObjectBundle& bundle : objectBundles) {
    const entities::GroupId groupIdVal =
        CheckAndGetGroupId(bundle, partitionName);
    if (groupIdVal == groupIds_.back()) {
      EXPECT_EQ(bundle.size(), 3);
    } else {
      EXPECT_EQ(bundle.size(), 2);
    }
    bundlesPerGroup[groupIdVal]++;
  }
  EXPECT_EQ(bundlesPerGroup.size(), 5);
  for (const auto& [_, bundleCount] : bundlesPerGroup) {
    EXPECT_EQ(bundleCount, expetcedBundlesPerGroup);
  }
}

CO_TEST_F(ObjectsToExploreGeneratorTest, InfeasibleBundleSize) {
  co_await setUpProblemForBundleGeneration();

  EquivalenceSets equivalenceSets(getUniverse());
  PackerMap<entities::ObjectId, int> mapping;
  const int objectsPerGroup = numObjects_ / numGroups_;
  const int objectsPerEquivSet = objectsPerGroup / 2;
  for (const auto i : folly::irange(numObjects_)) {
    mapping[object(i)] = i / objectsPerEquivSet;
  }
  equivalenceSets.mappingMerge(mapping);

  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto srcContainer = container(0);

  auto objectBundles = objectsGenerator.getObjectBundlesToExplore(
      partitionName,
      srcContainer,
      /*bundleSize=*/4,
      /*groupBundleSizeOverrides=*/{},
      problem.initial_assignment,
      equivalenceSets,
      /*createGreedyHeterogenousBundles=*/false);

  EXPECT_TRUE(objectBundles.empty());
}

CO_TEST_F(ObjectsToExploreGeneratorTest, HeterogeneousBundles) {
  co_await setUpProblemForBundleGeneration();

  EquivalenceSets equivalenceSets(getUniverse());
  PackerMap<entities::ObjectId, int> mapping;
  const int objectsPerGroup = numObjects_ / numGroups_;
  const int objectsPerEquivSet = objectsPerGroup / 2;
  for (const auto i : folly::irange(numObjects_)) {
    mapping[object(i)] = i / objectsPerEquivSet;
  }
  equivalenceSets.mappingMerge(mapping);

  auto& problem = getProblem();
  auto& objectsGenerator = problem.getObjectsGenerator();
  auto partitionName = "group";
  auto srcContainer = container(0);

  auto objectBundles = objectsGenerator.getObjectBundlesToExplore(
      partitionName,
      srcContainer,
      /*bundleSize=*/4,
      /*groupBundleSizeOverrides=*/{},
      problem.initial_assignment,
      equivalenceSets,
      /*createGreedyHeterogenousBundles=*/true);

  EXPECT_EQ(objectBundles.size(), numGroups_);
}
} // namespace facebook::rebalancer::packer::tests
