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
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/moves/GroupMoveWithHintStrategiesMoveType.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include <folly/container/irange.h>
#include <folly/coro/GtestHelpers.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

class MockGroupMoveWithHintStrategiesMoveType
    : public GroupMoveWithHintStrategiesMoveType {
 public:
  explicit MockGroupMoveWithHintStrategiesMoveType(
      const interface::LocalSearchSolverSpec& solverConfigs,
      const interface::GroupMoveWithHintStrategiesMoveTypeSpec& spec,
      const Problem& problem)
      : GroupMoveWithHintStrategiesMoveType(solverConfigs, spec, problem) {
    rng_.seed(std::random_device()());
  }

  MoveSet generateSampledContainersAndMoveSet(
      interface::MoveStrategyType strategy,
      const std::vector<entities::ContainerId>& scopeItemContainers,
      const std::vector<entities::ObjectId>& objectIds,
      const entities::ContainerId hotContainer,
      const Problem& problem) const {
    MoveStatsAggregator stats(problem.getUniverse().getPrecision());
    return GroupMoveWithHintStrategiesMoveType::
        generateSampledContainersAndMoveSet(
            strategy,
            scopeItemContainers,
            objectIds,
            hotContainer,
            problem,
            stats);
  }

  entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
  getPrimaryGroupToExplore(entities::GroupId currentGroup, Problem& problem) {
    return GroupMoveWithHintStrategiesMoveType::getPrimaryGroupToExplore(
        currentGroup, problem);
  }

  std::vector<MoveSet> generateAllMoveSets(
      entities::ObjectId hotObjectId,
      Problem& problem,
      const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
          secondaryGroupIdToMoveSets,
      const entities::ContainerId hotContainer) const {
    MoveStatsAggregator stats(problem.getUniverse().getPrecision());
    return GroupMoveWithHintStrategiesMoveType::generateAllMoveSets(
        hotObjectId, problem, secondaryGroupIdToMoveSets, hotContainer, stats);
  }

  static MoveSet getAllToUnassigned(
      const std::vector<entities::ObjectId>& objects,
      entities::ContainerId unassignedContainerId,
      const Problem& problem) {
    return GroupMoveWithHintStrategiesMoveType::getAllToUnassigned(
        objects, unassignedContainerId, problem);
  }

  static std::pair<MoveSet, std::optional<entities::GroupId>>
  findAllocatedSecondaryGroup(
      const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>&
          secondaryGroupIdToObjects,
      const std::optional<entities::ContainerId>& unassignedContainerId,
      const Problem& problem) {
    return GroupMoveWithHintStrategiesMoveType::findAllocatedSecondaryGroup(
        secondaryGroupIdToObjects, unassignedContainerId, problem);
  }

  std::vector<MoveSet> exploreTertiaryPartitionMoves(
      const std::vector<entities::ObjectId>& objects,
      const std::string& tertiaryPartitionName,
      int numScopeItemsToExplore,
      int moveSetsPerScopeItem,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          acceptingContainersPerScopeItem,
      const interface::MoveStrategyType& strategy,
      entities::ContainerId exclusionContainer,
      const Problem& problem,
      bool distinctScopeItems = false) const {
    MoveStatsAggregator stats(problem.getUniverse().getPrecision());
    return GroupMoveWithHintStrategiesMoveType::exploreTertiaryPartitionMoves(
        objects,
        tertiaryPartitionName,
        numScopeItemsToExplore,
        moveSetsPerScopeItem,
        acceptingContainersPerScopeItem,
        strategy,
        exclusionContainer,
        problem,
        stats,
        distinctScopeItems);
  }

  std::vector<MoveSet> exploreScopeItemMoves(
      const std::vector<entities::ObjectId>& objects,
      int moveSetsPerScopeItem,
      const ReferenceList<const std::vector<entities::ContainerId>>&
          acceptingContainersPerScopeItem,
      const interface::MoveStrategyType& strategy,
      entities::ContainerId exclusionContainer,
      const Problem& problem) const {
    MoveStatsAggregator stats(problem.getUniverse().getPrecision());
    return GroupMoveWithHintStrategiesMoveType::exploreScopeItemMoves(
        objects,
        moveSetsPerScopeItem,
        acceptingContainersPerScopeItem,
        strategy,
        exclusionContainer,
        problem,
        stats);
  }
};

class GroupMoveWithHintStrategiesTest : public MoveTestBase {
 protected:
  GroupMoveWithHintStrategiesTest() : MoveTestBase("shard", "rank") {}

