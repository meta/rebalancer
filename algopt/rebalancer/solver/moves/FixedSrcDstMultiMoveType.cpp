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

#include "algopt/rebalancer/solver/moves/FixedSrcDstMultiMoveType.h"

#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveHelper.h"
#include "algopt/rebalancer/solver/utils/OneObjectPerGroup.h"

#include <folly/container/irange.h>
#include <folly/String.h>

namespace facebook::rebalancer {

namespace {
using entities::ContainerId;
using entities::EquivalenceSetId;
using entities::GroupId;
using entities::ObjectId;
using entities::PartitionId;

class ObjectBundleSelector {
 public:
  explicit ObjectBundleSelector(int defaultBundleSize)
      : bundleSize_(defaultBundleSize) {}
  virtual ~ObjectBundleSelector() = default;

  // returns true if the bundle is complete
  // maxCount: optional limit on how many objects to process from the input set
  //   - When nullopt: single-call mode (complete bundle in one call)
  //   - When set: accumulation mode (partial building across multiple calls)
  virtual bool addObjects(
      const PackerSet<ObjectId>& objects,
      std::optional<size_t> maxCount = std::nullopt) {
    // If complete bundle have to be formed in single call, but not enough
    // objects
    if (!maxCount.has_value() &&
        static_cast<int>(objects.size()) < bundleSize_) {
      return false;
    }

    // Determine how many objects to process from this input set
    const size_t limit = maxCount.value_or(objects.size());

    size_t count = 0;
    for (auto object : objects) {
      if (count >= limit || isComplete()) {
        break;
      }
      addObject(object);
      count++;
    }
    return isComplete();
  }

  virtual void reset(std::optional<int> bundleSize = std::nullopt) {
    objects_.clear();
    if (bundleSize.has_value()) {
      bundleSize_ = bundleSize.value();
    }
  }

  ObjectBundle get() {
    if (isComplete()) {
      return objects_;
    }
    throw std::runtime_error("ObjectBundle is not complete");
  }

 protected:
  virtual void addObject(ObjectId object) {
    objects_.push_back(object);
  }

  bool isComplete() const {
    return static_cast<int>(objects_.size()) == bundleSize_;
  }

  int bundleSize_;
  ObjectBundle objects_;
};

// Build object bundles when the srcContainer is the special unassigned
// container In this case, always select the parts so that they are topology
// aware
class GpuTopologyAwareObjectSelector : public ObjectBundleSelector {
 public:
  GpuTopologyAwareObjectSelector(
      const entities::Map<ObjectId, std::vector<GroupId>>*
          objectToGpuGroupIdMapPtr,
      int bundleSize)
      : ObjectBundleSelector(bundleSize),
        objectToGpuGroupIdMapPtr_(objectToGpuGroupIdMapPtr) {}

  void addObject(ObjectId object) override {
    auto gpuGroupId = *objectToGpuGroupIdMapPtr_->at(object).begin();
    auto& selectedPartsForThisGroup = selectedObjectsPerGpuGroup_[gpuGroupId];
    selectedPartsForThisGroup.push_back(object);
    if (static_cast<int>(selectedPartsForThisGroup.size()) == bundleSize_) {
      // bundle for this GPU group is complete, select it
      objects_ = std::move(selectedPartsForThisGroup);
    }
  }

  void reset(std::optional<int> bundleSize = std::nullopt) override {
    selectedObjectsPerGpuGroup_.clear();
    ObjectBundleSelector::reset();
    if (bundleSize.has_value()) {
      throw std::runtime_error(
          "Bundle size cannot be changed for GpuTopologyBundleSelector");
    }
  }

