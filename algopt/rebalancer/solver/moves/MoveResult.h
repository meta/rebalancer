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

#pragma once

#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/moves/MoveSet.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/LocalSearchObjective.h"

#include <folly/small_vector.h>

namespace facebook::rebalancer {

struct ObjectiveDelta {
  // Refers to the problem's labeled objectives, which outlive every move
  // result, to avoid a shared_ptr copy per delta.
  const LabeledExpressionPtr& objective;
  double oldValue;
  double newValue;
  bool isPenalty;

  ObjectiveDelta(
      const LabeledExpressionPtr& objective,
      double oldValue,
      double newValue,
      bool isPenalty = false)
      : objective(objective),
        oldValue(oldValue),
        newValue(newValue),
        isPenalty(isPenalty) {}

  ObjectiveDelta(LabeledExpressionPtr&&, double, double, bool = false) = delete;
};

using ObjectiveDeltaSet = std::vector<ObjectiveDelta>;
using ObjectiveDeltaSets =
    folly::small_vector<ObjectiveDeltaSet, expected_tuple_size>;

class MoveResult {
 public:
  static MoveResult makeEmpty();
  static MoveResult makeValid(
      MoveSet moveSet,
      LocalSearchObjectiveValue oldValue,
      LocalSearchObjectiveValue newValue,
      std::optional<GlobalObjectiveValue> arbiterValue = std::nullopt,
      std::optional<ObjectiveDeltaSets> objectiveDeltas = std::nullopt,
      int objectiveDeltaIndexOffset = 0);
  static MoveResult makeInvalid(
      MoveSet moveSet,
      std::optional<LabeledExpressionSet> invalidConstraints = std::nullopt);

  // true if the result does not contain a move
  bool isEmpty() const;

  // true if the move does not break any constraints
  bool isValid() const;

  bool isBetter(const Precision& precision) const;
  bool isNeutral(const Precision& precision) const;
  bool isWorse(const Precision& precision) const;

  const MoveSet& getMoveSet() const;
  int64_t getEvalsCount() const;
  bool hasObjectiveDeltas() const;

  const ObjectiveDeltaSet& getFirstChangedObjectiveDelta(
      const Precision& precision) const;
  const ObjectiveDeltaSets& getObjectiveDeltas() const;

  std::string toString(const entities::Universe& universe) const;

  LabeledExpressionPtr getSmallestDeltaObjective(
      const Precision& precision) const;
  LabeledExpressionPtr getLargestDeltaObjective(
      const Precision& precision) const;
  LabeledExpressionPtr getFirstInvalidConstraint() const;

  // merges another move result into this one, keeping the best of the 2 moves,
  // and aggregating the stats of both
  // TODO: separate aggregator from the single result object
  void aggregate(MoveResult&& other);

  const LocalSearchObjectiveValue& getOldValue() const;
  const LocalSearchObjectiveValue& getValue() const;

 private:
  MoveResult();
  void clone(MoveResult& dst, const MoveResult& src);
  bool empty_ = false;
  bool valid_ = false;
  int64_t evalsCount_ = 0;
  MoveSet moveSet_;
  LocalSearchObjectiveValue oldValue_;
  LocalSearchObjectiveValue newValue_;
  // used only for tie-breaking during aggregation
  std::optional<GlobalObjectiveValue> arbiterValue_;
  std::optional<ObjectiveDeltaSets> objectiveDeltas_;
  int objectiveDeltaIndexOffset_ = 0;
  std::optional<LabeledExpressionSet> invalidConstraints_;
};

} // namespace facebook::rebalancer
