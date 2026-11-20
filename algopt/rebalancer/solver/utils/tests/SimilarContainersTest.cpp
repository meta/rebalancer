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

#include "algopt/rebalancer/algopt_common/TestUtils.h"
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"
#include "algopt/rebalancer/solver/utils/Precision.h"
#include "algopt/rebalancer/solver/utils/SimilarContainers.h"
#include <algopt/rebalancer/solver/utils/ContainerPotential.h>
#include <algopt/rebalancer/solver/utils/GlobalObjectiveValue.h>
#include <algopt/rebalancer/solver/utils/Util.h>

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashSet.h"
#endif

#include "gmock/gmock.h"
#include <boost/iterator/iterator_facade.hpp>
#include <folly/container/irange.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <ostream>
#include <ranges>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;
namespace facebook::rebalancer::packer::tests {
namespace {

Precision createPrecision() {
  facebook::algopt::common::thrift::PrecisionTolerances tolerances;
  return Precision(tolerances);
}

LocalSearchObjectiveValue makeGlobalValue(
    std::initializer_list<double> global) {
  const entities::Universe universe;
  GlobalObjective::Builder globalBuilder;
  int pos = 0;
  for (const auto value : global) {
    globalBuilder.setObjective(pos++, const_expr(value, universe));
  }
  return LocalSearchObjective(
             globalBuilder.build(universe), GlobalObjective(universe), universe)
      .getValue();
}
} // namespace

static std::vector<entities::ContainerId> make_class(
    const std::vector<int>& containers) {
  std::vector<entities::ContainerId> cls;
  cls.reserve(containers.size());
  for (auto id : containers) {
    cls.push_back(container(id));
  }
  return cls;
}

static int containerToClass(
    entities::ContainerId container,
    const std::vector<std::vector<entities::ContainerId>>& classes) {
  for (const auto i : folly::irange(classes.size())) {
    auto& containers = classes[i];
    if (find(containers.begin(), containers.end(), container) !=
        containers.end()) {
      return i;
    }
  }
  XLOG(FATAL) << "container " << container << " not found in classes";
  return 0;
}

static PackerMap<entities::ContainerId, ContainerPotential>
setContainerPotentials() {
  const auto precision = createPrecision();
  PackerMap<entities::ContainerId, ContainerPotential> containerPotentials;

  // just add some contributionPotentials for each of the 10 containers
  containerPotentials.emplace(
      container(1),
      ContainerPotential(makeGlobalValue({5, -10}), 10, precision));
  containerPotentials.emplace(
      container(2),
      ContainerPotential(makeGlobalValue({-15, -1}), 7, precision));
  containerPotentials.emplace(
      container(3),
      ContainerPotential(makeGlobalValue({50, -100}), 9, precision));
  containerPotentials.emplace(
      container(4), ContainerPotential(makeGlobalValue({5, 10}), 0, precision));

  containerPotentials.emplace(
      container(5), ContainerPotential(makeGlobalValue({0, -1}), 8, precision));
  containerPotentials.emplace(
      container(6), ContainerPotential(makeGlobalValue({0, -1}), 2, precision));
  containerPotentials.emplace(
      container(7),
      ContainerPotential(makeGlobalValue({-50, -100}), 2, precision));

  containerPotentials.emplace(
      container(8), ContainerPotential(makeGlobalValue({5, 10}), 3, precision));

  containerPotentials.emplace(
      container(9),
      ContainerPotential(makeGlobalValue({90, -100}), 4, precision));
  containerPotentials.emplace(
      container(10),
      ContainerPotential(makeGlobalValue({89, -1000}), 8, precision));

  return containerPotentials;
}

TEST(SimilarContainersTest, Random) {
  const std::vector<std::vector<entities::ContainerId>> classes = {
      make_class({1, 2, 3, 4}),
      make_class({5, 6, 7}),
      make_class({8}),
      make_class({9, 10})};
  SimilarContainers similar_containers(classes);
  auto f = [](entities::ContainerId /* unused */) -> bool { return true; };
  auto sample = similar_containers.stratifiedSample(9, f);
  vector<entities::ContainerId> containerOrder;
  vector<int> counts(4);
  for (auto container : sample) {
    ++counts[containerToClass(container, classes)];
    containerOrder.push_back(container);
  }
  EXPECT_EQ(3, counts[0]);
  EXPECT_EQ(3, counts[1]);
  EXPECT_EQ(1, counts[2]);
  EXPECT_EQ(2, counts[3]);

  // Verify total sample size; exact containers depend on std::shuffle which
  // is platform-dependent
  EXPECT_EQ(9u, containerOrder.size());
  // Verify no duplicates
  std::set<entities::ContainerId> containerSet(
      containerOrder.begin(), containerOrder.end());
  EXPECT_EQ(containerOrder.size(), containerSet.size());
}

TEST(SimilarContainersTest, RandomWithFilter) {
  const std::vector<std::vector<entities::ContainerId>> classes = {
      make_class({1, 2, 3, 4}),
      make_class({5, 6, 7}),
      make_class({8}),
      make_class({9, 10})};
  SimilarContainers similar_containers(classes);
  // Filter out 1, 5, 8, 9 (first elements).
  auto f = [](entities::ContainerId containerId) -> bool {
    auto x = containerId.asInt();
    return x != 1 && x != 5 && x != 8 && x != 9;
  };
  auto sample = similar_containers.stratifiedSample(9, f);
  vector<entities::ContainerId> containerOrder;
  vector<int> counts(4);
  for (auto container : sample) {
    ++counts[containerToClass(container, classes)];
    containerOrder.push_back(container);
  }
  EXPECT_EQ(3, counts[0]);
  EXPECT_EQ(2, counts[1]);
  EXPECT_EQ(0, counts[2]);
  EXPECT_EQ(1, counts[3]);

  // Exact order depends on hash map iteration; verify the set of containers
  std::set<entities::ContainerId> expectedSet = {
      container(10),
      container(2),
      container(7),
      container(4),
      container(6),
      container(3)};
  EXPECT_EQ(expectedSet.size(), containerOrder.size());
  EXPECT_EQ(
      expectedSet,
      std::set<entities::ContainerId>(
          containerOrder.begin(), containerOrder.end()));
}

TEST(SimilarContainersTest, RandomWithFilterExhaustOneGroup) {
  const std::vector<std::vector<entities::ContainerId>> classes = {
      make_class({1, 2, 3, 4}),
      make_class({5, 6, 7}),
      make_class({8}),
      make_class({9, 10})};
  SimilarContainers similar_containers(classes);
  // Filter out 5, 8, 9.
  auto f = [](entities::ContainerId x) -> bool { return x.asInt() < 5; };
  auto sample = similar_containers.stratifiedSample(4, f);
  vector<entities::ContainerId> containerOrder;
  vector<int> counts(1);
  for (auto container : sample) {
    ++counts[containerToClass(container, classes)];
    containerOrder.push_back(container);
  }
  EXPECT_EQ(4, counts[0]);

  // Exact order depends on hash map iteration; verify the set of containers
  std::set<entities::ContainerId> expectedSet = {
      container(1), container(2), container(4), container(3)};
  EXPECT_EQ(expectedSet.size(), containerOrder.size());
  EXPECT_EQ(
      expectedSet,
      std::set<entities::ContainerId>(
          containerOrder.begin(), containerOrder.end()));
}

TEST(SimilarContainersTest, OrderedGroupsNotInitialized) {
  const std::vector<std::vector<entities::ContainerId>> classes = {
      make_class({1, 2, 3, 4}),
      make_class({5, 6, 7}),
      make_class({8}),
      make_class({9, 10})};
  SimilarContainers similarContainers(classes);

  // trying to get an orderedSample results in an error if orderedGroups have
  // not been initialized
  REBALANCER_EXPECT_RUNTIME_ERROR(
      similarContainers.stratifiedOrderedSample(
          5,
          [](entities::ContainerId) { return true; },
          false /*addEqualSizeRandomSample*/),
      "orderedGroups have not been initialized");

  // just add the containerPotential of a container
  const auto precision = createPrecision();
  const PackerMap<entities::ContainerId, ContainerPotential>
      containerPotentials = {
          {container(1),
           ContainerPotential(makeGlobalValue({-50, -100}), 2, precision)},
      };
  similarContainers.setOrderedGroups(containerPotentials);

  // trying to get an orderedSample results in an error if not all groups have
  // been initialized
  REBALANCER_EXPECT_RUNTIME_ERROR(
      similarContainers.stratifiedOrderedSample(
          1,
          [](entities::ContainerId) { return true; },
          false /*addEqualSizeRandomSample*/),
      "orderedGroup 0 has size = 1, but it is expected to have size = 4. "
      "You need to initialize the containerPotential of every container that is part of some group in SimilarContainers");
}

TEST(SimilarContainersTest, ResetOrderedGroupsTest) {
  const std::vector<std::vector<entities::ContainerId>> classes = {
      make_class({1, 2, 3, 4}),
      make_class({5, 6, 7}),
      make_class({8}),
      make_class({9, 10})};
  SimilarContainers similarContainers(classes);

  const PackerMap<entities::ContainerId, ContainerPotential>
      containerPotentials = setContainerPotentials();

  // initialize orderedGroups
  similarContainers.setOrderedGroups(containerPotentials);

  // Given the containerPotentials, the following is the expected order in each
  // group (order if non-decreasing in containerPotential)
  // group0: {2, 1, 4, 3}
  // group1: {7, 6, 5}
  // group2: {8}
  // group3: {10, 9}

  {
    // Now, if we request 6 samples, we should receive two samples from group0
    // and group 1, and one sample from group2 and group3 and the containerIds
    // should be exactly {2, 1, 7, 6, 8, 10}
    constexpr int samplesSize = 6;
    auto computedSamples = similarContainers.stratifiedOrderedSample(
        samplesSize,
        [](entities::ContainerId) { return true; },
        false /*addEqualSizeRandomSample*/);
    const std::set<entities::ContainerId> expectedSamples = {
        container(2),
        container(1),
        container(7),
        container(6),
        container(8),
        container(10)};

    EXPECT_EQ(
        std::set<entities::ContainerId>(
            computedSamples.begin(), computedSamples.end()),
        expectedSamples);
  }

  {
    // If we request 6 samples, we should receive two samples from group0
    // and group 1, and one sample from group2 and group3. However, {2, 6, 8}
    // are non accepting, therefore we can get only at most 5 samples and the
    // containerIds should be exactly {1, 4, 7, 5, 10}.
    constexpr int samplesSize = 6;
    auto computedSamples = similarContainers.stratifiedOrderedSample(
        samplesSize,
        [](entities::ContainerId id) {
          return (
              id != container(2) && id != container(6) && id != container(8));
        },
        false /*addEqualSizeRandomSample*/);
    const std::set<entities::ContainerId> expectedSamples = {
        container(1), container(4), container(7), container(5), container(10)};

    EXPECT_EQ(
        std::set<entities::ContainerId>(
            computedSamples.begin(), computedSamples.end()),
        expectedSamples);
  }
}

TEST(SimilarContainersTest, UpdateOrderedGroupsTest) {
  const std::vector<std::vector<entities::ContainerId>> classes = {
      make_class({1, 2, 3, 4}),
      make_class({5, 6, 7}),
      make_class({8}),
      make_class({9, 10})};
  SimilarContainers similarContainers(classes);

  const PackerMap<entities::ContainerId, ContainerPotential>
      containerPotentials = setContainerPotentials();

  // initialize orderedGroups
  similarContainers.setOrderedGroups(containerPotentials);

  // next, update some of the containerPotentials; note that the potential
  // values below are *changes*
  const auto precision = createPrecision();
  PackerMap<entities::ContainerId, ContainerPotential>
      changesInContainerPotentials;
  changesInContainerPotentials.emplace(
      container(4), ContainerPotential(makeGlobalValue({-7, 1}), 8, precision));
  // after the change containerPotential of container4 is ((-2, 11), 8)
  changesInContainerPotentials.emplace(
      container(2), ContainerPotential(makeGlobalValue({15, 1}), 0, precision));
  // after the change containerPotential of container2 is ((0, 0), 7)
  changesInContainerPotentials.emplace(
      container(5), ContainerPotential(makeGlobalValue({0, 0}), -7, precision));
  // after the change containerPotential of container5 is ((0, -1), 1)

  similarContainers.updateOrderedGroups(changesInContainerPotentials);

  // Given the (updated) containerPotentials, the following is the expected
  // order in each group (order if non-decreasing in containerPotential)
  // group0: {4, 2, 1, 3}
  // group1: {7, 5, 6}
  // group2: {8}
  // group3: {10, 9}

  {
    // Now, if we request 8 samples, we should receive three samples from
    // group0,
    // two samples from group 1 and group3, and one sample from group2. The
    // containerIds should be exactly {4, 2, 1, 7, 5, 8, 10, 9}
    constexpr int samplesSize = 8;
    auto computedSamples = similarContainers.stratifiedOrderedSample(
        samplesSize,
        [](entities::ContainerId) { return true; },
        false /*addEqualSizeRandomSample*/);
    const std::set<entities::ContainerId> expectedSamples = {
        container(4),
        container(2),
        container(1),
        container(7),
        container(5),
        container(8),
        container(10),
        container(9)};

    EXPECT_EQ(
        std::set<entities::ContainerId>(
            computedSamples.begin(), computedSamples.end()),
        expectedSamples);
  }

  {
    // If we request 8 samples, we should receive three samples from
    // group0, two samples from group 1 and group3, and one sample from group2.
    // However, 'includeEqualSizeRandomSample' is set to true, therefore here we
    // expect all the containers to be picked
    constexpr int samplesSize = 8;
    auto computedSamples = similarContainers.stratifiedOrderedSample(
        samplesSize,
        [](entities::ContainerId) { return true; },
        true /*includeEqualSizeRandomSample*/);
    const std::set<entities::ContainerId> expectedSamples = {
        container(4),
        container(2),
        container(1),
        container(3),
        container(7),
        container(5),
        container(6),
        container(8),
        container(10),
        container(9)};

    EXPECT_EQ(10, computedSamples.size());

    EXPECT_EQ(
        std::set<entities::ContainerId>(
            computedSamples.begin(), computedSamples.end()),
        expectedSamples);
  }
}

TEST(SimilarContainersTest, sameSimilarContainers) {
  constexpr int groups = 60;
  constexpr int containersPerGroup = 1;

  std::vector<std::vector<int>> intClasses(
      groups, std::vector<int>(containersPerGroup));
  for (const auto i : folly::irange(groups)) {
    for (const auto j : folly::irange(containersPerGroup)) {
      intClasses[i][j] = i * containersPerGroup + j;
    }
  }
  std::vector<std::vector<entities::ContainerId>> classes;
  classes.reserve(groups);
  for (const auto i : folly::irange(groups)) {
    classes.push_back(make_class(intClasses[i]));
  }
  SimilarContainers similarContainers(classes);

  auto f = [](entities::ContainerId /* unused */) -> bool { return true; };
  auto sample1 = similarContainers.stratifiedSample(10, f);
  auto sample2 = similarContainers.stratifiedSample(10, f);
  auto sample3 = similarContainers.stratifiedSample(10, f);
  auto sample4 = similarContainers.stratifiedSample(10, f);

  EXPECT_EQ(sample1.size(), 10);
  EXPECT_EQ(sample2.size(), 10);
  EXPECT_EQ(sample3.size(), 10);
  EXPECT_EQ(sample4.size(), 10);
  std::unordered_set<entities::ContainerId> mySet;
  for (auto& sample : {sample1, sample2, sample3, sample4}) {
    for (auto container : sample) {
      mySet.insert(container);
    }
  }
  EXPECT_GT(mySet.size(), 10);
}

} // namespace facebook::rebalancer::packer::tests
