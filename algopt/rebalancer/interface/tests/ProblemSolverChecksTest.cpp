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
#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSpecs_types.h"
#include "algopt/rebalancer/solver/moves/FixedDestMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedSourceMoveType.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/Portability.h>
#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <utility>

namespace facebook::rebalancer::interface::tests {

class ProblemSolverChecksTest : public ::testing::TestWithParam<int> {};

INSTANTIATE_TEST_CASE_P(
    NumThreads,
    ProblemSolverChecksTest,
    testThreadCounts());

static std::unique_ptr<ProblemSolver> makeInitializedSolver(
    int threadCount,
    const std::vector<std::pair<std::string, std::vector<std::string>>>&
        initialAssignment = {{"h1", {"s1", "s2"}}, {"h2", {}}}) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = threadCount});
  solver->setContainerName("host");
  solver->setObjectName("shard");
  solver->setAssignment(initialAssignment);
  return solver;
}

static facebook::rebalancer::interface::GroupCapacitySpec
getDefaultGroupCapacitySpec() {
  std::map<std::string, std::map<std::string, double>> string2string2valMap;
  std::map<std::string, double> string2valMap;
  facebook::rebalancer::interface::GroupCapacitySpec spec;
  spec.scope() = "rack";
  spec.partitionName() = "job";
  spec.contributionPartition() = "job";
  spec.definition() = facebook::rebalancer::interface::
      GroupCapacitySpecDefinition::DURING_AND_AFTER;
  spec.bound() = facebook::rebalancer::interface::GroupCapacitySpecBound::EXACT;

  facebook::rebalancer::interface::Limit limit;
  limit.type() = facebook::rebalancer::interface::LimitType::ABSOLUTE;
  limit.globalLimit() = 2;
  limit.groupLimits() = string2valMap;

  facebook::rebalancer::interface::Limit contribution;
  contribution.type() = facebook::rebalancer::interface::LimitType::ABSOLUTE;
  contribution.globalLimit() = 1;
  contribution.scopeItemToGroupLimits() = string2string2valMap;

  spec.limit() = limit;
  spec.contribution() = contribution;

  return spec;
}

static ScopeItemList makeDimensionFilteredScopeItemList(
    const std::string& scopeName,
    const std::string& dimensionName) {
  ObjectDimensionBasedFilter dimensionFilter;
  dimensionFilter.dimensionName() = dimensionName;
  ScopeItemFilter scopeItemFilter;
  scopeItemFilter.objectDimensionBased() = dimensionFilter;
  ScopeItemList scopeItemList;
  scopeItemList.scopeName() = scopeName;
  scopeItemList.filter() = scopeItemFilter;
  return scopeItemList;
}

TEST_P(ProblemSolverChecksTest, SetObjectNameTwice) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("object_name_1");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setObjectName("object_name_2"),
      "object name already set to object_name_1");
}

TEST_P(ProblemSolverChecksTest, SetContainerNameTwice) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setContainerName("container_name_1");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setContainerName("container_name_2"),
      "container name already set to container_name_1");
}

TEST_P(ProblemSolverChecksTest, SetAssignmentWithoutContainerName) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setAssignment(
          std::vector<std::pair<std::string, std::vector<std::string>>>{
              {"c1", {"o1", "o2"}}, {"c2", {}}}),
      "container name not set");
}

TEST_P(ProblemSolverChecksTest, SetAssignmentWithoutObjectName) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setContainerName("container_name");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setAssignment(
          std::vector<std::pair<std::string, std::vector<std::string>>>{
              {"c1", {"o1", "o2"}}, {"c2", {}}}),
      "object name not set");
}

TEST_P(ProblemSolverChecksTest, SetAssignmentDuplicateObject) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setContainerName("host");
  solver->setObjectName("shard");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setAssignment(
          std::vector<std::pair<std::string, std::vector<std::string>>>{
              {"h1", {"s1", "s2"}}, {"h2", {"s2"}}}),
      "duplicate object s2");
}

TEST_P(ProblemSolverChecksTest, AddObjectDimensionUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectDimension(
          "cpu", std::map<std::string, double>{{"s1", 10}, {"s3", 20}}),
      "dimension cpu defined on unknown shard s3");
}

TEST_P(ProblemSolverChecksTest, AddContainerDimensionUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addContainerDimension(
          "cpu",
          std::vector<std::pair<std::string, double>>{{"h1", 10}, {"h3", 20}}),
      "dimension cpu defined on unknown host h3");
}

TEST_P(ProblemSolverChecksTest, AddScopeDimensionUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScopeDimension(
          "network", "rack", std::map<std::string, double>{{"r1", 10}}),
      "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, AddScopeDimensionUnknownScopeItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addScope(
      "rack",
      std::unordered_map<std::string, std::string>{{"h1", "r1"}, {"h2", "r2"}});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScopeDimension(
          "network",
          "rack",
          std::map<std::string, double>{{"r1", 10}, {"r3", 20}}),
      "dimension network defined on unknown rack r3");
}

TEST_P(ProblemSolverChecksTest, AddScopeTwice) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addScope(
      "rack",
      std::unordered_map<std::string, std::string>{{"h1", "r1"}, {"h2", "r2"}});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScope(
          "rack",
          std::unordered_map<std::string, std::string>{
              {"h1", "r1"}, {"h2", "r2"}}),
      "scope rack added twice");
}

TEST_P(ProblemSolverChecksTest, AddScopeUnknownContainer) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScope(
          "rack",
          std::unordered_map<std::string, std::string>{
              {"h1", "r1"}, {"h3", "r2"}}),
      "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, AddScopeDuplicateContainer) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScope(
          "rack",
          std::map<std::string, std::vector<std::string>>(
              {{"r1", {"h1"}}, {"r2", {"h1", "h2"}}})),
      "host 'h1' appears as part of more than one scope item in scope 'rack'");
}

TEST_P(ProblemSolverChecksTest, AddPartitionTwice) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "job",
          std::unordered_map<std::string, std::string>{
              {"s1", "j1"}, {"s2", "j2"}}),
      "partition job added twice");
}

TEST_P(ProblemSolverChecksTest, AddReservedPartition) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "equivalence_set_partition",
          std::unordered_map<std::string, std::string>{
              {"s1", "j1"}, {"s2", "j2"}}),
      "'equivalence_set_partition' is a reserved partition name");
}

TEST_P(ProblemSolverChecksTest, ForbiddenPartitionNamePrefix) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "rebalancer_internal_partition_test_error",
          std::unordered_map<std::string, std::string>{
              {"s1", "j1"}, {"s2", "j2"}}),
      "partition names are not allowed to start with prefix 'rebalancer_internal_partition', but got 'rebalancer_internal_partition_test_error'");
}

TEST_P(ProblemSolverChecksTest, AddPartitionUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "job",
          std::unordered_map<std::string, std::string>{
              {"s1", "j1"}, {"s3", "j2"}}),
      "unknown shard s3");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "rack";
  balanceSpec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(balanceSpec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(balanceSpec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecUnknownBlacklistedItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";

  facebook::rebalancer::interface::Filter filter;
  filter.itemsBlacklist() = {"h1", "h3"};
  balanceSpec.filter() = filter;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(balanceSpec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecUnboundedUpperLimit) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = facebook::rebalancer::interface::LimitType::ABSOLUTE;
  upperBounds.isDefaultLimitUnbounded() = true;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "BalanceSpec upperBounds cannot set isDefaultLimitUnbounded to true");
}

TEST_P(
    ProblemSolverChecksTest,
    BalanceSpecScopeItemUpperLimitsRequireRelativeUtilVariance) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = facebook::rebalancer::interface::LimitType::ABSOLUTE;
  upperBounds.scopeItemLimits() = {{"h1", 1.5}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "BalanceSpec upperBounds scopeItemLimits are only supported with RELATIVE_UTIL_VARIANCE formula, got LINEAR");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecRejectsBothUpperBoundFields) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  auto& upperBounds = spec.upperBounds().ensure();
  upperBounds.type() = facebook::rebalancer::interface::LimitType::ABSOLUTE;
  upperBounds.globalLimit() = 1;
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  spec.upperBound() = 0.5;
  FOLLY_POP_WARNING

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "BalanceSpec cannot set both deprecated upperBound and upperBounds");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecAcceptsSerializedUpperBoundDefault) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  FOLLY_PUSH_WARNING
  FOLLY_GNU_DISABLE_WARNING("-Wdeprecated-declarations")
  // Python serialization populates the deprecated field with its IDL default.
  // NOLINTNEXTLINE(facebook-hte-Deprecated)
  spec.upperBound() = 1;
  FOLLY_POP_WARNING

  EXPECT_NO_THROW(solver->addGoal(spec));
}

TEST_P(ProblemSolverChecksTest, BalanceSpecLegacySoftUpperBound) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::LEGACY;
  spec.softUpperBound() = 2;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "soft upper bound is not supported by legacy formula");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecLegacyBoundType) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::LEGACY;
  spec.boundType() =
      facebook::rebalancer::interface::BalanceSpecBoundType::ABSOLUTE;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "only relative bound type is supported by legacy formula");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecLegacyDefinition) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::LEGACY;
  spec.definition() =
      facebook::rebalancer::interface::BalanceSpecDefinition::NEW;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "only after definition is supported by legacy formula");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecIgnoreBoundType) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::SQUARES;
  spec.ignoreUpperBoundForIdealWithAbsOrRelBoundTypes() = false;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "ignoreUpperBoundForIdealWithAbsOrRelBoundTypes can only be set to False when using IDEAL formula");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecFixAverageToInitial) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::LEGACY;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "legacy formula must fix average to initial");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecIncludeInInitialAverage) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::LEGACY;
  spec.fixAverageToInitial() = true;
  spec.includeInInitialAverage() = {"h1"};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "include in initial average override is not supported by legacy formula");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecLegacyCapacityPerItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::LEGACY;
  spec.balanceMetric() =
      facebook::rebalancer::interface::BalanceSpecMetric::CAPACITY_PER_ITEM;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "LEGACY formula is not supported with CAPACITY_PER_ITEM metric");
}

TEST_P(ProblemSolverChecksTest, BalanceSpecIdealCapacityPerItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.formula() = facebook::rebalancer::interface::BalanceSpecFormula::IDEAL;
  spec.balanceMetric() =
      facebook::rebalancer::interface::BalanceSpecMetric::CAPACITY_PER_ITEM;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "IDEAL formula is not supported with CAPACITY_PER_ITEM metric");
}

TEST_P(ProblemSolverChecksTest, CapacitySpecUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::CapacitySpec spec;
  spec.scope() = "rack";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, CapacitySpecEmptyDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "";

  solver->addConstraint(spec);
}

TEST_P(ProblemSolverChecksTest, CapacitySpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, CapacitySpecUnknownLimitItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  facebook::rebalancer::interface::Limit limit;
  limit.scopeItemLimits() = {{"h1", 2}, {"h3", 4}};
  spec.limit() = limit;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, CapacitySpecUnexpectedGroupLimits) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  facebook::rebalancer::interface::Limit limit;
  limit.groupLimits() = {{"g1", 1}};
  spec.limit() = limit;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unexpected group limits");
}

TEST_P(ProblemSolverChecksTest, CapacitySpecUnexpectedGroupToScopeItemLimits) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  facebook::rebalancer::interface::Limit limit;
  limit.scopeItemToGroupLimits() = {{"g1", {{"r1", 1}}}};
  spec.limit() = limit;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unexpected group to scope item limits");
}

TEST_P(ProblemSolverChecksTest, MinimizeContainersSpecUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeContainersSpec spec;
  spec.scope() = "rack";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(solver->addGoal(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, MinimizeContainersSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MinimizeContainersSpecInvalidFilter) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.filter()->itemsBlacklist() = {"h7"};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown item h7 in scope host");
}

TEST_P(
    ProblemSolverChecksTest,
    MinimizeContainersSpecRejectsNonPositiveMaxFreeLimit) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  MinimizeContainersSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  MinimizeContainersTarget target;
  target.set_maxFreeLimit(0);
  spec.target() = std::move(target);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "expected maxFreeLimit for MinimizeContainersSpec dimension 'cpu' to be positive but got 0");
}

TEST_P(ProblemSolverChecksTest, ThrottlingSpecUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::ThrottlingSpec spec;
  spec.scope() = "rack";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, ThrottlingSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::ThrottlingSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, ThrottlingSpecInvalidLimit) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::ThrottlingSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";

  facebook::rebalancer::interface::Limit limit;
  limit.scopeItemToGroupLimits() = {{"g1", {{"r1", 1}}}};
  spec.limit() = limit;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unexpected group to scope item limits");
}

TEST_P(ProblemSolverChecksTest, AvoidMovingSpecUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::AvoidMovingSpec spec;
  spec.objects() = {"s1", "s3"};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown shard s3");
}

TEST_P(ProblemSolverChecksTest, OverrideContainerHotnessRanking) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->overrideContainerHotnessRanking({"h1"}),
      "2 != 1, total containers != specified descending hotness override");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->overrideContainerHotnessRanking({"h1", "h1"}),
      "container h1 specified more than once in descending hotness override");
}

TEST_P(ProblemSolverChecksTest, MovesInProgressSpecUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MovesInProgressSpec spec;

  facebook::rebalancer::interface::MoveInProgress move1;
  move1.objName() = "s1";
  move1.toContainer() = "h2";

  facebook::rebalancer::interface::MoveInProgress move2;
  move2.objName() = "s3";
  move2.toContainer() = "h2";

  spec.moves() = {move1, move2};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown shard s3");
}

