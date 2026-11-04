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

#include "algopt/rebalancer/solver/moves/FixedSourceMoveType.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"

namespace facebook::rebalancer {

namespace {

// When there is an incomplete bundle in the destination, the overrides
// dynamically adjusts the bundle size to complete that bundle.
// For example, for a requested bundle size of 5, if a container already has 2
// objects from a group in the initial assignment, we will attempt moves of
// size 3 from this group to try to complete the bundle.
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
      overrides[groupId] = bundleSize - (objects.size() % bundleSize);
    }
  }
  return overrides;
}

} // namespace

std::string FixedSourceMoveType::name() const {
  return kSingleFixedSourceMoveTypeName.str();
}

FixedSourceMoveType::FixedSourceMoveType(
    const interface::LocalSearchSolverSpec& configs,
    const interface::SingleFixedSourceMoveTypeSpec& singleFixedSourceSpec)
    : MoveType(configs) {
  // seed with constant value for reproducibility
  rng_ = std::make_shared<folly::Random::DefaultGenerator>(1 /* seed */);
  if (configs.singleFixedSourceMoveTypeSpec()) {
    singleFixedSourceSpec_ = configs.singleFixedSourceMoveTypeSpec().value();
  } else {
    singleFixedSourceSpec_ = singleFixedSourceSpec;
  }
  if (configs.specialContainer() &&
      !singleFixedSourceSpec_.specialContainer()) {
    singleFixedSourceSpec_.specialContainer() = *configs.specialContainer();
  }
}

void FixedSourceMoveType::makeSampled(
    interface::SingleFixedSourceMoveTypeSpec& spec,
    int sampleSize) {
  interface::SampleSize sampleSizeSettings;
  sampleSizeSettings.defaultSampleSize() = sampleSize;
  spec.sampleSize() = std::move(sampleSizeSettings);
}

void FixedSourceMoveType::getSingleMoveCandidates(
    entities::ContainerId srcContainerId,
    const MovesEvaluator& evaluator,
    folly::F14FastSet<std::pair<ObjectBundle, entities::ContainerId>>& moves) {
  auto& objects = evaluator.getDynamicObjects(srcContainerId);
  Problem& problem = evaluator.getProblem();
  const auto dedupedObjs = problem.assignment.getDistinctObjectsFromEquivSetIdx(
      srcContainerId, problem.getEquivalenceSets());

  const int numObjects = static_cast<int>(objects.size());
  auto containerObjectIds =
      Filter(dedupedObjs, [this, numObjects](entities::ObjectId /*object*/) {
        return getSamplingStatus(numObjects);
      });

  for (const auto objectId : containerObjectIds) {
    moves.emplace(ObjectBundle({objectId}), srcContainerId);
  }
}

