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

#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"

#include "algopt/rebalancer/solver/summary/LabeledObjectives.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"

#include <fmt/core.h>
#include <folly/container/irange.h>

#include <ranges>
#include <stdexcept>

namespace facebook::rebalancer {

GlobalLabeledObjectives GlobalLabeledObjectives::getLabeledObjectivesTuple(
    size_t begin,
    size_t end) const {
  if (end > size()) {
    throw std::runtime_error(
        fmt::format("invalid range: begin: {}, end: {}", begin, end));
  }
  labeled_objectives_tuple tuple;
  for (size_t pos = begin; pos < end; ++pos) {
    tuple.emplace_back(labeled_objectives_.at(pos));
  }
  return GlobalLabeledObjectives(std::move(tuple));
}

GlobalLabeledObjectives::labeled_objectives_tuple::const_iterator
GlobalLabeledObjectives::begin() const {
  return labeled_objectives_.begin();
}

GlobalLabeledObjectives::labeled_objectives_tuple::const_iterator
GlobalLabeledObjectives::end() const {
  return labeled_objectives_.end();
}

size_t GlobalLabeledObjectives::size() const {
  return labeled_objectives_.size();
}

const LabeledObjectives& GlobalLabeledObjectives::getFirstObjective() const {
  return getObjectiveAt(0);
}

const LabeledObjectives& GlobalLabeledObjectives::getObjectiveAt(
    int pos) const {
  return labeled_objectives_.at(pos);
}

LabeledObjectives&
GlobalLabeledObjectives::Builder::getOrCreateLabeledObjectivesAt(int pos) {
  if (pos < 0) {
    throw std::runtime_error(fmt::format("pos: {} must be >= 0", pos));
  }
  if (built_) {
    throw std::runtime_error(
        "cannot re-use already built global labeled objective builder");
  }

  auto size = static_cast<int>(labeled_objectives_.size());
  if (pos == size) {
    labeled_objectives_.emplace_back();
  }
  if (pos > size) {
    labeled_objectives_.resize(pos + 1, LabeledObjectives());
  }
  return labeled_objectives_.at(pos);
}

GlobalLabeledObjectives::Builder& GlobalLabeledObjectives::Builder::setRoot(
    int pos,
    ExprPtr root) {
  getOrCreateLabeledObjectivesAt(pos).setRoot(root);
  return *this;
}

GlobalLabeledObjectives::Builder& GlobalLabeledObjectives::Builder::addSingle(
    int pos,
    std::string name,
    ExprPtr expr,
    double weight) {
  getOrCreateLabeledObjectivesAt(pos).addSingle(
      std::move(name), std::move(expr), weight);
  return *this;
}

GlobalLabeledObjectives GlobalLabeledObjectives::Builder::build(
    const GlobalObjective& objective) {
  if (built_) {
    throw std::runtime_error("global labeled objective already built!");
  }
  built_ = true;

  auto size = static_cast<int>(labeled_objectives_.size());
  if (size != objective.size()) {
    throw std::runtime_error(
        fmt::format(
            "number of objectives {} != number of labeled objectives {}",
            objective.size(),
            labeled_objectives_.size()));
  }

  for (const auto pos : folly::irange(size)) {
    const auto& obj = objective.getObjectiveAt(pos);
    auto& labeled_obj = labeled_objectives_.at(pos);
    labeled_obj.setRoot(obj);
  }
  return GlobalLabeledObjectives(std::move(labeled_objectives_));
}

} // namespace facebook::rebalancer
