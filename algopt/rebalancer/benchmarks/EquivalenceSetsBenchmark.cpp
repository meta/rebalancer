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

#include "algopt/rebalancer/entities/ObjectValueTypes.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/interface/UniverseProblemBuilder.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionMoveLimit.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <folly/init/Init.h>

#include <memory>
#include <string>
#include <vector>

using namespace facebook::rebalancer;

std::unique_ptr<Problem> buildTestProblem(int numObjects, int numContainers) {
  entities::tests::UniverseBuilderTestUtils builder("object", "container");

  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(numContainers)) {
    initialAssignment[fmt::format("container{}", i)] = {};
  }
  for (const auto i : folly::irange(numObjects)) {
    initialAssignment["container0"].push_back(fmt::format("object{}", i));
  }
  builder.setInitialAssignment(initialAssignment);

  const auto universe = builder.buildUniverse();
  return packer::tests::createTestProblem(
      universe, {const_expr(0, *universe)}, const_expr(0, *universe));
}

BENCHMARK(EquivalenceSetsAccessTest) {
  folly::BenchmarkSuspender suspend;
  auto problem = buildTestProblem(1e5, 100);
  // artificially inject a lot of arbitrary equivalence set to the store
  for (const auto i : folly::irange(10)) {
    problem->getEquivalenceSetsStore().override(
        EquivalenceSets(problem->getUniverse(), fmt::format("es_{}", i)));
  }
  const folly::F14FastSet<std::string> emptySet;
  // reinitialize with all goals and constraints
  problem->getEquivalenceSetsStore().initialize(
      /*excludeConstraintNames=*/emptySet, /*excludeGoalNames=*/emptySet);
  suspend.dismiss();

  // Equivalence set access are quite common during move evaluation, the
  // following test ensures that they are as fast as possible
  folly::F14FastSet<entities::EquivalenceSetId> allEquivSets;
  for (auto objId : problem->getUniverse().getObjects().getObjectIds()) {
    allEquivSets.insert(problem->getEquivalenceSets().at(objId));
  }
  assert(allEquivSets.size() == problem->getEquivalenceSets().size());
  folly::doNotOptimizeAway(allEquivSets);
}

void benchmarkMappingMerge(int numObjects, int numEqSets, int nIter) {
  folly::BenchmarkSuspender suspend;
  entities::tests::UniverseBuilderTestUtils builder("object", "container");

  entities::Map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(numObjects)) {
    initialAssignment["container0"].push_back(fmt::format("object{}", i));
  }
  builder.setInitialAssignment(initialAssignment);
  const auto universe = builder.buildUniverse();

  PackerMap<entities::ObjectId, int> objectToValue;
  for (const auto i : folly::irange(numObjects)) {
    objectToValue[builder.object(i)] = i % numEqSets;
  }
  suspend.dismiss();

  for (const auto _ : folly::irange(nIter)) {
    EquivalenceSets equivalenceSets(*universe);
    equivalenceSets.mappingMerge(objectToValue);
    folly::doNotOptimizeAway(equivalenceSets);
  }
}

BENCHMARK(EquivalenceSetsMappingMerge1) {
  benchmarkMappingMerge(/*numObjects=*/1e6, /*numEqSets=*/1, /*nIter=*/500);
}

BENCHMARK(EquivalenceSetsMappingMerge2) {
  benchmarkMappingMerge(/*numObjects=*/1e6, /*numEqSets=*/10e3, /*nIter=*/500);
}

struct GroupBackedMappingMergeFixture {
  std::shared_ptr<const entities::Universe> universe;
  std::shared_ptr<const entities::Partition> partition;
  std::shared_ptr<const entities::GroupIdToDoubleMap> groupValues;
  entities::ObjectValues objectValues;
};

template <typename ObjectValuesT = entities::ObjectValues>
ObjectValuesT makeGroupBackedObjectValues(
    std::shared_ptr<const entities::GroupIdToDoubleMap> groupValues,
    std::shared_ptr<const entities::Partition> partition,
    std::size_t totalObjects,
    entities::PartitionId partitionId) {
  if constexpr (requires {
                  ObjectValuesT(
                      groupValues, partition, 0.0, totalObjects, partitionId);
                }) {
    return ObjectValuesT(
        std::move(groupValues),
        std::move(partition),
        /*defaultValue=*/0.0,
        totalObjects,
        partitionId);
  } else {
    return ObjectValuesT(
        std::move(groupValues),
        std::move(partition),
        /*defaultValue=*/0.0,
        totalObjects);
  }
}