 private:
  const entities::Map<ObjectId, std::vector<GroupId>>*
      objectToGpuGroupIdMapPtr_ = nullptr;
  // if objectToGpuGroup mapping is provided, the selected parts must be
  // GPU-topology aware. In this case, we will organize parts by their GPU
  // group and select the first GPU group as soon as the bundle is complete
  folly::F14FastMap<GroupId, ObjectBundle> selectedObjectsPerGpuGroup_;
};

// Build object bundles when the srcContainer is the special unassigned
// container. This version uses GpuTopologyOverlapTracker to ensure that
// selected objects don't overlap with already assigned GPUs.
// Simplified for adaptive allotments where bundle size is always 1.
class GpuTopologyAwareObjectSelectorV2 : public ObjectBundleSelector {
 public:
  explicit GpuTopologyAwareObjectSelectorV2(
      const OneObjectPerGroup& gpuTopologyOverlapTracker)
      : ObjectBundleSelector(/*defaultBundleSize = */ 1),
        gpuTopologyOverlapTracker_(gpuTopologyOverlapTracker) {}

  void addObject(ObjectId object) override {
    // Check if this object would overlap with assigned GPUs
    if (gpuTopologyOverlapTracker_.canPick(object)) {
      // Since bundle size is 1 for adaptive allotments, just add the object
      // directly
      objects_.push_back(object);
    }
  }

