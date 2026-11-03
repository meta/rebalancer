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

#include "algopt/rebalancer/solver/moves/SwapMoveType.h"

#include "algopt/rebalancer/algopt_common/Timer.h"
#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/solver/iterators/CartesianProduct.h"
#include "algopt/rebalancer/solver/iterators/Filter.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/Move.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"
#include "algopt/rebalancer/solver/moves/MoveTypeUtils.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"

#include <fmt/core.h>

namespace facebook::rebalancer {

namespace {

EquivSetToObjects getEquivSetToObjects(
    const ObjectStore& objects,
    const EquivalenceSets& equivalenceSets,
    const ObjectStore::Factory& objectStoreFactory) {
  EquivSetToObjects equivSetToObjects;
  for (const auto object : objects) {
    const auto equivSetId = equivalenceSets.at(object);
    if (auto* objectsInSet = folly::get_ptr(equivSetToObjects, equivSetId)) {
      objectsInSet->insert(object);
    } else {
      auto newObjectsInSet = objectStoreFactory.get();
      newObjectsInSet.insert(object);
      equivSetToObjects.emplace(equivSetId, std::move(newObjectsInSet));
    }
  }
  return equivSetToObjects;
}

folly::Random::DefaultGenerator& getRng() {
  // make random number generator thread local to avoid contention
  static thread_local folly::Random::DefaultGenerator rng(1 /*seed*/);
  return rng;
}

template <typename CollectionType>
bool isEmpty(const CollectionType& collection) {
  return collection.begin() == collection.end();
}

template <typename CollectionType>
bool containsExactlyOneElement(const CollectionType& collection) {
  auto first = collection.begin();
  if (isEmpty(collection)) {
    return false;
  }
  ++first;
  return first == collection.end();
}

} // namespace

SwapMoveType::SwapMoveType(
    const interface::LocalSearchSolverSpec& solverConfigs,
    const interface::SwapMoveTypeSpec& config)
    : AsyncSingleMovesMoveType(solverConfigs), config_(config) {}

std::string SwapMoveType::name() const {
  return kSwapMoveTypeName.str();
}

MoveResult SwapMoveType::exploreFromAllSingleMoves(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    MoveStatsAggregator& stats) {
  return exploreSwappingHotObjectWithObjectsInColdContainer(
      evaluator, hotContainer, hotObject, coldContainer, stats);
}

MoveResult SwapMoveType::exploreSwappingHotObjectWithObjectsInColdContainer(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    MoveStatsAggregator& stats,
    bool shouldParallelizeWithinColdContainer,
    std::optional<double> timeLimit) {
  struct GroupInfo {
    std::string partitionName;
    entities::GroupId groupId;
  };
  auto& problem = evaluator.getProblem();
  std::optional<GroupInfo> groupInfoOpt;
  if (auto partitionName =
          config_.partitionNameToExploreSwapsWithinObjectGroup()) {
    if (auto groupIdOpt =
            problem.getOnlyGroupIdIfExists(*partitionName, hotObject)) {
      // explore only within the hotObject's group in the given partition
      groupInfoOpt =
          GroupInfo{.partitionName = *partitionName, .groupId = *groupIdOpt};
    } else {
      // nothing to evaluate
      return MoveResult::makeEmpty();
    }
  }

  const auto& equivalenceSets = problem.getEquivalenceSets();
  std::optional<ObjectStore> groupDynamicObjects;
  std::optional<EquivSetToObjects> equivSetToGroupColdObjects;
  if (groupInfoOpt) {
    groupDynamicObjects.emplace(problem.getDynamicObjects(
        coldContainer, groupInfoOpt->partitionName, groupInfoOpt->groupId));
    equivSetToGroupColdObjects.emplace(getEquivSetToObjects(
        *groupDynamicObjects,
        equivalenceSets,
        problem.assignment.getObjectStoreFactory()));
  }
  const auto& dynamicObjects = groupDynamicObjects
      ? *groupDynamicObjects
      : evaluator.getDynamicObjects(coldContainer);
  // Without a group, use the index that CoreLocalSearchSolve builds when the
  // stage starts.
  const auto& equivSetIndex =
      problem.assignment.getObjectsIndexedByEquivSets(equivalenceSets);
  const auto& equivSetToColdObjects = equivSetToGroupColdObjects
      ? *equivSetToGroupColdObjects
      : equivSetIndex.getContainerObjects(coldContainer);

  return exploreAllAndGetBestResult(
      evaluator,
      hotContainer,
      hotObject,
      coldContainer,
      dynamicObjects,
      equivSetToColdObjects,
      stats,
      shouldParallelizeWithinColdContainer,
      timeLimit);
}

MoveResult SwapMoveType::exploreAllAndGetBestResult(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    entities::ObjectId hotObject,
    entities::ContainerId coldContainer,
    const ObjectStore& dynamicObjects,
    const EquivSetToObjects& equivSetToColdObjects,
    MoveStatsAggregator& stats,
    bool shouldParallelizeWithinColdContainer,
    std::optional<double> timeLimit) {
  auto& problem = evaluator.getProblem();
  auto& universe = problem.getUniverse();
  const auto& equivalenceSets = problem.getEquivalenceSets();

  std::optional<std::string> dimensionName;
  std::optional<entities::ScopeItemId> dimensionScopeItemId;
  const ObjectStore* hotObjectsInEquivSet = nullptr;
  if (auto swapRatioDimension = config_.swapRatioDimension()) {
    dimensionName = folly::get_default(
        *swapRatioDimension->value(),
        universe.getEntityName(hotContainer),
        *swapRatioDimension->defaultValue());
    const auto dimensionId = universe.getDimensionId(*dimensionName);
    dimensionScopeItemId = getDimensionScopeItemIdForContainer(
        universe, dimensionId, hotContainer);

    // built by CoreLocalSearchSolve when the stage starts
    const auto hotEquivalenceSetId = equivalenceSets.at(hotObject);
    const auto& hotObjectsByEquivalenceSet =
        problem.assignment.getObjectsIndexedByEquivSets(equivalenceSets)
            .getContainerObjects(hotContainer);
    hotObjectsInEquivSet =
        folly::get_ptr(hotObjectsByEquivalenceSet, hotEquivalenceSetId);
    if (hotObjectsInEquivSet == nullptr) {
      throw std::runtime_error(
          fmt::format(
              "Hot object {} is missing from dynamic equivalence set {} in container {}",
              hotObject,
              hotEquivalenceSetId,
              hotContainer));
    }
  }

  auto implicitCandidates = generateDimensionBasedSwapCandidates(
      universe,
      hotObjectsInEquivSet,
      hotObject,
      equivSetToColdObjects,
      dimensionName,
      dimensionScopeItemId);

  const auto numColdObjects = static_cast<int>(dynamicObjects.size());
  const auto shouldTryColdObjectBundle =
      [this, &evaluator, numColdObjects](const ObjectBundle& bundle) {
        assert(!bundle.empty());
        // since all objects in the bundle are equivalent,
        // it is sufficient to do this check with any object
        const auto representativeObj = *bundle.begin();
        return attemptMoveWithThisObject(
            evaluator, representativeObj, numColdObjects);
      };
  const auto& filter = problem.getInvalidMoveFilter();
  const auto shouldKeepCandidate =
      [&filter, hotContainer, coldContainer](const SwapCandidate& candidate) {
        return !anyMoveInvalid(filter, candidate.first, coldContainer) &&
            !anyMoveInvalid(filter, candidate.second, hotContainer);
      };

  const auto evaluate = [&evaluator, &stats, hotContainer, coldContainer](
                            const SwapCandidate& candidate) {
    const auto& hotObjects = candidate.first;
    const auto& coldObjects = candidate.second;
    assert(hotObjects.size() == 1 || coldObjects.size() == 1);
    MoveSet moves;
    for (const auto hotObj : hotObjects) {
      moves.insert(Move(hotObj, hotContainer, coldContainer));
    }
    for (const auto coldObj : coldObjects) {
      moves.insert(Move(coldObj, coldContainer, hotContainer));
    }
    auto result = evaluator.evaluate(std::move(moves));
    stats.add(result);
    return result;
  };

  auto bestResult = MoveResult::makeEmpty();
  if (shouldParallelizeWithinColdContainer) {
    assert(timeLimit.has_value());
    const algopt::Timer timer(true);
    for (const auto& [hotObjectBundle, coldObjectBundles] :
         implicitCandidates) {
      const auto remainingTime = timeLimit.value() - timer.getSeconds();
      if (remainingTime <= 0) {
        break;
      }

      auto filteredColdObjectBundles =
          Filter(coldObjectBundles, shouldTryColdObjectBundle);
      const std::vector<ObjectBundle> hotObjectBundles{hotObjectBundle};
      auto candidatesToEvaluate =
          CartesianProduct(hotObjectBundles, filteredColdObjectBundles);
      auto result = MoveHelper::findBest(
          problem.configs.threadPool.get(),
          Filter(candidatesToEvaluate, shouldKeepCandidate),
          std::function<MoveResult(SwapCandidate)>(evaluate),
          remainingTime,
          parallelExecutionSelector(),
          name());
      bestResult.aggregate(std::move(result));
    }
  } else {
    const auto& precision = universe.getPrecision();
    for (const auto& [hotObjectBundle, coldObjectBundles] :
         implicitCandidates) {
      auto filteredColdObjectBundles =
          Filter(coldObjectBundles, shouldTryColdObjectBundle);
      const std::vector<ObjectBundle> hotObjectBundles{hotObjectBundle};
      auto candidatesToEvaluate =
          CartesianProduct(hotObjectBundles, filteredColdObjectBundles);
      for (const auto& candidate :
           Filter(candidatesToEvaluate, shouldKeepCandidate)) {
        auto result = evaluate(candidate);
        bestResult.aggregate(std::move(result));
        if (*config_.greedyOnDst() && bestResult.isBetter(precision)) {
          return bestResult;
        }
      }
    }
  }
  return bestResult;
}

bool SwapMoveType::getSamplingStatus(int numObjects) const {
  if (!config_.sampleSize().has_value()) {
    return true;
  }
  auto sampleSize = *config_.sampleSize().value().defaultSampleSize();
  return MoveHelper::sampleWithProb(sampleSize, numObjects, getRng());
};

folly::F14FastSet<std::tuple<ObjectBundle, ObjectBundle, entities::ContainerId>>
SwapMoveType::getBundleMoveCandidates(
    entities::ContainerId srcContainerId,
    entities::ContainerId dstContainerId,
    const MovesEvaluator& evaluator,
    const interface::ObjectsToExploreOptions& bundleOptions) {
  folly::F14FastSet<
      std::tuple<ObjectBundle, ObjectBundle, entities::ContainerId>>
      moves;

  Problem& problem = evaluator.getProblem();
  const folly::F14FastMap<entities::GroupId, int> groupBundleSizeOverrides;

  const std::vector<ObjectBundle> srcObjectBundlesToExplore =
      getObjectBundlesToExplore(
          bundleOptions,
          srcContainerId,
          groupBundleSizeOverrides,
          problem,
          /*createGreedyHeterogenousBundles=*/true);
  const std::vector<ObjectBundle> dstObjectBundlesToExplore =
      getObjectBundlesToExplore(
          bundleOptions,
          dstContainerId,
          groupBundleSizeOverrides,
          problem,
          /*createGreedyHeterogenousBundles=*/true);

  auto candidates =
      CartesianProduct(srcObjectBundlesToExplore, dstObjectBundlesToExplore);
  const int numCandidates =
      srcObjectBundlesToExplore.size() * dstObjectBundlesToExplore.size();
  auto sampledCandidates =
      Filter(candidates, [this, numCandidates](const auto& /*candidates*/) {
        return getSamplingStatus(numCandidates);
      });

  for (const auto& [srcBundle, dstBundle] : sampledCandidates) {
    moves.emplace(srcBundle, dstBundle, dstContainerId);
  }

  return moves;
}

MoveResult SwapMoveType::findBestMoveWithBundleOptions(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /* hints */,
    double timeLimit,
    const interface::ObjectsToExploreOptions& bundleOptions) {
  auto& problem = evaluator.getProblem();
  auto customColdContainers = getCustomColdContainers(evaluator, hotContainer);
  auto coldContainers = Filter(
      customColdContainers ? *customColdContainers
                           : evaluator.getProblem().containers,
      getAcceptingContainersCheckFunc(problem, hotContainer));

  auto bestResult = MoveResult::makeEmpty();
  const std::function<MoveResult(
      std::tuple<ObjectBundle, ObjectBundle, entities::ContainerId>)>
      evaluate =
          [&](std::tuple<ObjectBundle, ObjectBundle, entities::ContainerId>
                  singleMove) {
            auto [hotObjectBundle, coldObjectBundle, coldContainer] =
                std::move(singleMove);
            MoveSet moves;
            for (const auto& hotObjectId : hotObjectBundle) {
              moves.insert(Move(hotObjectId, hotContainer, coldContainer));
            }
            for (const auto& coldObjectId : coldObjectBundle) {
              moves.insert(Move(coldObjectId, coldContainer, hotContainer));
            }
            auto result = evaluator.evaluate(std::move(moves));
            stats.add(result);
            return result;
          };

  const auto& filter = problem.getInvalidMoveFilter();
  const auto shouldKeepBundleCandidate =
      [&filter, hotContainer](
          const std::tuple<ObjectBundle, ObjectBundle, entities::ContainerId>&
              candidate) {
        const auto& [hotBundle, coldBundle, coldContainer] = candidate;
        return !anyMoveInvalid(filter, hotBundle, coldContainer) &&
            !anyMoveInvalid(filter, coldBundle, hotContainer);
      };

  const algopt::Timer timer(true);
  for (const auto& coldContainer : coldContainers) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      break;
    }
    if (coldContainer == hotContainer) {
      continue;
    }
    auto moves = getBundleMoveCandidates(
        hotContainer, coldContainer, evaluator, bundleOptions);
    auto result = MoveHelper::findBest(
        problem.configs.threadPool.get(),
        Filter(moves, shouldKeepBundleCandidate),
        evaluate,
        timeLimit - timer.getSeconds(),
        parallelExecutionSelector(),
        name());
    bestResult.aggregate(std::move(result));
  }
  return bestResult;
}

