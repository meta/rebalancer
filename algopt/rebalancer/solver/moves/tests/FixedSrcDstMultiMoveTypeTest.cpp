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

#include "algopt/rebalancer/interface/tests/utils.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/FixedSrcDstMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/EntityAttributes.h"

#include <folly/coro/GtestHelpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <memory>

namespace facebook::rebalancer::packer::tests {

class FixedSrcDstMultiMoveTypeTest : public MoveTestBase {
 protected:
  FixedSrcDstMultiMoveTypeTest() : MoveTestBase("part", "container") {}
};

CO_TEST_F(FixedSrcDstMultiMoveTypeTest, ServerRegainsCapacityAfterExhaustion) {
  // Server s0 has one part in 'reservation' and two spare parts in 'unassigned'
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"unassigned", {"s0_1", "s0_2"}},
          {"reservation", {"s0_0"}},
          {"other", {}},
      });
  const entities::Map<std::string, std::vector<std::string>> serverToParts = {
      {"s0", {"s0_0", "s0_1", "s0_2"}}};
  co_await addPartition("server", serverToParts);
  co_await addPartition("search_space", serverToParts);
  co_await addScope("shape", {{"M1", {"reservation", "other"}}});

  const auto universe = buildUniverse();
  const Assignment initialAssignment(
      universe->getContainers().getInitialAssignment());

  // Add a constraint that s0 can have at most one part in 'reservation'.
  auto partsInReservationExpr = const_expr(0, *universe);
  for (const auto& part : serverToParts.at("s0")) {
    partsInReservationExpr += variable(
        object(part), container("reservation"), *universe, initialAssignment);
  }
  const auto reservationConstraint =
      any_positive({partsInReservationExpr}, *universe);
  createProblem(/*objectiveTuple=*/{const_expr(0, *universe)},
                /*constraint=*/reservationConstraint);

  auto& problem = getProblem();
  const auto serverPartitionId = partitionId("server");
  problem.assignment.buildIndexByEquivalentSetsAndPartition(
      *universe, problem.getEquivalenceSets(), serverPartitionId);

  // s0 has room for one spare part while 'reservation' holds none of its parts
  problem.getEntityAttributeStore().registerAttributes(
      kAllowedScopeItemsPerGroupAttrName.str(),
      std::make_shared<AllowedScopeItemsPerGroup>(
          universe->getScope(scopeId("shape")),
          universe->getPartition(serverPartitionId),
          /*evaluate=*/
          [&](entities::GroupId, entities::ScopeItemId) -> size_t {
            return reservationConstraint->value > 0 ? 0 : 1;
          },
          interface::getTestExecutor(/*numThreads=*/1)));

  interface::RasLocalSearchMetadata rasMetadata;
  rasMetadata.serverPartition() = "server";
  rasMetadata.searchSpacePartition() = "search_space";
  rasMetadata.useAdaptiveAllotments() = true;
  rasMetadata.reservationCapacityShapeScopeName() = "shape";
  interface::FixedSrcMultiMoveTypeSpec spec;
  spec.specialContainer() = "unassigned";
  spec.rasLocalSearchMetadata() =
      std::make_shared<interface::RasLocalSearchMetadata>(rasMetadata);

  FixedSrcDstMoveCandidateGenerator generator(spec);
  const auto getCandidates = [&] {
    return generator.filterSourceObjectsByBundleSizeAndSearchSpacePartition(
        getMovesEvaluator(),
        container("unassigned"),
        container("reservation"),
        getMoveStatsAggregator());
  };

  // s0 already has a part in 'reservation', so none of its spare parts can move
  EXPECT_THAT(getCandidates(), ::testing::IsEmpty());

  // Moving that part to 'other' gives s0 capacity back, without returning any
  // part to 'unassigned'
  problem.apply(
      Move(object("s0_0"), container("reservation"), container("other"))
          .getChangeSet());

  // Exactly one of its spare parts must be a candidate again
  EXPECT_THAT(
      getCandidates(),
      ::testing::ElementsAre(
          ::testing::ElementsAre(
              ::testing::AnyOf(object("s0_1"), object("s0_2")))));
}

} // namespace facebook::rebalancer::packer::tests
