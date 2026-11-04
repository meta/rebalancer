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

#include "algopt/rebalancer/solver/moves/FixedDestMoveType.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"

#include <folly/Random.h>

namespace facebook::rebalancer {

namespace {

// When there is an incomplete bundle in the source hot container, the overrides
// dynamically adjusts the bundle size so that this move type can
// move out objects to complete the bundle
// For example, for a requested bundle size of 3, if a container has 5
// objects from a group in the initial assignment, we will attempt moves of
// size 2 from this group to bring back the group's assignment to a multiple of
// 3
folly::F14FastMap<entities::GroupId, int> getGroupBundleSizeOverrides(
    const std::string& partition,
    int bundleSize,
    entities::ContainerId container,
    const entities::Universe& universe,
    Assignment& assignment) {
  folly::F14FastMap<entities::GroupId, int> overrides;
  const auto partitionId = universe.getPartitionId(partition);

  assignment.buildIndexByPartition(universe, partitionId);
  const auto& groupToObjects =
      assignment.getObjectsIndexedByPartition(partitionId)
          .getContainerObjects(container);
  for (const auto& [groupId, objects] : groupToObjects) {
    if (objects.size() % bundleSize != 0) {
      overrides[groupId] = objects.size() % bundleSize;
    }
  }
  return overrides;
}

} // namespace

FixedDestMoveType::FixedDestMoveType(
    const interface::LocalSearchSolverSpec& configs,
    const interface::FixedDestMoveTypeSpec& spec)
    : SingleMoveType(configs, interface::SingleMoveTypeSpec()), spec_(spec) {
  if (!spec.specialContainer()) {
    spec_.specialContainer().copy_from(configs.specialContainer());
  }
  // seed with constant value for reproducibility
  rng_ = std::make_shared<folly::Random::DefaultGenerator>(1 /* seed */);
}

std::string FixedDestMoveType::name() const {
  return kFixedDestMoveTypeName.str();
}

std::optional<PackerSet<entities::ContainerId>>
FixedDestMoveType::getCustomColdContainers(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer) const {
  if (!spec_.specialContainer()) {
    throw std::runtime_error(
        "FixedDestMoveType needs a special container to perform moves to");
  }
  auto specialContainer =
      evaluator.getProblem().containerId(*spec_.specialContainer());
  if (hotContainer != specialContainer) {
    return PackerSet<entities::ContainerId>({specialContainer});
  }
  return PackerSet<entities::ContainerId>();
}

bool FixedDestMoveType::attemptMoveWithThisObject(
    const MovesEvaluator& evaluator,
    entities::ObjectId objectId,
    int numObjects) const {
  bool shouldMove = SingleMoveType::attemptMoveWithThisObject(
      evaluator, objectId, numObjects);
  if (!spec_.sampleSize().has_value()) {
    return shouldMove;
  }
  auto sampleSize = *spec_.sampleSize()->defaultSampleSize();
  shouldMove =
      shouldMove && MoveHelper::sampleWithProb(sampleSize, numObjects, *rng_);
  return shouldMove;
}

void FixedDestMoveType::makeSampled(
    interface::FixedDestMoveTypeSpec& spec,
    int sampleSize) {
  interface::SampleSize sampleSizeSettings;
  sampleSizeSettings.defaultSampleSize() = sampleSize;
  spec.sampleSize() = std::move(sampleSizeSettings);
}

bool FixedDestMoveType::getSamplingStatus(int numObjects) const {
  if (!spec_.sampleSize().has_value()) {
    return true;
  }
  auto sampleSize = *spec_.sampleSize().value().defaultSampleSize();
  return MoveHelper::sampleWithProb(sampleSize, numObjects, *rng_);
};

void FixedDestMoveType::getBundleMoveCandidates(
    entities::ContainerId srcContainerId,
    entities::ContainerId dstContainerId,
    const MovesEvaluator& evaluator,
    const interface::ObjectsToExploreOptions& bundleOptions,
    folly::F14FastSet<std::pair<ObjectBundle, entities::ContainerId>>& moves) {
  Problem& problem = evaluator.getProblem();
  auto bundleHints = spec_.objectBundleFormationHints();
  const bool adjustBundleSizeForIncompleteBundles = bundleHints &&
      bundleHints.value().adjustBundleSizeForIncompleteBundles().value_or(
          false);
  folly::F14FastMap<entities::GroupId, int> groupBundleSizeOverrides;
  if (adjustBundleSizeForIncompleteBundles) {
    const int bundleSize = MoveType::getBundleSize(bundleOptions);
    const std::string& partition =
        *bundleOptions.objectsFromGroupsSpec()->groupList()->partitionName();
    groupBundleSizeOverrides = getGroupBundleSizeOverrides(
        partition,
        bundleSize,
        srcContainerId,
        problem.getUniverse(),
        problem.assignment);
  }

  const std::vector<ObjectBundle> bundles = getObjectBundlesToExplore(
      bundleOptions,
      srcContainerId,
      groupBundleSizeOverrides,
      problem,
      /*createGreedyHeterogenousBundles=*/true);

  const int numBundles = static_cast<int>(bundles.size());
  auto sampledBundles =
      Filter(bundles, [this, numBundles](const ObjectBundle& /*bundle*/) {
        return getSamplingStatus(numBundles);
      });

  for (const auto& bundle : sampledBundles) {
    moves.emplace(bundle, dstContainerId);
  }
}

MoveResult FixedDestMoveType::findBestMoveWithBundleOptions(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /* hints */,
    double timeLimit,
    const interface::ObjectsToExploreOptions& bundleOptions) {
  auto& problem = evaluator.getProblem();
  auto customColdContainersMaybe =
      getCustomColdContainers(evaluator, hotContainer);
  auto customColdContainers = customColdContainersMaybe.has_value()
      ? std::move(*customColdContainersMaybe)
      : problem.containers;

  auto bestResult = MoveResult::makeEmpty();
  const std::function<MoveResult(
      std::pair<ObjectBundle, entities::ContainerId>)>
      evaluate =
          [&](std::pair<ObjectBundle, entities::ContainerId> singleMove) {
            auto [objectBundle, coldContainer] = std::move(singleMove);

            MoveSet moves;
            for (const auto objectId : objectBundle) {
              moves.insert(Move(objectId, hotContainer, coldContainer));
            }
            auto result = evaluator.evaluate(std::move(moves));
            stats.add(result);
            return result;
          };

  const algopt::Timer timer(true);
  for (const auto& coldContainer : customColdContainers) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      break;
    }
    folly::F14FastSet<std::pair<ObjectBundle, entities::ContainerId>> moves;
    if (coldContainer == hotContainer) {
      continue;
    }
    getBundleMoveCandidates(
        hotContainer, coldContainer, evaluator, bundleOptions, moves);