MoveResult SwapMoveType::findBestMove(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer,
    MoveStatsAggregator& stats,
    const SearchHints& hints,
    double timeLimit) {
  auto& problem = evaluator.getProblem();
  const auto& universe = problem.getUniverse();

  const std::optional<interface::ObjectsToExploreOptions> bundleOptions =
      getBundleOptions(
          universe, config_.objectBundleFormationHints(), hotContainer);
  if (bundleOptions.has_value()) {
    return findBestMoveWithBundleOptions(
        evaluator, hotContainer, stats, hints, timeLimit, *bundleOptions);
  }

  const bool evaluateHotObjectsInParallel = !*config_.greedyOnSrc();
  if (evaluateHotObjectsInParallel) {
    // attempt moves with all hot objects in parallel
    return AsyncSingleMovesMoveType::findBestMove(
        evaluator, hotContainer, stats, hints, timeLimit);
  }

  // else, attempt moves with one hot object at a time
  // exit early if successful

  auto& dynamicObjects = evaluator.getDynamicObjects(hotContainer);
  auto numObjects = dynamicObjects.size();
  const ObjectDeduper dedupedObjs(
      &evaluator.getProblem().getEquivalenceSets(), dynamicObjects);
  auto hotObjects = Filter(
      dedupedObjs, [this, &evaluator, numObjects](entities::ObjectId object) {
        return attemptMoveWithThisObject(evaluator, object, numObjects);
      });

  auto customColdContainers = getCustomColdContainers(evaluator, hotContainer);
  auto coldContainers = Filter(
      customColdContainers ? *customColdContainers
                           : evaluator.getProblem().containers,
      getAcceptingContainersCheckFunc(problem, hotContainer));

  const auto& precision = problem.getUniverse().getPrecision();

  auto bestResult = MoveResult::makeEmpty();
  if (isEmpty(coldContainers)) {
    // no candidates to evaluate
    return bestResult;
  }

  const algopt::Timer timer(true);
  for (const auto hotObject : hotObjects) {
    if (timer.getSeconds() >= timeLimit) {
      stats.incrNumTimeouts(bestResult);
      break;
    }
    auto result = MoveResult::makeEmpty();
    if (containsExactlyOneElement(coldContainers)) {
      // If there is only one cold container, and we are greedy on src but not
      // greedy on dst. That is, evaluateHotObjectsInParallel = false (so we
      // cannot parallelize on hot objects), but we are not greedy on dst (so we
      // can parallelize on cold objects) within the only cold container
      const bool shouldParallelizeWithinColdContainer = !*config_.greedyOnDst();
      result = exploreSwappingHotObjectWithObjectsInColdContainer(
          evaluator,
          hotContainer,
          hotObject,
          *coldContainers.begin(),
          stats,
          shouldParallelizeWithinColdContainer,
          timeLimit - timer.getSeconds());
    } else {
      // We have more than one cold container, so we can evaluate cold
      // containers in parallel
      const std::function<MoveResult(entities::ContainerId)> evaluate =
          [this, &evaluator, &stats, hotContainer, hotObject](
              auto coldContainer) {
            return exploreSwappingHotObjectWithObjectsInColdContainer(
                evaluator, hotContainer, hotObject, coldContainer, stats);
          };
      result = MoveHelper::findBest(
          evaluator.getProblem().configs.threadPool.get(),
          coldContainers,
          evaluate,
          timeLimit - timer.getSeconds(),
          parallelExecutionSelector(),
          name());
    }
    bestResult.aggregate(std::move(result));
    if (bestResult.isBetter(precision)) {
      break;
    }
  }
  return bestResult;
}