  folly::coro::Task<std::shared_ptr<const entities::Universe>> setUpUniverse(
      int numObjects = 24,
      int numContainers = 8,
      int numTables = 3,
      bool swap = false) {
    // assume table is the primary partition
    // assume shardtype is the secondary partition
    // ranks are containers
    // shards are objects
    // nodes are scopes

    // 8 shards in each table
    // 3 tables
    // 4 shard types
    // 2 shards per shard type in each table

    // shards in table 1: 1-8
    // shards in table 2: 9-16
    // shards in table 3: 17-24

    // 4 nodes, each node has 2 ranks
    // ranks in node 1: 0, 1
    // ranks in node 2: 2, 3
    // ranks in node 3: 4, 5
    // ranks in node 4: 6, 7

    entities::Map<std::string, std::vector<std::string>> assignment;
    for (const auto i : folly::irange(numContainers)) {
      assignment[fmt::format("rank{}", i)] = {};
    }
    // create dummyRank with all the shards
    assignment["dummyRank"] = {};

    for (const auto i : folly::irange(numObjects)) {
      if (swap && (i % 4 == 0)) {
        assignment["rank0"].push_back(fmt::format("shard{}", i));
      } else {
        assignment["dummyRank"].push_back(fmt::format("shard{}", i));
      }
    }
    setInitialAssignment(assignment);

    // setup tables partition
    for (const auto t : folly::irange(numTables)) {
      const int numObjectsPerGroup = numObjects / numTables;
      std::vector<std::string> tableObjects;
      for (const auto s : folly::irange(numObjectsPerGroup)) {
        tableObjects.push_back(
            fmt::format("shard{}", t * numObjectsPerGroup + s));
      }
      tableToShards[fmt::format("table{}", t)] = std::move(tableObjects);
    }
    co_await addPartition("tables", tableToShards);

    // setup shardTypes partition
    entities::Map<std::string, std::vector<std::string>> shardTypes;
    for (const auto i : folly::irange(4)) {
      shardTypes[fmt::format("shardType{}", i)] = {};
    }
    for (const auto i : folly::irange(numObjects)) {
      shardTypes[fmt::format("shardType{}", i % 4)].push_back(
          fmt::format("shard{}", i));
    }
    co_await addPartition("shardTypes", shardTypes);

    // setup tertiaryPartition
    entities::Map<std::string, std::vector<std::string>> tertiaryPartition;
    for (const auto i : folly::irange(4)) {
      tertiaryPartition[fmt::format("tertiaryPartitonIds{}", i)] = {};
    }
    for (const auto i : folly::irange(numObjects)) {
      tertiaryPartition[fmt::format("tertiaryPartitonIds{}", i % 4)].push_back(
          fmt::format("shard{}", i));
    }
    co_await addPartition("tertiaryPartition", tertiaryPartition);

    // setup worlds scope
    entities::Map<std::string, std::vector<std::string>> worlds;
    worlds["fakeWorld"] = {"dummyRank"};
    std::vector<std::string> realWorldContainers;
    for (const auto i : folly::irange(numContainers)) {
      realWorldContainers.push_back(fmt::format("rank{}", i));
    }
    worlds["realWorld"] = realWorldContainers;
    co_await addScope("worlds", worlds);

    // setup nodes scope
    entities::Map<std::string, std::vector<std::string>> nodes;
    nodes["dummyNode"] = {"dummyRank"};
    for (const auto i : folly::irange(4)) {
      const int numContainersPerScopeItem = numContainers / 4;
      std::vector<std::string> nodeContainers;
      for (const auto j : folly::irange(numContainersPerScopeItem)) {
        nodeContainers.push_back(
            fmt::format("rank{}", i * numContainersPerScopeItem + j));
      }
      nodes[fmt::format("node{}", i)] = nodeContainers;
    }
    co_await addScope("nodes", nodes);

    co_return buildUniverse();
  }

  ExprPtr getLookupExprOn(
      const PackerSet<entities::ContainerId>& containers,
      int numObjects = 24) {
    auto objectValues = PackerMap<entities::ObjectId, double>();
    for (const auto id : folly::irange(numObjects)) {
      objectValues.emplace(object(id), id);
    }
    auto objectVector = makeObjectVector(objectValues, getUniverse());
    const Assignment assignment(
        getUniverse().getContainers().getInitialAssignment());
    return std::make_shared<ObjectLookup>(
        objectVector,
        std::make_shared<PackerSet<entities::ContainerId>>(containers),
        getUniverse(),
        assignment);
  }

  std::shared_ptr<ObjectVector> makeAllUnequalObjectVectorLocal(
      int objectCount) {
    PackerMap<entities::ObjectId, double> objectToValue;
    for (const auto i : folly::irange(objectCount)) {
      objectToValue[object(i)] = i;
    }
    return makeObjectVector(
        objectToValue, /*defaultValue=*/0, objectCount, getUniverse());
  }

  void validateMoveToAndFromUnassignedContainer(
      const MoveSet& moveset,
      int expectedMoveToUnassigned,
      int expectedMoveFromUnassigned) {
    auto moveToUnassigned = 0;
    auto moveFromUnassigned = 0;
    for (const auto& move : moveset) {
      if (move.getDestinationContainer() == container("dummyRank")) {
        moveToUnassigned++;
      } else if (move.getSourceContainer() == container("dummyRank")) {
        moveFromUnassigned++;
      }
    }
    EXPECT_EQ(moveToUnassigned, expectedMoveToUnassigned);
    EXPECT_EQ(moveFromUnassigned, expectedMoveFromUnassigned);
  }

 protected:
  PackerMap<std::string, std::vector<std::string>> tableToShards;
};

