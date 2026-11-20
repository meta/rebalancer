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
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/tests/IdConverterTestUtils.h"

#include <fmt/core.h>
#include <folly/container/irange.h>
#include <gtest/gtest.h>

namespace facebook::rebalancer::packer::tests {

struct Compare {
  bool operator()(const Move& x, const Move& y) const {
    if (x.getObject() != y.getObject()) {
      return x.getObject() < y.getObject();
    }
    if (x.getSourceContainer() != y.getSourceContainer()) {
      return x.getSourceContainer() < y.getSourceContainer();
    }
    return x.getDestinationContainer() < y.getDestinationContainer();
  }
};

TEST(MoveSetTest, GetChangeSet) {
  MoveSet moves;
  moves.insert(Move{object(11), container(101), container(102)});
  moves.insert(Move{object(12), container(103), container(104)});
  auto changes = moves.getChangeSet();
  ASSERT_EQ(4, changes.size());
  EXPECT_EQ(object(11), changes.at(0).getObject());
  EXPECT_EQ(container(101), changes.at(0).getContainer());
  EXPECT_EQ(-1, changes.at(0).getValue());
  EXPECT_EQ(object(11), changes.at(1).getObject());
  EXPECT_EQ(container(102), changes.at(1).getContainer());
  EXPECT_EQ(1, changes.at(1).getValue());
  EXPECT_EQ(object(12), changes.at(2).getObject());
  EXPECT_EQ(container(103), changes.at(2).getContainer());
  EXPECT_EQ(-1, changes.at(2).getValue());
  EXPECT_EQ(object(12), changes.at(3).getObject());
  EXPECT_EQ(container(104), changes.at(3).getContainer());
  EXPECT_EQ(1, changes.at(3).getValue());
}

TEST(MoveSetTest, FromChangeSet) {
  ChangeSet changes;
  changes.insert(Change(object(11), container(101), -1));
  changes.insert(Change(object(11), container(102), 1));
  changes.insert(Change(object(12), container(103), -1));
  changes.insert(Change(object(12), container(104), 1));
  auto moves = MoveSet::fromChangeSet(changes);
  const std::set<Move, Compare> actual(moves.begin(), moves.end());
  const std::set<Move, Compare> expected = {
      Move(object(11), container(101), container(102)),
      Move(object(12), container(103), container(104))};
  EXPECT_EQ(expected, actual);
}

TEST(MoveSetTest, FromInverseChangeSet) {
  MoveSet moves;
  moves.insert(Move{object(11), container(101), container(102)});
  moves.insert(Move{object(12), container(103), container(104)});

  const auto inverse =
      MoveSet::fromChangeSet(moves.getChangeSet().getInverse());
  const std::set<Move, Compare> actual(inverse.begin(), inverse.end());
  const std::set<Move, Compare> expected = {
      Move(object(11), container(102), container(101)),
      Move(object(12), container(104), container(103))};

  EXPECT_EQ(expected, actual);
}

TEST(MoveSetTest, ToString) {
  entities::tests::UniverseBuilderTestUtils builder("task", "host");
  builder.setInitialAssignment(
      {{"host1", {"task1"}}, {"host2", {"task2"}}, {"host3", {}}});

  MoveSet moves;
  moves.insert(
      Move{
          builder.object("task1"),
          builder.container("host1"),
          builder.container("host3")});
  moves.insert(
      Move{
          builder.object("task2"),
          builder.container("host2"),
          builder.container("host3")});

  const auto universe = builder.buildUniverse();
  const auto result = moves.toString(*universe);
  // Container ids follow the iteration order of the initial assignment map,
  // which differs between the internal and OSS map implementations.
  EXPECT_EQ(
      fmt::format(
          "[\n"
          "  Object = task1 (id: {}), Source Container = host1 (id: {}), Destination Container = host3 (id: {})\n"
          "  Object = task2 (id: {}), Source Container = host2 (id: {}), Destination Container = host3 (id: {})\n"
          "]",
          builder.object("task1"),
          builder.container("host1"),
          builder.container("host3"),
          builder.object("task2"),
          builder.container("host2"),
          builder.container("host3")),
      result);
}

TEST(MoveSetTest, EmptyToString) {
  entities::tests::UniverseBuilderTestUtils builder("task", "host");
  builder.setInitialAssignment({{"host1", {}}});

  const auto universe = builder.buildUniverse();
  EXPECT_EQ("[]", MoveSet{}.toString(*universe));
}

TEST(MoveSetTest, FromChangeSetRemovedTwice) {
  ChangeSet changes;
  changes.insert(Change(object(11), container(101), -1));
  changes.insert(Change(object(11), container(102), -1));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      MoveSet::fromChangeSet(changes), "object 11 removed twice");
}

