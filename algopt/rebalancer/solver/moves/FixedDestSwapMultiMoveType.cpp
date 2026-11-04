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

#include "algopt/rebalancer/solver/moves/FixedDestSwapMultiMoveType.h"

#include "algopt/rebalancer/solver/iterators/CartesianProduct.h"
#include "algopt/rebalancer/solver/moves/FixedSrcDstMultiMoveType.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"

#include <folly/container/F14Map.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

namespace {
using ObjectBundle = std::vector<entities::ObjectId>;
using BundleIdx = int;
std::vector<BundleIdx> getBundleIndices(
    const std::vector<ObjectBundle>& bundles,
    auto maxSampleSize) {
  std::vector<BundleIdx> bundleIds;
  bundleIds.reserve(bundles.size());
  for (const auto idx : folly::irange(bundles.size())) {
    bundleIds.push_back(idx);
  }
  if (maxSampleSize && static_cast<size_t>(*maxSampleSize) < bundleIds.size()) {
    std::shuffle(
        bundleIds.begin(),
        bundleIds.end(),
        std::default_random_engine(0 /*seed*/));
    bundleIds.resize(*maxSampleSize);
  }
  return bundleIds;
}
} // namespace

std::string FixedDestSwapMultiMoveType::name() const {
  return kFixedDestSwapMultiMoveTypeName.str();
}

MoveResult FixedDestSwapMultiMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /* hints */,
    double timeLimit) {
  if (!spec_.specialContainer()) {
    throw std::runtime_error(
        "FixedDestMultiMoveType needs a special container to perform moves");
  }

  auto& p = evaluator.getProblem();
  // since its a swap move, calling special container as dst container is an
  // arbitrary choice, but assuming that special container is the dst, we can
  // try to be greedy on src container depending on the move type setting spec_
  auto dstContainer = p.containerId(*spec_.specialContainer());

  // If the move type has swap ratio dimension configured, 1:k swap is required
  // and the swap ratio is determined based on the hotObject and the
  // swapRatioDimension config. 1:k swaps need per-source dynamic bundle
  // recomputation, which is inherently the greedy-on-src iteration
  // structure (`findBestMoveWithSwapRatio`); there is no implemented
  // non-greedy 1:k algorithm today. The non-greedy branch falls through
  // to the regular non-greedy enumeration below, IGNORING the swap-ratio
  // config — that preserves non-greedy's broad equiv-set sampling at the
  // cost of not performing size-aware 1:k swaps in non-greedy stages.
  // Safe to fall through: `srcContainerMoveGenerator_` /
  // `dstContainerMoveGenerator_` are constructed once from `spec` and do
  // not bake in the swap-ratio config, and the non-ratio
  // `filterSourceObjectsByBundleSizeAndSearchSpacePartition` overload
  // used below doesn't reference it either.
  assert(spec_.rasLocalSearchMetadata());
  const auto& rasMetadata = *spec_.rasLocalSearchMetadata();
  if (rasMetadata.swapRatioDimension().has_value() &&
      *rasMetadata.useAdaptiveAllotments() && *spec_.greedyOnSrc()) {
    return findBestMoveWithSwapRatio(
        evaluator, hotContainer, dstContainer, stats, timeLimit);
  }

  // Original logic for non-swap-ratio case
  auto srcObjectBundles =
      srcContainerMoveGenerator_
          .filterSourceObjectsByBundleSizeAndSearchSpacePartition(
              evaluator, hotContainer, dstContainer, stats);

  auto dstObjectBundles =
      dstContainerMoveGenerator_
          .filterSourceObjectsByBundleSizeAndSearchSpacePartition(
              evaluator, dstContainer, hotContainer, stats);

  const std::vector<BundleIdx> srcObjectBundleIds =
      getBundleIndices(srcObjectBundles, spec_.maxSampleSizeOnSrc());
  const std::vector<BundleIdx> dstObjectBundleIds =
      getBundleIndices(dstObjectBundles, spec_.maxSampleSizeOnDst());

  const std::function<MoveResult(std::pair<BundleIdx, BundleIdx>)> evaluate =
      [&](std::pair<BundleIdx, BundleIdx> srcDstPair) {
        MoveSet moves;
        auto& srcObjectBundle = srcObjectBundles.at(srcDstPair.first);
        // Add moves from hot container to special container
        for (auto objectId : srcObjectBundle) {
          moves.insert(Move(objectId, hotContainer, dstContainer));
        }
        // Add moves from special container to hot container
        auto& dstObjectBundle = dstObjectBundles.at(srcDstPair.second);
        for (auto objectId : dstObjectBundle) {
          moves.insert(Move(objectId, dstContainer, hotContainer));
        }
        auto result = evaluator.evaluate(std::move(moves));
        stats.add(result);
        return result;
      };

  auto bestResult = MoveResult::makeEmpty();
  if (*spec_.greedyOnSrc()) {
    const auto& precision = p.getUniverse().getPrecision();
    const algopt::Timer timer(true);
    for (auto srcObjectBundleId : srcObjectBundleIds) {
      auto result = MoveHelper::findBest(
          evaluator.getProblem().configs.threadPool.get(),
          CartesianProduct(
              std::vector<BundleIdx>({srcObjectBundleId}), dstObjectBundleIds),
          evaluate,
          timeLimit - timer.getSeconds(),
          parallelExecutionSelector(),
          name());
      bestResult.aggregate(std::move(result));
      if (bestResult.isBetter(precision)) {
        // stop search once we found a better move
        return bestResult;
      }
      if (timer.getSeconds() > timeLimit) {
        // stop search once time limit is reached
        stats.incrNumTimeouts(bestResult);
        return bestResult;
      }
    }
  } else {
    return MoveHelper::findBest(
        evaluator.getProblem().configs.threadPool.get(),
        CartesianProduct(srcObjectBundleIds, dstObjectBundleIds),
        evaluate,
        timeLimit,
        parallelExecutionSelector(),
        name());
  }
  return bestResult;
}