TEST_P(ProblemSolverChecksTest, MovesInProgressSpecUnknownToContainer) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MovesInProgressSpec spec;

  facebook::rebalancer::interface::MoveInProgress move1;
  move1.objName() = "s1";
  move1.toContainer() = "h2";

  facebook::rebalancer::interface::MoveInProgress move2;
  move2.objName() = "s2";
  move2.toContainer() = "h3";

  spec.moves() = {move1, move2};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, MoveGroupSpecUnknownPartition) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MoveGroupSpec spec;
  spec.partitionName() = "job";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown partition job");
}

TEST_P(ProblemSolverChecksTest, GroupConstraintChecks) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  {
    facebook::rebalancer::interface::GroupCountSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    solver->addConstraint(spec);
  }

  {
    facebook::rebalancer::interface::GroupCountSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "xyz";

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec), "unknown partition xyz");
  }
}

TEST_P(ProblemSolverChecksTest, GroupCountSpecUnknownScopeItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limit()->scopeItemToGroupLimits() = {{"h0", {{"s1", 3}}}};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown item h0 in scope host");
}

TEST_P(ProblemSolverChecksTest, GroupCountSpecMinimumLimitWrongZeroAllowed) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.bound() = GroupCountSpecBound::MIN;
  spec.zeroAllowed() = false;
  spec.minimumLimit() = 3;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "minimumLimit requires zeroAllowed to be true");
}

TEST_P(ProblemSolverChecksTest, GroupCapacityCheck) {
  auto solver = makeInitializedSolver(GetParam());
  const std::map<std::string, std::string> rack_map = {
      {"h1", "rack0"}, {"h2", "rack1"}};

  const std::map<std::string, std::string> job_map = {
      {"s1", "job0"}, {"s2", "job1"}};
  const std::map<std::string, std::string> random_map = {
      {"s1", "whatsapp"}, {"s2", "instagram"}};
  const std::map<std::string, double> jobLimits = {{"job0", 1}, {"job1", 4}};
  const std::map<std::string, double> scopeContributions = {
      {"rack0", 1}, {"rack1", 2}};
  const std::map<std::string, std::map<std::string, double>>
      string2string2valMap = {{"rack0", {}}};
  const std::map<std::string, double> string2valMap = {{"rack0", 1}};
  solver->addPartition("job", job_map);
  solver->addPartition("random", random_map);

  solver->addScope("rack", rack_map);

  auto spec = getDefaultGroupCapacitySpec();
  solver->addConstraint(spec);
  spec.partitionName() = "wrongPartition";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown partition wrongPartition");

  spec = getDefaultGroupCapacitySpec();
  spec.scope() = "norack";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope norack");

  spec = getDefaultGroupCapacitySpec();
  spec.contributionPartition() = "wrongPartition";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown partition wrongPartition");

  // Test bundleConfig with wrong utilType (only STEP_MOD_K is supported)
  spec = getDefaultGroupCapacitySpec();
  spec.utilType() = GroupCapacitySpecUtilType::LINEAR;
  spec.bundleConfig().emplace();
  spec.bundleConfig()->type() = LimitType::ABSOLUTE;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "bundleConfig is only supported for GroupCapacitySpecUtilType::STEP_MOD_K");

  // Test bundleConfig with groupLimits (only scopeItem limits are supported)
  spec = getDefaultGroupCapacitySpec();
  spec.bundleConfig().emplace();
  spec.utilType() = GroupCapacitySpecUtilType::STEP_MOD_K;
  spec.bundleConfig()->type() = LimitType::ABSOLUTE;
  spec.bundleConfig()->groupLimits() = {{"job0", 1}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "only scopeItem limits are supported");

  // Test bundleConfig with scopeItemToGroupLimits (only scopeItem limits are
  // supported)
  spec = getDefaultGroupCapacitySpec();
  spec.bundleConfig().emplace();
  spec.utilType() = GroupCapacitySpecUtilType::STEP_MOD_K;
  spec.bundleConfig()->type() = LimitType::ABSOLUTE;
  spec.bundleConfig()->scopeItemToGroupLimits() = {{"rack0", {{"job0", 1}}}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "only scopeItem limits are supported");

  // Test bundleConfig with non-positive globalLimit (positive integers
  // required)
  spec = getDefaultGroupCapacitySpec();
  spec.bundleConfig().emplace();
  spec.utilType() = GroupCapacitySpecUtilType::STEP_MOD_K;
  spec.bundleConfig()->type() = LimitType::RELATIVE;
  spec.bundleConfig()->globalLimit() = 1;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "only absolute limits supported");

  // Test bundleConfig with non-positive globalLimit (positive integers
  // required)
  spec = getDefaultGroupCapacitySpec();
  spec.name() = "test";
  spec.bundleConfig().emplace();
  spec.utilType() = GroupCapacitySpecUtilType::STEP_MOD_K;
  spec.bundleConfig()->type() = LimitType::ABSOLUTE;
  spec.bundleConfig()->globalLimit() = 0;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "value 0 of test's bundleConfig global limit is not a positive integer");

  // Test bundleConfig with non-positive scopeItemLimits (positive integers
  // required)
  spec = getDefaultGroupCapacitySpec();
  spec.name() = "test";
  spec.bundleConfig().emplace();
  spec.bundleConfig()->type() = LimitType::ABSOLUTE;
  spec.utilType() = GroupCapacitySpecUtilType::STEP_MOD_K;
  spec.bundleConfig()->globalLimit() = 2.1;
  spec.bundleConfig()->scopeItemLimits() = {{"rack0", 2.5}, {"rack1", 3.5}};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "value 2.1 of test's bundleConfig global limit is not a positive integer");

  spec = getDefaultGroupCapacitySpec();
  spec.name() = "test";
  spec.bundleConfig().emplace();
  spec.bundleConfig()->type() = LimitType::ABSOLUTE;
  spec.utilType() = GroupCapacitySpecUtilType::STEP_MOD_K;
  spec.bundleConfig()->globalLimit() = 2;
  spec.bundleConfig()->scopeItemLimits() = {{"rack0", 2.5}, {"rack1", 3.5}};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "value 2.5 of test's bundleConfig scopeItem limit is not a positive integer");
}

TEST_P(ProblemSolverChecksTest, DuplicateSpecNames) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";
  balanceSpec.name() = "xyz";

  solver->addGoal(balanceSpec);

  facebook::rebalancer::interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.name() = "xyz";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(groupCountSpec), "duplicate spec name xyz");
}

TEST_P(ProblemSolverChecksTest, EmptyBipartiteSwapSubset) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::BipartiteSwapsSpec spec;
  spec.subsetContainers() = {};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "subsetContainers not set in BipartiteSwapsSpec"
      " so cannot determine left and right bipartite");
}

TEST_P(ProblemSolverChecksTest, MinimizeNthLargestUtilizationNegativeN) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::MinimizeNthLargestUtilizationSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.n() = -1;
  spec.name() = "xyz";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "MinimizeNthLargestUtilizationSpec::n must be non-negative");
}

TEST_P(
    ProblemSolverChecksTest,
    MinimizeNthLargestUtilizationNegativeTargetUtilization) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::MinimizeNthLargestUtilizationSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.n() = 0;
  spec.targetUtilization() = -0.1;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "MinimizeNthLargestUtilizationSpec::targetUtilization must be non-negative");
}

TEST_P(ProblemSolverChecksTest, MinimizeNthLargestUtilizationUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeNthLargestUtilizationSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";
  spec.n() = 0;
  spec.name() = "xyz";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, AddSameDimensionTwice) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension(
      "cpu", std::unordered_map<std::string, double>{{"s1", 10}, {"s2", 20}});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectDimension(
          "cpu",
          std::unordered_map<std::string, double>{{"s1", 100}, {"s2", 200}}),
      "dimension cpu already defined on shard");
}

TEST_P(ProblemSolverChecksTest, AddObjectDimensionBeforeSettingObjectName) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectDimension(
          "cpu",
          std::unordered_map<std::string, double>{{"s1", 10}, {"s2", 20}}),
      "object name not set");
}

TEST_P(
    ProblemSolverChecksTest,
    AddContainerDimensionBeforeSettingContainerName) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addContainerDimension(
          "cpu", std::map<std::string, double>{{"h1", 10}, {"h2", 20}}),
      "container name not set");
}

TEST_P(ProblemSolverChecksTest, AddDimensionOnUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScopeDimension(
          "cpu", "rack", std::map<std::string, double>{{"r1", 10}, {"r2", 20}}),
      "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, GroupCountSpecEmptyDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "";

  solver->addGoal(groupCountSpec);
}

TEST_P(ProblemSolverChecksTest, GroupCountSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(groupCountSpec), "unknown dimension network");
}

TEST_P(
    ProblemSolverChecksTest,
    GroupCountSpecIncompatibleRelativeToScopeItemUtil) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::GroupCountSpec groupCountSpec;
  groupCountSpec.scope() = "host";
  groupCountSpec.partitionName() = "job";
  groupCountSpec.zeroAllowed() = true;
  groupCountSpec.limitRelativeTo() = facebook::rebalancer::interface::
      GroupCountSpecLimitRelativeTo::SCOPE_ITEM_UTIL;
  groupCountSpec.bound() =
      facebook::rebalancer::interface::GroupCountSpecBound::MIN;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(groupCountSpec),
      "limitRelativeTo=SCOPE_ITEM_UTIL is not compatible with bound=MIN and "
      "zeroAllowed=true");
}

TEST_P(ProblemSolverChecksTest, AggregatedGroupSpecEmptyDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::AggregatedGroupSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "";

  solver->addGoal(spec);
}

TEST_P(ProblemSolverChecksTest, AggregatedGroupSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::AggregatedGroupSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, ExclusiveScopeItemsSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::ExclusiveScopeItemsSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MinimizeMovementSpecUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeMovementSpec spec;
  spec.scope() = "rack";
  spec.dimension() = "network";
  spec.name() = "xyz";

  REBALANCER_EXPECT_RUNTIME_ERROR(solver->addGoal(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, MinimizeMovementSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeMovementSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";
  spec.name() = "xyz";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MinimizeMovementSpecNegativeAllowance) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeMovementSpec spec;
  spec.scope() = "host";
  spec.allowance() = -1;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "MinimizeMovementSpec::allowance must be non-negative");
}

TEST_P(
    ProblemSolverChecksTest,
    MinimizeMovementSpecAllowanceWithNormalization) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeMovementSpec spec;
  spec.scope() = "host";
  spec.allowance() = 10;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "Non-zero values of MinimizeMovementSpec::allowance are not supported "
      "when MinimizeMovementSpec::doNotNormalize is set to false");
}

TEST_P(ProblemSolverChecksTest, ScopeAffinitiesSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::ScopeAffinitiesSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MinimizeSquaresSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MinimizeSquaresSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MinimizeSquaresSpecFilter) {
  auto solver = makeInitializedSolver(GetParam());

  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::MinimizeSquaresSpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  spec.filter()->itemsBlacklist() = {"abc"};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown item abc in scope host");
}

TEST_P(ProblemSolverChecksTest, CapacityRatioSpecEmptyDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::CapacityRatioSpec spec;
  spec.scope() = "host";
  spec.dimension() = "";

  solver->addGoal(spec);
}

TEST_P(ProblemSolverChecksTest, CapacityRatioSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::CapacityRatioSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MaximizeAllocationSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::MaximizeAllocationSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(
    ProblemSolverChecksTest,
    MaximizeFreeCapacityUnitsSpecRejectsInvalidUnitSizes) {
  auto makeSolver = [&]() {
    auto solver = makeInitializedSolver(GetParam());
    solver->addObjectDimension(
        "cpu",
        /*objectToValue=*/std::map<std::string, double>{},
        /*defaultValue=*/1);
    solver->addContainerDimension(
        "cpu",
        /*containerToValue=*/std::map<std::string, double>{},
        /*defaultValue=*/8);
    return solver;
  };
  auto makeSpec = []() {
    MaximizeFreeCapacityUnitsSpec spec;
    spec.scope() = "host";
    spec.dimension() = "cpu";
    spec.unitSize()->type() = LimitType::ABSOLUTE;
    spec.unitSize()->globalLimit() = 4;
    return spec;
  };

  {
    auto solver = makeSolver();
    auto spec = makeSpec();
    spec.unitSize()->type() = LimitType::RELATIVE;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec),
        "expected limit of type ABSOLUTE, but got limit of type RELATIVE");
  }
  {
    auto solver = makeSolver();
    auto spec = makeSpec();
    spec.unitSize()->globalLimit() = 0;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec),
        "expected global limit value to be positive but got 0");
  }
  {
    auto solver = makeSolver();
    auto spec = makeSpec();
    spec.unitSize()->isDefaultLimitUnbounded() = true;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec),
        "MaximizeFreeCapacityUnitsSpec unit sizes must be bounded");
  }
  {
    auto solver = makeSolver();
    auto spec = makeSpec();
    spec.unitSize()->scopeItemLimits() = {{"h1", 0}};
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec),
        "expected limit for scope item 'h1' to be positive but got 0");
  }
  {
    auto solver = makeSolver();
    auto spec = makeSpec();
    spec.unitSize()->scopeItemLimits() = {{"unknown", 4}};
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec), "unknown item unknown in scope host");
  }
}

