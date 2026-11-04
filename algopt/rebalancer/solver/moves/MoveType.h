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

#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/moves/DestinationsToExploreGenerator.h"
#include "algopt/rebalancer/solver/moves/MoveResult.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/moves/ObjectsToExploreGenerator.h"
#include "algopt/rebalancer/solver/utils/ParallelExecutionSelector.h"
#include "algopt/rebalancer/solver/utils/SearchHints.h"

namespace facebook::rebalancer {

class MovesEvaluator;
struct MovesSummary;

// Lightweight config extracted from LocalSearchSolverSpec.
// Contains only the fields actually used by MoveType base class.
struct MoveTypeBaseConfig {
  int32_t minHotObjects = 0;
  int32_t stratifiedSampleSize = 0;
  bool includeEqualSizeRandomSampleForSingleColdestMoveType = false;

  static MoveTypeBaseConfig fromSpec(
      const interface::LocalSearchSolverSpec& spec) {
    return MoveTypeBaseConfig{
        .minHotObjects = *spec.minHotObjects(),
        .stratifiedSampleSize = *spec.stratifiedSampleSize(),
        .includeEqualSizeRandomSampleForSingleColdestMoveType =
            *spec.includeEqualSizeRandomSampleForSingleColdestMoveType(),
    };
  }
};

class MoveType {
 protected:
  const MoveTypeBaseConfig configs_;

 public:
  explicit MoveType(const interface::LocalSearchSolverSpec& configs)
      : configs_(MoveTypeBaseConfig::fromSpec(configs)),
        parallelExecutionSelector_(
            configs.parallelExecutionConfig().to_optional()) {}
  virtual ~MoveType();

  virtual std::string name() const = 0;

  virtual MoveResult findBestMove(
      const MovesEvaluator& evaluator,
      entities::ContainerId hotContainer,
      MoveStatsAggregator& stats,
      const SearchHints& hints,
      double timeLimit) = 0;

  const ParallelExecutionSelector& parallelExecutionSelector() const {
    return parallelExecutionSelector_;
  }

 protected:
  // Accessor methods for subclasses
  int32_t minHotObjects() const {
    return configs_.minHotObjects;
  }
  int32_t stratifiedSampleSize() const {
    return configs_.stratifiedSampleSize;
  }
  bool includeEqualSizeRandomSample() const {
    return configs_.includeEqualSizeRandomSampleForSingleColdestMoveType;
  }
  ParallelExecutionSelector& parallelExecutionSelector() {
    return parallelExecutionSelector_;
  }

  ReferenceList<const std::vector<entities::ContainerId>>
  getDestinationsToExplore(
      const interface::DestinationsToExploreOptions& destinationsToExplore,
      entities::ContainerId hotContainerId,
      entities::ObjectId hotObjectId,
      Problem& problem);

  static ReferenceList<const std::vector<entities::ObjectId>>
  getObjectsToExplore(
      const interface::ObjectsToExploreOptions& objectsToExplore,
      entities::ContainerId hotContainerId,
      Problem& problem,
      MoveStatsAggregator& stats);

  static int getBundleSize(
      const interface::ObjectsToExploreOptions& exploreOptions);

  static std::vector<ObjectBundle> getObjectBundlesToExplore(
      const interface::ObjectsToExploreOptions& objectsToExploreOptions,
      entities::ContainerId srcContainer,
      const folly::F14FastMap<entities::GroupId, int>& groupBundleSizeOverrides,
      Problem& problem,
      bool createGreedyHeterogenousBundles);

  static int getSampleSize(
      const interface::SampleSize& sampleSize,
      const std::string& objectName);

 protected:
  mutable std::mt19937 rng_;

 private:
  ParallelExecutionSelector parallelExecutionSelector_;

  static std::string getMoveDescription(
      const interface::MovesSummary& moveSummary);
};
} // namespace facebook::rebalancer