MoveResult FixedDestSwapMultiMoveType::findBestMoveWithSwapRatio(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ContainerId dstContainer,
    MoveStatsAggregator& stats,
    double timeLimit) {
  const auto& rasMetadata = *spec_.rasLocalSearchMetadata();
  auto bestResult = MoveResult::makeEmpty();
  if (!rasMetadata.swapRatioDimension().has_value() ||
      !*rasMetadata.useAdaptiveAllotments()) {
    return bestResult;
  } else {
    assert(rasMetadata.serverPartition().has_value());
  }
  // Generate source bundles (always needed)
  auto srcObjectBundles =
      srcContainerMoveGenerator_
          .filterSourceObjectsByBundleSizeAndSearchSpacePartition(
              evaluator, hotContainer, dstContainer, stats);

  const std::vector<BundleIdx> srcObjectBundleIds =
      getBundleIndices(srcObjectBundles, spec_.maxSampleSizeOnSrc());

  // Evaluation function for swap ratio mode
  const std::function<MoveResult(std::pair<BundleIdx, ObjectBundle>)>
      evaluateWithMultiObjects =
          [&](const std::pair<BundleIdx, ObjectBundle>& srcDstPair) {
            MoveSet moves;
            auto& srcObjectBundle = srcObjectBundles.at(srcDstPair.first);
            // Add moves from hot container to special container
            for (auto objectId : srcObjectBundle) {
              moves.insert(Move(objectId, hotContainer, dstContainer));
            }
            // Add moves from special container to hot container
            for (auto objectId : srcDstPair.second) {
              moves.insert(Move(objectId, dstContainer, hotContainer));
            }
            auto result = evaluator.evaluate(std::move(moves));
            stats.add(result);
            return result;
          };

  const auto& universe = evaluator.getProblem().getUniverse();
  const auto& precision = universe.getPrecision();
  const algopt::Timer timer(true);
  const auto& swapRatioDimension = *rasMetadata.swapRatioDimension();
  const auto dimensionName = folly::get_default(
      *swapRatioDimension.value(),
      universe.getEntityName(hotContainer),
      *swapRatioDimension.defaultValue());
  const auto swapRatioDimensionId = universe.getDimensionId(dimensionName);
  const auto dimensionScopeItemId = getDimensionScopeItemIdForContainer(
      universe, swapRatioDimensionId, hotContainer);
  const auto& objectDimension =
      universe.getObjects().getDimension(swapRatioDimensionId).only();
  // Within one findBestMove call, destination bundles depend on the hot object
  // only through this dimension value. Cache by its exact representation so
  // repeated shapes do not rebuild the same bundles for every source object.
  folly::F14FastMap<double, std::vector<ObjectBundle>> hotValueToBundles;

  for (auto srcObjectBundleId : srcObjectBundleIds) {
    const auto& srcObjectBundle = srcObjectBundles.at(srcObjectBundleId);
    const auto hotObject = srcObjectBundle[0];
    const auto hotValue =
        objectDimension.getValue(hotObject, dimensionScopeItemId);

    // Build the configuration from the same value used as the cache key.
    auto [it, inserted] =
        hotValueToBundles.try_emplace(hotValue, std::vector<ObjectBundle>{});
    if (inserted) {
      // Create config with lambda that safely captures the universe
      // reference.
      auto serverPartitionId =
          universe.getPartitionId(*rasMetadata.serverPartition());
      MultiObjectSelectionConfig swapRatioConfig;
      swapRatioConfig.getBundleSizeForGroup =
          [&universe,
           hotObject,
           swapRatioDimensionId,
           serverPartitionId,
           dimensionScopeItemId](entities::GroupId serverId) -> int {
        // Get any object from this server/group to calculate the swap ratio
        const auto& serverPartition = universe.getPartition(serverPartitionId);
        const auto& objectsInGroup = serverPartition.getObjectIds(serverId);
        assert(!objectsInGroup.empty());
        const auto representativeColdObject = objectsInGroup[0];
        return static_cast<int>(calculateSwapRatio(
            universe,
            hotObject,
            representativeColdObject,
            swapRatioDimensionId,
            dimensionScopeItemId));
      };
      it->second = dstContainerMoveGenerator_
                       .filterSourceObjectsByBundleSizeAndSearchSpacePartition(
                           evaluator,
                           dstContainer,
                           hotContainer,
                           stats,
                           swapRatioConfig);
    }

    auto result = MoveHelper::findBest(
        evaluator.getProblem().configs.threadPool.get(),
        CartesianProduct(
            std::vector<BundleIdx>({srcObjectBundleId}), it->second),
        evaluateWithMultiObjects,
        timeLimit - timer.getSeconds(),
        parallelExecutionSelector(),
        name());
    bestResult.aggregate(std::move(result));

    if (bestResult.isBetter(precision)) {
      // stop search once we found a better move
      return bestResult;
    }
    if (timer.getSeconds() > timeLimit) {
      // stop search once time limit is reached
      stats.incrNumTimeouts(bestResult);
      return bestResult;
    }
  }

  return bestResult;
}

} // namespace facebook::rebalancer
