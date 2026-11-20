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

#include "algopt/rebalancer/solver/utils/SimilarContainers.h"

#include "algopt/rebalancer/algopt_common/ValueSortedMap.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/utils/ContainerPotential.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>

#include <algorithm>
#include <numeric>
#include <ranges>
#include <stddef.h>
#include <stdexcept>
#include <vector>

namespace facebook::rebalancer {

SimilarContainers::SimilarContainers(
    const std::vector<std::vector<entities::ContainerId>>& similarContainers) {
  for (auto& classContainers : similarContainers) {
    for (auto containerId : classContainers) {
      container_to_group[containerId] = groups.size();
    }
    groups.push_back(classContainers);
  }
}

PackerSet<entities::ContainerId> SimilarContainers::stratifiedSample(
    int size,
    const std::function<bool(entities::ContainerId)>&
        accepting_containers_check) {
  size_t count_empty_groups = 0;
  std::vector<int> indexes(groups.size());
  std::iota(indexes.begin(), indexes.end(), 0);
  std::shuffle(indexes.begin(), indexes.end(), rng);
  // First just random shuffle all elements in each group.
  for (const auto i : folly::irange(groups.size())) {
    std::shuffle(groups[i].begin(), groups[i].end(), rng);
    if (groups[i].empty()) {
      ++count_empty_groups;
    }
  }
  // Since we take one by one from every group we can use the following
  // algorithm:
  // We store pointers for every group from its beginning. We circle over the
  // groups and move the pointer forward until we have accepting container. If
  // we found it, we take it. Then we go for the next group. The process is
  // finished either when we collect `size` containers, or when all of the
  // groups are exhausted.
  PackerSet<entities::ContainerId> sample;
  sample.reserve(size);
  std::vector<size_t> current_index_in_group(groups.size());
  int taken = 0;
  size_t groupIndex = 0;
  while (taken < size && count_empty_groups < groups.size()) {
    const auto i = indexes.at(groupIndex);
    while (current_index_in_group.at(i) < groups[i].size()) {
      if (accepting_containers_check(groups[i][current_index_in_group.at(i)])) {
        sample.insert(groups[i][current_index_in_group.at(i)]);
        ++current_index_in_group.at(i);
        if (current_index_in_group.at(i) == groups[i].size()) {
          // Current groups is done.
          ++count_empty_groups;
        }
        ++taken;
        break;
      }
      ++current_index_in_group.at(i);
      if (current_index_in_group.at(i) == groups[i].size()) {
        // Current groups is done.
        ++count_empty_groups;
      }
    }
    ++groupIndex;
    if (groupIndex == groups.size()) {
      groupIndex = 0;
    }
  }
  return sample;
}

PackerSet<entities::ContainerId> SimilarContainers::stratifiedOrderedSample(
    int size,
    const std::function<bool(entities::ContainerId)>& acceptingContainers,
    bool addEqualSizeRandomSample) {
  if (!orderedGroups.has_value()) {
    throw std::runtime_error("orderedGroups have not been initialized");
  }

  PackerSet<entities::ContainerId> result;
  size_t maxSampleSize = size;
  if (addEqualSizeRandomSample) {
    maxSampleSize *= 2;
    result.reserve(maxSampleSize);
    result = stratifiedSample(size, acceptingContainers);
  } else {
    result.reserve(maxSampleSize);
  }

  auto targetCounts = groupCounts(size);
  for (const auto groupIndex : folly::irange(orderedGroups->size())) {
    const auto& orderedGroup = orderedGroups->at(groupIndex);
    if (orderedGroup.size() != groups.at(groupIndex).size()) {
      throw std::runtime_error(
          fmt::format(
              "orderedGroup {} has size = {}, but it is expected to have size = {}. You need to initialize the containerPotential of every container that is part of some group in SimilarContainers",
              groupIndex,
              orderedGroup.size(),
              groups.at(groupIndex).size()));
    }

    int nPickedFromGroup = 0;
    auto it = orderedGroup.begin();
    while (nPickedFromGroup < targetCounts.at(groupIndex) &&
           it != orderedGroup.end()) {
      auto coldestContainerInGroup = it->first;
      if (acceptingContainers(coldestContainerInGroup) &&
          result.insert(coldestContainerInGroup).second) {
        ++nPickedFromGroup;
      }

      ++it;
      if (result.size() >= maxSampleSize) {
        return result;
      }
    }
  }

  return result;
}

void SimilarContainers::setOrderedGroups(
    const ContainerPotentials& containerPotentials) {
  // If orderedGroups has never been set before, then create k orderedGroups,
  // where k = groups.size()
  if (!orderedGroups.has_value()) {
    orderedGroups = std::vector<OrderedGroup>();
    for ([[maybe_unused]] const auto _ : folly::irange(groups.size())) {
      const OrderedGroup emptyOrderedGroupMap;
      orderedGroups->push_back(emptyOrderedGroupMap);
    }
  }

  for (const auto& [containerId, containerPotential] : containerPotentials) {
    const int groupIndex =
        folly::get_default(container_to_group, containerId, -1);
    if (groupIndex == -1) {
      // if the container is not part of any group, then ignore
      continue;
    }

    auto& orderedGroup = orderedGroups->at(groupIndex);
    orderedGroup.assign(containerId, containerPotential);
  }
}

void SimilarContainers::updateOrderedGroups(
    const ContainerPotentials& changesInContainerPotentials) {
  for (const auto& [containerId, changeInContainerPotential] :
       changesInContainerPotentials) {
    const int groupIndex =
        folly::get_default(container_to_group, containerId, -1);
    if (groupIndex == -1) {
      // if the container is not part of any group, then ignore
      continue;
    }

    auto& orderedGroup = orderedGroups->at(groupIndex);
    if (!orderedGroup.contains(containerId)) {
      throw std::runtime_error(
          fmt::format(
              "Container with id = {} does not belong to the right orderedGroup. Has orderedGroups been properly initialized?",
              containerId));
    }

    auto& currentContainerPotential = orderedGroup.at(containerId);
    auto newContainerPotential =
        currentContainerPotential + changeInContainerPotential;

    // update orderedGroup with the new ContainerPotential value
    orderedGroup.assign(containerId, newContainerPotential);
  }
}

std::vector<int> SimilarContainers::groupCounts(int size) const {
  int total_size = 0;
  for (auto& group : groups) {
    total_size += group.size();
  }
  size = std::min(size, total_size);
  std::vector<int> counts(groups.size());
  size_t group_id = 0;
  for (int count = 0; count < size; ++group_id) {
    if (group_id == groups.size()) {
      group_id = 0;
    }
    if (counts.at(group_id) < static_cast<int>(groups[group_id].size())) {
      ++counts.at(group_id);
      ++count;
    }
  }
  return counts;
}

} // namespace facebook::rebalancer
