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

#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSetsStore.h"

#include "algopt/rebalancer/algopt_common/FilterAndTransformUtils.h"
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/utils/AffectedByChangeInfo.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/treeprof/Event.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashMap.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/hash/Hash.h>
#include <folly/lang/Hint.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <folly/String.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <assert.h>
#include <functional>
#include <ostream>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace facebook::rebalancer {

namespace {

using algopt::common::FilterAndTransformUtils::selectAllowedItems;
using algopt::common::thrift::FilterType;
using folly::hash::commutative_hash_combine_range;
using folly::hash::hash_combine;
using folly::hash::hash_range;

[[nodiscard]] std::string getCacheKey(
    const interface::CustomEquivalenceSetConfig& config,
    const std::vector<entities::PartitionId>& partitionIds) {
  // check if we need to build custom equivalence sets
  // default setup is to just use all goals and constraints
  auto isUnrestricted = [](const auto& goalOrConstraintConfig) {
    // if empty blocklist => all goals/constraints are allowed
    return goalOrConstraintConfig.filterType() == FilterType::BLOCKLIST &&
        goalOrConstraintConfig.stringsToFilter()->empty();
  };
  if (isUnrestricted(*config.constraintSelectionConfig()) &&
      isUnrestricted(*config.goalSelectionConfig()) && partitionIds.empty()) {
    // an equivalence set with all goals and constraints
    return std::string(kDefaultKey);
  }
  // if yes, build a unique key for this config
  const auto& constraintNames =
      *config.constraintSelectionConfig()->stringsToFilter();
  const auto& goalNames = *config.goalSelectionConfig()->stringsToFilter();
  const auto& partitionNames = *config.partitionNames();
  auto constraintsHash =
      hash_range(constraintNames.begin(), constraintNames.end());
  auto goalsHash = hash_range(goalNames.begin(), goalNames.end());
  auto partitionHash = hash_range(partitionNames.begin(), partitionNames.end());
  const auto hash = hash_combine(constraintsHash, goalsHash, partitionHash);

  return fmt::format(
      "{}_{}_{}",
      hash,
      fmt::underlying(*config.constraintSelectionConfig()->filterType()),
      fmt::underlying(*config.goalSelectionConfig()->filterType()));
}

void updateEquivalentSetsBySearchSpacePartition(
    EquivalenceSets& equivalenceSets,
    const Problem& problem,
    entities::PartitionId partitionId) {
  const auto& universe = problem.getUniverse();
  PackerMap<entities::ObjectId, entities::GroupId> objectIdToGroupId;
  auto& searchSpacePartition = universe.getPartition(partitionId);
  for (auto& [objectId, groupId] :
       searchSpacePartition.getObjectIdToGroupIds()) {
    if (groupId.size() > 1) {
      throw std::runtime_error(
          fmt::format("Every object must belong to a single group"));
    }
    auto onlyGroupId = *groupId.begin();
    objectIdToGroupId.emplace(objectId, onlyGroupId);
  }
  // All objects that belong to the same group are equivalent
  equivalenceSets.mappingMerge(objectIdToGroupId);
}

} // namespace

EquivalenceSetsStore::EquivalenceSetsStore(Problem& problem)
    : problem_(problem) {
  allConstraintIds_ = problem_.getMaterializedProblem()->userConstraints |
      std::ranges::views::keys | algopt::utils::to<std::vector>;
  allGoalIds_ = problem_.getMaterializedProblem()->userGoals |
      std::ranges::views::keys | algopt::utils::to<std::vector>;
}