TEST(MoveSetTest, FromChangeSetAddedTwice) {
  ChangeSet changes;
  changes.insert(Change(object(11), container(101), 1));
  changes.insert(Change(object(11), container(102), 1));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      MoveSet::fromChangeSet(changes), "object 11 added twice");
}

TEST(MoveSetTest, FromChangeSetMismatch) {
  ChangeSet changes;
  changes.insert(Change(object(11), container(101), 1));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      MoveSet::fromChangeSet(changes),
      "mismatch between number of objects added and removed");
}

TEST(MoveSetTest, FromChangeSetRemovedNotAdded) {
  ChangeSet changes;
  changes.insert(Change(object(11), container(101), 1));
  changes.insert(Change(object(12), container(102), -1));
  REBALANCER_EXPECT_RUNTIME_ERROR(
      MoveSet::fromChangeSet(changes), "object 12 removed but not added");
}

TEST(MoveSetTest, CompareSameMovesInMoveSet) {
  std::vector<Move> moves;
  moves.reserve(100);
  for (const auto i : folly::irange(100)) {
    moves.emplace_back(object(i), container(i + 1), container(i + 2));
  }

  auto moveSet1 = MoveSet();
  auto moveSet2 = MoveSet();
  for (auto& move : moves) {
    moveSet1.insert(move);
    moveSet2.insert(move);
  }

  EXPECT_EQ(0, MoveSet::compare(moveSet1, moveSet2));
  EXPECT_EQ(0, MoveSet::compare(moveSet2, moveSet1));
}

TEST(MoveSetTest, BetterMoveSet) {
  {
    // less moves will be better than more moves
    std::vector<Move> moves;
    moves.reserve(100);
    for (const auto i : folly::irange(100)) {
      moves.emplace_back(object(i), container(i + 1), container(i + 2));
    }

    auto moveSet1 = MoveSet();
    auto moveSet2 = MoveSet();
    for (auto& move : moves) {
      moveSet1.insert(move);
      moveSet2.insert(move);
    }
    moveSet2.insert(Move{object(1000), container(1010), container(1020)});
    EXPECT_EQ(1, MoveSet::compare(moveSet1, moveSet2));
    EXPECT_EQ(-1, MoveSet::compare(moveSet2, moveSet1));
  }

  {
    // When the number of moves are the same, the move set with a
    // structurally smaller move (by hash) will be better
    const Move move = Move{object(3), container(1), container(2)};
    const Move move1 = Move{object(1), container(2), container(3)};
    const Move move2 = Move{object(1), container(2), container(4)};

    MoveSet moveSet1;
    MoveSet moveSet2;
    moveSet1.insert(move);
    moveSet1.insert(move1);
    moveSet2.insert(move);
    moveSet2.insert(move2);

    // MoveSet::compare should be antisymmetric
    auto cmp = MoveSet::compare(moveSet1, moveSet2);
    EXPECT_NE(0, cmp);
    EXPECT_EQ(-cmp, MoveSet::compare(moveSet2, moveSet1));
  }
}

} // namespace facebook::rebalancer::packer::tests