TEST_P(ProblemSolverChecksTest, WorkingSetSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::WorkingSetSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, UtilIncreaseCostSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::UtilIncreaseCostSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(
    ProblemSolverChecksTest,
    RasRebalancingMovementSpecUnknownStayedDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("network", std::map<std::string, double>{});

  facebook::rebalancer::interface::RasRebalancingMovementSpec spec;
  spec.scope() = "host";
  spec.stayedDimension() = "cpu";
  spec.incomingDimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension cpu");
}

TEST_P(
    ProblemSolverChecksTest,
    RasRebalancingMovementSpecUnknownIncomingDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::RasRebalancingMovementSpec spec;
  spec.scope() = "host";
  spec.stayedDimension() = "cpu";
  spec.incomingDimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, CapacityWithSupplyAndDrSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::CapacityWithSupplyAndDrSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";
  spec.prodScope() = "host";
  spec.prodItem() = "h1";
  spec.partitionName() = "job";
  spec.supplyPartition() = "job";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, FlowSpecEmptyDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::FlowSpec spec;
  spec.scope() = "host";
  spec.dimension() = "";

  solver->addGoal(spec);
}

TEST_P(ProblemSolverChecksTest, FlowSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::FlowSpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, DrainCapacitySpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::DrainCapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "network";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, enableMoveStats) {
  auto solver = makeInitializedSolver(GetParam());
  MoveStatsSpec spec;
  spec.trackObjectsWhitelist() = {"fake_host"};
  EXPECT_NO_THROW(solver->enableMoveStats(spec));
  spec.trackObjects() = true;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->enableMoveStats(spec), "unknown shard fake_host");
  spec.trackObjects() = false;
  spec.printSourceContainersWhitelist() = {"fake_container"};
  EXPECT_NO_THROW(solver->enableMoveStats(spec));
  spec.trackContainers() = true;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->enableMoveStats(spec),
      "unknown item fake_container in scope host");
}

TEST_P(ProblemSolverChecksTest, AvoidAssignmentsSpecUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::AvoidAssignmentsSpec spec;
  spec.scope() = "job";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope job");
}

TEST_P(ProblemSolverChecksTest, AvoidAssignmentsSpecUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::AvoidAssignmentsSpec spec;
  spec.scope() = "host";

  facebook::rebalancer::interface::AvoidAssignment avoidSpec;
  avoidSpec.object() = "s3";
  avoidSpec.scopeItems() = {"h1"};
  spec.assignments() = {avoidSpec};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown shard s3");
}

TEST_P(ProblemSolverChecksTest, AvoidAssignmentsSpecUnknownScopeItem) {
  auto solver = makeInitializedSolver(GetParam());

  facebook::rebalancer::interface::AvoidAssignmentsSpec spec;
  spec.scope() = "host";

  facebook::rebalancer::interface::AvoidAssignment avoidSpec;
  avoidSpec.object() = "s1";
  avoidSpec.scopeItems() = {"h3"};
  spec.assignments() = {avoidSpec};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, EmptyName) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setObjectName(""), "entity names must not be empty");
}

TEST_P(ProblemSolverChecksTest, NameReuseBetweenObjectAndContainer) {
  auto solver =
      initializeTestProblemSolver({.executorThreadCount = GetParam()});
  solver->setObjectName("xyz");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->setContainerName("xyz"), "name xyz already used as object name");
}

TEST_P(ProblemSolverChecksTest, NameReuseBetweenObjectAndScope) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addScope("shard", std::map<std::string, std::string>{}),
      "name shard already used as object name");
}

TEST_P(ProblemSolverChecksTest, NameReuseBetweenObjectAndDimension) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addContainerDimension("shard", std::map<std::string, double>{}),
      "name shard already used as object name");
}

TEST_P(ProblemSolverChecksTest, NameReuseBetweenObjectAndPartition) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "shard",
          folly::F14FastMap<std::string, std::string>{{"s1", "group1"}}),
      "name shard already used as object name");
}

TEST_P(ProblemSolverChecksTest, NameReuseBetweenContainerAndPartition) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "host",
          std::vector<std::pair<std::string, std::string>>{{"s1", "group1"}}),
      "name host already used as scope name");
}

TEST_P(ProblemSolverChecksTest, NameReuseBetweenDimensionAndPartition) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("dim", std::map<std::string, double>{});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addPartition(
          "dim",
          std::vector<std::pair<std::string, std::string>>{{"s1", "group1"}}),
      "name dim already used as dimension name");
}

TEST_P(ProblemSolverChecksTest, ExclusiveGroupsUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition("job", std::map<std::string, std::string>{});
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  ExclusiveGroupsSpec spec;
  spec.scope() = "rack";
  spec.partitionName() = "job";
  spec.dimension() = "cpu";
  REBALANCER_EXPECT_RUNTIME_ERROR(solver->addGoal(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, ExclusiveGroupsUnknownPartition) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  ExclusiveGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "cpu";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown partition job");
}

TEST_P(ProblemSolverChecksTest, ExclusiveGroupsUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition("job", std::map<std::string, std::string>{});
  ExclusiveGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "cpu";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension cpu");
}

TEST_P(ProblemSolverChecksTest, ExclusiveGroupsDuplicateName) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition("job", std::map<std::string, std::string>{});
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  ExclusiveGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "cpu";
  spec.name() = "xyz";
  solver->addGoal(spec);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "duplicate spec name xyz");
}

TEST_P(ProblemSolverChecksTest, DynamicObjectDimensionUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addDynamicObjectDimension(
          "cpu",
          "rack",
          folly::F14FastMap<
              std::string,
              folly::F14FastMap<std::string, double>>{}),
      "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, DynamicObjectDimensionUnknownScopeItem) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addDynamicObjectDimension(
          "cpu",
          "host",
          folly::
              F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
                  {"h3", {{"s1", 3}}}}),
      "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, DynamicObjectDimensionUnknownObject) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addDynamicObjectDimension(
          "cpu",
          "host",
          folly::
              F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
                  {"h1", {{"s3", 3}}}}),
      "unknown shard s3");
}

TEST_P(ProblemSolverChecksTest, RedefineObjectDimensionAsDynamic) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addDynamicObjectDimension(
          "cpu",
          "host",
          folly::F14FastMap<
              std::string,
              folly::F14FastMap<std::string, double>>{}),
      "dimension cpu already defined on shard");
}

TEST_P(ProblemSolverChecksTest, RedefineObjectDimensionAsNonDynamic) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addDynamicObjectDimension(
      "cpu",
      "host",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectDimension("cpu", std::map<std::string, double>{}),
      "dimension cpu already defined on shard");
}

TEST_P(
    ProblemSolverChecksTest,
    DynamicObjectDimensionUnknownPartitionWithGroupedValue) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addDynamicObjectDimension(
          "cpu",
          "host",
          "job",
          folly::
              F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
                  {"h1", {{"j0", 3}}}}),
      "unknown partition job");
}

TEST_P(
    ProblemSolverChecksTest,
    DynamicObjectDimensionUnknownGroupWithGroupedValue) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addDynamicObjectDimension(
          "cpu",
          "host",
          "job",
          folly::
              F14FastMap<std::string, folly::F14FastMap<std::string, double>>{
                  {"h1", {{"j3", 3}}}}),
      "unknown group j3 in partition job");
}

TEST_P(ProblemSolverChecksTest, AssignmentAffinitiesUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  AssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.dimension() = "affinity";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension affinity");
}

TEST_P(ProblemSolverChecksTest, AssignmentAffinitiesMultipleSources) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addDynamicObjectDimension(
      "affinity",
      "host",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{});

  AssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.dimension() = "affinity";
  spec.affinities() = {makeAssignmentAffinity("s1", "h1", 1)};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "AssignmentAffinitiesSpec cannot specify both affinities and a dimension");
}

TEST_P(ProblemSolverChecksTest, AssignmentAffinitiesDimensionOnOtherScope) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addScope(
      "rack",
      std::unordered_map<std::string, std::string>{{"h1", "r1"}, {"h2", "r2"}});
  solver->addDynamicObjectDimension(
      "affinity",
      "host",
      folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>{});

  AssignmentAffinitiesSpec spec;
  spec.scope() = "rack";
  spec.dimension() = "affinity";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec),
      "AssignmentAffinitiesSpec dimension 'affinity' must use the same scope 'rack' as the spec");
}

TEST_P(ProblemSolverChecksTest, GroupAssignmentAffinitiesUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  GroupAssignmentAffinitiesSpec spec;
  spec.scope() = "rack";
  REBALANCER_EXPECT_RUNTIME_ERROR(solver->addGoal(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, GroupAssignmentAffinitiesUnknownPartition) {
  auto solver = makeInitializedSolver(GetParam());
  GroupAssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown partition job");
}

TEST_P(ProblemSolverChecksTest, GroupAssignmentAffinitiesUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  GroupAssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown dimension cpu");
}

TEST_P(ProblemSolverChecksTest, GroupAssignmentAffinitiesUnknownGroup) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  GroupAssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  GroupScopeItemAffinity affinity;
  affinity.group() = "j3";
  spec.affinities() = {affinity};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown group j3 in partition job");
}

TEST_P(ProblemSolverChecksTest, GroupAssignmentAffinitiesUnknownScopeItem) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  GroupAssignmentAffinitiesSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  GroupScopeItemAffinity affinity;
  affinity.group() = "j1";
  affinity.scopeItem() = "h3";
  spec.affinities() = {affinity};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, NonAcceptingDuplicateName) {
  auto solver = makeInitializedSolver(GetParam());

  NonAcceptingSpec spec;
  spec.name() = "abc";
  spec.scope() = "host";
  spec.items() = {"h1"};

  solver->addConstraint(spec);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "duplicate spec name abc");
}

TEST_P(ProblemSolverChecksTest, NonAcceptingUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  NonAcceptingSpec spec;
  spec.scope() = "rack";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, NonAcceptingUnknownScopeItem) {
  auto solver = makeInitializedSolver(GetParam());

  NonAcceptingSpec spec;
  spec.scope() = "host";
  spec.items() = {"h3"};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, SumOfMaxEmptyDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  SumOfMaxSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";

  REBALANCER_EXPECT_RUNTIME_ERROR(solver->addGoal(spec), "unknown dimension ");
}

TEST_P(ProblemSolverChecksTest, RoutingLatencyUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "rack";
  spec.routingConfigName() = "routingConfig1";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, RoutingLatencyPercentileNotSet) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.routingConfigName() = "routingConfig1";
  spec.latencyMetric()->type() = RoutingLatencyMetric::PERCENTILE;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "Expected 'latencyMetric.Percentile' to be set when metric type is RoutingLatencyMetric.PERCENTILE");
}

TEST_P(ProblemSolverChecksTest, RoutingLatencyOldConfig) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.routingConfigName() = "routingConfig1";
  spec.metric() = RoutingLatencyMetric::MAX;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "The field 'metric' in RoutingLatencySpec is deprecated; use `RoutingLatencySpec.latencyMetric` instead");
}

TEST_P(ProblemSolverChecksTest, RoutingLatencyPercentileInvalidRange) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.routingConfigName() = "routingConfig1";
  spec.latencyMetric()->type() = RoutingLatencyMetric::PERCENTILE;
  spec.latencyMetric()->percentile() = 0;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "Expected 'latencyMetric.Percentile' to be in the range (0, 100], but got 0");
}

TEST_P(ProblemSolverChecksTest, RoutingLatencyUnknownPartition) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "host";
  spec.partition() = "jobs";
  spec.routingConfigName() = "routingConfig1";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown partition jobs");
}

TEST_P(ProblemSolverChecksTest, RoutingLatencyUnknownRoutingConfig) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.routingConfigName() = "routingConfig11";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "Routing config 'routingConfig11' not found. Add it using addRoutingConfig(...)");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "rack",
          "job",
          {},
          {{"h1", {{"h2", 10}}}},
          std::nullopt),
      "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownPartition) {
  auto solver = makeInitializedSolver(GetParam());
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1", "host", "job", {}, {{"h1", {{"h2", 10}}}}, {}),
      "unknown partition job");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownGroup) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j0", rings}},
          {{"h1", {{"h2", 10}}}},
          std::nullopt),
      "unknown group j0 in partition job");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownOrigin) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h0";
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h0", 10}}}},
          std::nullopt),
      "unknown item h0 in scope host");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigDuplicateOrigin) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    ring.destinationScopeItemSets() = {};
    rings.routingRings()->push_back(ring);
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h2", 10}}}},
          std::nullopt),
      "duplicate origin scope item h1");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigNegativeTraffic) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      std::vector<std::pair<std::string, std::string>>{
          {"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    ring.originTraffic() = -1;
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h2", 10}}}},
          std::nullopt),
      "origin traffic from h1 is -1 but it must be non-negative");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownDestination) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    ring.originTraffic() = 10;
    auto destinationScopeItemSets =
        std::vector<std::vector<std::string>>{{"h0"}};
    ring.destinationScopeItemSets() = destinationScopeItemSets;
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h0", 10}}}},
          std::nullopt),
      "unknown item h0 in scope host");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigDuplicateDestination) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    ring.originTraffic() = 10;
    auto destinationScopeItemSets =
        std::vector<std::vector<std::string>>{{"h1", "h1"}};
    ring.destinationScopeItemSets() = destinationScopeItemSets;
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h2", 10}}}},
          std::nullopt),
      "duplicate destination scope item h1");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownLatencyOrigin) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {},
          {{"h0", {{"h1", 10}}}},
          std::nullopt),
      "unknown item h0 in scope host");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigUnknownLatencyDestination) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {},
          {{"h1", {{"h0", 10}}}},
          std::nullopt),
      "unknown item h0 in scope host");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigNegativeLatency) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {},
          {{"h1", {{"h2", -10}}}},
          std::nullopt),
      "latency from h1 to h2 is -10 but it must be non-negative");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigDuplicateError) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {},
          {{"h2", {{"h1", 10}}}},
          std::nullopt),
      "Routing config 'routingConfig1' previously added");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigNoDefaultDestinationSet) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h2", 10}}}},
          std::nullopt),
      "origin scope item 'h1' is specified for routingRing w.r.t. group 'j1', but routinRing has neither destinationScopeItemSets defined nor is there a default destinationScopeItemSets given for this origin scope item");
}