CO_TEST_F(GroupMoveWithHintStrategiesTest, TestRandomSamplingWithReplacement) {
  const auto universe = co_await setUpUniverse();
  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType1", option1);
  hintMap.emplace("shardType2", option2);
  hintMap.emplace("shardType3", option1);
  hintMap.emplace("shardType4", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  const MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());
  const std::vector<entities::ContainerId> containerIds = {container("rank1")};
  const std::vector<entities::ObjectId> objectIds = {object(0), object(4)};

  const auto generatedMoveSet =
      mockGroupMoveWithHintStrategiesMoveType
          .generateSampledContainersAndMoveSet(
              interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT,
              containerIds,
              objectIds,
              container("dummyRank"),
              getProblem());

  EXPECT_EQ(generatedMoveSet.size(), 2);
  // destination containers can repeat
  const auto expectedDestinationContainers =
      std::set<entities::ContainerId>({containerIds.at(0)});
  const auto actualDestinationContainers = std::set<entities::ContainerId>(
      {generatedMoveSet.at(0).getDestinationContainer(),
       generatedMoveSet.at(1).getDestinationContainer()});
  EXPECT_EQ(expectedDestinationContainers, actualDestinationContainers);

  // source container is dummyRank
  const auto expectedSourceContainers =
      std::set<entities::ContainerId>{container("dummyRank")};
  const auto actualSourceContainers = std::set<entities::ContainerId>(
      {generatedMoveSet.at(0).getSourceContainer(),
       generatedMoveSet.at(1).getSourceContainer()});
  EXPECT_EQ(expectedSourceContainers, actualSourceContainers);

  // both objects are moved
  const auto expectedObjects =
      std::set<entities::ObjectId>(objectIds.begin(), objectIds.end());
  const auto actualObjects = std::set<entities::ObjectId>(
      {generatedMoveSet.at(0).getObject(), generatedMoveSet.at(1).getObject()});
  EXPECT_EQ(expectedObjects, actualObjects);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, SampledMoveSetDroppedIfInvalid) {
  const auto universe = co_await setUpUniverse();
  auto filter = std::make_unique<InvalidMoveFilter>(
      universe->getNumObjects(),
      universe->getContainers().getContainerIds().size());
  filter->markInvalid({object(4)}, container("rank1"));
  createProblem(
      /*objectiveTuple=*/{const_expr(0, *universe)},
      /*constraint=*/const_expr(0, *universe),
      /*higherPriorityObjConfig=*/std::nullopt,
      /*nonAcceptingContainers=*/{},
      std::move(filter));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  const MockGroupMoveWithHintStrategiesMoveType moveType(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());
  const std::vector<entities::ContainerId> containerIds = {container("rank1")};
  const auto sample = [&](const std::vector<entities::ObjectId>& objectIds) {
    return moveType.generateSampledContainersAndMoveSet(
        interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT,
        containerIds,
        objectIds,
        container("dummyRank"),
        getProblem());
  };

  // object4 cannot move into rank1, so any set that moves it there is dropped.
  EXPECT_TRUE(sample({object(0), object(4)}).empty());
  const auto moveSet = sample({object(0)});
  CO_ASSERT_EQ(1, moveSet.size());
  EXPECT_EQ(
      Move(object(0), container("dummyRank"), container("rank1")),
      moveSet.at(0));
}
CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    TestRandomSamplingWithoutReplacement) {
  const auto universe =
      co_await setUpUniverse(/*numObjects=*/12, /*numContainers=*/8);
  createProblem(/*objectiveTuple=*/{const_expr(0, *universe)},
                /*constraint=*/const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType1", option1);
  hintMap.emplace("shardType2", option2);
  hintMap.emplace("shardType3", option1);
  hintMap.emplace("shardType4", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  const MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());
  const std::vector<entities::ContainerId> containerIds = {
      container("rank7"), container("rank5")};

  const std::vector<entities::ObjectId> objectIds = {object(0), object(4)};

  const auto generatedMoveSet =
      mockGroupMoveWithHintStrategiesMoveType
          .generateSampledContainersAndMoveSet(
              interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
              containerIds,
              objectIds,
              container("dummyRank"),
              getProblem());

  EXPECT_EQ(generatedMoveSet.size(), 2);
  const auto expectedDestinationContainers =
      std::set<entities::ContainerId>(containerIds.begin(), containerIds.end());
  const auto actualDestinationContainers = std::set<entities::ContainerId>(
      {generatedMoveSet.at(0).getDestinationContainer(),
       generatedMoveSet.at(1).getDestinationContainer()});
  EXPECT_EQ(expectedDestinationContainers, actualDestinationContainers);

  // source container is dummyRank
  const auto expectedSourceContainers =
      std::set<entities::ContainerId>{container("dummyRank")};
  const auto actualSourceContainers = std::set<entities::ContainerId>(
      {generatedMoveSet.at(0).getSourceContainer(),
       generatedMoveSet.at(1).getSourceContainer()});
  EXPECT_EQ(expectedSourceContainers, actualSourceContainers);

  // both objects are moved
  const auto expectedObjects =
      std::set<entities::ObjectId>(objectIds.begin(), objectIds.end());
  const auto actualObjects = std::set<entities::ObjectId>(
      {generatedMoveSet.at(0).getObject(), generatedMoveSet.at(1).getObject()});
  EXPECT_EQ(expectedObjects, actualObjects);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    RandomSamplingWithReplacementGeneratesEmptyWhenObjectsGreaterThanContainers) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType1", option1);
  hintMap.emplace("shardType2", option2);
  hintMap.emplace("shardType3", option1);
  hintMap.emplace("shardType4", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  const MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());
  std::vector<entities::ContainerId> containerIds;
  for (const auto i : folly::irange(2, 6)) {
    containerIds.push_back(container(i));
  }
  const std::vector<entities::ObjectId> objectIds = {
      object(0), object(4), object(8), object(12), object(16)};

  const auto generatedMoveSet =
      mockGroupMoveWithHintStrategiesMoveType
          .generateSampledContainersAndMoveSet(
              interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
              containerIds,
              objectIds,
              container("dummyRank"),
              getProblem());

  EXPECT_EQ(generatedMoveSet.size(), 0);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, TestMove) {
  const auto universe = co_await setUpUniverse();
  createProblem(/*objectiveTuple=*/{getLookupExprOn({container("dummyRank")})},
                /*constraint=*/const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType0", option1);
  hintMap.emplace("shardType1", option2);
  hintMap.emplace("shardType2", option1);
  hintMap.emplace("shardType3", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  // this will be ignored as we do not have unassigned container assigned
  interface::SecondaryGroupReplacementConfig config;
  config.secondaryGroupToAllowedReplacements() = {
      {"shardType0", {}},
      {"shardType1", {}},
      {"shardType2", {}},
      {"shardType3", {}}};
  moveTypeSpec.secondaryGroupReplacementConfig() = config;

  GroupMoveWithHintStrategiesMoveType groupMoveWithHintStrategiesMoveType(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto moveResult = groupMoveWithHintStrategiesMoveType.findBestMove(
      getMovesEvaluator(),
      container("dummyRank"),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_EQ(moveResult.getMoveSet().size(), 2);
  EXPECT_EQ(20, getTotalMovesEvaluated());

  validateMoveToAndFromUnassignedContainer(moveResult.getMoveSet(), 0, 2);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, TestSwapMove) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/32,
      /*numContainers=*/8,
      /*numTables=*/4,
      /*swap=*/true);
  createProblem(/*objectiveTuple=*/{getLookupExprOn({container("dummyRank")})},
                /*constraint=*/const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  moveTypeSpec.unassignedContainer() = "dummyRank";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType0", option1);
  hintMap.emplace("shardType1", option2);
  hintMap.emplace("shardType2", option1);
  hintMap.emplace("shardType3", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  GroupMoveWithHintStrategiesMoveType groupMoveWithHintStrategiesMoveType(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto moveResult = groupMoveWithHintStrategiesMoveType.findBestMove(
      getMovesEvaluator(),
      container("dummyRank"),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_EQ(moveResult.getMoveSet().size(), 4);

  // 2 moves from shardType0 because their scope is worlds and they don't need
  // to move back to dummyRank, 4 moves from shardType2 because their scope is
  // worlds, 16 moves from shardType1 and shardType3 because their scope is
  // nodes and there are 4 nodes 2 + 4 + 16 + 16 = 38
  EXPECT_EQ(38, getTotalMovesEvaluated());

  validateMoveToAndFromUnassignedContainer(moveResult.getMoveSet(), 2, 2);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, getPrimaryGroupToExplore) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType1", option1);
  hintMap.emplace("shardType2", option2);
  hintMap.emplace("shardType3", option1);
  hintMap.emplace("shardType4", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  const auto tablePartitionId = partitionId("tables");
  const auto tableId = groupId(tablePartitionId, "table0");

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());
  const auto tableToExplore =
      mockGroupMoveWithHintStrategiesMoveType.getPrimaryGroupToExplore(
          tableId, getProblem());

  EXPECT_EQ(tableToExplore.size(), 4);
  const auto partitionId = MoveTestBase::partitionId("shardTypes");
  EXPECT_GT(tableToExplore.at(groupId(partitionId, "shardType0")).size(), 0);
  EXPECT_GT(tableToExplore.at(groupId(partitionId, "shardType1")).size(), 0);
  EXPECT_GT(tableToExplore.at(groupId(partitionId, "shardType2")).size(), 0);
  EXPECT_GT(tableToExplore.at(groupId(partitionId, "shardType3")).size(), 0);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, generateAllMoveSetsNoValidMoveSets) {
  const auto universe =
      co_await setUpUniverse(/*numObjects=*/64, /*numContainers=*/8);
  createProblem(/*objectiveTuple=*/{const_expr(0, *universe)},
                /*constraint=*/const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;

  interface::MoveStrategy option;
  option.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType0", option);
  hintMap.emplace("shardType1", option);
  hintMap.emplace("shardType2", option);
  hintMap.emplace("shardType3", option);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartitionId = partitionId("tables");
  const auto tableId = groupId(tablePartitionId, "table0");
  const auto objectId = object(tableToShards.at("table0").at(0));
  const auto tableToExplore =
      mockGroupMoveWithHintStrategiesMoveType.getPrimaryGroupToExplore(
          tableId, getProblem());

  const auto allMoves =
      mockGroupMoveWithHintStrategiesMoveType.generateAllMoveSets(
          objectId, getProblem(), tableToExplore, container("dummyRank"));

  // expect no moves generated because container count is less than object count
  EXPECT_EQ(allMoves.size(), 0);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, generateAllMoveSetsSingularMoveSet) {
  const auto universe =
      co_await setUpUniverse(/*numObjects=*/3, /*numContainers=*/8);
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;

  interface::MoveStrategy option;
  option.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType0", option);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartitionId = partitionId("tables");
  const auto tableId = groupId(tablePartitionId, "table0");
  const auto objectId = object(tableToShards.at("table0").at(0));
  const auto tableToExplore =
      mockGroupMoveWithHintStrategiesMoveType.getPrimaryGroupToExplore(
          tableId, getProblem());

  const auto allMoves =
      mockGroupMoveWithHintStrategiesMoveType.generateAllMoveSets(
          objectId, getProblem(), tableToExplore, container("dummyRank"));

  // expect 1 moveset generated for each node -> 4 movesets generated
  EXPECT_EQ(allMoves.size(), 4);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, tertiaryMoveSetsOnly) {
  const auto universe = co_await setUpUniverse();
  createProblem(/*objectiveTuple=*/{const_expr(0, *universe)},
                /*constraint=*/const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  std::map<std::string, interface::MoveStrategy> hintMap;

  interface::MoveStrategy option;
  option.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option.moveToScopeItems() = moveToScopeItemsSpec2;
  option.tertiaryPartition() = "tertiaryPartition";
  option.numScopeItemsToExplorePerTertiaryGroup() = 10;

  hintMap.emplace("shardType0", option);
  hintMap.emplace("shardType1", option);
  hintMap.emplace("shardType2", option);
  hintMap.emplace("shardType3", option);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartitionId = partitionId("tables");
  const auto tableId = groupId(tablePartitionId, "table0");
  const auto objectId = object(tableToShards.at("table0").at(0));
  const auto tableToExplore =
      mockGroupMoveWithHintStrategiesMoveType.getPrimaryGroupToExplore(
          tableId, getProblem());

  const auto allMoves =
      mockGroupMoveWithHintStrategiesMoveType.generateAllMoveSets(
          objectId, getProblem(), tableToExplore, container("dummyRank"));

  // Each shardType generates 10 movesets, and there are 4 shardTypes -> 40
  // movesets
  EXPECT_EQ(allMoves.size(), 40);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, EnsureAllTablesExplored) {
  const int numObjects = 24;
  const auto universe = co_await setUpUniverse(numObjects);
  createProblem(
      {const_expr(0, *universe)},
      object_lookup(
          makeAllUnequalObjectVectorLocal(numObjects),
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container("dummyRank")}),
          Assignment(universe->getContainers().getInitialAssignment())));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  defaultScopeItems.scopeItems() = {"realWorld"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;

  interface::MoveStrategy strategy;
  strategy.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  strategy.moveToScopeItems() = moveToScopeItemsSpec;

  auto& thriftGroupToMoveStrategy =
      *moveTypeSpec.moveStrategies()->groupToMoveStrategy();
  thriftGroupToMoveStrategy.emplace("shardType0", strategy);
  thriftGroupToMoveStrategy.emplace("shardType1", strategy);
  thriftGroupToMoveStrategy.emplace("shardType2", strategy);
  thriftGroupToMoveStrategy.emplace("shardType3", strategy);

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto bestMove = mockGroupMoveWithHintStrategiesMoveType.findBestMove(
      getMovesEvaluator(),
      /*hotContainer=*/container("dummyRank"),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      /*timeLimit=*/std::numeric_limits<double>::max());

  // no move is expected to improve the objective because the objective is zero
  EXPECT_EQ(0, bestMove.getMoveSet().size());

  // we expect all tables to be explored
  // + there are 3 tables with 4 shard types each
  // + we explore 1 move per shardType and each shardType has 2 shards
  // So total moves explored = 3 * 4 * 2
  EXPECT_EQ(24, getTotalMovesEvaluated());
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ZeroMoveSetGeneratedForSecondaryGroup) {
  const int numObjects = 24;
  const auto universe = co_await setUpUniverse(numObjects);
  createProblem(
      {const_expr(0, *universe)},
      object_lookup(
          makeAllUnequalObjectVectorLocal(numObjects),
          std::make_shared<PackerSet<entities::ContainerId>>(
              PackerSet<entities::ContainerId>{container("dummyRank")}),
          Assignment(universe->getContainers().getInitialAssignment())));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  defaultScopeItems.scopeItems() = {"realWorld"};

  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;

  interface::MoveStrategy strategy;
  strategy.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  strategy.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy strategy2;
  strategy2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  strategy2.moveToScopeItems() = moveToScopeItemsSpec;
  strategy2.moveSetsGeneratedPerScopeItem() = 0;

  auto& thriftGroupToMoveStrategy =
      *moveTypeSpec.moveStrategies()->groupToMoveStrategy();
  thriftGroupToMoveStrategy.emplace("shardType0", strategy);
  thriftGroupToMoveStrategy.emplace("shardType1", strategy2);
  thriftGroupToMoveStrategy.emplace("shardType2", strategy);
  thriftGroupToMoveStrategy.emplace("shardType3", strategy2);

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto bestMove = mockGroupMoveWithHintStrategiesMoveType.findBestMove(
      getMovesEvaluator(),
      /*hotContainer=*/container("dummyRank"),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      /*timeLimit=*/std::numeric_limits<double>::max());

  // no move is expected to improve the objective because the objective is zero
  EXPECT_EQ(0, bestMove.getMoveSet().size());

  // we expect all tables to be explored
  // + there are 3 tables with 4 shard types each, 2 shard types we will not
  // generate movesets for
  // + we explore 1 move per shardType and each shardType has 2 shards
  // So total moves explored = 3 * 2 * 2 = 12
  EXPECT_EQ(12, getTotalMovesEvaluated());
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, GroupToExplore) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/32, /*numContainers=*/8, /*numTables=*/4, /*swap=*/true);
  createProblem(
      {getLookupExprOn({container("dummyRank")})}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  moveTypeSpec.unassignedContainer() = "dummyRank";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType0", option1);
  hintMap.emplace("shardType1", option2);
  hintMap.emplace("shardType2", option1);
  hintMap.emplace("shardType3", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  interface::SecondaryGroupReplacementConfig config;
  config.secondaryGroupToAllowedReplacements() = {
      {"shardType0", {"shardType2", "shardType3"}}};
  moveTypeSpec.secondaryGroupReplacementConfig() = config;

  GroupMoveWithHintStrategiesMoveType groupMoveWithHintStrategiesMoveType(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto moveResult = groupMoveWithHintStrategiesMoveType.findBestMove(
      getMovesEvaluator(),
      container("dummyRank"),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_EQ(moveResult.getMoveSet().size(), 4);

  // for shardType2, swap will generate 4 moves, and for shardType3, swap will
  // generate 4 moves per node (4 nodes)
  // so total moves evaluated = 4 + (4 * 4) = 20
  EXPECT_EQ(20, getTotalMovesEvaluated());

  validateMoveToAndFromUnassignedContainer(moveResult.getMoveSet(), 2, 2);
}

CO_TEST_F(GroupMoveWithHintStrategiesTest, GroupToExploreNoMoves) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/32, /*numContainers=*/8, /*numTables=*/4, /*swap=*/true);
  createProblem(
      {getLookupExprOn({container("dummyRank")})}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  moveTypeSpec.unassignedContainer() = "dummyRank";
  std::map<std::string, interface::MoveStrategy> hintMap;
  interface::MoveStrategy option1;
  option1.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "worlds";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option1.moveToScopeItems() = moveToScopeItemsSpec;

  interface::MoveStrategy option2;
  option2.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec2;
  interface::ScopeItemList defaultScopeItems2;
  defaultScopeItems2.scopeName() = "nodes";
  moveToScopeItemsSpec2.defaultScopeItems() = defaultScopeItems2;
  option2.moveToScopeItems() = moveToScopeItemsSpec2;

  hintMap.emplace("shardType0", option1);
  hintMap.emplace("shardType1", option2);
  hintMap.emplace("shardType2", option1);
  hintMap.emplace("shardType3", option2);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  interface::SecondaryGroupReplacementConfig config;
  config.secondaryGroupToAllowedReplacements() = {{"shardType0", {}}};
  moveTypeSpec.secondaryGroupReplacementConfig() = config;

  GroupMoveWithHintStrategiesMoveType groupMoveWithHintStrategiesMoveType(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto moveResult = groupMoveWithHintStrategiesMoveType.findBestMove(
      getMovesEvaluator(),
      container("dummyRank"),
      getMoveStatsAggregator(),
      getEmptySearchHints(),
      std::numeric_limits<double>::max());

  EXPECT_EQ(moveResult.getMoveSet().size(), 0);

  EXPECT_EQ(0, getTotalMovesEvaluated());

  validateMoveToAndFromUnassignedContainer(moveResult.getMoveSet(), 0, 0);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    findAllocatedSecondaryGroupNoUnassignedContainer) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartId = partitionId("tables");
  const auto tableId = groupId(tablePartId, "table0");
  const auto secondaryGroupToObjects =
      mock.getPrimaryGroupToExplore(tableId, getProblem());

  const auto [moveSet, allocatedGroupId] =
      MockGroupMoveWithHintStrategiesMoveType::findAllocatedSecondaryGroup(
          secondaryGroupToObjects, std::nullopt, getProblem());

  EXPECT_EQ(moveSet.size(), 0);
  EXPECT_FALSE(allocatedGroupId.has_value());
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    findAllocatedSecondaryGroupAllObjectsUnassigned) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartId = partitionId("tables");
  const auto tableId = groupId(tablePartId, "table0");
  const auto secondaryGroupToObjects =
      mock.getPrimaryGroupToExplore(tableId, getProblem());

  const auto [moveSet, allocatedGroupId] =
      MockGroupMoveWithHintStrategiesMoveType::findAllocatedSecondaryGroup(
          secondaryGroupToObjects,
          std::make_optional(container("dummyRank")),
          getProblem());

  EXPECT_EQ(moveSet.size(), 0);
  EXPECT_FALSE(allocatedGroupId.has_value());
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    findAllocatedSecondaryGroupReturnsAllocatedGroup) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/32, /*numContainers=*/8, /*numTables=*/4, /*swap=*/true);
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartId = partitionId("tables");
  const auto tableId = groupId(tablePartId, "table0");
  const auto secondaryGroupToObjects =
      mock.getPrimaryGroupToExplore(tableId, getProblem());

  const auto dummyRankId = container("dummyRank");
  const auto [moveSet, allocatedGroupId] =
      MockGroupMoveWithHintStrategiesMoveType::findAllocatedSecondaryGroup(
          secondaryGroupToObjects,
          std::make_optional(dummyRankId),
          getProblem());

  EXPECT_TRUE(allocatedGroupId.has_value());

  // shardType0 is the only group with objects on rank0 (i.e., allocated)
  const auto shardTypesPartId = partitionId("shardTypes");
  EXPECT_EQ(allocatedGroupId.value(), groupId(shardTypesPartId, "shardType0"));

  // shardType0 in table0 has 2 objects (shard0 and shard4), both on rank0
  EXPECT_EQ(moveSet.size(), 2);
  for (const auto& move : moveSet) {
    EXPECT_EQ(move.getDestinationContainer(), dummyRankId);
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreTertiaryPartitionMovesGeneratesMoveSets) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // shard0 -> tertiaryPartitonIds0, shard1 -> tertiaryPartitonIds1
  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  // 4 scope items (nodes), each with 2 containers
  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")},
      {container("rank4"), container("rank5")},
      {container("rank6"), container("rank7")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreTertiaryPartitionMoves(
      objects,
      "tertiaryPartition",
      /*numScopeItemsToExplore=*/5,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  EXPECT_EQ(moveSets.size(), 10);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
    for (const auto& move : moveSet) {
      EXPECT_NE(move.getDestinationContainer(), container("dummyRank"));
      EXPECT_EQ(move.getSourceContainer(), container("dummyRank"));
    }
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreTertiaryPartitionMovesUsesDistinctScopeItems) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const std::vector<entities::ObjectId> objects = {
      object(0),
      object(1),
      object(2),
      object(3),
      object(4),
      object(5),
      object(6),
      object(7)};
  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")},
      {container("rank4"), container("rank5")},
      {container("rank6"), container("rank7")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  entities::Map<entities::ContainerId, size_t> containerToScopeItem;
  for (const auto scopeItemIndex : folly::irange(scopeItemContainers.size())) {
    const auto& containers = scopeItemContainers.at(scopeItemIndex);
    acceptingContainersPerScopeItem.push_back(std::cref(containers));
    for (const auto containerId : containers) {
      containerToScopeItem[containerId] = scopeItemIndex;
    }
  }

  const auto moveSets = mock.exploreTertiaryPartitionMoves(
      objects,
      "tertiaryPartition",
      /*numScopeItemsToExplore=*/5,
      /*moveSetsPerScopeItem=*/1,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem(),
      /*distinctScopeItems=*/true);

  EXPECT_EQ(moveSets.size(), 5);
  for (const auto& moveSet : moveSets) {
    entities::Set<size_t> destinationScopeItems;
    for (const auto& move : moveSet) {
      destinationScopeItems.insert(
          containerToScopeItem.at(move.getDestinationContainer()));
    }
    EXPECT_EQ(moveSet.size(), objects.size());
    EXPECT_EQ(destinationScopeItems.size(), 4);
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreTertiaryPartitionMovesZeroExploreIterations) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreTertiaryPartitionMoves(
      objects,
      "tertiaryPartition",
      /*numScopeItemsToExplore=*/0,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  EXPECT_EQ(moveSets.size(), 0);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreTertiaryPartitionMovesSingleGroup) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // shard0 and shard4 are both in tertiaryPartitonIds0 (0%4=0, 4%4=0)
  const std::vector<entities::ObjectId> objects = {object(0), object(4)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")},
      {container("rank4"), container("rank5")},
      {container("rank6"), container("rank7")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreTertiaryPartitionMoves(
      objects,
      "tertiaryPartition",
      /*numScopeItemsToExplore=*/5,
      /*moveSetsPerScopeItem=*/1,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  EXPECT_EQ(moveSets.size(), 5);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreTertiaryPartitionMovesInsufficientContainersWithoutReplacement) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // shard0, shard4, shard8 all in tertiaryPartitonIds0 (3 objects, 1 group)
  const std::vector<entities::ObjectId> objects = {
      object(0), object(4), object(8)};

  // Each scope item has only 2 containers, less than the 3 objects per group
  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreTertiaryPartitionMoves(
      objects,
      "tertiaryPartition",
      /*numScopeItemsToExplore=*/5,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  EXPECT_EQ(moveSets.size(), 0);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreTertiaryPartitionMovesWithReplacementStrategy) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // shard0 -> tertiaryPartitonIds0, shard1 -> tertiaryPartitonIds1
  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")},
      {container("rank4"), container("rank5")},
      {container("rank6"), container("rank7")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreTertiaryPartitionMoves(
      objects,
      "tertiaryPartition",
      /*numScopeItemsToExplore=*/3,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  EXPECT_EQ(moveSets.size(), 6);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesGeneratesMoveSets) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")},
      {container("rank4"), container("rank5")},
      {container("rank6"), container("rank7")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  // containers.size() (2) == objects.size() (2) with WITHOUT_REPLACEMENT
  // so hasUniqueSample is true, generating only 1 moveset per scope item
  // 4 scope items * 1 = 4
  EXPECT_EQ(moveSets.size(), 4);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
    for (const auto& move : moveSet) {
      EXPECT_NE(move.getDestinationContainer(), container("dummyRank"));
      EXPECT_EQ(move.getSourceContainer(), container("dummyRank"));
    }
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesWithReplacementStrategy) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/3,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITH_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  // WITH_REPLACEMENT: hasUniqueSample is false, so 3 movesets per scope item
  // 2 scope items * 3 = 6
  EXPECT_EQ(moveSets.size(), 6);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesInsufficientContainers) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // 3 objects but each scope item only has 2 containers
  const std::vector<entities::ObjectId> objects = {
      object(0), object(1), object(2)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")},
      {container("rank2"), container("rank3")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  // All scope items have fewer containers than objects, so all are skipped
  EXPECT_EQ(moveSets.size(), 0);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesExtraContainersGeneratesMultiple) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // 2 objects, scope items with 3+ containers (not a unique sample)
  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1"), container("rank2")},
      {container("rank3"), container("rank4"), container("rank5")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/3,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  // containers.size() (3) > objects.size() (2) with WITHOUT_REPLACEMENT
  // hasUniqueSample is false, so generates moveSetsPerScopeItem per scope item
  // 2 scope items * 3 = 6
  EXPECT_EQ(moveSets.size(), 6);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesSkipsScopeItemsWithInsufficientContainers) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  // 2 objects, mix of scope items: some with enough, some without
  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0")},
      {container("rank2"), container("rank3")},
      {container("rank4")},
      {container("rank6"), container("rank7")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/2,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  // Scope items with 1 container are skipped (insufficient for 2 objects)
  // 2 valid scope items with containers.size() == objects.size() -> unique
  // sample 2 scope items * 1 = 2
  EXPECT_EQ(moveSets.size(), 2);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), objects.size());
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    GetAllToUnassignedCreatesCorrectMoves) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/32, /*numContainers=*/8, /*numTables=*/4, /*swap=*/true);
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  // shard0 and shard4 are on rank0 (swap=true assigns i%4==0 to rank0)
  const std::vector<entities::ObjectId> objects = {object(0), object(4)};
  const auto dummyRankId = container("dummyRank");

  const auto moveSet =
      MockGroupMoveWithHintStrategiesMoveType::getAllToUnassigned(
          objects, dummyRankId, getProblem());

  EXPECT_EQ(moveSet.size(), 2);
  for (const auto& move : moveSet) {
    EXPECT_EQ(move.getDestinationContainer(), dummyRankId);
    EXPECT_EQ(move.getSourceContainer(), container("rank0"));
  }

  const auto movedObjects = std::set<entities::ObjectId>(
      {moveSet.at(0).getObject(), moveSet.at(1).getObject()});
  const auto expectedObjects =
      std::set<entities::ObjectId>(objects.begin(), objects.end());
  EXPECT_EQ(movedObjects, expectedObjects);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    GetAllToUnassignedSkipsSameContainerMoves) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  // All objects are on dummyRank, so moving them to dummyRank produces
  // moves where source == destination (the method still creates them)
  const std::vector<entities::ObjectId> objects = {object(0), object(1)};
  const auto dummyRankId = container("dummyRank");

  const auto moveSet =
      MockGroupMoveWithHintStrategiesMoveType::getAllToUnassigned(
          objects, dummyRankId, getProblem());

  // getAllToUnassigned always creates moves, even source == dest
  EXPECT_EQ(moveSet.size(), 2);
  for (const auto& move : moveSet) {
    EXPECT_EQ(move.getSourceContainer(), dummyRankId);
    EXPECT_EQ(move.getDestinationContainer(), dummyRankId);
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    findAllocatedSecondaryGroupEmptyMap) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  const entities::Map<entities::GroupId, std::vector<entities::ObjectId>>
      emptyMap;

  const auto [moveSet, allocatedGroupId] =
      MockGroupMoveWithHintStrategiesMoveType::findAllocatedSecondaryGroup(
          emptyMap, std::make_optional(container("dummyRank")), getProblem());

  EXPECT_EQ(moveSet.size(), 0);
  EXPECT_FALSE(allocatedGroupId.has_value());
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesEmptyAcceptingContainers) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const std::vector<entities::ObjectId> objects = {object(0), object(1)};

  ReferenceList<const std::vector<entities::ContainerId>>
      emptyAcceptingContainers;

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/2,
      emptyAcceptingContainers,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  EXPECT_EQ(moveSets.size(), 0);
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    ExploreScopeItemMovesSingleObjectSingleScopeItem) {
  const auto universe = co_await setUpUniverse();
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";

  const MockGroupMoveWithHintStrategiesMoveType mock(
      interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const std::vector<entities::ObjectId> objects = {object(0)};

  const std::vector<std::vector<entities::ContainerId>> scopeItemContainers = {
      {container("rank0"), container("rank1")}};

  ReferenceList<const std::vector<entities::ContainerId>>
      acceptingContainersPerScopeItem;
  for (const auto& vec : scopeItemContainers) {
    acceptingContainersPerScopeItem.push_back(std::cref(vec));
  }

  const auto moveSets = mock.exploreScopeItemMoves(
      objects,
      /*moveSetsPerScopeItem=*/3,
      acceptingContainersPerScopeItem,
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT,
      container("dummyRank"),
      getProblem());

  // containers.size() (2) > objects.size() (1) → not unique sample
  // 1 scope item * 3 movesets = 3
  EXPECT_EQ(moveSets.size(), 3);
  for (const auto& moveSet : moveSets) {
    EXPECT_EQ(moveSet.size(), 1);
    EXPECT_EQ(moveSet.at(0).getSourceContainer(), container("dummyRank"));
    EXPECT_NE(moveSet.at(0).getDestinationContainer(), container("dummyRank"));
  }
}

CO_TEST_F(
    GroupMoveWithHintStrategiesTest,
    generateAllMoveSetsWithTertiaryAndSwap) {
  const auto universe = co_await setUpUniverse(
      /*numObjects=*/32, /*numContainers=*/8, /*numTables=*/4, /*swap=*/true);
  createProblem({const_expr(0, *universe)}, const_expr(0, *universe));

  interface::GroupMoveWithHintStrategiesMoveTypeSpec moveTypeSpec;
  moveTypeSpec.primaryPartition() = "tables";
  moveTypeSpec.secondaryPartition() = "shardTypes";
  moveTypeSpec.unassignedContainer() = "dummyRank";
  std::map<std::string, interface::MoveStrategy> hintMap;

  interface::MoveStrategy option;
  option.type() =
      interface::MoveStrategyType::RANDOM_SAMPLING_WITHOUT_REPLACEMENT;
  interface::MoveToScopeItemsSpec moveToScopeItemsSpec;
  interface::ScopeItemList defaultScopeItems;
  defaultScopeItems.scopeName() = "nodes";
  moveToScopeItemsSpec.defaultScopeItems() = defaultScopeItems;
  option.moveToScopeItems() = moveToScopeItemsSpec;
  option.tertiaryPartition() = "tertiaryPartition";
  option.numScopeItemsToExplorePerTertiaryGroup() = 2;

  hintMap.emplace("shardType0", option);
  hintMap.emplace("shardType1", option);
  hintMap.emplace("shardType2", option);
  hintMap.emplace("shardType3", option);

  moveTypeSpec.moveStrategies()->groupToMoveStrategy() = hintMap;

  MockGroupMoveWithHintStrategiesMoveType
      mockGroupMoveWithHintStrategiesMoveType(
          interface::LocalSearchSolverSpec{}, moveTypeSpec, getProblem());

  const auto tablePartitionId = partitionId("tables");
  const auto tableId = groupId(tablePartitionId, "table0");
  const auto objectId = object(tableToShards.at("table0").at(0));
  const auto tableToExplore =
      mockGroupMoveWithHintStrategiesMoveType.getPrimaryGroupToExplore(
          tableId, getProblem());

  const auto allMoves =
      mockGroupMoveWithHintStrategiesMoveType.generateAllMoveSets(
          objectId, getProblem(), tableToExplore, container("dummyRank"));

  // With swap=true, shardType0 has objects on rank0 (allocated).
  // findAllocatedSecondaryGroup finds shardType0, creates baseMoveSet.
  // For the other 3 shardTypes (shardType1-3), generateAllMoveSets merges
  // baseMoveSet into their movesets.
  // Verify all movesets are non-empty
  EXPECT_GT(allMoves.size(), 0);
  for (const auto& moveSet : allMoves) {
    EXPECT_GT(moveSet.size(), 0);
  }
}

} // namespace facebook::rebalancer::packer::tests
