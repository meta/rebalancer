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

#include "algopt/rebalancer/solver/moves/tests/MoveTestBase.h"

#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionUtils.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"

#include <folly/container/irange.h>

namespace facebook::rebalancer::packer::tests {

template <typename T>
MoveTestBaseT<T>::MoveTestBaseT(
    const std::string& objectName,
    const std::string& containerName)
    : entities::tests::UniverseBuilderTestUtils(objectName, containerName) {}

template <typename T>
void MoveTestBaseT<T>::createProblem(
    const std::vector<ExprPtr>& objectiveTuple,
    ExprPtr constraint,
    const std::optional<algopt::common::thrift::HigherPriorityObjectivesConfig>&
        higherPriorityObjConfig,
    const PackerSet<entities::ContainerId>& nonAcceptingContainers,
    std::unique_ptr<InvalidMoveFilter> invalidMoveFilter) {
  problem_ = createTestProblem(
      getUniversePtr(),
      objectiveTuple,
      std::move(constraint),
      nonAcceptingContainers,
      ProblemConfigs{},
      /*performInitialFullApply=*/true,
      /*enableParallelizedBoundsComputing=*/false,
      std::move(invalidMoveFilter));
  // CoreLocalSearchSolve builds this index when a stage starts.
  problem_->assignment.buildIndexByEquivalentSets(
      problem_->getEquivalenceSets());
  auto objSize = problem_->objective.size();
  movesEvaluator_.emplace(
      *problem_,
      0,
      objSize,
      "Stage 1: load balancing",
      higherPriorityObjConfig);
}

template <typename T>
Problem& MoveTestBaseT<T>::getProblem() {
  return *problem_;
}

template <typename T>
const MovesEvaluator& MoveTestBaseT<T>::getMovesEvaluator() {
  if (!movesEvaluator_) {
    throw std::runtime_error("movesEvaluator_ is not set");
  }

  return *movesEvaluator_;
}

template <typename T>
MoveStatsAggregator& MoveTestBaseT<T>::getMoveStatsAggregator() {
  if (!stats_) {
    stats_ =
        std::make_unique<MoveStatsAggregator>(getUniverse().getPrecision());
  }
  stats_->clear();
  return *stats_;
}

template <typename T>
void MoveTestBaseT<T>::setEquivalenceSets(EquivalenceSets equivalenceSets) {
  auto& equivSetStore = problem_->getEquivalenceSetsStore();
  equivSetStore.override(std::move(equivalenceSets));
  problem_->assignment.buildIndexByEquivalentSets(
      problem_->getEquivalenceSets());
}

template <typename T>
int64_t MoveTestBaseT<T>::getTotalMovesEvaluated() {
  return stats_->getGlobalStats().getTotalMoves();
}

template <typename T>
void MoveTestBaseT<T>::verifyMovesAreAsExpected(
    int lineNumber,
    std::string fileName,
    const std::vector<Move>& expectedMoveSet,
    const MoveSet& actualMoveSet) const {
  const auto& universe = getUniverse();
  auto makeTuple = [&](auto& move) {
    return std::make_tuple(
        universe.getEntityName(move.getObject()),
        universe.getEntityName(move.getSourceContainer()),
        universe.getEntityName(move.getDestinationContainer()));
  };

  std::set<std::tuple<std::string, std::string, std::string>> expectedMoves;
  for (const auto& move : expectedMoveSet) {
    expectedMoves.insert(makeTuple(move));
  }

  std::set<std::tuple<std::string, std::string, std::string>> actualMoves;
  for (const auto& move : actualMoveSet) {
    actualMoves.insert(makeTuple(move));
  }

  EXPECT_EQ(expectedMoves, actualMoves) << fmt::format(
      " moveset mismatch for call from line {} in {}", lineNumber, fileName);
}

template <typename T>
const SearchHints& MoveTestBaseT<T>::getEmptySearchHints() const {
  return searchHints_;
}

template <typename T>
ExprPtr MoveTestBaseT<T>::makeObjectLookup(
    ExprPtr objectVector,
    const PackerSet<entities::ContainerId>& containers) {
  const Assignment assignment(
      getUniverse().getContainers().getInitialAssignment());
  return object_lookup(
      std::move(objectVector),
      std::make_shared<PackerSet<entities::ContainerId>>(containers),
      assignment);
}

template <typename T>
std::shared_ptr<ObjectVector> MoveTestBaseT<T>::makeAllUnequalObjectVector(
    int objectCount,
    bool negateAllValues) {
  PackerMap<entities::ObjectId, double> values;
  for (const auto i : folly::irange(1, objectCount + 1)) {
    values[object(i)] = negateAllValues ? -i : i;
  }
  return makeObjectVector(values, getUniverse());
}

// explicit instantiations
template class MoveTestBaseT<>;
template class MoveTestBaseT<::testing::TestWithParam<std::tuple<bool, bool>>>;

} // namespace facebook::rebalancer::packer::tests