TEST_P(
    ProblemSolverChecksTest,
    RoutingConfigDefaultDestinationSetDoesNotContainOrigin) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h2", 10}}}},
          std::make_optional<std::unordered_map<
              std::string,
              std::vector<std::vector<std::string>>>>({{"h2", {{"h1"}}}})),
      "origin scope item 'h1' is specified for routingRing w.r.t. group 'j1', but routinRing has neither destinationScopeItemSets defined nor is there a default destinationScopeItemSets given for this origin scope item");
}

TEST_P(ProblemSolverChecksTest, RoutingConfigDuplicateDefaultDestinations) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupRoutingRings rings;
  {
    RoutingRing ring;
    ring.originScopeItem() = "h1";
    rings.routingRings()->push_back(ring);
  }

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addRoutingConfig(
          "routingConfig1",
          "host",
          "job",
          {{"j1", rings}},
          {{"h1", {{"h2", 10}}}},
          std::make_optional<std::unordered_map<
              std::string,
              std::vector<std::vector<std::string>>>>(
              {{"h1", {{"h2", "h2"}}}})),
      "duplicate destination scope item h2");
}

TEST_P(ProblemSolverChecksTest, GroupDiversityUnknownScope) {
  auto solver = makeInitializedSolver(GetParam());

  GroupDiversitySpec spec;
  spec.scope() = "rack";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown scope rack");
}

TEST_P(ProblemSolverChecksTest, GroupDiversityUnknownPartition) {
  auto solver = makeInitializedSolver(GetParam());

  GroupDiversitySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown partition job");
}

TEST_P(ProblemSolverChecksTest, GroupDiversityUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job",
      folly::F14FastMap<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupDiversitySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension cpu");
}

TEST_P(ProblemSolverChecksTest, GroupDiversityUnknownHostInLimit) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupDiversitySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  spec.limit()->scopeItemLimits() = {{"h7", 3}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown item h7 in scope host");
}

TEST_P(ProblemSolverChecksTest, GroupDiversityUnexpectedGroupLimits) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  GroupDiversitySpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  spec.limit()->groupLimits() = {{"j1", 3}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unexpected group limits");
}

TEST_P(ProblemSolverChecksTest, CapacityWithGroupPresenceUnexpectedWeights) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  {
    CapacityWithGroupPresenceSpec spec;
    spec.scope() = "host";
    spec.partition() = "job";
    spec.dimension() = "cpu";
    spec.groupToPresenceWeight()->type() = LimitType::RELATIVE;

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec),
        "expected limit of type ABSOLUTE, but got limit of type RELATIVE");
  }

  {
    CapacityWithGroupPresenceSpec spec;
    spec.scope() = "host";
    spec.partition() = "job";
    spec.dimension() = "cpu";
    spec.groupToPresenceWeight()->type() = LimitType::ABSOLUTE;
    spec.groupToPresenceWeight()->globalLimit() = -1.0;

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec),
        "expected global limit value to be non-negative but got -1");
  }

  {
    CapacityWithGroupPresenceSpec spec;
    spec.scope() = "host";
    spec.partition() = "job";
    spec.dimension() = "cpu";
    spec.groupToPresenceWeight()->type() = LimitType::ABSOLUTE;
    spec.groupToPresenceWeight()->globalLimit() = 0.0;
    spec.groupToPresenceWeight()->groupLimits() = {{"j1", 1.0}, {"j2", -2.0}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec),
        "expected limit for group 'j2' to be non-negative but got -2");
  }
}

TEST_P(
    ProblemSolverChecksTest,
    CapacityWithGroupPresenceNegativeGroupUtilMultiplier) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  CapacityWithGroupPresenceSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  GroupUtilMultiplier multiplier;
  multiplier.target() = GroupUtilMultiplierTarget::COMMON;
  multiplier.value() = Limit{};
  multiplier.value()->type() = LimitType::ABSOLUTE;
  multiplier.value()->globalLimit() = -1.0;
  spec.groupUtilMultipliers()->push_back(std::move(multiplier));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "expected global limit value to be non-negative but got -1");
}

TEST_P(ProblemSolverChecksTest, CapacityWithGroupPresenceCheckInvalidPenalty) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  CapacityWithGroupPresenceSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  spec.groupToExtraAdditivePenalty()->type() = LimitType::RELATIVE;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "expected limit of type ABSOLUTE, but got limit of type RELATIVE");
}

TEST_P(ProblemSolverChecksTest, CapacityWithGroupPresenceCheckValidPenalty2) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  CapacityWithGroupPresenceSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  spec.groupToExtraAdditivePenalty()->globalLimit() = 2.0;
  spec.groupToExtraAdditivePenalty()->groupLimits() = {
      {"j1", 1.0}, {"j42", -2.0}};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown group j42 in partition job");
}

TEST_P(ProblemSolverChecksTest, CapacityWithGroupPresenceCheckValidPenalty) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  CapacityWithGroupPresenceSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";
  spec.groupToExtraAdditivePenalty()->globalLimit() = 2.0;
  spec.groupToExtraAdditivePenalty()->groupLimits() = {
      {"j1", 1.0}, {"j2", -2.0}};
  spec.groupToExtraAdditivePenalty()->scopeItemToGroupLimits() = {
      {"h1", {{"j1", -10.0}, {"j2", 24.0}}}};

  // should not throw any error even with negative penalty values
  solver->addConstraint(spec);
}

TEST_P(ProblemSolverChecksTest, DiversifyWithinScopeItemSpecExistenceCheck) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  {
    DiversifyWithinScopeItemSpec spec;
    spec.scope() = "host1";
    spec.partition() = "job";
    spec.dimension() = "cpu";
    spec.groupToLimit()->type() = LimitType::RELATIVE;
    spec.groupToLimit()->groupLimits() = {{"j1", 1.0}, {"j2", 1.0}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec), "unknown scope host1");
  }

  {
    DiversifyWithinScopeItemSpec spec;
    spec.scope() = "host";
    spec.partition() = "job1";
    spec.dimension() = "cpu";
    spec.groupToLimit()->type() = LimitType::RELATIVE;
    spec.groupToLimit()->groupLimits() = {{"j1", 1.0}, {"j2", 1.0}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec), "unknown partition job1");
  }

  {
    DiversifyWithinScopeItemSpec spec;
    spec.scope() = "host";
    spec.partition() = "job";
    spec.dimension() = "cpu1";
    spec.groupToLimit()->type() = LimitType::RELATIVE;
    spec.groupToLimit()->groupLimits() = {{"j1", 1.0}, {"j2", 1.0}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec), "unknown dimension cpu1");
  }
}

TEST_P(ProblemSolverChecksTest, DiversifyWithinScopeItemSpecNegativeLimits) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  {
    DiversifyWithinScopeItemSpec spec;
    spec.scope() = "host";
    spec.partition() = "job";
    spec.dimension() = "cpu";
    spec.groupToLimit()->type() = LimitType::RELATIVE;
    spec.groupToLimit()->groupLimits() = {{"j1", 1.0}, {"j2", -2.0}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addGoal(spec),
        "expected limit for group 'j2' to be non-negative but got -2");
  }
}

TEST_P(
    ProblemSolverChecksTest,
    ObjectPartitionRoutingDimensionDuplicateDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::make_optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>({}));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectPartitionRoutingDimension(
          "cpu", "job", "routingConfig1", {}),
      "dimension cpu already defined on shard");
}

TEST_P(ProblemSolverChecksTest, ObjectPartitionRoutingDimensionInvalidGroup) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::make_optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>({}));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectPartitionRoutingDimension(
          "cpu", "job", "routingConfig1", {{"j3", 4}}),
      "unknown group j3 in partition job");
}

TEST_P(
    ProblemSolverChecksTest,
    ObjectPartitionRoutingDimensionInvalidGroupInStaticValueMap) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::make_optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>({}));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addObjectPartitionRoutingDimension(
          "cpu", "job", "routingConfig1", {{"j2", 4}}, {{"j3", 6}}),
      "unknown group j3 in partition job");
}

TEST_P(
    ProblemSolverChecksTest,
    GroupCountRelativeToGroupToScopeItemRoutingTraffic) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  facebook::rebalancer::interface::GroupCountSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.limitRelativeTo() =
      GroupCountSpecLimitRelativeTo::GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec),
      "routingConfigForLimit parameter should be set when using GroupCountSpecLimitRelativeTo::GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC");
}

TEST_P(ProblemSolverChecksTest, UsingGroupFilterWhenScopeItemExpected) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension("cpu", std::map<std::string, double>{});

  facebook::rebalancer::interface::BalanceSpec balanceSpec;
  balanceSpec.scope() = "host";
  balanceSpec.dimension() = "cpu";

  // it does not make sense to use Group filter with BalanceSpec
  facebook::rebalancer::interface::Filter filter;
  filter.type() = facebook::rebalancer::interface::FilterType::GROUP;
  balanceSpec.filter() = filter;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(balanceSpec),
      "Expected filter to be of type FilterType::SCOPE_ITEM");
}

TEST_P(ProblemSolverChecksTest, UsingScopeItemFilterWhenGroupExpected) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  solver->addRoutingConfig(
      "routingConfig1",
      "host",
      "job",
      {},
      {{"h1", {{"h2", 10}}}},
      std::nullopt);

  RoutingLatencySpec spec;
  spec.scope() = "host";
  spec.routingConfigName() = "routingConfig1";
  spec.partition() = "job";

  // the default filter is of type SCOPE_ITEM; it does not make sense to use
  // SCOPE_ITEM filter with RoutingLatencySpec
  facebook::rebalancer::interface::Filter filter;
  spec.filter() = filter;

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addGoal(spec), "Expected filter to be of type FilterType::GROUP");
}
TEST_P(ProblemSolverChecksTest, ColocateGroupsSpecIncorrectLimit) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  {
    facebook::rebalancer::interface::ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";

    // the default value of limit type is RELATIVE and it does not make sense
    // to use RELATIVE limit for ColocateGroupsSpec
    Limit limit;
    limit.globalLimit() = 2.0;
    spec.limits() = limit;

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec),
        "expected limit of type ABSOLUTE, but got limit of type RELATIVE");
  }

  {
    facebook::rebalancer::interface::ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    // it does not make sense to use scopeItemLimits in ColocateGroupSpec
    spec.limits()->scopeItemLimits() = {{"h1", 2}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec),
        "unexpected scopeItemLimits found in Limit struct; expected only groupLimits to be set");
  }

  {
    facebook::rebalancer::interface::ColocateGroupsSpec spec;
    spec.scope() = "host";
    spec.partitionName() = "job";
    // it does not make sense to use scopeItemToGroupLimits in
    // ColocateGroupSpec
    spec.limits()->scopeItemToGroupLimits() = {
        {"h1", std::map<std::string, double>{{"j1", 2.0}}},
    };

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addConstraint(spec),
        "unexpected scopeItemToGroupLimits found in Limit struct; expected only groupLimits to be set");
  }
}

TEST_P(ProblemSolverChecksTest, ColocateGroupsSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  facebook::rebalancer::interface::ColocateGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.dimension() = "network";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, ColocateGroupsSpecUknownGroupWeight) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  facebook::rebalancer::interface::ColocateGroupsSpec spec;
  spec.scope() = "host";
  spec.partitionName() = "job";
  spec.groupToWeight() = {{"j1", 2.0}, {"j22", -2.0}};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown group j22 in partition job");
}

TEST_P(ProblemSolverChecksTest, MoveToCurrentScopeItemSpecIncorrectScopeTest) {
  auto makeMoveToCurrentScopeItemSpec = [](auto& scopeName) {
    MoveToCurrentScopeItemSpec moveToCurrentScopeItemSpec;
    moveToCurrentScopeItemSpec.scopeNameForExploringMovesToCurrentScopeItem() =
        scopeName;
    DestinationsToExploreOptions destinations;
    destinations.set_moveToCurrentScopeItem() = moveToCurrentScopeItemSpec;

    return destinations;
  };
  auto makeSingleRandomSpec = [&](auto& scopeName) {
    SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
    singleRandomSpec.destinationsToExplore() =
        makeMoveToCurrentScopeItemSpec(scopeName);
    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = 1;
    singleRandomSpec.stratifiedSampleSize() = sampleSize;

    return singleRandomSpec;
  };

  auto solver = makeInitializedSolver(GetParam());
  {
    // test localSearchSolverSpec error
    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(makeSingleRandomSpec("wrongScope")));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown scope wrongScope");
  }

  {
    // test localSearchStageSolverSpec with 5 stages; stage 1 has a correct
    // scope name, while stage 4 has an incorrect scope name
    LocalSearchStageSolverSpec stageSolverSpec;
    for (const auto i : folly::irange(5)) {
      LocalSearchStageSpec stageSpec;
      stageSpec.begin() = 0;
      stageSpec.end() = 1;
      // add incorrect scope name to stage 4
      auto scope = i == 3 ? "wrongScopeStage4" : "rack";
      stageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec(makeSingleRandomSpec(scope)));
      stageSpec.solverSpec()->exploreMovesFromContainersNotInObjective() =
          false;
      stageSolverSpec.stageSpecs()->push_back(stageSpec);
    }

    solver->addScope(
        "rack",
        folly::F14FastMap<std::string, std::string>{
            {"h1", "r1"}, {"h2", "r2"}});

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(stageSolverSpec), "unknown scope wrongScopeStage4");
  }
}

