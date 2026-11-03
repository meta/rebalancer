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

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/solver/moves/DestinationsToExploreGenerator.h"
#include "algopt/rebalancer/solver/moves/ObjectsToExploreGenerator.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/container/irange.h>
#include <folly/CppAttributes.h>

#include <optional>
#include <tuple>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

// A swap candidate is represented as (hotBundle, coldBundle).
using SwapCandidate = std::pair<ObjectBundle, ObjectBundle>;

// An implicit swap candidate is represented as (hotBundle, coldBundles).
using SwapCandidateImplicit =
    std::tuple<ObjectBundle, std::vector<ObjectBundle>>;

using EquivSetToObjects = PackerMap<entities::EquivalenceSetId, ObjectStore>;

std::function<bool(entities::ContainerId)> getAcceptingContainersCheckFunc(
    const Problem& problem,
    entities::ContainerId hotContainer);

// Output container defaults to a set; callers wanting a different container
// pass the container template explicitly: getRandomSample<std::vector>(...).
// The element type is deduced from the input.
template <
    template <typename...> class OutputContainer = PackerSet,
    typename InputCollection>
auto getRandomSample(
    const InputCollection& idsCollection,
    size_t sampleSize,
    std::mt19937& rng,
    std::optional<typename InputCollection::value_type> idToExcludeFromSample =
        std::nullopt) {
  using Output = OutputContainer<typename InputCollection::value_type>;

  if (sampleSize == 0) {
    return Output{};
  }

  Output sampledIds;
  algopt::utils::reserveIfPossible(sampledIds, sampleSize);

  if (idToExcludeFromSample.has_value()) {
    InputCollection filteredIds;
    algopt::utils::reserveIfPossible(
        filteredIds, static_cast<size_t>(idsCollection.size()));
    std::copy_if(
        idsCollection.begin(),
        idsCollection.end(),
        algopt::utils::getBackInserterElseInserter(filteredIds),
        [&idToExcludeFromSample](const auto& id) {
          return id != idToExcludeFromSample.value();
        });

    std::sample(
        filteredIds.begin(),
        filteredIds.end(),
        algopt::utils::getBackInserterElseInserter(sampledIds),
        sampleSize,
        rng);
  } else {
    std::sample(
        idsCollection.begin(),
        idsCollection.end(),
        algopt::utils::getBackInserterElseInserter(sampledIds),
        sampleSize,
        rng);
  }

  return sampledIds;
}

template <typename Input>
std::vector<size_t> getSamplesSizeForEachSet(
    const ReferenceList<const std::vector<Input>>& idsList,
    size_t requiredSampleSize,
    std::mt19937& rng) {
  using ConstVectorIterator = typename std::vector<Input>::const_iterator;
  struct SetIterInfo {
    ConstVectorIterator curr;
    ConstVectorIterator end;
  };

  const auto setCount = idsList.size();
  size_t setsFinished = 0;
  std::vector<SetIterInfo> setToInfo;
  for (const auto& idsRef : idsList) {
    auto& ids = idsRef.get();
    if (ids.size() == 0) {
      ++setsFinished;
    }
    setToInfo.push_back({ids.begin(), ids.end()});
  }

  std::vector<size_t> setToSamplesToTake(setCount, 0);
  size_t samplesTaken = 0;
  int index = 0;
  std::vector<size_t> indexes(setCount);
  std::iota(indexes.begin(), indexes.end(), 0);
  std::shuffle(indexes.begin(), indexes.end(), rng);
  while (samplesTaken < requiredSampleSize && setsFinished < setCount) {
    auto& info = setToInfo.at(indexes[index]);
    if (info.curr != info.end) {
      ++setToSamplesToTake.at(indexes[index]);
      ++samplesTaken;
      ++info.curr;
      if (info.curr == info.end) {
        ++setsFinished;
      }
    }

    index = (index + 1) % setCount;
  }

  return setToSamplesToTake;
}

template <class Input>
std::vector<Input> getRandomSampleWithReplacement(
    const std::vector<Input>& idsCollection,
    int sampleSize,
    std::mt19937& rng,
    std::optional<Input> idToExcludeFromSample = std::nullopt);

