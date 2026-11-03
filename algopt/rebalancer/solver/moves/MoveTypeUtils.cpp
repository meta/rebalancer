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

#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"

#include <fmt/core.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace facebook::rebalancer {

namespace {

template <typename CandidateObjects>
ObjectBundle buildAnchoredBundle(
    const CandidateObjects& candidateObjects,
    const entities::ObjectId anchorObject,
    const size_t bundleSize) {
  const auto candidateObjectCount = candidateObjects.size();
  if (bundleSize == 0) {
    throw std::runtime_error(
        fmt::format(
            "Cannot build a zero-sized bundle anchored at object {}",
            anchorObject));
  }
  if (candidateObjectCount < bundleSize) {
    throw std::runtime_error(
        fmt::format(
            "Cannot build bundle of size {} anchored at object {} from only {} candidates",
            bundleSize,
            anchorObject,
            candidateObjectCount));
  }

  if (std::find(
          candidateObjects.begin(), candidateObjects.end(), anchorObject) ==
      candidateObjects.end()) {
    throw std::runtime_error(
        fmt::format(
            "Anchor object {} is not present in the candidate bundle",
            anchorObject));
  }

  ObjectBundle bundle;
  bundle.reserve(bundleSize);
  bundle.push_back(anchorObject);
  if (bundle.size() == bundleSize) {
    return bundle;
  }
  for (const auto object : candidateObjects) {
    if (object == anchorObject) {
      continue;
    }
    bundle.push_back(object);
    if (bundle.size() == bundleSize) {
      return bundle;
    }
  }
  throw std::runtime_error(
      fmt::format(
          "Failed to build bundle of size {} anchored at object {}",
          bundleSize,
          anchorObject));
}

std::pair<size_t, size_t> getBundleSizes(
    const entities::Universe& universe,
    entities::ObjectId hotObject,
    entities::ObjectId coldObject,
    entities::DimensionId dimensionId,
    std::optional<entities::ScopeItemId> dimensionScopeItemId) {
  const auto& objectDimension =
      universe.getObjects().getDimension(dimensionId).only();
  const auto hotValue =
      objectDimension.getValue(hotObject, dimensionScopeItemId);
  const auto coldValue =
      objectDimension.getValue(coldObject, dimensionScopeItemId);

  if (hotValue > coldValue && coldValue > 0) {
    return {
        1,
        calculateSwapRatio(
            universe,
            hotObject,
            coldObject,
            dimensionId,
            dimensionScopeItemId)};
  }
  if (coldValue > hotValue && hotValue > 0) {
    // TEMPORARY: When enableKToOneSwaps is false, degrade k:1 swaps to 1:1
    // (pre-D97512700 behavior). Remove once k:1 swaps are validated.
    if (!ProblemConfigs::enableKToOneSwaps) {
      return {1, 1};
    }
    return {
        calculateSwapRatio(
            universe, coldObject, hotObject, dimensionId, dimensionScopeItemId),
        1};
  }
  return {1, 1};
}

} // namespace

std::function<bool(entities::ContainerId)> getAcceptingContainersCheckFunc(
    const Problem& problem,
    entities::ContainerId hotContainer) {
  return [&problem, hotContainer](entities::ContainerId container) {
    return container != hotContainer &&
        !problem.not_accepting_containers.contains(container);
  };
}

std::optional<entities::ScopeItemId> getDimensionScopeItemIdForContainer(
    const entities::Universe& universe,
    entities::DimensionId dimensionId,
    entities::ContainerId containerId) {
  const auto& objectDimension =
      universe.getObjects().getDimension(dimensionId).only();
  if (!objectDimension.isDynamic()) {
    return std::nullopt;
  }
  return universe.getScope(objectDimension.getScopeId())
      .getScopeItemId(containerId);
}

size_t calculateSwapRatio(
    const entities::Universe& universe,
    const entities::ObjectId largerObject,
    const entities::ObjectId smallerObject,
    const entities::DimensionId dimensionId,
    std::optional<entities::ScopeItemId> dimensionScopeItemId) {
  const auto& objectDimension =
      universe.getObjects().getDimension(dimensionId).only();
  const auto largerValue =
      objectDimension.getValue(largerObject, dimensionScopeItemId);
  const auto smallerValue =
      objectDimension.getValue(smallerObject, dimensionScopeItemId);
  if (smallerValue > 0 && largerValue > smallerValue) {
    return static_cast<size_t>(std::ceil(largerValue / smallerValue));
  }
  return 1;
}