TEST_P(ProblemSolverChecksTest, GroupMoveLimitSpecUnknownDimension) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
  facebook::rebalancer::interface::GroupMoveLimitSpec spec;
  spec.partitionName() = "job";
  spec.dimension() = "network";
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(spec), "unknown dimension network");
}

TEST_P(ProblemSolverChecksTest, MoveToScopeItemSpecsTest) {
  auto makeScopeItemList = [](const std::string& scopeName,
                              const std::vector<std::string>& scopeItems = {}) {
    ScopeItemList scopeItemList;
    scopeItemList.scopeName() = scopeName;
    scopeItemList.scopeItems() = scopeItems;

    return scopeItemList;
  };

  auto makeSingleRandomSpec = [&](const MoveToScopeItemsSpec& spec) {
    DestinationsToExploreOptions destinations;
    destinations.set_moveToScopeItems() = spec;

    SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
    singleRandomSpec.destinationsToExplore() = destinations;

    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = 1;
    singleRandomSpec.stratifiedSampleSize() = sampleSize;

    return singleRandomSpec;
  };

  auto solver = makeInitializedSolver(GetParam());
  {
    // test localSearchSolverSpec error; empty MoveToScopeItemsSpec
    const MoveToScopeItemsSpec moveToScopeItemsSpec;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));
    solverSpec.singleRandomStratifiedMoveTypeSpec() =
        makeSingleRandomSpec(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown scope ");
  }

  {
    // test localSearchSolverSpec error; empty defaultScopeItems
    MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.objectToScopeItems() = {
        {"s1", makeScopeItemList("host")}, {"s2", makeScopeItemList("host")}};

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));
    solverSpec.singleRandomStratifiedMoveTypeSpec() =
        makeSingleRandomSpec(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown scope ");
  }

  {
    // test localSearchSolverSpec error; wrong object
    MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.defaultScopeItems() = makeScopeItemList("host");
    moveToScopeItemsSpec.objectToScopeItems() = {
        {"wrongObject", makeScopeItemList("host")},
        {"s2", makeScopeItemList("host")}};

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));
    solverSpec.singleRandomStratifiedMoveTypeSpec() =
        makeSingleRandomSpec(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown shard wrongObject");
  }

  {
    // test localSearchStageSolverSpec with 5 stages; stage 1 has a correct
    // scope name, while stage 4 has an incorrect scope name
    LocalSearchStageSolverSpec stageSolverSpec;
    for (const auto _ : folly::irange(5)) {
      LocalSearchStageSpec stageSpec;
      stageSpec.begin() = 0;
      stageSpec.end() = 1;
      stageSpec.solverSpec()->moveTypeList()->push_back(
          ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));
      stageSpec.solverSpec()->exploreMovesFromContainersNotInObjective() =
          false;
      stageSolverSpec.stageSpecs()->push_back(stageSpec);
    }

    solver->addScope(
        "rack",
        folly::F14FastMap<std::string, std::string>{
            {"h1", "r1"}, {"h2", "r2"}});

    // add singleRandomSpec to first and fourth stages
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    moveToScopeItemsSpec1.defaultScopeItems() = makeScopeItemList("rack");
    moveToScopeItemsSpec1.objectToScopeItems() = {
        {"s1", makeScopeItemList("host")}, {"s2", makeScopeItemList("host")}};
    stageSolverSpec.stageSpecs()
        ->at(0)
        .solverSpec()
        ->singleRandomStratifiedMoveTypeSpec() =
        makeSingleRandomSpec(moveToScopeItemsSpec1);

    MoveToScopeItemsSpec moveToScopeItemsSpec2;
    moveToScopeItemsSpec2.defaultScopeItems() = makeScopeItemList("rack");
    moveToScopeItemsSpec2.objectToScopeItems() = {
        {"s1", makeScopeItemList("host")},
        {"s2", makeScopeItemList("wrongHost")}};
    stageSolverSpec.stageSpecs()
        ->at(3)
        .solverSpec()
        ->singleRandomStratifiedMoveTypeSpec() =
        makeSingleRandomSpec(moveToScopeItemsSpec2);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(stageSolverSpec), "unknown scope wrongHost");
  }

  {
    // check if partition exists
    DestinationsToExploreOptions destinationsToExploreOption;

    MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.defaultScopeItems() = makeScopeItemList("rack");

    GroupToScopeItemList groupToScopeItemList;
    groupToScopeItemList.groupToScopeItemList()->emplace(
        "j1", makeScopeItemList("host"));
    groupToScopeItemList.groupToScopeItemList()->emplace(
        "j2", makeScopeItemList("rack"));
    groupToScopeItemList.partitionName() = "job";
    moveToScopeItemsSpec.scopeItemsPerGroups() = groupToScopeItemList;

    destinationsToExploreOption.set_moveToScopeItems(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "name", destinationsToExploreOption),
        "unknown partition job");
  }
  {
    // check if group exists
    solver->addPartition(
        "job",
        std::unordered_map<std::string, std::string>{
            {"s1", "j1"}, {"s2", "j2"}});
    DestinationsToExploreOptions destinationsToExploreOption;

    MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.defaultScopeItems() = makeScopeItemList("rack");

    GroupToScopeItemList groupToScopeItemList;
    groupToScopeItemList.groupToScopeItemList()->emplace(
        "j1", makeScopeItemList("host"));
    groupToScopeItemList.groupToScopeItemList()->emplace(
        "j3", makeScopeItemList("rack"));
    groupToScopeItemList.partitionName() = "job";
    moveToScopeItemsSpec.scopeItemsPerGroups() = groupToScopeItemList;

    destinationsToExploreOption.set_moveToScopeItems(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "name2", destinationsToExploreOption),
        "unknown group j3 in partition job");
  }

  {
    // filter must reference an existing dimension
    MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.defaultScopeItems() =
        makeDimensionFilteredScopeItemList("rack", "network");

    DestinationsToExploreOptions destinationsToExploreOption;
    destinationsToExploreOption.set_moveToScopeItems(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "filterBadDimension", destinationsToExploreOption),
        "unknown dimension network");
  }

  {
    // filter requires a dynamic dimension; a static one is rejected.
    solver->addObjectDimension(
        "size", std::map<std::string, double>{{"s1", 1.0}, {"s2", 2.0}});

    MoveToScopeItemsSpec moveToScopeItemsSpec;
    moveToScopeItemsSpec.defaultScopeItems() =
        makeDimensionFilteredScopeItemList("rack", "size");

    DestinationsToExploreOptions destinationsToExploreOption;
    destinationsToExploreOption.set_moveToScopeItems(moveToScopeItemsSpec);

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "filterStaticDimension", destinationsToExploreOption),
        "ObjectDimensionBasedFilter dimension 'size' is expected to be dynamic on scope 'rack'");
  }
}

TEST_P(ProblemSolverChecksTest, SampleSizeTest) {
  auto solver = makeInitializedSolver(GetParam());

  auto makeSingleRandomSpec = [&](const SampleSize& spec) {
    MoveToCurrentScopeItemSpec currentSpec;
    currentSpec.scopeNameForExploringMovesToCurrentScopeItem() = "host";

    DestinationsToExploreOptions destinations;
    destinations.set_moveToCurrentScopeItem() = currentSpec;

    SingleRandomStratifiedMoveTypeSpec singleRandomSpec;
    singleRandomSpec.destinationsToExplore() = destinations;
    singleRandomSpec.stratifiedSampleSize() = spec;
    return singleRandomSpec;
  };

  auto addLocalSearchSolverSpec = [&](const SampleSize& spec) {
    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));
    solverSpec.singleRandomStratifiedMoveTypeSpec() =
        makeSingleRandomSpec(spec);
    solver->addSolver(solverSpec);
  };

  {
    // test negative default value for sample size
    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = -10;

    REBALANCER_EXPECT_RUNTIME_ERROR(
        addLocalSearchSolverSpec(sampleSize),
        "expected default sample size to be non-negative but got -10");
  }

  {
    // test negative object sample size
    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = 10;
    sampleSize.objectToSampleSize() = {{"s1", 1}, {"s2", -2}};

    REBALANCER_EXPECT_RUNTIME_ERROR(
        addLocalSearchSolverSpec(sampleSize),
        "expected sample size for 's2' to be non-negative but got -2");
  }

  {
    // not properly initialized
    SampleSize sampleSize;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        addLocalSearchSolverSpec(sampleSize),
        "No positive value found in SampleSize struct. Make it sure it is initialized properly and atleast one value should be > 0");
  }

  {
    // all values are 0
    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = 0;

    REBALANCER_EXPECT_RUNTIME_ERROR(
        addLocalSearchSolverSpec(sampleSize),
        "No positive value found in SampleSize struct. Make it sure it is initialized properly and atleast one value should be > 0");
  }
}

TEST_P(ProblemSolverChecksTest, SingleRandomObjectStratifiedMoveTypeTest) {
  auto solver = makeInitializedSolver(GetParam());

  {
    // Test if partition exists
    GroupList groupList;
    groupList.partitionName() = "group";

    ObjectsFromGroupsSpec objectsFromGroupsSpec;
    objectsFromGroupsSpec.groupList() = groupList;

    ObjectsToExploreOptions objectsToExploreOptions;
    objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = 1;

    SingleRandomObjectStratifiedMoveTypeSpec spec;
    spec.objectsToExploreOptions() = objectsToExploreOptions;
    spec.stratifiedSampleSize() = sampleSize;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition group");
  }

  {
    // Test that objectToSampleSize should not be populated
    solver->addPartition(
        "group",
        std::unordered_map<std::string, std::string>{
            {"s1", "j1"}, {"s2", "j2"}});
    GroupList groupList;
    groupList.partitionName() = "group";

    ObjectsFromGroupsSpec objectsFromGroupsSpec;
    objectsFromGroupsSpec.groupList() = groupList;

    ObjectsToExploreOptions objectsToExploreOptions;
    objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

    SampleSize sampleSize;
    sampleSize.defaultSampleSize() = 1;
    sampleSize.objectToSampleSize() = {{"s1", 1}, {"s2", 2}};

    SingleRandomObjectStratifiedMoveTypeSpec spec;
    spec.objectsToExploreOptions() = objectsToExploreOptions;
    spec.stratifiedSampleSize() = sampleSize;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "objectToSampleSize is not supported in this move type");
  }

  {
    // Test that bundle sizes > 1 is rejected for this move type.
    GroupList groupList;
    groupList.partitionName() = "group";

    ObjectsFromGroupsSpec objectsFromGroupsSpec;
    objectsFromGroupsSpec.groupList() = groupList;
    objectsFromGroupsSpec.bundleSize() = 2;

    ObjectsToExploreOptions objectsToExploreOptions;
    objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

    SingleRandomObjectStratifiedMoveTypeSpec spec;
    spec.objectsToExploreOptions() = objectsToExploreOptions;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "bundleSize != 1 is not supported in this move type");
  }
}

TEST_P(ProblemSolverChecksTest, SwapMoveTypeTests) {
  auto verify = [](SwapMoveTypeSpec spec, const std::string& errorMsg = "") {
    auto solver = makeInitializedSolver(GetParam());
    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(std::move(spec)));
    if (errorMsg.empty()) {
      solver->addSolver(solverSpec);
    } else {
      REBALANCER_EXPECT_RUNTIME_ERROR(
          solver->addSolver(solverSpec), errorMsg.c_str());
    }
  };

  // check that vanilla swap move type works
  verify(SwapMoveTypeSpec());

  // check that swap with sample size also works
  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 1;
  SwapMoveTypeSpec spec;
  spec.sampleSize() = sampleSize;
  verify(spec);

  // check that swap with invalid sample size throws
  sampleSize.objectToSampleSize() = {{"s1", 1}, {"s2", 2}};
  spec.sampleSize() = sampleSize;
  verify(spec, "objectToSampleSize is not supported in this move type");

  // SwapMoveType only reads defaultScopeItems; other MoveToScopeItemsSpec
  // fields must be rejected rather than silently ignored.
  auto verifySwapRejects = [&](const MoveToScopeItemsSpec& moveToScopeItems,
                               const std::string& unsupportedField) {
    DestinationsToExploreOptions destinationsToExplore;
    destinationsToExplore.set_moveToScopeItems(moveToScopeItems);
    SwapMoveTypeSpec swapSpec;
    swapSpec.destinationsToExplore() = destinationsToExplore;
    verify(
        swapSpec,
        fmt::format(
            "SwapMoveType does not support {}: only unfiltered MoveToScopeItemsSpec.defaultScopeItems is supported",
            unsupportedField));
  };

  ScopeItemList rackScopeItems;
  rackScopeItems.scopeName() = "rack";

  {
    MoveToScopeItemsSpec moveToScopeItems;
    moveToScopeItems.defaultScopeItems() =
        makeDimensionFilteredScopeItemList("rack", "network");
    verifySwapRejects(
        moveToScopeItems, "MoveToScopeItemsSpec.defaultScopeItems.filter");
  }

  {
    MoveToScopeItemsSpec moveToScopeItems;
    moveToScopeItems.defaultScopeItems() = rackScopeItems;
    moveToScopeItems.objectToScopeItems() = {{"s1", rackScopeItems}};
    verifySwapRejects(
        moveToScopeItems, "MoveToScopeItemsSpec.objectToScopeItems");
  }

  {
    GroupToScopeItemList scopeItemsPerGroups;
    scopeItemsPerGroups.partitionName() = "job";
    scopeItemsPerGroups.groupToScopeItemList() = {{"j1", rackScopeItems}};

    MoveToScopeItemsSpec moveToScopeItems;
    moveToScopeItems.defaultScopeItems() = rackScopeItems;
    moveToScopeItems.scopeItemsPerGroups() = scopeItemsPerGroups;
    verifySwapRejects(
        moveToScopeItems, "MoveToScopeItemsSpec.scopeItemsPerGroups");
  }
}

