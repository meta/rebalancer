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

#include "algopt/rebalancer/solver/utils/ExclusiveGroups.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#endif

#include <gtest/gtest.h>

#include <string>
#include <utility>

using namespace facebook::rebalancer;

TEST(ExclusiveGroupsTest, ComputeExclusiveGroupsAssignmentMinimizeDeficit) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  auto actual = computeExclusiveGroupsAssignment(
      {{"group1", 6}, {"group2", 4}},
      {{"container1", 7}, {"container2", 5}},
      {{"group1", {{"container1", 3}, {"container2", 3}}},
       {"group2", {{"container1", 3}, {"container2", 1}}}});
  const PackerMap<std::string, std::string> expected = {
      {"container1", "group1"}, {"container2", "group2"}};
  EXPECT_EQ(expected, actual);
}

TEST(ExclusiveGroupsTest, ComputeExclusiveGroupsAssignmentMinimizeMoves) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  auto actual = computeExclusiveGroupsAssignment(
      {{"group1", 3}, {"group2", 3}, {"group3", 3}},
      {{"container1", 3}, {"container2", 3}, {"container3", 3}},
      {{"group1", {{"container1", 2}}},
       {"group2", {{"container1", 1}, {"container2", 1}, {"container3", 1}}},
       {"group3", {{"container2", 1}}}});
  const PackerMap<std::string, std::string> expected = {
      {"container1", "group1"},
      {"container2", "group3"},
      {"container3", "group2"}};
  EXPECT_EQ(expected, actual);
}

TEST(ExclusiveGroupsTest, ComputeExclusiveGroupsAssignmentMinimizeDeviation) {
  REBALANCER_SKIP_IF_NO_MIP_SOLVER();
  auto assignment = computeExclusiveGroupsAssignment(
      {{"group1", 30}, {"group2", 20}, {"group3", 50}},
      {{"container1", 100},
       {"container2", 100},
       {"container3", 100},
       {"container4", 100},
       {"container5", 100},
       {"container6", 100},
       {"container7", 100},
       {"container8", 100},
       {"container9", 100},
       {"container10", 100}},
      {});
  PackerMap<std::string, int> groupCount;
  for (auto& [scopeItem, group] : assignment) {
    ++groupCount[group];
  }
  EXPECT_EQ(3, groupCount["group1"]);
  EXPECT_EQ(2, groupCount["group2"]);
  EXPECT_EQ(5, groupCount["group3"]);
}