 private:
  const OneObjectPerGroup& gpuTopologyOverlapTracker_;
};

ContainerId getNonFixedContainer(
    ContainerId srcContainerId,
    ContainerId dstContainerId,
    ContainerId specialContainerId) {
  if (specialContainerId == srcContainerId) {
    return dstContainerId;
  } else if (specialContainerId == dstContainerId) {
    return srcContainerId;
  } else {
    throw std::runtime_error(
        "Either the source or destination container of FixedSrcDstMove must be fixed");
  }
}

bool isGpuContainer(
    const entities::Universe& universe,
    const interface::RasLocalSearchMetadata& rasMetadata,
    ContainerId container) {
  const auto& containerName = universe.getEntityName(container);
  return rasMetadata.gpuContainerNames()->contains(containerName);
}

std::shared_ptr<ObjectBundleSelector> buildObjectBundleSelector(
    int bundleSize,
    Problem& problem,
    const interface::RasLocalSearchMetadata& rasMetadata,
    ContainerId srcContainer,
    ContainerId dstContainer,
    const std::string& specialContainerName) {
  const auto& universe = problem.getUniverse();

  const auto& resContainer = getNonFixedContainer(
      srcContainer,
      dstContainer,
      universe.getContainerId(specialContainerName));
  if (isGpuContainer(universe, rasMetadata, resContainer)) {
    // For GPU containers, check if we should use V2 selector (with GPU topology
    // overlap tracking for adaptive allotments)
    if (*rasMetadata.useAdaptiveAllotments() &&
        !rasMetadata.physicalGpuOverlapPartitionName()->empty()) {
      // Fetch GpuTopologyOverlapTracker from EntityAttributeStore
      const auto& gpuTopologyOverlapTracker =
          problem.getEntityAttributeStore().getAttributes<OneObjectPerGroup>(
              kGpuTopologyOverlapTrackerAttrName.str());
      // Adaptive allotments are enabled, use V2 selector. Bundle size is
      // expected to be 1
      assert(bundleSize == 1);
      return std::make_shared<GpuTopologyAwareObjectSelectorV2>(
          gpuTopologyOverlapTracker);
    } else {
      // Fall back to the original GPU topology aware non adaptive selector
      auto partitionNamePtr = folly::get_ptr(
          *rasMetadata.gpuBundlePartitionNamePerBundleSize(), bundleSize);
      if (!partitionNamePtr) {
        // No GPU bundle partition available for this bundle size, use basic
        // selector
        return std::make_shared<ObjectBundleSelector>(bundleSize);
      }
      auto gpuPartitionId = universe.getPartitionId(*partitionNamePtr);
      auto objectToGpuGroupIdPtr =
          &universe.getPartition(gpuPartitionId).getObjectIdToGroupIds();
      return std::make_shared<GpuTopologyAwareObjectSelector>(
          objectToGpuGroupIdPtr, bundleSize);
    }
  } else {
    return std::make_shared<ObjectBundleSelector>(bundleSize);
  }
}

std::optional<folly::F14FastSet<GroupId>>
getValidSearchSpaceGroupsForThisDestination(
    const MovesEvaluator& evaluator,
    ContainerId dstContainer,
    PartitionId partitionId,
    const interface::RasLocalSearchMetadata& rasMetadata) {
  auto& p = evaluator.getProblem();
  auto& universe = p.getUniverse();
  auto groupsPtr = folly::get_ptr(
      *rasMetadata.searchSpaceGroupsPerContainer(),
      p.containerName(dstContainer));

  if (groupsPtr) {
    folly::F14FastSet<GroupId> validSearchSpaceGroups;
    for (auto& groupName : *groupsPtr) {
      const auto groupId = universe.getGroupId(partitionId, groupName);
      validSearchSpaceGroups.insert(groupId);
    }
    return validSearchSpaceGroups;
  }
  return std::nullopt;
}

folly::F14FastSet<GroupId> getForbiddenCandidateServers(
    const Problem& p,
    ContainerId containerId,
    PartitionId serverPartitionId,
    const interface::RasLocalSearchMetadata& rasMetadata) {
  const auto& srcContainerName = p.containerName(containerId);
  auto conflictContainersPtr = folly::get_ptr(
      *rasMetadata.conflictContainersPerContainer(), srcContainerName);

  folly::F14FastSet<GroupId> forbiddenCandidateServers;
  if (conflictContainersPtr) {
    for (auto& containerName : *conflictContainersPtr) {
      auto conflictContainerId = p.containerId(containerName);
      // all servers in this container are forbidden
      auto& forbiddenServerPartsPerServer =
          p.assignment.getObjectsIndexedByPartition(serverPartitionId)
              .getContainerObjects(conflictContainerId);
      for (auto& [server, _] : forbiddenServerPartsPerServer) {
        forbiddenCandidateServers.insert(server);
      }
    }
  }
  return forbiddenCandidateServers;
}

bool isMoveInvalid(
    EquivalenceSetId equivSetId,
    const Problem& problem,
    const folly::F14FastSet<GroupId>& validSearchSpaceGroups,
    const entities::Partition& searchSpacePartition) {
  // based on the set of allowed objects in the dst container, we can
  // statically figure out if a move from src-> dst is invalid. This reduces
  // the number of moves that need to be evaluated.
  auto anyObjectId = *problem.getEquivalenceSets().getSet(equivSetId).begin();
  auto& groupIds = searchSpacePartition.getObjectIdToGroupIds().at(anyObjectId);
  // move is bound to be unsuccessful if this equivSet is not present in the
  // set of allowed sets for this container.
  return !validSearchSpaceGroups.contains(groupIds.at(0));
}

} // namespace

template <typename MoveTypeSpecT>
MoveResult FixedSrcDstMultiMoveType<MoveTypeSpecT>::findBestMoveHelper(
    const MovesEvaluator& evaluator,
    ContainerId srcContainer,
    ContainerId dstContainer,
    MoveStatsAggregator& stats,
    const SearchHints& /*hints*/,
    double timeLimit) {
  // We will do this per container for every applied move, should be ok
  // TODO: re-evaluate if this becomes a perf bottleneck
  auto relevantObjectBundles =
      moveCandidateGenerator_
          .filterSourceObjectsByBundleSizeAndSearchSpacePartition(
              evaluator, srcContainer, dstContainer);

  using BundleIdx = int;
  // objects are server parts, and object group id is essentially server id
  std::vector<BundleIdx> relevantObjectBundleIndices;
  relevantObjectBundleIndices.reserve(relevantObjectBundles.size());
  for (const auto i : folly::irange(relevantObjectBundles.size())) {
    relevantObjectBundleIndices.push_back(i);
  }

  const std::function<MoveResult(BundleIdx)> evaluate =
      [&](BundleIdx bundleIdx) {
        MoveSet moves;
        auto& objectBundle = relevantObjectBundles.at(bundleIdx);
        for (auto objectId : objectBundle) {
          moves.insert(Move(objectId, srcContainer, dstContainer));
        }
        auto result = evaluator.evaluate(std::move(moves));
        stats.add(result);
        return result;
      };

  auto bestResult = MoveHelper::findBest(
      evaluator.getProblem().configs.threadPool.get(),
      relevantObjectBundleIndices,
      evaluate,
      timeLimit,
      parallelExecutionSelector(),
      name());
  // return after we have tried all objects that may improve the objective
  return bestResult;
}

template <typename MoveTypeSpecT>
int FixedSrcDstMoveCandidateGenerator<MoveTypeSpecT>::getMultiMoveBundleSize(
    ContainerId srcContainer,
    ContainerId dstContainer,
    const entities::Universe& universe) {
  assert(spec_.specialContainer());
  assert(spec_.rasLocalSearchMetadata());

  auto specialContainer = *spec_.specialContainer();
  auto& rasMetadata = *spec_.rasLocalSearchMetadata();

  auto containerId = getNonFixedContainer(
      srcContainer, dstContainer, universe.getContainerId(specialContainer));
  auto containerNamekey = universe.getEntityName(containerId);

  if (auto allowedSizesPtr = folly::get_ptr(
          *rasMetadata.multiMoveBundleSizePerContainer(), containerNamekey)) {
    // For now we only support one allowed bundle size (shape) per reservation
    if (allowedSizesPtr->size() != 1) {
      throw std::runtime_error(
          "MultiMoveBundleSizePerContainer should have only one allowed size");
    }
    return *allowedSizesPtr->begin();
  }
  auto defaultBundleSize = *rasMetadata.defaultMultiMoveBundleSize();
  XLOG(DBG3) << fmt::format(
      "Bundle size not found for container {}, using default {}",
      containerNamekey,
      defaultBundleSize);
  return defaultBundleSize;
}

template <typename MoveTypeSpecT>
std::vector<ObjectBundle> FixedSrcDstMoveCandidateGenerator<MoveTypeSpecT>::
    filterSourceObjectsByBundleSizeAndSearchSpacePartition(
        const MovesEvaluator& evaluator,
        ContainerId srcContainer,
        ContainerId dstContainer,
        std::optional<MultiObjectSelectionConfig> multiObjSelectionConfig) {
  if (!spec_.rasLocalSearchMetadata()) {
    throw std::runtime_error(
        "FixedSourceMultiMoveType needs RASLocalSearchMetadata");
  }
  auto& rasMetadata = *spec_.rasLocalSearchMetadata();
  auto& problem = evaluator.getProblem();
  const auto& universe = problem.getUniverse();
  const int maxSampleSizePerEquivSet = *spec_.maxSamplesPerEquivSet();
  const auto* const invalidMoveFilter = problem.getInvalidMoveFilter();

  // 1. Get configured RAS bundle size for the reservation container (non
  // special)
  auto bundleSizeForThisContainer =
      getMultiMoveBundleSize(srcContainer, dstContainer, universe);
  assert(spec_.specialContainer());

  // 2. This was added for legacy (memory-only) representation of stacking where
  // each server part represented 1GB of memory. This model is still used for
  // GPU toplogy (ai-group solves). For compute group, we have moved to adaptive
  // allotments where the bundle-size is always one.
  // TODO: get rid of this ObjectBundleSelector once we move everything to
  // adaptive-allotments.
  const bool formBundlesAtEquivSetGranularity =
      *rasMetadata.useAdaptiveAllotments() && multiObjSelectionConfig;

  auto objectBundleSelector = buildObjectBundleSelector(
      bundleSizeForThisContainer,
      problem,
      rasMetadata,
      srcContainer,
      dstContainer,
      *spec_.specialContainer());
  const auto specialContainerId =
      universe.getContainerId(*spec_.specialContainer());

  // 3. Search space partition limits the objects that need to be considered for
  // moves
  assert(rasMetadata.searchSpacePartition().has_value());
  const auto searchSpacePartitionId =
      universe.getPartitionId(*rasMetadata.searchSpacePartition());
  const auto& searchSpacePartition =
      universe.getPartition(searchSpacePartitionId);

  // 4. Server partition limits bundles of server parts from only a single
  // server
  assert(rasMetadata.serverPartition().has_value());
  const auto serverPartitionId =
      universe.getPartitionId(*rasMetadata.serverPartition());

  // 5. Magically fetch PackerMap<EquivalenceSetId, PackerMap<GroupId,
  // PackerSet<ObjectId>>> Each eqv sets in src container grouped by server
  const auto& currentSrcContainerEquivSets =
      problem.assignment
          .getObjectsIndexedByEquivSetsAndPartition(serverPartitionId)
          .getContainerObjects(srcContainer);

  // 6. So we are going to move a bundle of objects to a dstContainer. But
  // dstContainer might limit only some group of objects from a
  // searchSpacePartition. Again helps limit the object bundle we need to pick
  const auto validSearchSpaceGroups =
      getValidSearchSpaceGroupsForThisDestination(
          evaluator, dstContainer, searchSpacePartitionId, rasMetadata);

  // 7. Just like above but block-list, the destination could prevent server
  // parts from certain server to be forbidden in dstContainer
  const auto forbiddenCandidateServers = getForbiddenCandidateServers(
      problem, dstContainer, serverPartitionId, rasMetadata);

  // 8. For adaptive allotments, get the destination container's capacity shape
  // to filter servers that don't have enough resources for this reservation
  // type
  std::optional<entities::ScopeItemId> dstContainerShape = std::nullopt;
  if (*rasMetadata.useAdaptiveAllotments()) {
    assert(rasMetadata.reservationCapacityShapeScopeName().has_value());
    const auto capacityShapeScopeId = universe.getScopeId(
        rasMetadata.reservationCapacityShapeScopeName().value());
    dstContainerShape =
        universe.getScope(capacityShapeScopeId).getScopeItemId(dstContainer);
  }

  // 9. Main loop: iterate through each equivalence set in the source
  // container and generate move candidates while applying all the filters
  // above
  std::vector<ObjectBundle> moveCandidates;
  int invalidEquivSets = 0;
  int numForbiddenServers = 0;
  const auto* const serverToAllowedCount =
      (dstContainerShape && srcContainer == specialContainerId)
      ? &problem.getEntityAttributeStore()
             .getAttributes<AllowedScopeItemsPerGroup>(
                 kAllowedScopeItemsPerGroupAttrName.str())
             .getGroupsWithNonZeroCount(*dstContainerShape)
      : nullptr;
  for (auto& [equivSetId, partsPerServer] : currentSrcContainerEquivSets) {
    if (formBundlesAtEquivSetGranularity) {
      assert(multiObjSelectionConfig.has_value());
      const auto serverId = partsPerServer.begin()->first;
      auto multiObjBundleSize =
          multiObjSelectionConfig->getBundleSizeForGroup(serverId);
      objectBundleSelector->reset(multiObjBundleSize);
    }
    if (validSearchSpaceGroups &&
        isMoveInvalid(
            equivSetId,
            problem,
            *validSearchSpaceGroups,
            searchSpacePartition)) {
      invalidEquivSets++;
      continue;
    }
    int numCandidatesSelected = 0;
    for (auto& [serverId, thisServerParts] : partsPerServer) {
      // 1. Skip forbidden servers
      if (forbiddenCandidateServers.contains(serverId)) {
        numForbiddenServers++;
        continue;
      }

      // 2. Check resource limits and calculate maxCount if needed
      std::optional<size_t> maxCount = std::nullopt;

      if (serverToAllowedCount) {
        const auto* const allowedCount =
            folly::get_ptr(*serverToAllowedCount, serverId);
        if (!allowedCount) {
          // Not enough resources for this reservation type
          continue;
        }

        // If we're forming bundles at equiv-set granularity, constrain maxCount
        if (formBundlesAtEquivSetGranularity) {
          maxCount = std::min(
              *allowedCount, static_cast<size_t>(bundleSizeForThisContainer));
        }
      } else if (formBundlesAtEquivSetGranularity) {
        // No resource checking, but still need maxCount for bundle formation
        maxCount = static_cast<size_t>(bundleSizeForThisContainer);
      }

      // 3. Build bundle
      if (objectBundleSelector->addObjects(thisServerParts, maxCount)) {
        auto bundle = objectBundleSelector->get();
        objectBundleSelector->reset();
        // A bundle moves as a unit, so if any of its objects can never be valid
        // at the destination the whole move is dead on arrival. Drop it during
        // enumeration, before it is ever enqueued for evaluation.
        if (!anyMoveInvalid(invalidMoveFilter, bundle, dstContainer)) {
          moveCandidates.emplace_back(std::move(bundle));
          ++numCandidatesSelected;
          if (numCandidatesSelected >= maxSampleSizePerEquivSet) {
            break;
          }
        }
      }
      if (!formBundlesAtEquivSetGranularity) {
        // Bundle not built but still need to reset for next server
        objectBundleSelector->reset();
      }
    }
  }

  // 11. Log debugging information about filtering results and return final
  // move candidates for evaluation
  XLOG(DBG2) << fmt::format(
      "{} / {} equiv sets , {} servers were deemed invalid for dst container {}",
      invalidEquivSets,
      currentSrcContainerEquivSets.size(),
      numForbiddenServers,
      universe.getEntityName(dstContainer));
  XLOG(DBG2) << getDebugInfo(
      srcContainer, dstContainer, moveCandidates, universe);
  return moveCandidates;
}

template <typename MoveTypeSpecT>
std::string FixedSrcDstMoveCandidateGenerator<MoveTypeSpecT>::getDebugInfo(
    ContainerId srcContainer,
    ContainerId dstContainer,
    const std::vector<ObjectBundle>& moveCandidates,
    const entities::Universe& universe) {
  constexpr int MAX_DESC_SIZE = 2;
  constexpr int PARTS_DESC_SIZE = 2;
  std::vector<std::string> desc;
  for (auto& bundle : moveCandidates) {
    if (bundle.empty()) {
      continue;
    }
    std::vector<std::string> partsDesc;
    for (int i = 0; i < PARTS_DESC_SIZE && i < static_cast<int>(bundle.size());
         ++i) {
      partsDesc.push_back(universe.getEntityName(bundle.at(i)));
    }

    desc.push_back(
        fmt::format(
            "{} parts like {}", bundle.size(), folly::join(", ", partsDesc)));
    if (desc.size() >= MAX_DESC_SIZE) {
      int remainingCandidates = moveCandidates.size() - desc.size();
      if (remainingCandidates > 0) {
        desc.push_back(fmt::format("... and {} more", remainingCandidates));
      }
      break;
    }
  }
  return fmt::format(
      "Move from {} to {} has {} candidates:  {}",
      universe.getEntityName(srcContainer),
      universe.getEntityName(dstContainer),
      moveCandidates.size(),
      folly::join("; ", desc));
}
// instantiate the template specializations

template class FixedSrcDstMultiMoveType<interface::FixedSrcMultiMoveTypeSpec>;
template class FixedSrcDstMultiMoveType<interface::FixedDestMultiMoveTypeSpec>;
template class FixedSrcDstMultiMoveType<
    interface::FixedDestSwapMultiMoveTypeSpec>;
} // namespace facebook::rebalancer