TEST_P(ProblemSolverChecksTest, MultiStageConfigNoStagesTest) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchStageSolverSpec stageSolverSpec;
  MultiStageConfig multiStageConfig;
  multiStageConfig.moveLimit() = 10;
  stageSolverSpec.multiStageConfigs() = {multiStageConfig};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "MultiStageConfig stage list must be non-empty");
}

TEST_P(ProblemSolverChecksTest, EmptyExecutionConfig) {
  auto solver = makeInitializedSolver(GetParam());
  ASSERT_NE(solver, nullptr);
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));
  solverSpec.parallelExecutionConfig() = ParallelExecutionConfig{};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "ParallelExecutionConfig cannot be empty");
}

TEST_P(ProblemSolverChecksTest, EmptyStageExecutionConfig) {
  auto solver = makeInitializedSolver(GetParam());
  ASSERT_NE(solver, nullptr);
  LocalSearchStageSolverSpec solverSpec;
  solverSpec.parallelExecutionConfig() = ParallelExecutionConfig{};

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "ParallelExecutionConfig cannot be empty");
}

TEST_P(ProblemSolverChecksTest, NegativeBatchingMaxConcurrency) {
  auto solver = makeInitializedSolver(GetParam());
  ASSERT_NE(solver, nullptr);
  BatchingExecutionConfig batchingConfig;
  batchingConfig.maxConcurrency() = -1;
  ParallelExecutionConfig executionConfig;
  executionConfig.set_batching(std::move(batchingConfig));
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));
  solverSpec.parallelExecutionConfig() = std::move(executionConfig);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "expected BatchingExecutionConfig.maxConcurrency to be non-negative but got -1");
}

TEST_P(ProblemSolverChecksTest, NegativeAutoBatchingMaxConcurrency) {
  auto solver = makeInitializedSolver(GetParam());
  ASSERT_NE(solver, nullptr);
  AutoExecutionConfig autoConfig;
  autoConfig.batching()->maxConcurrency() = -1;
  ParallelExecutionConfig executionConfig;
  executionConfig.set_autoExecution(std::move(autoConfig));
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));
  solverSpec.parallelExecutionConfig() = std::move(executionConfig);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "expected BatchingExecutionConfig.maxConcurrency to be non-negative but got -1");
}

TEST_P(ProblemSolverChecksTest, MultiStageConfigNegativeStageTest) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchStageSolverSpec stageSolverSpec;
  MultiStageConfig multiStageConfig;
  multiStageConfig.stageIds() = {1, -1};
  multiStageConfig.moveLimit() = 10;
  stageSolverSpec.multiStageConfigs() = {multiStageConfig};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "MultiStageConfig stage id in stage list must be non-negative");
}

TEST_P(ProblemSolverChecksTest, MultiStageConfigNegativeMoveLimitTest) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchStageSolverSpec stageSolverSpec;
  MultiStageConfig multiStageConfig;
  multiStageConfig.stageIds() = {1, 12};
  multiStageConfig.moveLimit() = 10;
  MultiStageConfig multiStageConfig1;
  multiStageConfig1.stageIds() = {10, 12};
  multiStageConfig1.moveLimit() = -10;
  stageSolverSpec.multiStageConfigs() = {multiStageConfig1, multiStageConfig};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "MultiStageConfig moveLimit must be non-negative");
}

TEST_P(ProblemSolverChecksTest, MultiStageConfigNoMoveLimitOrTimeLimit) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchStageSolverSpec stageSolverSpec;
  MultiStageConfig multiStageConfig;
  multiStageConfig.stageIds() = {1, 12};
  stageSolverSpec.multiStageConfigs() = {multiStageConfig};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "MultiStageConfig must have either moveLimit or solveTime set");
}

TEST_P(ProblemSolverChecksTest, MultiStageConfigNegativeTimeLimit) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchStageSolverSpec stageSolverSpec;
  MultiStageConfig multiStageConfig;
  multiStageConfig.stageIds() = {1, 12};
  multiStageConfig.solveTime() = -10;
  stageSolverSpec.multiStageConfigs() = {multiStageConfig};
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "MultiStageConfig solveTime must be non-negative");
}

TEST_P(ProblemSolverChecksTest, FixedDestMoveType_Vanilla) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(FixedDestMoveTypeSpec()));
  solver->addSolver(solverSpec);
}

TEST_P(ProblemSolverChecksTest, FixedDestMoveType_Sampled) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  FixedDestMoveTypeSpec moveSpec;
  facebook::rebalancer::FixedDestMoveType::makeSampled(moveSpec, 1);
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(moveSpec));
  solver->addSolver(solverSpec);
}

TEST_P(ProblemSolverChecksTest, FixedDestMoveType_objectToSampleSize) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  FixedDestMoveTypeSpec moveSpec;
  facebook::rebalancer::FixedDestMoveType::makeSampled(moveSpec, 1);
  moveSpec.sampleSize()->objectToSampleSize() = {{"s1", 1}, {"s2", 2}};
  solverSpec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(moveSpec));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "objectToSampleSize is not supported in this move type");
}

TEST_P(ProblemSolverChecksTest, SingleFixedSourceMoveType_Sampled) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  SingleFixedSourceMoveTypeSpec spec;
  facebook::rebalancer::FixedSourceMoveType::makeSampled(spec, 1);
  spec.specialContainer().emplace("specialContainer");
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
  solver->addSolver(solverSpec);
}

TEST_P(ProblemSolverChecksTest, SingleFixedSourceMoveType_objectToSampleSize) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  SampleSize sampleSize;
  sampleSize.defaultSampleSize() = 1;
  sampleSize.objectToSampleSize() = {{"s1", 1}, {"s2", 2}};
  SingleFixedSourceMoveTypeSpec spec;
  spec.sampleSize() = sampleSize;
  spec.specialContainer().emplace("specialContainer");
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "objectToSampleSize is not supported in this move type");
}

TEST_P(
    ProblemSolverChecksTest,
    SingleFixedSourceMoveType_missingRequiredFields) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  const SingleFixedSourceMoveTypeSpec spec;
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "SingleFixedSourceMoveTypeSpec needs a special container or a list of scope items to perform moves from");
}

TEST_P(
    ProblemSolverChecksTest,
    SingleFixedSourceMoveType_specialContainerPresents) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  SingleFixedSourceMoveTypeSpec spec;
  spec.specialContainer() = "specialContainer";
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
  solver->addSolver(solverSpec);
}

TEST_P(ProblemSolverChecksTest, SingleFixedSourceMoveType_scopeItemsPresent) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  SingleFixedSourceMoveTypeSpec spec;
  spec.scopeItemList()->scopeItems() = {"s1", "s2"};
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
  solver->addSolver(solverSpec);
}

TEST_P(ProblemSolverChecksTest, SingleFixedSourceMoveType_objectBundles) {
  auto makeObjectBundleFormationHints = [&](const std::string& container,
                                            const std::string& partition,
                                            int bundleSize) {
    ObjectBundleFormationHints hints;
    folly::F14FastMap<std::string, ObjectsToExploreOptions>
        scopeItemToObjectsToExploreOptions;

    GroupList groupList;
    groupList.partitionName() = partition;

    ObjectsFromGroupsSpec objectsFromGroupsSpec;
    objectsFromGroupsSpec.groupList() = groupList;
    objectsFromGroupsSpec.bundleSize() = bundleSize;
    ObjectsToExploreOptions objectsToExploreOptions;
    objectsToExploreOptions.set_objectsFromGroupsSpec(objectsFromGroupsSpec);

    scopeItemToObjectsToExploreOptions[container] = objectsToExploreOptions;
    hints.scopeName() = "host";
    hints.scopeItemToObjectsToExploreOptions() =
        scopeItemToObjectsToExploreOptions;
    return hints;
  };

  auto solver = makeInitializedSolver(GetParam());
  SingleFixedSourceMoveTypeSpec spec;
  spec.specialContainer() = "specialContainer";

  // spec rejected when partition does not exist.
  {
    spec.objectBundleFormationHints() = makeObjectBundleFormationHints(
        /*container=*/"h1", /*partition=*/"p1", /*bundleSize=*/1);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition p1");
  }

  // spec rejected when bundle size <= 1
  {
    solver->addPartition(
        "p1",
        std::unordered_map<std::string, std::string>{
            {"s1", "j1"}, {"s2", "j2"}});

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "bundleSize <=1 is not supported in this move type");
  }

  // Spec accepted when bundle size > 1
  {
    spec.objectBundleFormationHints() = makeObjectBundleFormationHints(
        /*container=*/"h1", /*partition=*/"p1", /*bundleSize=*/2);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

    solver->addSolver(solverSpec);
  }
}

