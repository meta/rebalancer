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
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/FixedDestMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedDestSwapMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedSourceMoveType.h"
#include "algopt/rebalancer/solver/moves/FixedSourceMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/GreedyGroupToScopeItemMoveType.h"
#include "algopt/rebalancer/solver/moves/GroupRoutingMoveType.h"
#include "algopt/rebalancer/solver/moves/KLSearchMoveType.h"
#include "algopt/rebalancer/solver/moves/MoveTypeFactory.h"
#include "algopt/rebalancer/solver/moves/ReplicaDropMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleChainFastMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleChainMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleEndChainMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleFastMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleGreedyMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleRandomBatchesMoveType.h"
#include "algopt/rebalancer/solver/moves/SingleRandomStratifiedMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapFullContainersMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapFullWithEmptyContainersMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapMoveType.h"
#include "algopt/rebalancer/solver/moves/SwapNMoveType.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/moves/TripleLoopMoveType.h"

#include <gtest/gtest.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <memory>
#include <utility>

using namespace facebook::rebalancer;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::packer::tests {

class MoveTypeFactoryTest : public MoveTestBase {
 protected:
  MoveTypeFactoryTest() : MoveTestBase("object", "container") {}

  void SetUp() override {
    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"container0", {"object0"}}});
    const auto universe = buildUniverse();
    createProblem(
        /*objectiveTuple=*/{const_expr(0, *universe)},
        /*constraint=*/const_expr(0, *universe));
  }
};

TEST_F(MoveTypeFactoryTest, createLSSSWithEmptyNamesAndSpecs) {
  LocalSearchSolverSpec spec;
  EXPECT_EQ(spec.moveTypes()->size(), 0);
  EXPECT_EQ(spec.moveTypeList()->size(), 0);

  MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);
  EXPECT_EQ(spec.moveTypes()->size(), 0);
  EXPECT_EQ(spec.moveTypeList()->size(), 0);

  // nothing should be created because moveTypeList is empty
  auto moves = MoveTypeFactory::createMoveTypes(spec, getProblem());
  EXPECT_EQ(moves.size(), 0);
}

TEST_F(MoveTypeFactoryTest, createLSSSAndSetMoveTypes) {
  LocalSearchSolverSpec spec;

  auto& moveTypes = *spec.moveTypes();
  moveTypes = {"SINGLE"};
  EXPECT_EQ(moveTypes.size(), 1);
  EXPECT_EQ(moveTypes.at(0), "SINGLE");

  const auto& moveTypeList = *spec.moveTypeList();
  EXPECT_EQ(moveTypeList.size(), 0);

  MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);
  EXPECT_EQ(moveTypes.size(), 0);
  EXPECT_EQ(moveTypeList.size(), 1);
  EXPECT_EQ(
      MoveTypeSpec::Type::singleMoveTypeSpec, moveTypeList.at(0).getType());

  // creation should happen
  auto moves = MoveTypeFactory::createMoveTypes(spec, getProblem());
  EXPECT_EQ(moves.size(), 1);
  EXPECT_EQ(moves.at(0)->name(), "SINGLE");
}

TEST_F(MoveTypeFactoryTest, createLSSSAndSetMoveTypeList) {
  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  EXPECT_EQ(spec.moveTypes()->size(), 0);
  EXPECT_EQ(spec.moveTypeList()->size(), 2);

  MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);
  EXPECT_EQ(spec.moveTypes()->size(), 0);
  EXPECT_EQ(spec.moveTypeList()->size(), 2);

  // creation should happen
  auto moves = MoveTypeFactory::createMoveTypes(spec, getProblem());
  EXPECT_EQ(moves.size(), 2);
}

TEST_F(MoveTypeFactoryTest, createLSSSAndSetMoveTypeListWithMixedTypes) {
  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec("GROUP_ROUTING"));
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
  spec.groupRoutingMoveTypeSpec() = GroupRoutingMoveTypeSpec();
  EXPECT_EQ(spec.moveTypes()->size(), 0);
  EXPECT_EQ(spec.moveTypeList()->size(), 2);

  MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);
  EXPECT_EQ(spec.moveTypes()->size(), 0);
  EXPECT_EQ(spec.moveTypeList()->size(), 2);
  EXPECT_EQ(
      spec.moveTypeList()->at(0).getType(),
      MoveTypeSpec::Type::groupRoutingMoveTypeSpec);

  // creation should happen
  auto moves = MoveTypeFactory::createMoveTypes(spec, getProblem());
  EXPECT_EQ(moves.size(), 2);
}