    const auto& filter = problem.getInvalidMoveFilter();
    const auto shouldKeepCandidate =
        [&filter, &stats](
            const std::pair<ObjectBundle, entities::ContainerId>& candidate) {
          const auto& [objectBundle, destContainer] = candidate;
          return !anyMoveInvalid(filter, objectBundle, destContainer, stats);
        };
    // evaluate moves from all containers in parallel
    auto result = MoveHelper::findBest(
        problem.configs.threadPool.get(),
        Filter(moves, shouldKeepCandidate),
        evaluate,
        timeLimit - timer.getSeconds(),
        parallelExecutionSelector(),
        name());
    bestResult.aggregate(std::move(result));
  }

  return bestResult;
}

MoveResult FixedDestMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& hints,
    double timeLimit) {
  auto& problem = evaluator.getProblem();
  const auto& universe = problem.getUniverse();
  const std::optional<interface::ObjectsToExploreOptions> bundleOptions =
      getBundleOptions(
          universe, spec_.objectBundleFormationHints(), hotContainer);

  if (bundleOptions.has_value()) {
    return findBestMoveWithBundleOptions(
        evaluator, hotContainer, stats, hints, timeLimit, *bundleOptions);
  }
  return SingleMoveType::findBestMove(
      evaluator, hotContainer, stats, hints, timeLimit);
}

} // namespace facebook::rebalancer