std::vector<SwapCandidateImplicit> generateDimensionBasedSwapCandidates(
    const entities::Universe& universe,
    const ObjectStore* FOLLY_NULLABLE hotObjectsInEquivSet,
    entities::ObjectId hotObject,
    const EquivSetToObjects& equivSetToColdObjects,
    const std::optional<std::string>& dimensionName,
    std::optional<entities::ScopeItemId> dimensionScopeItemId) {
  if (!dimensionName.has_value()) {
    std::vector<ObjectBundle> coldObjectBundles;
    coldObjectBundles.reserve(equivSetToColdObjects.size());
    for (const auto& [_, coldObjectsInEquivSet] : equivSetToColdObjects) {
      assert(!coldObjectsInEquivSet.empty());
      coldObjectBundles.emplace_back(
          ObjectBundle({*coldObjectsInEquivSet.begin()}));
    }
    return {std::make_tuple(
        ObjectBundle({hotObject}), std::move(coldObjectBundles))};
  }

  if (hotObjectsInEquivSet == nullptr) {
    throw std::invalid_argument(
        fmt::format(
            "Hot object {} needs its equivalence set to swap by dimension {}",
            hotObject,
            *dimensionName));
  }
  const auto& hotObjects = *hotObjectsInEquivSet;
  const auto dimensionId = universe.getDimensionId(*dimensionName);
  folly::F14FastMap<size_t, SwapCandidateImplicit> candidatesPerHotBundleSize;

  const auto appendColdBundleForHotBundleSize =
      [&candidatesPerHotBundleSize, &hotObjects, hotObject](
          const size_t hotBundleSize,
          const ObjectStore& coldObjectsInEquivSet,
          const entities::ObjectId coldObject,
          const size_t coldBundleSize) {
        auto candidateIt = candidatesPerHotBundleSize.find(hotBundleSize);
        if (candidateIt == candidatesPerHotBundleSize.end()) {
          auto hotBundle =
              buildAnchoredBundle(hotObjects, hotObject, hotBundleSize);
          candidateIt =
              candidatesPerHotBundleSize
                  .emplace(
                      hotBundleSize,
                      std::make_tuple(
                          std::move(hotBundle), std::vector<ObjectBundle>{}))
                  .first;
        }

        auto& [_, coldBundles] = candidateIt->second;
        coldBundles.push_back(buildAnchoredBundle(
            coldObjectsInEquivSet, coldObject, coldBundleSize));
      };

  for (const auto& [_, coldObjectsInEquivSet] : equivSetToColdObjects) {
    assert(!coldObjectsInEquivSet.empty());

    const auto coldObject = *coldObjectsInEquivSet.begin();
    const auto [hotBundleSize, coldBundleSize] = getBundleSizes(
        universe, hotObject, coldObject, dimensionId, dimensionScopeItemId);
    if (hotBundleSize != 1 && coldBundleSize != 1) {
      throw std::runtime_error("Expect either 1:k or k:1 bundles");
    }
    if (hotObjects.size() < hotBundleSize ||
        coldObjectsInEquivSet.size() < coldBundleSize) {
      continue;
    }

    appendColdBundleForHotBundleSize(
        hotBundleSize, coldObjectsInEquivSet, coldObject, coldBundleSize);
  }

  std::vector<SwapCandidateImplicit> candidates;
  candidates.reserve(candidatesPerHotBundleSize.size());
  for (auto& [_, candidate] : candidatesPerHotBundleSize) {
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

std::optional<interface::ObjectsToExploreOptions> getBundleOptions(
    const entities::Universe& universe,
    ::apache::thrift::optional_field_ref<interface::ObjectBundleFormationHints&>
        bundleHints,
    entities::ContainerId containerId) {
  std::optional<interface::ObjectsToExploreOptions> bundleOptions;
  if (bundleHints.has_value() &&
      bundleHints.value().scopeItemToObjectsToExploreOptions()) {
    const auto scopeId = universe.getScopeId(*bundleHints.value().scopeName());
    const auto& scopeItemToObjectsToExploreOptions =
        bundleHints.value().scopeItemToObjectsToExploreOptions().value();

    const auto& scope = universe.getScope(scopeId);
    std::optional<entities::ScopeItemId> containerScopeItemId =
        scope.getScopeItemId(containerId);

    if (containerScopeItemId) {
      const std::string& containerScopeItemName =
          universe.getEntityName(*containerScopeItemId);
      if (scopeItemToObjectsToExploreOptions.contains(containerScopeItemName)) {
        bundleOptions =
            scopeItemToObjectsToExploreOptions.at(containerScopeItemName);
      }
    }
  }
  return bundleOptions;
}

} // namespace facebook::rebalancer