TEST_F(MoveTypeFactoryTest, rolloutDefaultsUnsetParallelExecutionToAuto) {
  LocalSearchSolverSpec spec;
  spec.moveTypeList()->push_back(
      ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
  const auto isExploring = [&] {
    const auto moves = MoveTypeFactory::createMoveTypes(spec, getProblem());
    return std::as_const(*moves.front())
        .parallelExecutionSelector()
        .isExploring();
  };

  EXPECT_FALSE(isExploring());

  getProblem().configs.useAutoParallelExecution = true;
  EXPECT_TRUE(isExploring());

  // An explicit config wins over the rollout default.
  spec.parallelExecutionConfig().emplace().set_slidingWindow(
      SlidingWindowExecutionConfig{});
  EXPECT_FALSE(isExploring());
}

// Round-trips through the serializer so the spec carries the presence bits a
// saved bundle would have, which is what the backfill keys off.
static SingleRandomStratifiedMoveTypeSpec savedStratifiedSpec() {
  return apache::thrift::CompactSerializer::deserialize<
      SingleRandomStratifiedMoveTypeSpec>(
      apache::thrift::CompactSerializer::serialize<std::string>(
          SingleRandomStratifiedMoveTypeSpec()));
}

// A bundle predating minObjectsToExplore: T_STOP encodes the empty struct.
static SingleRandomStratifiedMoveTypeSpec preFieldStratifiedSpec() {
  return apache::thrift::CompactSerializer::deserialize<
      SingleRandomStratifiedMoveTypeSpec>(std::string(1, '\0'));
}

TEST_F(MoveTypeFactoryTest, replayBackfillsMinObjectsToExploreOnlyWhenMissing) {
  {
    // Typed specs from old bundles inherit the saved solver-wide value.
    LocalSearchSolverSpec spec;
    spec.minHotObjects() = 7;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(preFieldStratifiedSpec()));

    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);

    EXPECT_EQ(
        7,
        *spec.moveTypeList()
             ->at(0)
             .singleRandomStratifiedMoveTypeSpec()
             .as_const()
             .value()
             .minObjectsToExplore());
  }

  {
    // Name-based configs kept their stratified spec at the solver level.
    LocalSearchSolverSpec spec;
    spec.minHotObjects() = 7;
    spec.singleRandomStratifiedMoveTypeSpec() = preFieldStratifiedSpec();
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));

    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);

    EXPECT_EQ(
        7, *spec.singleRandomStratifiedMoveTypeSpec()->minObjectsToExplore());
  }

  {
    // Bundles saved since the field landed always carry it, so a deliberate
    // per-move-type 1 must survive a different solver-wide value.
    LocalSearchSolverSpec spec;
    spec.minHotObjects() = 7;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(savedStratifiedSpec()));

    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);

    EXPECT_EQ(
        1,
        *spec.moveTypeList()
             ->at(0)
             .singleRandomStratifiedMoveTypeSpec()
             .as_const()
             .value()
             .minObjectsToExplore());
  }

  {
    // An explicit non-default value is left alone.
    auto stratifiedSpec = preFieldStratifiedSpec();
    stratifiedSpec.minObjectsToExplore() = 3;
    LocalSearchSolverSpec spec;
    spec.minHotObjects() = 7;
    spec.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(std::move(stratifiedSpec)));

    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(spec);

    EXPECT_EQ(
        3,
        *spec.moveTypeList()
             ->at(0)
             .singleRandomStratifiedMoveTypeSpec()
             .as_const()
             .value()
             .minObjectsToExplore());
  }
}

static void checkSingleMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE");

  const auto movePtr = dynamic_cast<SingleMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SINGLE");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type SINGLE not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_singleMoveTypeSpec(SingleMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleMoveType(move);
  }
}

static void checkSwapMoveType(
    std::shared_ptr<MoveType> move,
    const char* partition = nullptr) {
  EXPECT_EQ(move->name(), "SWAP");

  const auto movePtr = dynamic_cast<SwapMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);

  const auto& opt =
      movePtr->config_.partitionNameToExploreSwapsWithinObjectGroup();
  if (!partition) {
    EXPECT_FALSE(opt.has_value());
  } else {
    EXPECT_TRUE(opt.has_value());
    EXPECT_EQ(opt.value(), partition);
  }
}

