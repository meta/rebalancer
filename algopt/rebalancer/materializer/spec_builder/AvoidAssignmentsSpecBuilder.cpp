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

#include "algopt/rebalancer/materializer/spec_builder/AvoidAssignmentsSpecBuilder.h"

#include "algopt/rebalancer/entities/ObjectStaticDimension.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"

using namespace facebook::rebalancer::entities;

namespace facebook::rebalancer::materializer {

AvoidAssignmentsSpecBuilder::AvoidAssignmentsSpecBuilder(
    std::shared_ptr<const Universe> universe,
    interface::AvoidAssignmentsSpec spec)
    : SpecBuilder(std::move(universe)), spec_(std::move(spec)) {}

folly::coro::Task<ExprPtr> AvoidAssignmentsSpecBuilder::goalCoro(
    ExpressionBuilder& /* expressionBuilder */) const {
  throw std::runtime_error("AvoidAssignmentsSpec not supported as a goal");
}

folly::coro::Task<std::vector<ConstraintInfo>>
AvoidAssignmentsSpecBuilder::constraints(
    ExpressionBuilder& expressionBuilder) const {
  auto scopeId = universe_->getScopeId(*spec_.scope());
  const auto numObjects = universe_->getNumObjects();
  const auto numAvoidAssignments = spec_.assignments()->size();
  std::vector<ConstraintInfo> result;
  const auto& scope = universe_->getScope(scopeId);
  const auto& initialAssignment =
      expressionBuilder.getUpdatedInitialAssignment();
  Map<ScopeItemId, ObjectIdToDoubleMap> scopeItemToObjectPenalties;
  Set<ScopeItemId> initiallyBrokenScopeItems;
  for (auto& assignment : *spec_.assignments()) {
    const auto objectId = universe_->getObjectId(*assignment.object());
    const auto initialScopeItemId =
        scope.getScopeItemId(initialAssignment.getContainer(objectId));
    for (auto& scopeItemName : *assignment.scopeItems()) {
      auto scopeItemId = universe_->getScopeItemId(scopeId, scopeItemName);
      auto [it, _] = scopeItemToObjectPenalties.try_emplace(
          scopeItemId,
          numObjects,
          /*defaultValue=*/0.0,
          /*expectedNonDefaultSize=*/numAvoidAssignments);
      it->second.emplace(objectId, 1.0);
      if (initialScopeItemId == scopeItemId) {
        initiallyBrokenScopeItems.insert(scopeItemId);
      }
    }
  }

  for (auto& [scopeItemId, penalties] : scopeItemToObjectPenalties) {
    const ObjectStaticDimension splDim(std::move(penalties));
    result.emplace_back(
        co_await expressionBuilder.getAbsoluteUtil(
            UtilMetric::AFTER, splDim, scopeId, scopeItemId));
    // Keeps forbidden objects that did not start on this scope item from
    // moving in. It holds initially, so it stays hard even though the AFTER
    // constraint above is broken.
    if (initiallyBrokenScopeItems.contains(scopeItemId)) {
      result.emplace_back(
          co_await expressionBuilder.getAbsoluteUtil(
              UtilMetric::NEW, splDim, scopeId, scopeItemId));
    }
  }

  co_return result;
}

std::string AvoidAssignmentsSpecBuilder::description() const {
  return fmt::format(
      "Avoid {} assignments on scope {}",
      spec_.assignments()->size(),
      *spec_.scope());
}

void AvoidAssignmentsSpecBuilder::populateInvalidMoveFilter(
    InvalidMoveFilter& invalidMoveFilter,
    const Assignment& updatedInitialAssignment) const {
  const auto scopeId = universe_->getScopeId(*spec_.scope());
  const auto& scope = universe_->getScope(scopeId);
  Map<ScopeItemId, std::vector<ObjectId>> scopeItemToInvalidObjects;
  for (const auto& assignment : *spec_.assignments()) {
    const auto objectId = universe_->getObjectId(*assignment.object());
    // An avoided object may stay in, and move within, the scope item it
    // starts in.
    const auto initialScopeItemId =
        scope.getScopeItemId(updatedInitialAssignment.getContainer(objectId));
    for (const auto& scopeItemName : *assignment.scopeItems()) {
      const auto scopeItemId =
          universe_->getScopeItemId(scopeId, scopeItemName);
      if (scopeItemId != initialScopeItemId) {
        scopeItemToInvalidObjects[scopeItemId].push_back(objectId);
      }
    }
  }
  for (const auto& [scopeItemId, invalidObjects] : scopeItemToInvalidObjects) {
    for (const auto& containerId : scope.getContainerIds(scopeItemId)) {
      invalidMoveFilter.markInvalid(invalidObjects, containerId);
    }
  }
}

SpecParameters AvoidAssignmentsSpecBuilder::getSpecInfo() const {
  return SpecParameters{
      .name = *spec_.name(),
      .scope = *spec_.scope(),
      .size = static_cast<int>(spec_.assignments()->size())};
}

} // namespace facebook::rebalancer::materializer