/**
Given a list of references to vectors which are expected to be disjoint
(disjointeness is not enforced), where the list has size n, and a
totalRequiredSampleSize, return at most totalRequiredSampleSize samples where
the samples S from each set are taken uniformly at random from the set and the
number of samples from each set is "as equal as possible". Here "as equal as
possible" refers to a round-robin way of picking the samples, where, for
example, if the number of samples requested is 8 and there are four sets with
sizes 2, 1, 3, 0, 5, respectively, then the number of samples from each set is
2, 1, 2, 0, 3, respectively.
*/
template <typename Input>
PackerSet<Input> getEqualSizeRandomSamplesFromEachSetIn(
    const ReferenceList<const std::vector<Input>>& inputCollection,
    size_t totalRequiredSampleSize,
    std::mt19937& rng,
    std::optional<Input> idToExcludeFromSample = std::nullopt) {
  if (totalRequiredSampleSize < 0) {
    throw std::runtime_error(
        fmt::format(
            "found negative sample size {} in getEqualSizeRandomContainersSampleFromEachSetIn()",
            totalRequiredSampleSize));
  }
  if (totalRequiredSampleSize == 0) {
    return {};
  }

  // when idToExcludeFromSample is provided, we may have to potentially sample
  // totalRequiredSampleSize + 1 samples; hence get set sizes based on the
  // updated sample size
  const auto updateSampleSize = idToExcludeFromSample.has_value()
      ? totalRequiredSampleSize + 1
      : totalRequiredSampleSize;
  const auto setToSampleSize =
      getSamplesSizeForEachSet(inputCollection, updateSampleSize, rng);
  std::vector<size_t> indexes(inputCollection.size());
  std::iota(indexes.begin(), indexes.end(), 0);
  std::shuffle(indexes.begin(), indexes.end(), rng);
  PackerSet<Input> sampledIds;
  for (const auto i : folly::irange(inputCollection.size())) {
    const auto index = indexes.at(i);
    const auto toSample = totalRequiredSampleSize - sampledIds.size();
    if (toSample == 0) {
      break;
    }

    const auto sampleSize = std::min(toSample, setToSampleSize.at(index));
    if (sampleSize == 0) {
      continue;
    }

    auto sample = getRandomSample(
        inputCollection[index].get(), sampleSize, rng, idToExcludeFromSample);
    sampledIds.insert(sample.begin(), sample.end());
  }

  return sampledIds;
}

// Returns the scope item for a container when dimensionId identifies a dynamic
// dimension. Static dimensions and out-of-scope containers return nullopt.
std::optional<entities::ScopeItemId> getDimensionScopeItemIdForContainer(
    const entities::Universe& universe,
    entities::DimensionId dimensionId,
    entities::ContainerId containerId);

// Compute swap ratio between two objects based on a dimension.
// Returns ceil(largerValue / smallerValue) when largerValue > smallerValue > 0,
// otherwise returns 1 (fallback to 1:1 swap for zero or equal values).
//
// @param largerObject Object expected to have the larger dimension value
// @param smallerObject Object expected to have the smaller dimension value
// @param dimensionId Dimension ID to use for ratio calculation
// @param dimensionScopeItemId Scope item to use for dynamic dimensions
// @return Swap ratio: how many smallerObjects needed to match one largerObject
size_t calculateSwapRatio(
    const entities::Universe& universe,
    entities::ObjectId largerObject,
    entities::ObjectId smallerObject,
    entities::DimensionId dimensionId,
    std::optional<entities::ScopeItemId> dimensionScopeItemId = std::nullopt);

/**
 * Generates ratio-aware implicit swap candidates for SwapMoveType.
 *
 * Each returned entry owns one hot bundle and all compatible cold bundles for
 * that hot bundle. SwapMoveType can then enumerate the actual
 * (hotBundle, coldBundle) pairs lazily instead of materializing every explicit
 * combination upfront.
 *
 * The selected hotObject always anchors the hot bundle. When dimensionName is
 * configured, the side with the smaller objects is bundled to form the uneven
 * swap candidate, and hotObjectsInEquivSet must hold the hot object's
 * equivalence set in its container; otherwise it is not read. Throws if the
 * required bundle cannot be formed from the currently dynamic objects.
 */
std::vector<SwapCandidateImplicit> generateDimensionBasedSwapCandidates(
    const entities::Universe& universe,
    const ObjectStore* FOLLY_NULLABLE hotObjectsInEquivSet,
    entities::ObjectId hotObject,
    const EquivSetToObjects& equivSetToColdObjects,
    const std::optional<std::string>& dimensionName,
    std::optional<entities::ScopeItemId> dimensionScopeItemId = std::nullopt);

template <class Input>
std::vector<Input> getRandomSampleWithReplacement(
    const std::vector<Input>& idsCollection,
    int sampleSize,
    std::mt19937& rng,
    std::optional<Input> idToExcludeFromSample) {
  if (sampleSize < 0) {
    throw std::runtime_error(
        fmt::format(
            "found negative sample size {} in getRandomSampleWithReplacement()",
            sampleSize));
  }
  if (sampleSize == 0 || idsCollection.empty() ||
      (idsCollection.size() == 1 && idToExcludeFromSample.has_value() &&
       idsCollection.at(0) == idToExcludeFromSample.value())) {
    return {};
  }
  std::vector<Input> sampledIds;
  auto uniformDist =
      std::uniform_int_distribution<>(0, idsCollection.size() - 1);
  while (sampledIds.size() < (size_t)sampleSize) {
    auto index = uniformDist(rng);
    auto id = idsCollection.at(index);
    if (idToExcludeFromSample.has_value() &&
        id == idToExcludeFromSample.value()) {
      continue;
    }
    sampledIds.push_back(id);
  }
  return sampledIds;
}

std::optional<interface::ObjectsToExploreOptions> getBundleOptions(
    const entities::Universe& universe,
    ::apache::thrift::optional_field_ref<interface::ObjectBundleFormationHints&>
        bundleHints,
    entities::ContainerId containerId);

} // namespace facebook::rebalancer