TEST_F(MoveTypeFactoryTest, createSwapMoveType) {
  {
    // setting move type names with helper function
    // no specific config in LSSS
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SwapMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSwapMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    // no specific config in LSSS
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SWAP");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type SWAP not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built swap spec
    // no specific config in LSSS
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    SwapMoveTypeSpec swapSpec;
    swapSpec.partitionNameToExploreSwapsWithinObjectGroup().emplace() =
        "partition-spec";
    moveSpec.set_swapMoveTypeSpec(std::move(swapSpec));
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSwapMoveType(move, "partition-spec");
  }
}

static void checkTripleLoopMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "TRIPLE_LOOP");

  const auto movePtr = dynamic_cast<TripleLoopMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createTripleLoopMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(TripleLoopMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkTripleLoopMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("TRIPLE_LOOP");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type TRIPLE_LOOP not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_tripleLoopMoveTypeSpec(TripleLoopMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkTripleLoopMoveType(move);
  }
}

static void checkKLSearchMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "KL_SEARCH");

  const auto movePtr = dynamic_cast<KLSearchMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createKLSearchMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(KLSearchMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkKLSearchMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("KL_SEARCH");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type KL_SEARCH not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_klSearchMoveTypeSpec(KLSearchMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkKLSearchMoveType(move);
  }
}

static void checkGroupRoutingMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "GROUP_ROUTING");

  const auto movePtr = dynamic_cast<GroupRoutingMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createGroupRoutingMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(GroupRoutingMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkGroupRoutingMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("GROUP_ROUTING");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type GROUP_ROUTING not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_groupRoutingMoveTypeSpec(GroupRoutingMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkGroupRoutingMoveType(move);
  }
}

static void checkSingleChainMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_CHAIN");

  const auto movePtr = dynamic_cast<SingleChainMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleChainMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleChainMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleChainMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SINGLE_CHAIN");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type SINGLE_CHAIN not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built name spec but transform before
    // creating
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SINGLE_CHAIN");
    config.moveTypeList()->push_back(std::move(moveSpec));
    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(config);
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleChainMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_singleChainMoveTypeSpec(SingleChainMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleChainMoveType(move);
  }
}

static void checkSingleChainFastMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_CHAIN_FAST");

  const auto movePtr = dynamic_cast<SingleChainFastMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleChainFastMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleChainFastMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleChainFastMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SINGLE_CHAIN_FAST");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type SINGLE_CHAIN_FAST not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built name spec but transform before
    // creating
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SINGLE_CHAIN_FAST");
    config.moveTypeList()->push_back(std::move(moveSpec));
    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(config);
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleChainFastMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_singleChainFastMoveTypeSpec(SingleChainFastMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleChainFastMoveType(move);
  }
}

static void checkSingleFixedSourceMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_FIXED_SOURCE");

  const auto movePtr = dynamic_cast<FixedSourceMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleFixedSourceMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleFixedSourceMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleFixedSourceMoveType(move);
  }
  {
    // setting move type names with hand-built name spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SINGLE_FIXED_SOURCE");
    config.moveTypeList()->push_back(std::move(moveSpec));
    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type SINGLE_FIXED_SOURCE not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_singleFixedSourceMoveTypeSpec(SingleFixedSourceMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleFixedSourceMoveType(move);
  }
}

static void checkSingleFastMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_FAST");

  const auto movePtr = dynamic_cast<SingleFastMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleFastMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleFastMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleFastMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_singleFastMoveTypeSpec(SingleFastMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleFastMoveType(move);
  }
}

static void checkSingleGreedyMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_GREEDY");

  const auto movePtr = dynamic_cast<SingleGreedyMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleGreedyMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleGreedyMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleGreedyMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    const SingleGreedyMoveTypeSpec singleSpec;
    moveSpec.set_singleGreedyMoveTypeSpec(singleSpec);
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleGreedyMoveType(move);
  }
}