TEST_P(ProblemSolverChecksTest, GroupMoveWithHintStrategiesMoveType) {
  auto solver = makeInitializedSolver(GetParam());

  auto makeGroupMoveWithStrategies =
      [&](const auto& primaryPartition,
          const auto& secondaryPartition,
          const auto& hintMap,
          std::optional<std::string> unassignedContainer = std::nullopt,
          const SecondaryGroupReplacementConfig&
              secondaryGroupReplacementConfig =
                  SecondaryGroupReplacementConfig()) {
        GroupMoveWithHintStrategiesMoveTypeSpec spec;
        spec.primaryPartition() = primaryPartition;
        spec.secondaryPartition() = secondaryPartition;
        spec.moveStrategies() = hintMap;
        if (unassignedContainer.has_value()) {
          spec.unassignedContainer() = unassignedContainer.value();
        }
        spec.secondaryGroupReplacementConfig() =
            secondaryGroupReplacementConfig;
        return spec;
      };

  MoveStrategies groupToMoveStrategy;
  std::map<std::string, MoveStrategy> hintOptionMap;
  MoveStrategy hintOption1;
  hintOption1.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  hintOption1.moveSetsGeneratedPerScopeItem() = 1;
  MoveToScopeItemsSpec moveToScopeItemsSpec;
  ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  hintOption1.moveToScopeItems() = moveToScopeItemsSpec;

  MoveStrategy hintOption2;
  hintOption2.type() = MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  hintOption2.moveSetsGeneratedPerScopeItem() = 1;
  MoveToScopeItemsSpec moveToScopeItemsSpec2;
  ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "worlds";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  hintOption2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintOptionMap["rank1"] = hintOption1;
  hintOptionMap["rank2"] = hintOption2;
  groupToMoveStrategy.groupToMoveStrategy() = hintOptionMap;
  {
    // No primary partition
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition primaryPartition");
  }
  {
    // no secondary partition
    solver->addPartition(
        "primaryPartition", std::map<std::string, std::string>{{"s1", "j1"}});
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition secondaryPartition");
  }
  {
    // throw if the unassigned container is not null but does not exist in the
    // universe
    solver->addPartition(
        "secondaryPartition",
        std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition",
        "secondaryPartition",
        groupToMoveStrategy,
        "NotAContainer");
    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "unknown item NotAContainer in scope host");
  }
  {
    // no hint map
    const MoveStrategies groupToMoveStrategy2;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy2);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "groupToMoveStrategy is empty; you need to specify groupToMoveStrategy");
  }
  {
    // movesets generated is less than 1
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    hintOption.moveSetsGeneratedPerScopeItem() = -1;
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    ScopeItemList defaultScopeItems1;
    defaultScopeItems1.scopeName() = "worlds";
    moveToScopeItemsSpec1.defaultScopeItems() = defaultScopeItems1;
    hintOption.moveToScopeItems() = moveToScopeItemsSpec1;

    MoveStrategies groupToMoveStrategy2;
    std::map<std::string, MoveStrategy> hintOptionMap2;
    hintOptionMap2["j1"] = hintOption;
    groupToMoveStrategy2.groupToMoveStrategy() = hintOptionMap2;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy2);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "moveSetsGeneratedPerScopeItem must be non-negative in MoveStrategyType");
  }

  {
    // scope must exist in hint Options
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    hintOption.moveSetsGeneratedPerScopeItem() = 0;
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    ScopeItemList defaultScopeItems1;
    defaultScopeItems1.scopeName() = "worlds";
    moveToScopeItemsSpec1.defaultScopeItems() = defaultScopeItems1;
    hintOption.moveToScopeItems() = moveToScopeItemsSpec1;

    MoveStrategies groupToMoveStrategy2;
    groupToMoveStrategy2.groupToMoveStrategy()["j1"] = hintOption;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy2);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown scope worlds");
  }

  {
    // tertiary partition must exist
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    hintOption.moveSetsGeneratedPerScopeItem() = 1;
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    ScopeItemList defaultScopeItems1;
    defaultScopeItems1.scopeName() = "worlds";
    moveToScopeItemsSpec1.defaultScopeItems() = defaultScopeItems1;
    hintOption.moveToScopeItems() = moveToScopeItemsSpec1;
    hintOption.tertiaryPartition() = "tertiaryPartition";

    MoveStrategies groupToMoveStrategy2;
    groupToMoveStrategy2.groupToMoveStrategy()["j1"] = hintOption;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy2);

    solver->addScope(
        "worlds",
        std::unordered_map<std::string, std::string>{
            {"h1", "n1"}, {"h2", "n2"}});

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition tertiaryPartition");
  }

  {
    // check scopeItem
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    hintOption.moveSetsGeneratedPerScopeItem() = 1;
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    ScopeItemList defaultScopeItems1;
    defaultScopeItems1.scopeName() = "worlds";
    moveToScopeItemsSpec1.defaultScopeItems() = defaultScopeItems1;
    hintOption.moveToScopeItems() = moveToScopeItemsSpec1;
    hintOption.tertiaryPartition() = "tertiaryPartition";

    MoveStrategies groupToMoveStrategy2;
    groupToMoveStrategy2.groupToMoveStrategy()["j1"] = hintOption;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy2);

    solver->addPartition(
        "tertiaryPartition",
        std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});
    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "numScopeItemsToExplorePerTertiaryGroup must be set when tertiaryPartition is set in MoveStrategyType");
  }

  {
    // if tertiary partition is not set, numScopeItemsToExplorePerTertiaryGroup
    // should not be set
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    hintOption.moveSetsGeneratedPerScopeItem() = 1;
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    ScopeItemList defaultScopeItems1;
    defaultScopeItems1.scopeName() = "worlds";
    moveToScopeItemsSpec1.defaultScopeItems() = defaultScopeItems1;
    hintOption.moveToScopeItems() = moveToScopeItemsSpec1;
    hintOption.numScopeItemsToExplorePerTertiaryGroup() = 1;

    MoveStrategies groupToMoveStrategy2;
    groupToMoveStrategy2.groupToMoveStrategy()["j1"] = hintOption;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", groupToMoveStrategy2);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "numScopeItemsToExplorePerTertiaryGroup must not be set when tertiaryPartition is not set in MoveStrategyType");
  }

  {
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
    MoveToScopeItemsSpec distinctMoveToScopeItemsSpec;
    ScopeItemList distinctDefaultScopeItems;
    distinctDefaultScopeItems.scopeName() = "worlds";
    distinctMoveToScopeItemsSpec.defaultScopeItems() =
        distinctDefaultScopeItems;
    hintOption.moveToScopeItems() = distinctMoveToScopeItemsSpec;
    hintOption.distinctScopeItemsAcrossTertiaryGroups() = true;

    MoveStrategies moveStrategies;
    moveStrategies.groupToMoveStrategy()["j1"] = hintOption;
    const auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", moveStrategies);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "distinctScopeItemsAcrossTertiaryGroups requires tertiaryPartition in MoveStrategyType");
  }

  {
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    MoveToScopeItemsSpec replacementMoveToScopeItemsSpec;
    ScopeItemList replacementDefaultScopeItems;
    replacementDefaultScopeItems.scopeName() = "worlds";
    replacementMoveToScopeItemsSpec.defaultScopeItems() =
        replacementDefaultScopeItems;
    hintOption.moveToScopeItems() = replacementMoveToScopeItemsSpec;
    hintOption.tertiaryPartition() = "tertiaryPartition";
    hintOption.numScopeItemsToExplorePerTertiaryGroup() = 1;
    hintOption.distinctScopeItemsAcrossTertiaryGroups() = true;

    MoveStrategies moveStrategies;
    moveStrategies.groupToMoveStrategy()["j1"] = hintOption;
    const auto spec = makeGroupMoveWithStrategies(
        "primaryPartition", "secondaryPartition", moveStrategies);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "distinctScopeItemsAcrossTertiaryGroups requires RANDOM_SAMPLING_WITHOUT_REPLACEMENT in MoveStrategyType");
  }

  {
    // if SecondaryGroupToSecondaryGroups does not have a group that exist,
    // throw an error
    MoveStrategy hintOption;
    hintOption.type() = MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
    hintOption.moveSetsGeneratedPerScopeItem() = 1;
    MoveToScopeItemsSpec moveToScopeItemsSpec1;
    ScopeItemList defaultScopeItems1;
    defaultScopeItems1.scopeName() = "worlds";
    moveToScopeItemsSpec1.defaultScopeItems() = defaultScopeItems1;
    hintOption.moveToScopeItems() = moveToScopeItemsSpec1;

    SecondaryGroupReplacementConfig secondaryGroupReplacementConfig;
    secondaryGroupReplacementConfig.secondaryGroupToAllowedReplacements() = {
        {"j1", {"j3333"}}};

    MoveStrategies groupToMoveStrategy2;
    groupToMoveStrategy2.groupToMoveStrategy()["j1"] = hintOption;
    auto spec = makeGroupMoveWithStrategies(
        "primaryPartition",
        "secondaryPartition",
        groupToMoveStrategy2,
        std::nullopt,
        secondaryGroupReplacementConfig);

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "unknown group j3333 in partition secondaryPartition");
  }
}

TEST_P(ProblemSolverChecksTest, ColocateGroupsMoveTypeSpecInvalidPartition) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchSolverSpec solverSpec;
  ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "partitionName";
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "unknown partition partitionName");
}

TEST_P(ProblemSolverChecksTest, ColocateGroupsMoveTypeSpecInvalidScope) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  LocalSearchSolverSpec solverSpec;
  ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "job";
  spec.colocationScopeName() = "colocationScope";

  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "unknown scope colocationScope");
}

TEST_P(
    ProblemSolverChecksTest,
    ColocateGroupsMoveTypeSpecNonDisjointRelatedGroups) {
  auto solver =
      makeInitializedSolver(GetParam(), {{"h1", {"s1", "s2"}}, {"h2", {"s3"}}});
  solver->addPartition(
      "job",
      std::map<std::string, std::string>{
          {"s1", "j1"}, {"s2", "j2"}, {"s3", "j3"}});

  LocalSearchSolverSpec solverSpec;
  ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "job";
  spec.colocationScopeName() = "host";

  ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo1;
  relatedGroupsInfo1.relatedGroups() = {"j1", "j2"};

  ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo2;
  relatedGroupsInfo2.relatedGroups() = {"j3", "j2"};

  spec.relatedGroupsList() = {relatedGroupsInfo1, relatedGroupsInfo2};

  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "Group 'j2' is present in multiple related groups in ColocateGroupsMoveTypeSpec. Across all ColocateGroupsMoveTypeRelatedGroupsInfo, the relatedGroups sets need to be disjoint");
}

TEST_P(ProblemSolverChecksTest, ColocateGroupsMoveTypeSpecUknownGroup) {
  auto solver =
      makeInitializedSolver(GetParam(), {{"h1", {"s1", "s2"}}, {"h2", {"s3"}}});
  solver->addPartition(
      "job",
      std::map<std::string, std::string>{
          {"s1", "j1"}, {"s2", "j2"}, {"s3", "j3"}});

  LocalSearchSolverSpec solverSpec;
  ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "job";
  spec.colocationScopeName() = "host";

  ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo1;
  relatedGroupsInfo1.relatedGroups() = {"j1", "j2"};

  ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo2;
  relatedGroupsInfo2.relatedGroups() = {"j33", "j3"};

  spec.relatedGroupsList() = {relatedGroupsInfo1, relatedGroupsInfo2};

  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "unknown group j33 in partition job");
}

TEST_P(ProblemSolverChecksTest, ColocateGroupsMoveTypeNegativeSampleSize) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  LocalSearchSolverSpec solverSpec;
  ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "job";
  spec.colocationScopeName() = "host";

  ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo1;
  relatedGroupsInfo1.relatedGroups() = {"j1", "j2"};
  spec.relatedGroupsList() = {relatedGroupsInfo1};

  spec.defaultSampleSize() = -1;

  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec),
      "expected ColocateGroupsMoveTypeSpec.defaultSampleSize to be non-negative but got -1");
}

TEST_P(
    ProblemSolverChecksTest,
    ColocateGroupsMoveTypeIncorrectColocationScopeItemToGroupToContainers) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "job", std::map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  LocalSearchSolverSpec solverSpec;
  ColocateGroupsMoveTypeSpec spec;
  spec.partitionName() = "job";
  spec.colocationScopeName() = "host";
  spec.colocationScopeItemToGroupToContainers() = {
      {"h1", {{"j1", {"h11"}}}},
  };

  ColocateGroupsMoveTypeRelatedGroupsInfo relatedGroupsInfo1;
  relatedGroupsInfo1.relatedGroups() = {"j1", "j2"};
  spec.relatedGroupsList() = {relatedGroupsInfo1};
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "unknown item h11 in scope host");
}

TEST_P(ProblemSolverChecksTest, HigherPriorityObjConfigTestInvalidTuplePos) {
  auto solver = makeInitializedSolver(GetParam());

  algopt::common::thrift::AllowedWorsening allowedWorsening;
  allowedWorsening.percent() = 0;
  allowedWorsening.absolute() = 100;

  algopt::common::thrift::HigherPriorityObjectivesConfig objConfig;
  objConfig.tuplePosToAllowedWorsening() = {
      {0, allowedWorsening}, {1, allowedWorsening}};

  LocalSearchStageSpec stageSpec;
  stageSpec.begin() = 1;
  stageSpec.end() = 2;
  stageSpec.solverSpec()->moveTypeList()->emplace_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));
  stageSpec.higherPriorityObjConfig() = std::move(objConfig);

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs()->emplace_back(std::move(stageSpec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "Expected each tuple position in HigherPriorityObjConfig.tuplePosToAllowedWorsening() to be in the interval [0, stageSpec.begin()), but got tuplePos=1 and stageSpec.begin()=1");
}

TEST_P(ProblemSolverChecksTest, HigherPriorityObjConfigTestNegativeDeviation) {
  auto solver = makeInitializedSolver(GetParam());

  algopt::common::thrift::AllowedWorsening allowedWorsening;
  allowedWorsening.percent() = -0.5;
  allowedWorsening.absolute() = 100;

  algopt::common::thrift::HigherPriorityObjectivesConfig objConfig;
  objConfig.tuplePosToAllowedWorsening() = {{0, allowedWorsening}};

  LocalSearchStageSpec stageSpec;
  stageSpec.begin() = 1;
  stageSpec.end() = 2;
  stageSpec.solverSpec()->moveTypeList()->emplace_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));
  stageSpec.higherPriorityObjConfig() = std::move(objConfig);

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs()->emplace_back(std::move(stageSpec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "expected allowedWorsening.percent to be non-negative but got -0.5");
}

TEST_P(ProblemSolverChecksTest, HigherPriorityObjConfigOptimalSolver) {
  auto solver = makeInitializedSolver(GetParam());

  algopt::common::thrift::AllowedWorsening allowedWorsening;
  allowedWorsening.percent() = -0.5;
  allowedWorsening.absolute() = 100;

  algopt::common::thrift::HigherPriorityObjectivesConfig objConfig;
  objConfig.tuplePosToAllowedWorsening() = {{0, allowedWorsening}};

  MultiObjectiveSolveSettings multiObjSolveSettings;
  multiObjSolveSettings.higherPriorityObjConfig() = std::move(objConfig);

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.multiObjSolveSettings() = std::move(multiObjSolveSettings);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(optimalSolverSpec),
      "expected allowedWorsening.percent to be non-negative but got -0.5");
}

TEST_P(ProblemSolverChecksTest, LocalSearchStageSolverBeginEnd) {
  auto solver = makeInitializedSolver(GetParam());
  LocalSearchStageSpec stageSpec;
  stageSpec.solverSpec()->moveTypeList()->emplace_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec{}));

  LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs()->emplace_back(std::move(stageSpec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(stageSolverSpec),
      "Expected stageSpec.begin() < stageSpec.end(), but got begin=0, end=0");
}

TEST_P(ProblemSolverChecksTest, addDestinationsToExploreOptionsTest) {
  auto solver = makeInitializedSolver(GetParam());
  {
    // check destinationsToExploreOptions is valid
    interface::DestinationsToExploreOptions destinationsToExploreOptions;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "", destinationsToExploreOptions),
        "addDestinationsToExploreOptions must have non-empty names");
  }
  {
    // check destinationsToExploreOptions is valid
    interface::DestinationsToExploreOptions destinationsToExploreOptions;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "destinationsToExploreOptions1", destinationsToExploreOptions),
        "DestinationsToExploreOptions is empty; you need to specify one of the options");
  }
  {
    // check cannot add name as an option
    interface::DestinationsToExploreOptions destinationsToExploreOptions;
    destinationsToExploreOptions.set_destinationToExploreName("myHint");

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addDestinationsToExploreOptions(
            "destinationsToExploreOptions1", destinationsToExploreOptions),
        "interface::DestinationsToExploreOptions::Type::destinationToExploreName should not be directly added. Please use interface::ProblemSolver::addDestinationToExploreOptions instead");
  }
  {
    // cannot add name if it does not exist as an option
    interface::DestinationsToExploreOptions destinationsToExploreOptions;
    destinationsToExploreOptions.set_destinationToExploreName("myHint");

    interface::SingleRandomStratifiedMoveTypeSpec spec;
    spec.destinationsToExplore() = destinationsToExploreOptions;

    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "Destinations to explore option 'myHint' not found. Add it using addDestinationsToExploreOptions(...)");
  }
}