void EquivalenceSetsStore::initialize(
    const interface::CustomEquivalenceSetConfig& config) {
  const auto& universe = problem_.getUniverse();
  const auto partitionIds = *config.partitionNames() |
      std::ranges::views::transform([&](auto& partitionName) {
        return universe.getPartitionId(partitionName);
      }) |
      algopt::utils::to<std::vector>;
  // translate the different ways of specifying constraints and goals to an
  // allowed list of goals and constraint
  const auto allowedConstraintIds = selectAllowedItems<entities::ConstraintId>(
      allConstraintIds_,
      *config.constraintSelectionConfig(),
      [&universe](const auto& constraintName) {
        return universe.getConstraintId(constraintName);
      });
  const auto allowedGoalIds = selectAllowedItems<entities::GoalId>(
      allGoalIds_,
      *config.goalSelectionConfig(),
      [&universe](const auto& goalName) {
        return universe.getGoalId(goalName);
      });

  const auto cacheKey = getCacheKey(config, partitionIds);
  // build equivalence sets using the allowed list of constraints and goals
  buildAndStoreInclusive(
      allowedConstraintIds, allowedGoalIds, partitionIds, cacheKey);
}

void EquivalenceSetsStore::initialize(
    const std::string& searchSpacePartitionName,
    const std::vector<std::string>& constraintNames,
    const std::vector<std::string>& goalNames) {
  interface::CustomEquivalenceSetConfig config;
  config.partitionNames() = {searchSpacePartitionName};
  // setup constraint allowlist
  config.constraintSelectionConfig()->stringsToFilter() = constraintNames;
  config.constraintSelectionConfig()->filterType() = FilterType::ALLOWLIST;
  // setup goal allowlist
  config.goalSelectionConfig()->stringsToFilter() = goalNames;
  config.goalSelectionConfig()->filterType() = FilterType::ALLOWLIST;
  initialize(config);
}

void EquivalenceSetsStore::initialize(
    const folly::F14FastSet<std::string>& excludeConstraintNames,
    const folly::F14FastSet<std::string>& excludeGoalNames) {
  interface::CustomEquivalenceSetConfig config;
  config.partitionNames() = {};
  // setup constraint blocklist
  config.constraintSelectionConfig()->stringsToFilter() =
      std::vector<std::string>(
          excludeConstraintNames.begin(), excludeConstraintNames.end());
  config.constraintSelectionConfig()->filterType() = FilterType::BLOCKLIST;
  // setup goal blocklist
  config.goalSelectionConfig()->stringsToFilter() = std::vector<std::string>(
      excludeGoalNames.begin(), excludeGoalNames.end());
  config.goalSelectionConfig()->filterType() = FilterType::BLOCKLIST;
  initialize(config);
}

void EquivalenceSetsStore::buildAndStoreInclusive(
    const std::vector<entities::ConstraintId>& constraintIds,
    const std::vector<entities::GoalId>& goalIds,
    const std::vector<entities::PartitionId>& partitionIds,
    const std::string& cacheKey) {
  if (equivalenceSetsCache_.contains(cacheKey)) {
    updateMostRecentEquivSetsPtr(cacheKey);
  } else if (cacheKey == kDefaultKey) {
    buildAndStoreDefault();
  } else {
    const auto& universe = problem_.getUniverse();
    EquivalenceSets equivalenceSets(universe, cacheKey);
    // the descriptors of how sets were created helps with debugging
    std::vector<std::string> equivSetDescriptors;
    equivSetDescriptors.reserve(partitionIds.size() + 1);
    for (auto partitionId : partitionIds) {
      auto before = equivalenceSets.size();
      updateEquivalentSetsBySearchSpacePartition(
          equivalenceSets, problem_, partitionId);
      equivSetDescriptors.push_back(
          fmt::format(
              "{} sets created by user provided partition: {}",
              equivalenceSets.size() - before,
              universe.getEntityName(partitionId)));
    }

    const auto currentCount = equivalenceSets.size();
    updateEquivalenceSets(equivalenceSets, constraintIds, goalIds);
    equivSetDescriptors.push_back(
        fmt::format(
            "{} created due to selected goals and constraints",
            equivalenceSets.size() - currentCount));

    XLOG(INFO) << fmt::format(
        "{} equivalence sets initialized with {}",
        equivalenceSets.size(),
        folly::join(",", equivSetDescriptors));

    saveToEquivalenceSetCache(std::move(equivalenceSets));
  }
}

void EquivalenceSetsStore::override(EquivalenceSets equivalenceSets) {
  equivalenceSetsCache_.erase(equivalenceSets.getKey());
  saveToEquivalenceSetCache(std::move(equivalenceSets));
}