static void checkFixedSourceMultiMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "FIXED_SOURCE_MULTIPLE");

  const auto movePtr = dynamic_cast<FixedSourceMultiMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createFixedSourceMultiMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(FixedSrcMultiMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkFixedSourceMultiMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_fixedSrcMultiMoveTypeSpec(FixedSrcMultiMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkFixedSourceMultiMoveType(move);
  }
  {
    // setting move type by name
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("FIXED_SOURCE_MULTIPLE");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type FIXED_SOURCE_MULTIPLE not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
}

static void checkFixedDestMultiMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "FIXED_DEST_MULTIPLE");

  const auto movePtr = dynamic_cast<FixedDestMultiMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createFixedDestMultiMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(FixedDestMultiMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkFixedDestMultiMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_fixedDestMultiMoveTypeSpec(FixedDestMultiMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkFixedDestMultiMoveType(move);
  }
  {
    // setting move type by name
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("FIXED_DEST_MULTIPLE");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type FIXED_DEST_MULTIPLE not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
}

static void checkFixedDestSwapMultiMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "FIXED_DEST_SWAP_MULTIPLE");

  const auto movePtr = dynamic_cast<FixedDestSwapMultiMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, fixedDestinationSwapSkipsSameContainer) {
  // The fixture's only container already owns its object. Swapping with
  // that same container cannot improve any objective, in either search mode
  // or allotment representation, and must perform no evaluations.
  for (const bool greedy : {false, true}) {
    for (const bool adaptive : {false, true}) {
      SCOPED_TRACE(::testing::Message() << greedy << ", " << adaptive);
      FixedDestSwapMultiMoveTypeSpec spec;
      spec.specialContainer() = "container0";
      spec.greedyOnSrc() = greedy;
      auto metadata = std::make_shared<RasLocalSearchMetadata>();
      metadata->useAdaptiveAllotments() = adaptive;
      spec.rasLocalSearchMetadata() = std::move(metadata);
      const LocalSearchSolverSpec solverSpec;
      FixedDestSwapMultiMoveType move(solverSpec, spec);
      const auto result = move.findBestMove(
          getMovesEvaluator(),
          getProblem().containerId("container0"),
          getMoveStatsAggregator(),
          getEmptySearchHints(),
          1.0);
      EXPECT_TRUE(result.getMoveSet().empty());
      EXPECT_EQ(getTotalMovesEvaluated(), 0);
    }
  }
}

TEST_F(MoveTypeFactoryTest, createFixedDestSwapMultiMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(FixedDestSwapMultiMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkFixedDestSwapMultiMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_fixedDestSwapMultiMoveTypeSpec(
        FixedDestSwapMultiMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkFixedDestSwapMultiMoveType(move);
  }
  {
    // setting move type by name
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("FIXED_DEST_SWAP_MULTIPLE");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type FIXED_DEST_SWAP_MULTIPLE not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
}

static void checkSwapNMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SWAP_N");

  const auto movePtr = dynamic_cast<SwapNMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSwapNMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SwapNMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSwapNMoveType(move);
  }
  {
    // setting move type by name
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SWAP_N");
    config.moveTypeList()->push_back(std::move(moveSpec));

    REBALANCER_EXPECT_RUNTIME_ERROR(
        MoveTypeFactory::createMoveTypes(config, getProblem()),
        "Move type SWAP_N not supported by name; it either does not exist, or you should use its typed move type spec instead");
  }
  {
    // setting move type names with hand-built name spec but transform before
    // creating
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_moveTypeName("SWAP_N");
    config.moveTypeList()->push_back(std::move(moveSpec));
    MoveTypeFactory::transformMoveTypesForReplayingSavedInstances(config);
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSwapNMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    const SwapNMoveTypeSpec singleSpec;
    moveSpec.set_swapNMoveTypeSpec(singleSpec);
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSwapNMoveType(move);
  }
}

static void checkSingleRandomBatchesMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_RANDOM_BATCHES");

  const auto movePtr = dynamic_cast<SingleRandomBatchesMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleRandomBatchesMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleRandomBatchesMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleRandomBatchesMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    const SingleRandomBatchesMoveTypeSpec singleSpec;
    moveSpec.set_singleRandomBatchesMoveTypeSpec(singleSpec);
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleRandomBatchesMoveType(move);
  }
}

static void checkSwapFullContainersMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SWAP_FULL_CONTAINERS");

  const auto movePtr = dynamic_cast<SwapFullContainersMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSwapFullContainersMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SwapFullContainersMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSwapFullContainersMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_swapFullContainersMoveTypeSpec(
        SwapFullContainersMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSwapFullContainersMoveType(move);
  }
}