const GroupBackedMappingMergeFixture& groupBackedMappingMergeFixture() {
  constexpr int kNumObjects = 200'000;
  constexpr int kNumGroups = 1'000;

  static const GroupBackedMappingMergeFixture fixture = [] {
    const std::string kPartitionName = "partition";
    entities::tests::UniverseBuilderTestUtils builder("object", "container");

    entities::Map<std::string, std::vector<std::string>> initialAssignment;
    initialAssignment["container0"].reserve(kNumObjects);

    entities::Map<std::string, std::vector<std::string>> groupToObjectNames;
    groupToObjectNames.reserve(kNumGroups);
    for (const auto groupIndex : folly::irange(kNumGroups)) {
      groupToObjectNames[fmt::format("group{}", groupIndex)].reserve(
          kNumObjects / kNumGroups);
    }

    for (const auto objectIndex : folly::irange(kNumObjects)) {
      const auto objectName = fmt::format("object{}", objectIndex);
      initialAssignment["container0"].push_back(objectName);
      groupToObjectNames[fmt::format("group{}", objectIndex % kNumGroups)]
          .push_back(objectName);
    }
    builder.setInitialAssignment(initialAssignment);
    folly::coro::blockingWait(
        builder.addPartition(kPartitionName, groupToObjectNames));
    const auto universe = builder.buildUniverse();
    const auto partitionId = builder.partitionId(kPartitionName);

    entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
        groupsById;
    groupsById.reserve(groupToObjectNames.size());
    auto groupValues = std::make_shared<entities::GroupIdToDoubleMap>();
    groupValues->reserve(groupToObjectNames.size());

    for (const auto groupIndex : folly::irange(kNumGroups)) {
      const auto groupName = fmt::format("group{}", groupIndex);
      const auto groupId = builder.groupId(partitionId, groupName);
      auto objectIds = std::vector<entities::ObjectId>{};
      objectIds.reserve(groupToObjectNames[groupName].size());
      for (const auto& objectName : groupToObjectNames[groupName]) {
        objectIds.push_back(builder.object(objectName));
      }
      groupsById.emplace(groupId, std::move(objectIds));
      groupValues->emplace(groupId, static_cast<double>(groupIndex) + 1.0);
    }

    auto partition =
        std::make_shared<const entities::Partition>(std::move(groupsById));
    return GroupBackedMappingMergeFixture{
        .universe = universe,
        .partition = partition,
        .groupValues = groupValues,
        .objectValues = makeGroupBackedObjectValues(
            groupValues, partition, kNumObjects, partitionId)};
  }();

  return fixture;
}

BENCHMARK(EquivalenceSetsMappingMergeGroupBackedObjectValues) {
  folly::BenchmarkSuspender suspend;
  const auto& fixture = groupBackedMappingMergeFixture();
  suspend.dismiss();

  EquivalenceSets equivalenceSets(*fixture.universe);
  equivalenceSets.mappingMerge(fixture.objectValues);
  folly::doNotOptimizeAway(equivalenceSets);
}

// Measures EquivalenceSets::at() throughput
void benchmarkAtThroughput(int numObjects, int nIter) {
  folly::BenchmarkSuspender suspend;
  const auto problem = buildTestProblem(numObjects, /*numContainers=*/100);
  const auto& universe = problem->getUniverse();
  const auto& objectIds = universe.getObjects().getObjectIds();
  constexpr int kNumEqSets = 1024;

  PackerMap<entities::ObjectId, int> objectToValue;
  for (const auto objectId : objectIds) {
    objectToValue[objectId] = objectId.asIndex() % kNumEqSets;
  }
  EquivalenceSets equivalenceSets(universe);
  equivalenceSets.mappingMerge(objectToValue);
  suspend.dismiss();

  size_t acc = 0;
  for (const auto _ : folly::irange(nIter)) {
    for (const auto objectId : objectIds) {
      acc += static_cast<size_t>(equivalenceSets.at(objectId));
    }
  }
  folly::doNotOptimizeAway(acc);
}

BENCHMARK(EquivalenceSetsAtThroughput) {
  benchmarkAtThroughput(/*numObjects=*/1e6, /*nIter=*/100);
}

BENCHMARK(GroupBackedMoveLimitEquivalence) {
  folly::BenchmarkSuspender suspend;
  constexpr int kObjects = 100'000;
  constexpr int kContainers = 1'000;
  constexpr int kCostGroups = 100;
  interface::UniverseProblemBuilder builder(nullptr);
  builder.setObjectName("object");
  builder.setContainerName("container");
  builder.setGroupBackedDynamicDimensions(true);
  entities::Map<std::string, std::vector<std::string>> assignment;
  entities::Map<std::string, std::vector<std::string>> groups;
  entities::Map<std::string, entities::Map<std::string, double>> values;
  std::vector<std::string> objects;
  for (int c = 0; c < kContainers; ++c) {
    const auto container = fmt::format("container{}", c);
    assignment[container] = {};
    for (int g = 0; g < kCostGroups; ++g) {
      values[container][fmt::format("group{}", g)] = 2.0 + (c + g) % 3;
    }
  }
  for (int o = 0; o < kObjects; ++o) {
    const auto object = fmt::format("object{}", o);
    objects.push_back(object);
    assignment["container0"].push_back(object);
    groups[fmt::format("group{}", o % kCostGroups)].push_back(object);
  }
  builder.setAssignment(assignment);
  builder.addPartition("cost", groups);
  builder.addPartition(
      "migration",
      entities::Map<std::string, std::vector<std::string>>{{"all", objects}});
  builder.addDynamicObjectDimension("cost", "container", "cost", values, 1.0);
  const auto universe = builder.build();
  ObjectPartitionMoveLimit expression(
      *universe, Assignment(universe->getContainers().getInitialAssignment()),
      universe->getPartitionId("migration"), universe->getDimensionId("cost"),
      {}, {}, {});
  suspend.dismiss();

  EquivalenceSets sets(*universe);
  expression.updateEquivalenceSets(sets);
  sets.finalize();
  folly::doNotOptimizeAway(sets);
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