EquivalenceSets EquivalenceSetsStore::buildDefaultEquivalenceSets() {
  // when building default equivalence sets, we use the existing orchestrator
  // because it avoids the need for initializing it again
  algopt::treeprof::EventRecorder event("Build default equivalence sets");
  EquivalenceSets equivalenceSets(
      problem_.getUniverse(), std::string(kDefaultKey));
  problem_.getOrchestrator().updateEquivalenceSets(
      equivalenceSets, problem_.getUniverse().getNumObjects());
  event.stop();
  const auto duration = event.durationInSeconds();
  XLOG(INFO) << fmt::format("Equivalence sets built in {:.2f} s", duration);
  totalEquivSetsBuildTime_ += duration;
  return equivalenceSets;
}

void EquivalenceSetsStore::clear() {
  equivalenceSetsCache_.clear();
  mostRecentEquivSetsPtr_ = nullptr;
}

const EquivalenceSets& EquivalenceSetsStore::get() const {
  if (!mostRecentEquivSetsPtr_) {
    // if an equivalence set is not explicitly initialized, build and return the
    // default one, remove constness to call buildAndStoreDefault
    auto writableStore = const_cast<EquivalenceSetsStore*>(this);
    writableStore->buildAndStoreDefault();
  }
  assert(mostRecentEquivSetsPtr_);
  return *mostRecentEquivSetsPtr_;
}

void EquivalenceSetsStore::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets,
    const std::vector<entities::ConstraintId>& constraintIds,
    const std::vector<entities::GoalId>& goalIds) {
  const algopt::treeprof::Event event("Create custom equivalence sets");
  std::vector<Expression*> roots;
  for (const auto& constraintId : constraintIds) {
    const auto ctrExpr =
        problem_.getMaterializedProblem()->userConstraints.at(constraintId);
    roots.push_back(ctrExpr.get());
  }
  for (auto& goalId : goalIds) {
    const auto goalExpr =
        problem_.getMaterializedProblem()->userGoals.at(goalId);
    roots.push_back(goalExpr.get());
  }
  // We will use orchestrator with the set of roots corresponding to provided
  // constraint and goal ids to further update the equivalence set partitioning.
  buildCustomEquivalenceSets(roots, problem_, equivalenceSets);
}

void EquivalenceSetsStore::buildCustomEquivalenceSets(
    const std::vector<Expression*>& roots,
    const Problem& problem,
    EquivalenceSets& equivalenceSets) {
  algopt::treeprof::EventRecorder equivSetBuildEvent(
      "Build equivalence sets using orchestrator");
  Orchestrator equivSetOrchestrator;
  equivSetOrchestrator.init(
      roots, AffectedByChangeDecisionData(0, 0) /*unused*/);
  equivSetOrchestrator.updateEquivalenceSets(
      equivalenceSets, problem.getUniverse().getNumObjects());
  equivSetBuildEvent.stop();
  auto duration = equivSetBuildEvent.durationInSeconds();
  XLOG(INFO) << fmt::format("Equivalence sets built in {:.2f} s", duration);
  totalEquivSetsBuildTime_ += duration;
}

double EquivalenceSetsStore::getTotalBuildTime() const {
  return totalEquivSetsBuildTime_;
}

void EquivalenceSetsStore::saveToEquivalenceSetCache(
    EquivalenceSets equivalenceSets) {
  // copied because the sets are moved into the cache
  const auto key = equivalenceSets.getKey();
  equivalenceSetsCache_.getSavedOrCompute(key, [&]() {
    return std::make_unique<EquivalenceSets>(std::move(equivalenceSets));
  });
  updateMostRecentEquivSetsPtr(key);
}

void EquivalenceSetsStore::updateMostRecentEquivSetsPtr(
    const std::string& key) {
  mostRecentEquivSetsPtr_ = equivalenceSetsCache_.at(key).get();
}

void EquivalenceSetsStore::buildAndStoreDefault() {
  saveToEquivalenceSetCache(buildDefaultEquivalenceSets());
}

} // namespace facebook::rebalancer