std::optional<PackerSet<entities::ContainerId>>
SwapMoveType::getCustomColdContainers(
    const MovesEvaluator& evaluator,
    entities::ContainerId hotContainer) const {
  if (auto destinationsToExplore = config_.destinationsToExplore()) {
    auto exploreOption = destinationsToExplore->getType();
    if (exploreOption ==
        interface::DestinationsToExploreOptions::Type::moveToScopeItems) {
      auto allowedColdContainersList =
          evaluator.getProblem()
              .getDestinationsGenerator()
              .getAcceptingDestinations(
                  destinationsToExplore->moveToScopeItems().value(),
                  hotContainer);
      PackerSet<entities::ContainerId> allowedColdContainers;
      for (auto& containers : allowedColdContainersList) {
        allowedColdContainers.insert(
            containers.get().begin(), containers.get().end());
      }
      return allowedColdContainers;
    } else {
      throw std::runtime_error(
          "Unsupported destinationsToExplore option for SwapMoveType:"
          "only supports moveToScopeItems; per object specialization is not supported");
    }
  }
  return std::nullopt;
}

bool SwapMoveType::attemptMoveWithThisObject(
    const MovesEvaluator& evaluator,
    entities::ObjectId objectId,
    int numObjectsInContainer) const {
  auto shouldAttemptMove = AsyncSingleMovesMoveType::attemptMoveWithThisObject(
      evaluator, objectId, numObjectsInContainer);
  if (auto sampleSize = config_.sampleSize()) {
    shouldAttemptMove =
        shouldAttemptMove &&
        MoveHelper::sampleWithProb(
            *sampleSize->defaultSampleSize(), numObjectsInContainer, getRng());
  }
  return shouldAttemptMove;
}