void FixedSourceMoveType::getBundleMoveCandidates(
    entities::ContainerId srcContainerId,
    entities::ContainerId dstContainerId,
    const MovesEvaluator& evaluator,
    const interface::ObjectsToExploreOptions& bundleOptions,
    folly::F14FastSet<std::pair<ObjectBundle, entities::ContainerId>>& moves) {
  auto bundleHints = singleFixedSourceSpec_.objectBundleFormationHints();
  const bool adjustBundleSizeForIncompleteBundles = bundleHints &&
      bundleHints.value().adjustBundleSizeForIncompleteBundles().value_or(
          false);

  Problem& problem = evaluator.getProblem();
  folly::F14FastMap<entities::GroupId, int> groupBundleSizeOverrides;
  if (adjustBundleSizeForIncompleteBundles) {
    const int bundleSize = MoveType::getBundleSize(bundleOptions);
    const std::string& partition =
        *bundleOptions.objectsFromGroupsSpec()->groupList()->partitionName();
    groupBundleSizeOverrides = getGroupBundleSizeOverrides(
        partition,
        bundleSize,
        dstContainerId,
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
    moves.emplace(bundle, srcContainerId);
  }
}

MoveResult FixedSourceMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  if (!singleFixedSourceSpec_.specialContainer() &&
      !singleFixedSourceSpec_.scopeItemList()->scopeItems()) {
    throw std::runtime_error(
        "FixedSourceMoveType needs a special container or a list of scope items to perform moves from");
  }

  auto& p = evaluator.getProblem();
  const auto& universe = p.getUniverse();
  const auto& precision = universe.getPrecision();

  std::string scopeName;
  std::vector<std::string> scopeItems;
  bool stopEarlyAtScopeItemThatImprovesObjective = false;

  if (singleFixedSourceSpec_.scopeItemList()->scopeItems()) {
    auto scopeItemsRef = singleFixedSourceSpec_.scopeItemList();
    if (scopeItemsRef->scopeItems()->empty()) {
      throw std::runtime_error(
          "FixedSourceMoveType needs list of scope items to perform moves from");
    } else {
      scopeName = *scopeItemsRef->scopeName();
      scopeItems = *scopeItemsRef->scopeItems();
      stopEarlyAtScopeItemThatImprovesObjective =
          *singleFixedSourceSpec_.stopEarlyAtScopeItemThatImprovesObjective();
    }
  } else {
    scopeName = universe.getContainerTypeName();
    scopeItems = {*singleFixedSourceSpec_.specialContainer()};
  }

  const std::optional<interface::ObjectsToExploreOptions> bundleOptions =
      getBundleOptions(
          universe,
          singleFixedSourceSpec_.objectBundleFormationHints(),
          hotContainer);

  auto bestResult = MoveResult::makeEmpty();

  const std::function<MoveResult(
      std::pair<ObjectBundle, entities::ContainerId>)>
      evaluate =
          [&](std::pair<ObjectBundle, entities::ContainerId> singleMove) {
            auto [objectBundle, containerId] = std::move(singleMove);

            MoveSet moves;
            for (const auto objectId : objectBundle) {
              moves.insert(Move(objectId, containerId, hotContainer));
            }
            auto result = evaluator.evaluate(std::move(moves));
            stats.add(result);
            return result;
          };

  const auto scopeId = universe.getScopeId(scopeName);
  const auto& scope = universe.getScope(scopeId);
  const algopt::Timer timer(true);
  for (const auto& scopeItemName : scopeItems) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      break;
    }
    const auto& containerIds =
        scope.getContainerIds(universe.getScopeItemId(scopeId, scopeItemName));

    folly::F14FastSet<std::pair<ObjectBundle, entities::ContainerId>> moves;

    for (const auto& containerId : containerIds) {
      if (containerId == hotContainer) {
        continue;
      }
      if (bundleOptions.has_value()) {
        getBundleMoveCandidates(
            containerId, hotContainer, evaluator, *bundleOptions, moves);
      } else {
        getSingleMoveCandidates(containerId, evaluator, moves);
      }
    }

    const auto& filter = p.getInvalidMoveFilter();
    const auto shouldKeepCandidate =
        [&filter, &stats, hotContainer](
            const std::pair<ObjectBundle, entities::ContainerId>& candidate) {
          return !anyMoveInvalid(filter, candidate.first, hotContainer, stats);
        };

    // evaluate moves from all containers in a scope item in parallel
    auto result = MoveHelper::findBest(
        p.configs.threadPool.get(),
        Filter(moves, shouldKeepCandidate),
        evaluate,
        timeLimit - timer.getSeconds(),
        parallelExecutionSelector(),
        name());
    bestResult.aggregate(std::move(result));

    if (stopEarlyAtScopeItemThatImprovesObjective &&
        bestResult.isBetter(precision)) {
      // if we have a move that improves the objective in the current scope item
      // we can stop looking at other scope items
      break;
    }
  }

  return bestResult;
}

bool FixedSourceMoveType::getSamplingStatus(int numObjects) const {
  if (!singleFixedSourceSpec_.sampleSize().has_value()) {
    return true;
  }
  auto sampleSize =
      *singleFixedSourceSpec_.sampleSize().value().defaultSampleSize();
  return MoveHelper::sampleWithProb(sampleSize, numObjects, *rng_);
}

} // namespace facebook::rebalancer
