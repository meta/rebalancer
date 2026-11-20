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

#include "algopt/rebalancer/solver/utils/FinalEvaluationSummaryHelper.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <folly/container/MapUtil.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

interface::FinalEvaluationSummary FinalEvaluationSummaryHelper::makeSummary(
    const Problem& problem,
    const MoveStatsAggregator& stats) {
  interface::FinalEvaluationSummary result;
  *result.globalStats() = makeStats(stats.getGlobalStats());
  for (auto container : problem.containers) {
    const auto& source = stats.getStatsForSourceContainer(container);
    if (source.getTotalMoves() != 0) {
      result.sourceContainerStats()[problem.containerName(container)] =
          makeStats(source);
    }
    const auto& destination = stats.getStatsForDestinationContainer(container);
    if (destination.getTotalMoves() != 0) {
      result.destinationContainerStats()[problem.containerName(container)] =
          makeStats(destination);
    }
  }
  result.globalStats()->nonAcceptingContainersCount() =
      problem.not_accepting_containers.size();
  result.globalStats()->fixedContainersCount() =
      problem.getFixedContainers().size();
  result.globalStats()->fixedObjectsCount() = problem.fixed_objects.size();

  // if objects are not being tracked, return early
  if (!*problem.configs.moveStatsSpec.trackObjects()) {
    return result;
  }

  // Maps a pair of {final container, equivalence class} to a move stats object.
  // All objects in the same final container and equivalence class have the
  // exact same move stats. Because of an optimization, local search will only
  // populate move stats for a single arbitrary object of the equivalence class.
  PackerMap<
      std::pair<entities::ContainerId, entities::EquivalenceSetId>,
      MoveStats>
      containerEquivalenceStats;
  for (auto objectId : problem.assignment.getObjects()) {
    auto containerId = problem.assignment.getContainer(objectId);
    auto equivalenceId = problem.getEquivalenceSets().at(objectId);
    const auto& objectStats = stats.getStatsForObject(objectId);
    if (objectStats.getTotalMoves() != 0) {
      containerEquivalenceStats.emplace(
          std::make_pair(containerId, equivalenceId), objectStats);
    }
  }

  auto addObjectStatsToResult = [&](entities::ObjectId objectId) {
    if (!problem.getEquivalenceSets().hasObject(objectId)) {
      return;
    }

    auto equivalenceId = problem.getEquivalenceSets().at(objectId);
    auto containerId = problem.assignment.getContainer(objectId);
    auto objectStats = folly::get_ptr(
        containerEquivalenceStats, std::make_pair(containerId, equivalenceId));
    if (objectStats) {
      result.objectStats()->emplace(
          problem.getUniverse().getEntityName(objectId),
          makeStats(*objectStats));
    }
  };

  // Populate move stats for the requested objects, if stats have been collected
  // for the corresponding pairs of {final container, equivalence class}.
  if (problem.configs.moveStatsSpec.trackObjectsWhitelist()) {
    auto& trackObjectsWhitelist =
        *problem.configs.moveStatsSpec.trackObjectsWhitelist();
    for (auto& objectName : trackObjectsWhitelist) {
      auto objectId = problem.objectId(objectName);
      addObjectStatsToResult(objectId);
    }
  } else { // all objects are being tracked
    for (auto objectId : problem.assignment.getObjects()) {
      addObjectStatsToResult(objectId);
    }
  }

  return result;
}

interface::FinalEvaluationStats FinalEvaluationSummaryHelper::makeStats(
    const MoveStats& stats) {
  interface::FinalEvaluationStats result;
  *result.totalCount() = stats.getTotalMoves();
  for (auto& [constraint, count] : stats.getInvalidMoveReasons()) {
    result.brokenConstraints()[constraint->name] += count;
  }
  for (auto& [objective, count] : stats.getWorseMoveReasons()) {
    result.worstObjectives()[objective->name] += count;
  }
  return result;
}

} // namespace facebook::rebalancer