TEST_P(
    ProblemSolverChecksTest,
    SingleFastMoveTypeSpecInvalidDestinationsToExplore) {
  auto solver = makeInitializedSolver(GetParam());

  // Set up a MoveToScopeItemsSpec with an invalid scope item (h3 doesn't exist
  // in scope "host")
  ScopeItemList scopeItemList;
  scopeItemList.scopeName() = "host";
  scopeItemList.scopeItems() = {"h3"};

  MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = scopeItemList;

  interface::DestinationsToExploreOptions destinationsToExploreOptions;
  destinationsToExploreOptions.set_moveToScopeItems() = moveToScopeItemsSpec;

  interface::SingleFastMoveTypeSpec spec;
  spec.destinationsToExplore() = destinationsToExploreOptions;

  LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpec), "unknown item h3 in scope host");
}

TEST_P(ProblemSolverChecksTest, CapacitySpecWithGroupUtilizationBound) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addContainerDimension(
      "cpu", std::map<std::string, double>{{"h1", 10}, {"h2", 20}});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  interface::CapacitySpec spec;
  spec.scope() = "host";
  spec.dimension() = "cpu";
  interface::Limit limit;
  limit.scopeItemLimits() = {{"h1", 15}, {"h2", 25}};
  spec.limit() = limit;

  {
    // The limits are applied to utilization of a group but can be
    // specified in any way (using a global default, per scope item default or a
    // specific value for per-group scopeItem combination)
    interface::GroupUtilizationBound groupUtilizationBound;
    groupUtilizationBound.partitionName() = "job";
    groupUtilizationBound.aggregationScope() = "host";
    groupUtilizationBound.perGroupValues()->scopeItemLimits() = {
        {"h1", 15}, {"h2", 25}};
    auto validSpec = spec;
    interface::UtilizationBound utilizationBound;
    utilizationBound.set_groupUtilizationBound(groupUtilizationBound);
    validSpec.utilizationBound() = utilizationBound;
    solver->addConstraint(validSpec);
  }

  {
    // This should be allowed
    interface::GroupUtilizationBound groupUtilizationBound;
    groupUtilizationBound.partitionName() = "job";
    groupUtilizationBound.aggregationScope() = "host";
    groupUtilizationBound.perGroupValues()->groupLimits() = {{"j1", 10}};
    auto validSpec = spec;
    interface::UtilizationBound utilizationBound;
    utilizationBound.set_groupUtilizationBound(groupUtilizationBound);
    validSpec.utilizationBound() = utilizationBound;
    solver->addConstraint(validSpec);
  }
}

TEST_P(ProblemSolverChecksTest, CapacitySpecWithGroupBundleUtilization) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addPartition(
      "pod",
      std::unordered_map<std::string, std::string>{{"s1", "p1"}, {"s2", "p2"}});

  const auto makeBundleUtilization =
      [](std::map<std::string, double> bundleSizes) {
        interface::GroupBundleUtilization bundleUtilization;
        bundleUtilization.partitionName() = "pod";
        bundleUtilization.scope() = "host";
        bundleUtilization.bundleSizes() = std::move(bundleSizes);
        return bundleUtilization;
      };
  const auto makeSpec =
      [](interface::GroupBundleUtilization bundleUtilization) {
        interface::CapacitySpec spec;
        spec.name() = "bundled capacity";
        spec.scope() = "host";
        spec.dimension() = "shard_count";
        spec.definition() = interface::CapacitySpecDefinition::AFTER;
        spec.bound() = interface::CapacitySpecBound::MIN;
        spec.limit()->type() = interface::LimitType::ABSOLUTE;
        spec.limit()->globalLimit() = 3;
        spec.groupBundleUtilization() = std::move(bundleUtilization);
        return spec;
      };
  const auto validSpec = makeSpec(makeBundleUtilization({{"h1", 3}}));

  EXPECT_NO_THROW(solver->addConstraint(validSpec));

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(makeSpec(makeBundleUtilization({}))),
      "GroupBundleUtilization.bundleSizes must not be empty");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(makeSpec(makeBundleUtilization({{"h1", 0}}))),
      "GroupBundleUtilization.bundleSizes[h1] must be positive and finite, got 0");
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(makeSpec(makeBundleUtilization(
          {{"h1", std::numeric_limits<double>::infinity()}}))),
      "GroupBundleUtilization.bundleSizes[h1] must be positive and finite, got inf");

  auto duringAndAfterSpec = validSpec;
  duringAndAfterSpec.definition() =
      interface::CapacitySpecDefinition::DURING_AND_AFTER;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(duringAndAfterSpec),
      "GroupBundleUtilization requires CapacitySpec.definition AFTER, got DURING_AND_AFTER");

  auto zeroAllowedSpec = validSpec;
  zeroAllowedSpec.zeroAllowed() = true;
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(zeroAllowedSpec),
      "GroupBundleUtilization is not supported with CapacitySpec.zeroAllowed");

  auto combinedUtilization = validSpec;
  interface::GroupUtilizationBound groupUtilizationBound;
  groupUtilizationBound.partitionName() = "pod";
  groupUtilizationBound.aggregationScope() = "host";
  groupUtilizationBound.perGroupValues()->globalLimit() = 3;
  interface::UtilizationBound utilizationBound;
  utilizationBound.set_groupUtilizationBound(groupUtilizationBound);
  combinedUtilization.utilizationBound() = std::move(utilizationBound);
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(combinedUtilization),
      "CapacitySpec.utilizationBound and groupBundleUtilization cannot both be set");
}

TEST_P(ProblemSolverChecksTest, CustomEquivalenceSet) {
  auto solver = makeInitializedSolver(GetParam());
  // Add two partitions
  solver->addPartition(
      "test_partition",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  CapacitySpec capacitySpec;
  capacitySpec.name() = "upperbound utilization goal";
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "shard_count";
  capacitySpec.limit()->globalLimit() = 1;
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  solver->addGoal(capacitySpec);

  capacitySpec.name() = "lowerbound utilization constraint";
  capacitySpec.bound() = CapacitySpecBound::MIN;
  solver->addConstraint(capacitySpec);

  algopt::common::thrift::StringListFilterConfig selectedGoals;
  selectedGoals.stringsToFilter()->emplace_back("upperbound utilization goal");
  selectedGoals.filterType() = algopt::common::thrift::FilterType::ALLOWLIST;
  algopt::common::thrift::StringListFilterConfig selectedConstraints;
  selectedConstraints.stringsToFilter()->emplace_back(
      "lowerbound utilization constraint");
  selectedConstraints.filterType() =
      algopt::common::thrift::FilterType::ALLOWLIST;
  // add a valid config
  CustomEquivalenceSetConfig validConfig;
  validConfig.partitionNames()->emplace_back("test_partition");
  validConfig.constraintSelectionConfig() = std::move(selectedConstraints);
  validConfig.goalSelectionConfig() = std::move(selectedGoals);

  LocalSearchSolverSpec solverSpec;
  const SingleMoveTypeSpec spec;
  solverSpec.moveTypeList()->push_back(ProblemSolver::makeMoveTypeSpec(spec));
  solverSpec.customEquivalenceSetConfig() = validConfig;
  solver->addSolver(solverSpec);

  // add an invalid config
  {
    solverSpec.customEquivalenceSetConfig() = validConfig;
    solverSpec.customEquivalenceSetConfig()->partitionNames() = {
        "invalid_partition"};
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition invalid_partition");
  }

  // add invalid goal
  {
    solverSpec.customEquivalenceSetConfig() = validConfig;
    solverSpec.customEquivalenceSetConfig()
        ->goalSelectionConfig()
        ->stringsToFilter()
        ->emplace_back("invalid_goal");
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "Spec 'invalid_goal' does not exist");
  }

  // add invalid constraint
  {
    solverSpec.customEquivalenceSetConfig() = validConfig;
    solverSpec.customEquivalenceSetConfig()
        ->constraintSelectionConfig()
        ->stringsToFilter()
        ->emplace_back("invalid_constraint");
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec),
        "Spec 'invalid_constraint' does not exist");
  }

  // add more than one partition, currently not supported
  // add invalid constraint
  {
    solverSpec.customEquivalenceSetConfig() = validConfig;
    solverSpec.customEquivalenceSetConfig()->partitionNames() = {
        "test_partition", "test_partition2"};
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->addSolver(solverSpec), "unknown partition test_partition2");
  }
}

TEST_P(ProblemSolverChecksTest, MoveCandidatePruningConstraintNames) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addScope(
      "assignable",
      std::map<std::string, std::string>{{"h1", "a1"}, {"h2", "a1"}});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j1"}});

  CapacitySpec capacitySpec;
  capacitySpec.name() = "host_capacity";
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "shard_count";
  capacitySpec.limit()->globalLimit() = 1;
  capacitySpec.limit()->type() = LimitType::ABSOLUTE;
  capacitySpec.bound() = CapacitySpecBound::MAX;
  solver->addConstraint(capacitySpec);

  MinimizeMovementSpec goalSpec;
  goalSpec.name() = "a_goal";
  solver->addGoal(goalSpec);

  const auto solverSpecPruning = [](std::vector<std::string> constraintNames) {
    GreedyGroupToScopeItemMoveTypeSpec moveTypeSpec;
    moveTypeSpec.scopeItemMovesScope() = "assignable";
    moveTypeSpec.groupMovesPartition() = "job";
    moveTypeSpec.candidatePruning()->constraintNames() =
        std::move(constraintNames);
    LocalSearchSolverSpec solverSpec;
    solverSpec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(std::move(moveTypeSpec)));
    return solverSpec;
  };

  // A real constraint name is accepted.
  solver->addSolver(solverSpecPruning({"host_capacity"}));

  // An unknown name is rejected.
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpecPruning({"does_not_exist"})),
      "Constraint 'does_not_exist' does not exist");

  // A goal name is rejected: pruning names constraints, not goals.
  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(solverSpecPruning({"a_goal"})),
      "Constraint 'a_goal' does not exist");
}

TEST_P(ProblemSolverChecksTest, PerObjectiveValueCheck) {
  auto solver = makeInitializedSolver(GetParam());
  algopt::common::thrift::PerObjectiveValue perObjectiveValue;
  perObjectiveValue.objectiveIndexToValue() = {{0, 0.5}, {-1, 0.5}};
  MultiObjectiveSolveSettings multiObjSolveSettings;
  multiObjSolveSettings.paramNamesToValues() = {
      {"param1", std::move(perObjectiveValue)}};

  OptimalSolverSpec optimalSolverSpec;
  optimalSolverSpec.multiObjSolveSettings() = std::move(multiObjSolveSettings);

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addSolver(optimalSolverSpec),
      "expected perObjectiveValue.objectiveIndexToValue.objectiveIndex to be non-negative but got -1");
}

TEST_P(ProblemSolverChecksTest, CapacityWithGroupPresenceSpecGroupFilter) {
  auto solver = makeInitializedSolver(GetParam());
  solver->addObjectDimension("cpu", std::map<std::string, double>{});
  solver->addPartition(
      "job",
      std::unordered_map<std::string, std::string>{{"s1", "j1"}, {"s2", "j2"}});

  CapacityWithGroupPresenceSpec spec;
  spec.scope() = "host";
  spec.partition() = "job";
  spec.dimension() = "cpu";

  // Test with valid group filter
  facebook::rebalancer::interface::Filter validGroupFilter;
  validGroupFilter.type() = facebook::rebalancer::interface::FilterType::GROUP;
  validGroupFilter.itemsBlacklist() = {"j1"};
  spec.groupFilter() = validGroupFilter;

  // This should succeed
  solver->addConstraint(spec);

  // Test with group filter containing unknown group
  facebook::rebalancer::interface::Filter invalidGroupFilter;
  invalidGroupFilter.type() =
      facebook::rebalancer::interface::FilterType::GROUP;
  invalidGroupFilter.itemsBlacklist() = {"j3"}; // j3 doesn't exist

  CapacityWithGroupPresenceSpec invalidSpec = spec;
  invalidSpec.groupFilter() = invalidGroupFilter;
  invalidSpec.name() = "invalidSpec";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(invalidSpec), "unknown group j3 in partition job");

  // Test with wrong filter type (should be GROUP, not SCOPE_ITEM)
  facebook::rebalancer::interface::Filter wrongTypeFilter;
  wrongTypeFilter.type() =
      facebook::rebalancer::interface::FilterType::SCOPE_ITEM;
  wrongTypeFilter.itemsBlacklist() = {"h1"};

  CapacityWithGroupPresenceSpec wrongTypeSpec = spec;
  wrongTypeSpec.groupFilter() = wrongTypeFilter;
  wrongTypeSpec.name() = "wrongTypeSpec";

  REBALANCER_EXPECT_RUNTIME_ERROR(
      solver->addConstraint(wrongTypeSpec),
      "Expected filter to be of type FilterType::GROUP");
}

TEST_P(
    ProblemSolverChecksTest,
    checkAbsoluteEpsilonAndRelativeEpsilonAreNotTooSmall) {
  auto solver = makeInitializedSolver(GetParam());
  {
    facebook::algopt::common::thrift::PrecisionTolerances tolerances;
    tolerances.absolute() = 1e-17;
    tolerances.relative() = 1e-17;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->setPrecision(tolerances),
        "absoluteEpsilon must be bigger than std::numeric_limits<double>::epsilon(), but got 1e-17");
  }
  {
    facebook::algopt::common::thrift::PrecisionTolerances tolerances;
    tolerances.absolute() = 1e-12;
    tolerances.relative() = 1e-17;
    REBALANCER_EXPECT_RUNTIME_ERROR(
        solver->setPrecision(tolerances),
        "relativeEpsilon must be bigger than std::numeric_limits<double>::epsilon(), but got 1e-17");
  }
}

} // namespace facebook::rebalancer::interface::tests