static void checkSwapFullWithEmptyContainersMoveType(
    std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SWAP_FULL_WITH_EMPTY_CONTAINERS");

  const auto movePtr =
      dynamic_cast<SwapFullWithEmptyContainersMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSwapFullWithEmptyContainersMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(
            SwapFullWithEmptyContainersMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSwapFullWithEmptyContainersMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_swapFullWithEmptyContainersMoveTypeSpec(
        SwapFullWithEmptyContainersMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSwapFullWithEmptyContainersMoveType(move);
  }
}

static void checkSingleEndChainMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_END_CHAIN");

  const auto movePtr = dynamic_cast<SingleEndChainMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleEndChainMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleEndChainMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleEndChainMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    moveSpec.set_singleEndChainMoveTypeSpec(SingleEndChainMoveTypeSpec());
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleEndChainMoveType(move);
  }
}

static void checkReplicaDropMoveType(std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "REPLICA_DROP");

  const auto movePtr = dynamic_cast<ReplicaDropMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createReplicaDropMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    ReplicaDropMoveTypeSpec replicaDropMoveTypeSpec;
    replicaDropMoveTypeSpec.replicaDropPartition() = "partition";
    replicaDropMoveTypeSpec.replicaDropScope() = "scope";
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(replicaDropMoveTypeSpec));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkReplicaDropMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    ReplicaDropMoveTypeSpec replicaDropMoveTypeSpec;
    replicaDropMoveTypeSpec.replicaDropPartition() = "partition";
    replicaDropMoveTypeSpec.replicaDropScope() = "scope";
    MoveTypeSpec moveSpec;
    moveSpec.set_replicaDropMoveTypeSpec(replicaDropMoveTypeSpec);
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkReplicaDropMoveType(move);
  }
}

static void checkGreedyGroupToScopeItemMoveType(
    std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "GREEDY_GROUP_TO_SCOPE_ITEM");

  const auto movePtr =
      dynamic_cast<GreedyGroupToScopeItemMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createGreedyGroupToScopeItemMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    GreedyGroupToScopeItemMoveTypeSpec greedyGroupToScopeItemMoveTypeSpec;
    greedyGroupToScopeItemMoveTypeSpec.scopeItemMovesScope() = "scope";
    greedyGroupToScopeItemMoveTypeSpec.groupMovesPartition() = "partition";
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(
            std::move(greedyGroupToScopeItemMoveTypeSpec)));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkGreedyGroupToScopeItemMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    GreedyGroupToScopeItemMoveTypeSpec greedyGroupToScopeItemMoveTypeSpec;
    greedyGroupToScopeItemMoveTypeSpec.scopeItemMovesScope() = "scope";
    greedyGroupToScopeItemMoveTypeSpec.groupMovesPartition() = "partition";
    MoveTypeSpec moveSpec;
    moveSpec.set_greedyGroupToScopeItemMoveTypeSpec(
        std::move(greedyGroupToScopeItemMoveTypeSpec));
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkGreedyGroupToScopeItemMoveType(move);
  }
}

static void checkSingleRandomStratifiedMoveType(
    std::shared_ptr<MoveType> move) {
  EXPECT_EQ(move->name(), "SINGLE_RANDOM_STRATIFIED");

  const auto movePtr =
      dynamic_cast<SingleRandomStratifiedMoveType*>(move.get());
  EXPECT_TRUE(!!movePtr);
}

TEST_F(MoveTypeFactoryTest, createSingleRandomStratifiedMoveType) {
  {
    // setting move type names with helper function
    LocalSearchSolverSpec config;
    config.moveTypeList()->push_back(
        ProblemSolver::makeMoveTypeSpec(SingleRandomStratifiedMoveTypeSpec()));
    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);
    const auto& move = moves.at(0);
    checkSingleRandomStratifiedMoveType(move);
  }
  {
    // setting move type names with hand-built single spec
    LocalSearchSolverSpec config;
    MoveTypeSpec moveSpec;
    SingleRandomStratifiedMoveTypeSpec singleRandomStratifiedSpec;
    moveSpec.set_singleRandomStratifiedMoveTypeSpec(
        std::move(singleRandomStratifiedSpec));
    config.moveTypeList()->push_back(std::move(moveSpec));

    const auto moves = MoveTypeFactory::createMoveTypes(config, getProblem());
    EXPECT_EQ(moves.size(), 1);

    const auto& move = moves.at(0);
    checkSingleRandomStratifiedMoveType(move);
  }
}

} // namespace facebook::rebalancer::packer::tests