// SwapMove type specialization definitions
void SwapMoveType::makeGreedy(
    interface::SwapMoveTypeSpec& spec,
    bool greedyOnSrc,
    bool greedyOnDest) {
  spec.greedyOnSrc() = greedyOnSrc;
  spec.greedyOnDst() = greedyOnDest;
}

void SwapMoveType::makeFixedDest(
    interface::SwapMoveTypeSpec& spec,
    const std::string& containerScopeName,
    const std::string& specialContainerName) {
  // setup destination to explore config
  interface::MoveToScopeItemsSpec moveToScopeItems;
  interface::ScopeItemList scopeItemList;
  scopeItemList.scopeName() = containerScopeName;
  scopeItemList.scopeItems() = {specialContainerName};
  moveToScopeItems.defaultScopeItems() = std::move(scopeItemList);
  interface::DestinationsToExploreOptions destinationsToExplore;
  algopt::utils::assignThriftUnion(
      destinationsToExplore, std::move(moveToScopeItems));
  // register it to the spec
  spec.destinationsToExplore() = std::move(destinationsToExplore);
}

void SwapMoveType::makeSampled(
    interface::SwapMoveTypeSpec& spec,
    int sampleSize) {
  interface::SampleSize sampleSizeSettings;
  sampleSizeSettings.defaultSampleSize() = sampleSize;
  spec.sampleSize() = sampleSizeSettings;
}

} // namespace facebook::rebalancer
