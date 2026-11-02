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

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectPartitionRoutingDimension.h"
#include "algopt/rebalancer/entities/ObjectScalarDimension.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/materializer/utils/Cache.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingLatencyLookup.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingRing.h"
#include "algopt/rebalancer/solver/expressions/GroupRoutingTrafficLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionMoveLimit.h"
#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/expressions/StableStayed.h"
#include "algopt/rebalancer/solver/summary/metrics/Metrics.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/treeprof/ExecutorWrapper.h"

#include <folly/coro/Collect.h>
#include <folly/hash/Hash.h>
#include <folly/synchronization/ThreadAnnotations.h>

#include <limits>

namespace facebook::rebalancer::materializer {

constexpr double DEFAULT_APPROX_LOWER_BOUND = 0;
constexpr int DEFAULT_APPROX_PIECE_COUNT = 100;

// verfifies that the template parameter is either an int or a ScalarDimension
template <typename DimensionOrIndex>
concept IsDimensionOrIndexType = std::is_same_v<DimensionOrIndex, int> ||
    std::is_same_v<DimensionOrIndex, entities::ObjectScalarDimension>;

// Keyed by filteredGroupIds's contents, so equal filters reuse one cached
// PartitionInfo instead of rebuilding its object->group map.
struct PartitionInfoCacheKey {
  entities::PartitionId partitionId;
  std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds;

  bool operator==(const PartitionInfoCacheKey& other) const {
    if (partitionId != other.partitionId) {
      return false;
    }
    if (filteredGroupIds == other.filteredGroupIds) {
      return true;
    }
    return filteredGroupIds && other.filteredGroupIds &&
        *filteredGroupIds == *other.filteredGroupIds;
  }
};

} // namespace facebook::rebalancer::materializer

namespace std {
template <>
struct hash<facebook::rebalancer::materializer::PartitionInfoCacheKey> {
  std::size_t operator()(
      const facebook::rebalancer::materializer::PartitionInfoCacheKey& key)
      const {
    const auto filterHash = key.filteredGroupIds
        ? facebook::rebalancer::UnorderedSetHash{}(*key.filteredGroupIds)
        : std::numeric_limits<std::size_t>::max();
    return folly::hash::hash_combine(key.partitionId, filterHash);
  }
};
} // namespace std

namespace facebook::rebalancer::materializer {

// One configured scope item.
struct BundleItem {
  entities::ScopeItemId scopeItemId;
  double size;
  std::shared_ptr<const std::vector<entities::GroupId>> groupIds;
};

class ExpressionBuilder {
 public:
  ExpressionBuilder(
      std::shared_ptr<const entities::Universe> universe,
      const entities::Map<
          entities::ContainerId,
          std::vector<entities::ObjectId>>& updatedInitialAssignment,
      std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor = nullptr,
      std::shared_ptr<Metrics::Builder> metrics = nullptr);

  // Absolute util is defined as the sum of dimension values for objects in a
  // scope item which match the condition given by the choice of metric.
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId) FOLLY_TS_REQUIRES(!applyfunc);

  // Similar to getAbsoluteUtil, but the util by groups of the partition is
  // upperbounded / lowerbounded by the provided groupLimits.
  // If a group is not present in the groupLimits, its util is not bounded.
  // will be an aggregation of possibly many AbsoluteUtil expressions.
  folly::coro::Task<ExprPtr> getBoundedAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      entities::PartitionId partitionId,
      const entities::Map<entities::GroupId, double>& groupLimits,
      bool isUpperBound = true,
      std::optional<double> defaultValue = std::nullopt,
      std::optional<entities::ScopeId> aggregationScopeId = std::nullopt)
      FOLLY_TS_REQUIRES(!applyfunc);

  // Complete-bundle contribution plus raw utilization from unconfigured items:
  //   sum_G floor(util(item, G) / size) * size.
  folly::coro::Task<ExprPtr> getBundledAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId capacityScopeId,
      entities::ScopeItemId capacityScopeItemId,
      entities::PartitionId partitionId,
      entities::ScopeId configuredScopeId,
      const std::vector<BundleItem>& bundleItems) FOLLY_TS_REQUIRES(!applyfunc);

  // Relative util is the absolute util divided by the scope item's size in the
  // same dimension.
  folly::coro::Task<ExprPtr> getRelativeUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId);

  // Absolute util considering objects of a specific group only.
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      entities::PartitionId partitionId,
      entities::GroupId groupId) FOLLY_TS_REQUIRES(!applyfunc);

  // Relative util considering objects of a specific group only.
  folly::coro::Task<ExprPtr> getRelativeUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      entities::PartitionId partitionId,
      entities::GroupId groupId);

  // Absolute util at a single index of a vector object dimension, instead of
  // the max over all indices.
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      int dimensionIndex) FOLLY_TS_REQUIRES(!applyfunc);

  // Absolute util for a custom object dimension. Not cached.
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      const entities::ObjectScalarDimension& objectDimension,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId) FOLLY_TS_REQUIRES(!applyfunc);

  // Absolute util over a set of scopeItemIds in a scope. If groupId and
  // partitionId are given, the util is computed for the group. Not cached.
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      const std::vector<entities::ScopeItemId>& scopeItemIds,
      std::optional<entities::PartitionId> partitionId = std::nullopt,
      std::optional<entities::GroupId> groupId = std::nullopt)
      FOLLY_TS_REQUIRES(!applyfunc);

  // Let S_i be a scopeItem in the main scope S and A be another scope called
  // aggregation scope. We assume that A is a strict sub-scope of S. That is
  // every scopeItem in A belongs to exactly one scopitem of S. This is common
  // for example in DC Hierarchy where S = 'datacenter' and A = 'msb'.
  // In some cases, instead of computing getAbsoluteUtil(S_i,...) which is
  // basically the utilization by collectively looking at objects in S_i, we
  // might be interested in "aggregating" at a different scope A. More
  // precisely, getAggregatedAbsoluteUtil(S_i, A) = Sum_{A_j \in S_i}
  // getAbsoluteUtil(A_j, ...) Here A_j is the set of sub-scopeItems of S_i
  // induced by A In the DC hierarchy example, this corresponds to all msbs in a
  // datacenter.
  folly::coro::Task<ExprPtr> getAggregatedAbsoluteUtil(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeId aggregationScopeId,
      entities::ScopeItemId scopeItemId,
      entities::PartitionId partitionId,
      entities::GroupId groupId) FOLLY_TS_REQUIRES(!applyfunc);

  // Absolute util out of scope for a custom object dimension.
  folly::coro::Task<ExprPtr> getAbsoluteUtilOutOfScope(
      UtilMetric metric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId) FOLLY_TS_REQUIRES(!applyfunc);

  // Absolute util out of scope for a custom object dimension. Not cached.
  std::shared_ptr<Expression> getAbsoluteUtilOutOfScope(
      UtilMetric metric,
      const entities::ObjectScalarDimension& objectDimension,
      entities::ScopeId scopeId);

  // Returns expression representing an object assigned to container
  std::shared_ptr<Expression> isAssigned(
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      entities::ObjectId objectId);

  // Is object assigned to container
  std::shared_ptr<Expression> isAssigned(
      entities::ContainerId containerId,
      entities::ObjectId objectId);

  std::shared_ptr<Expression> getObjectPartitionLookup(
      UtilMetric metric,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      ExprPtr objectPartition,
      const entities::Map<entities::GroupId, double>& overrides,
      ObjectPartitionLookupPenaltyTransform penaltyTransform,
      int groupsAllowed,
      bool minBound);

  struct ScopeParams {
    entities::ScopeId scopeId;
    entities::ScopeItemId scopeItemId;

    bool operator==(const ScopeParams& other) const {
      return scopeId == other.scopeId && scopeItemId == other.scopeItemId;
    }
  };

  ExprPtr getObjectPartition(
      const entities::Map<entities::GroupId, double>& groupLimits,
      entities::DimensionId dimensionId,
      entities::PartitionId partitionId,
      bool normalizeByGroupSize,
      const std::optional<ScopeParams>& scopeParams = std::nullopt,
      std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds =
          nullptr,
      double defaultGroupCoefficient = 1.0);

 private:
  std::shared_ptr<const PartitionInfo> getPartitionInfo(
      entities::PartitionId partitionId,
      std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds);

  ExprPtr createObjectPartition(
      const entities::Map<entities::GroupId, double>& groupLimits,
      entities::DimensionId dimensionId,
      entities::PartitionId partitionId,
      bool normalizeByGroupSize,
      const std::optional<ScopeParams>& scopeParams,
      std::shared_ptr<const entities::Set<entities::GroupId>> filteredGroupIds,
      double defaultGroupCoefficient);

  std::shared_ptr<Expression> createObjectPartitionLookup(
      UtilMetric metric,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      ExprPtr objectPartition,
      const entities::Map<entities::GroupId, double>& overrides,
      ObjectPartitionLookupPenaltyTransform penaltyTransform,
      int groupsAllowed,
      bool minBound);

 public:
  const Assignment& getUpdatedInitialAssignment() const {
    return updatedInitialAssignment_;
  }

  std::shared_ptr<ObjectPartitionMoveLimit> getObjectPartitionMoveLimit(
      const entities::Map<entities::GroupId, double>& groupLimits,
      entities::PartitionId partitionId,
      entities::DimensionId dimensionId,
      entities::Set<entities::ContainerId> sourceContainerIdsNotAffectingLimit,
      entities::Set<entities::ContainerId>
          destinationContainerIdsNotAffectingLimit) const;

  std::shared_ptr<GroupRoutingLatencyLookup> getGroupRoutingLatencyLookup(
      entities::RoutingConfigId routingConfigId,
      entities::GroupId groupId,
      const interface::RoutingLatencyMetricInfo& metric);

  std::shared_ptr<GroupRoutingTrafficLookup> getGroupRoutingTrafficLookup(
      entities::RoutingConfigId routingConfigId,
      entities::PartitionId partitionId,
      entities::GroupId groupId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId);

  // This the cached version of 'binary_min' operation, and hence it is here (as
  // opposed to most operations being in Operators.cpp). Caller
  // needs to make sure that A and B are binary. Caching can result in a large
  // reduction in materialization time when this operation is heavily used.
  ExprPtr binaryMin(ExprPtr A, ExprPtr B);

  /** Assuming that @param innerScopeId is fully nested inside @param
  outerScopeId, this function returns the projection of @param outerScopeItemId
  onto the inner scope, and returns the resulting scopeItems.
   For example:
    outer scope:    |---s1----|---s2---|
    inner scope:    |-a1-|-a2-|---a3---|
    getNestedImage(s1) = {a1, a2}, getNestedImage(s2) = {a3}
   NOTE: this function will throw if the scopes are not properly nested.
   Building of images is cached, so that it is done only once.
  */
  std::vector<entities::ScopeItemId> getNestedImage(
      entities::ScopeId outerScopeId,
      entities::ScopeId innerScopeId,
      entities::ScopeItemId outerScopeItemId);

  /** This function returns the projection of @param outerGroupId
  onto the inner partition, and returns the resulting groups of the
  innerPartition.
   For example:
    outer partition:    |---g1----|---g2---|
    inner partition:    |-a1-|-a2-|---a3---|
    getNestedImage(g1) = {a1, a2}, getNestedImage(g2) = {a3}
   NOTE: this  will throw if the partitions are not properly nested.
   This function is cached
  */
  std::shared_ptr<const entities::Set<entities::GroupId>> getNestedImage(
      entities::PartitionId outerPartitionId,
      entities::PartitionId innerPartitionId,
      entities::GroupId outerGroupId);

 public:
  // Serializes access to context_ from getUpperBound()/getLowerBound().
  // Capability-annotated so the FOLLY_TS_GUARDED_BY/REQUIRES claims below are
  // checked under -Wthread-safety on Clang.
  folly::AnnotatedMutex applyfunc;
  // Upper bound of the values the given expression may take.
  double getUpperBound(const Expression& expression)
      FOLLY_TS_REQUIRES(!applyfunc);
  // Lower bound of the values the given expression may take.
  double getLowerBound(const Expression& expression)
      FOLLY_TS_REQUIRES(!applyfunc);

 private:
  // Scope images explained with an example
  // ======================================
  //
  // Consider the following example with 2 scopes:
  // - Scope "s" (source) with 2 items: s1, s2
  // - Scope "d" (destination) with 2 items: d1, d2
  //
  // Consider 6 containers (c1, c2, c3, c4, c5, c6) and the following mapping:
  // - Scope "s":
  //   - s1: c1, c5
  //   - s2: c2, c3, c4
  // - Scope "d":
  //   - d1: c1, c2
  //   - d2: c3, c4, c6
  //
  // The same information can be represented nicely as a 2D table:
  //            +----+--------+----------+
  //            | d1 |   d2   | not-in-d |
  // +----------+----+--------+----------+
  // | s1       | c1 |        | c5       |
  // +----------+----+--------+----------+
  // | s2       | c2 | c3, c4 |          |
  // +----------+----+--------+----------+
  // | not-in-s |    | c6     |          |
  // +----------+----+--------+----------+
  //
  // The image of scope "d" when projected onto scope item "s1" is:
  // {d1: [c1], not-in-d: [c5]}
  //
  // The image of scope "d" when projected onto scope item "s2" is:
  // {d1: [c2], d2: [c3, c4]}

  // Represents the image of a destination scope when projected onto an item of
  // a source scope.
  struct ScopeImage {
    // Map of destination scope items to list of containers.
    entities::Map<
        entities::ScopeItemId,
        std::shared_ptr<entities::Set<entities::ContainerId>>>
        inScope;
    // List of containers outside of the destination scope.
    std::shared_ptr<entities::Set<entities::ContainerId>> outOfScope;
  };

  // ScopeImages represents the image of a destination scope when projected onto
  // each item of the source scope.
  struct ScopeImages {
    // Source scope item to scope image.
    entities::Map<entities::ScopeItemId, ScopeImage> inScope;
    // Destination scope projected onto the outside of the source scope.
    ScopeImage outOfScope;
  };

  // Internal interface for absolute util where partition and group are
  // optional but expected together.
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      Descriptor descriptor) FOLLY_TS_REQUIRES(!applyfunc);

  // Internal interface for relative util where partition and group are optional
  // but expected together.
  folly::coro::Task<ExprPtr> getRelativeUtil(
      UtilMetric metric,
      Descriptor descriptor);

  // Builds an expression representing the absolute util for a scalar dimension.
  // Scalar dimension is either explicitly provided or implicitly inferred using
  // the descriptor and dimensionIdx
  template <typename DimensionOrIdx>
    requires IsDimensionOrIndexType<DimensionOrIdx>
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      const Descriptor& descriptor,
      const DimensionOrIdx& dimensionOrIdx) FOLLY_TS_REQUIRES(!applyfunc);

  // Builds an expression representing the absolute util for a given
  // ObjectPartitionRoutingDimension, which is a special scalar dimension that
  // is defined on an ObjectPartition and a RoutingConfig
  folly::coro::Task<ExprPtr> getAbsoluteUtil(
      UtilMetric metric,
      const Descriptor& descriptor,
      const entities::ObjectPartitionRoutingDimension&
          objectPartitionRoutingDimension);

  // Sum of dimension values for objects currently placed in the scope item.
  folly::coro::Task<ExprPtr> getAbsoluteUtilAfter(
      const Descriptor& descriptor,
      int dimensionIndex);
  folly::coro::Task<ExprPtr> getAbsoluteUtilAfter(
      const Descriptor& descriptor,
      const entities::ObjectScalarDimension& objectDimension);

  // Sum of dimension values for objects currently placed in the scope item.
  // This function is uncached and supports dynamic dimensions.
  folly::coro::Task<ExprPtr> getAbsoluteUtilAfterDynamic(
      const Descriptor& descriptor,
      int dimensionIndex);

  // Sum of dimension values for objects initially placed in the scope item.
  folly::coro::Task<ExprPtr> getAbsoluteUtilInitial(
      const Descriptor& descriptor,
      int dimensionIndex);
  folly::coro::Task<ExprPtr> getAbsoluteUtilInitial(
      const Descriptor& descriptor,
      const entities::ObjectScalarDimension& objectDimension);

  // Sum of dimension values for objects currently placed in the scope item
  // which where in the same scope item initially. In other words, objects which
  // have stayed in their initial scope item and haven't moved elsewhere.
  folly::coro::Task<ExprPtr> getAbsoluteUtilStayed(
      const Descriptor& descriptor,
      int dimensionIndex);
  folly::coro::Task<ExprPtr> getAbsoluteUtilStayed(
      const Descriptor& descriptor,
      std::shared_ptr<ObjectVector> initialObjectVector,
      std::shared_ptr<ObjectVector> fullObjectVector);
  folly::coro::Task<ExprPtr> getAbsoluteUtilStayed(
      const Descriptor& descriptor,
      const entities::ObjectScalarDimension& objectDimension);

  // Instantiates an object vector for the given scalar dimension.
  std::shared_ptr<ObjectVector> getObjectVector(
      entities::DimensionId dimensionId,
      int dimensionIndex);
  std::shared_ptr<ObjectVector> getObjectVector(
      entities::DimensionId dimensionId,
      int dimensionIndex,
      entities::PartitionId partitionId,
      entities::GroupId groupId);
  std::shared_ptr<ObjectVector> getObjectVector(
      entities::DimensionId dimensionId,
      int dimensionIndex,
      std::optional<entities::ScopeItemId> scopeItemId);
  std::shared_ptr<ObjectVector> getObjectVector(
      entities::DimensionId dimensionId,
      int dimensionIndex,
      entities::PartitionId partitionId,
      entities::GroupId groupId,
      std::optional<entities::ScopeItemId> scopeItemId);
  std::shared_ptr<ObjectVector> getObjectVector(
      const entities::ObjectScalarDimension& objectDimension);
  std::shared_ptr<ObjectVector> getObjectVector(
      const entities::ObjectScalarDimension& objectDimension,
      std::optional<entities::ScopeItemId> scopeItemId);

  // Instantiates an object vector for the given scalar dimension, restricted to
  // objects initially in the given scope item.
  std::shared_ptr<ObjectVector> getInitialObjectVector(
      const Descriptor& descriptor,
      int dimensionIndex);
  std::shared_ptr<ObjectVector> getInitialObjectVector(
      const Descriptor& descriptor,
      const entities::ObjectScalarDimension& objectDimension);
  // Instantiates an object vector for the given scalar dimension, consisting of
  // all objects
  std::shared_ptr<ObjectVector> getInitialObjectVectorFull(
      const Descriptor& descriptor,
      int dimensionIndex);
  std::shared_ptr<ObjectVector> getInitialObjectVectorFull(
      const Descriptor& descriptor,
      const entities::ObjectScalarDimension& objectDimension);

  // Instantiates an object lookup given a scope item and an object vector.
  std::shared_ptr<ObjectLookup> getObjectLookup(
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      std::shared_ptr<ObjectVector> objectVector);

  // Instantiates an object lookup given a list of containers and an object
  // vector.
  std::shared_ptr<ObjectLookup> getObjectLookup(
      std::shared_ptr<const PackerSet<entities::ContainerId>> containerIds,
      std::shared_ptr<ObjectVector> objectVector);

  // Instantiates an object lookup for containers outside a given scope.
  std::shared_ptr<ObjectLookup> getObjectLookupOutOfScope(
      entities::ScopeId scopeId,
      std::shared_ptr<ObjectVector> objectVector);

  /** Instantiates a stable stayed expression given a scope item and an object
  vector. Recall that stable stayed is basically a ObjectLookup using
  @param initialObjectVector as the object_vector for performing lookups.
  Moreover, it updates the equivalence sets using @param fullObjectVector
  @param initialObjectVector is the dimension values for inital assignment
  restricted only to objects in the lookup container
  @param fullObjectVector is the dimension values for inital assignment
  consisting of all the objects
  **/
  std::shared_ptr<StableStayed> getStableStayed(
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      std::shared_ptr<ObjectVector> initialObjectVector,
      std::shared_ptr<ObjectVector> fullObjectVector);

  std::shared_ptr<StableStayed> getStableStayed(
      std::shared_ptr<const PackerSet<entities::ContainerId>> containerIds,
      std::shared_ptr<ObjectVector> initialObjectVector,
      std::shared_ptr<ObjectVector> fullObjectVector);

  std::shared_ptr<StableStayed> getStableStayedOutOfScope(
      entities::ScopeId scopeId,
      std::shared_ptr<ObjectVector> initialObjectVector,
      std::shared_ptr<ObjectVector> fullObjectVector);

  std::shared_ptr<GroupRoutingRing> getGroupRoutingRing(
      entities::RoutingConfigId routingConfigId,
      entities::GroupId groupId);

  std::shared_ptr<GroupRoutingTrafficLookup> getGroupRoutingTrafficLookup(
      entities::RoutingConfigId routingConfigId,
      entities::GroupId groupId,
      entities::ScopeItemId scopeItemId);

  // List of objects belonging to groupId of partitionId which are initially
  // placed in containerId.
  const std::vector<entities::ObjectId>& getInitialObjects(
      entities::PartitionId partitionId,
      entities::GroupId groupId,
      entities::ContainerId containerId);

  // Computes the initial objects for a given partition.
  const entities::Map<
      entities::GroupId,
      entities::Map<entities::ContainerId, std::vector<entities::ObjectId>>>&
  getInitialObjects(entities::PartitionId partitionId);

  // List of containers which don't belong to any scope item of scopeId.
  const std::shared_ptr<const entities::Set<entities::ContainerId>>
  getContainersOutOfScopePtr(entities::ScopeId scopeId);

  // List of containerIds that belong to a set of scopeItemIds; also returns if
  // the sets of containers of the given scopeItems are disjoint
  std::pair<std::shared_ptr<const PackerSet<entities::ContainerId>>, bool>
  getContainersIdsInScopeItems(
      entities::ScopeId scopeId,
      const std::vector<entities::ScopeItemId>& scopeItemIds) const;

  // Returns the image of a source scope item to a destination scope. The
  // source scope item may possibly map to one or more destination scope
  // items. The result contains a map of destination scope items to list the
  // of its containers which are present in the source scope item.
  const ScopeImage& getScopeImage(
      entities::ScopeId destinationScopeId,
      entities::ScopeId sourceScopeId,
      entities::ScopeItemId sourceScopeItemId);

  // Returns the image of scope destinationScopeId when projected outside of the
  // scope sourceScopeId.
  const ScopeImage& getOutOfScopeImage(
      entities::ScopeId destinationScopeId,
      entities::ScopeId sourceScopeId);

  // Returns the images of scope destinationScopeId when projected on each scope
  // item of scope sourceScopeId.
  const ScopeImages& getScopeImages(
      entities::ScopeId destinationScopeId,
      entities::ScopeId sourceScopeId);

  // Utilization metric DURING double counts the object's contribution in both
  // source and destination containers, so if all dimension values are positive,
  // the value of duringExpr can only increase. In that case, we can override
  // the lower bound with the initial value of duringExpr
  static ExprPtr maybeApplyBoundsOverrideForDuringExpr(
      ExprPtr duringExpr,
      const entities::ObjectScalarDimension& dimension);

 private:
  std::shared_ptr<const entities::Universe> universe_;

  // Classification of objects by initial container and group membership.
  // Instead of computing the object intersection between each pair of group and
  // container on demand, we save work by computing the intersection of all
  // pairs in one go, and re-using the results as needed.
  // initialObjects[partitionId][groupId][containerId] is the set of objects
  // which belong to groupId of partitionId which are also initially placed in
  // containerId.
  Cache<
      entities::PartitionId,
      entities::Map<
          entities::GroupId,
          entities::
              Map<entities::ContainerId, std::vector<entities::ObjectId>>>>
      initialObjectsCache_;

  // Cache for getAbsoluteUtil(metric, descriptor)
  AsyncCache<std::tuple<UtilMetric, Descriptor>, ExprPtr> absoluteUtilCache_;

  // groupIds is derived from the other key fields.
  AsyncCache<
      std::tuple<
          UtilMetric,
          entities::DimensionId,
          entities::PartitionId,
          entities::ScopeId,
          entities::ScopeItemId,
          double>,
      ExprPtr>
      bundledItemAbsoluteUtilCache_;

  // Cache for getRelativeUtil(metric, descriptor)
  AsyncCache<std::tuple<UtilMetric, Descriptor>, ExprPtr> relativeUtilCache_;

  // Cache for getObjectVector(dimensionId, dimensionIndex)
  Cache<std::tuple<entities::DimensionId, int>, std::shared_ptr<ObjectVector>>
      objectVectorCache_;

  // Cache for getAbsoluteUtilAfter(descriptor, dimensionIndex)
  AsyncCache<std::tuple<Descriptor, int>, ExprPtr> absoluteUtilAfterCache_;

  // Cache for getAbsoluteUtilInitial(descriptor, dimensionIndex)
  AsyncCache<std::tuple<Descriptor, int>, ExprPtr> absoluteUtilInitialCache_;

  // Cache for getAbsoluteUtilStayed(descriptor, dimensionIndex)
  AsyncCache<std::tuple<Descriptor, int>, ExprPtr> absoluteUtilStayedCache_;

  // Cache for getObjectVector(descriptor, dimensionIndex, partitionId,
  // groupId)
  Cache<
      std::tuple<
          entities::DimensionId,
          int,
          entities::PartitionId,
          entities::GroupId>,
      std::shared_ptr<ObjectVector>>
      objectVectorWithGroupCache_;

  // Cache for getObjectVector(descriptor, dimensionIndex, scopeItemId)
  Cache<
      std::tuple<
          entities::DimensionId,
          int,
          std::optional<entities::ScopeItemId>>,
      std::shared_ptr<ObjectVector>>
      objectVectorDynamicCache_;

  // Cache for getObjectVector(descriptor, dimensionIndex, partitionId, groupId,
  // scopeItemId)
  Cache<
      std::tuple<
          entities::DimensionId,
          int,
          entities::PartitionId,
          entities::GroupId,
          std::optional<entities::ScopeItemId>>,
      std::shared_ptr<ObjectVector>>
      objectVectorDynamicWithGroupCache_;

  // Cache for getContainersOutOfScopePtr(scopeId)
  Cache<
      entities::ScopeId,
      std::shared_ptr<const entities::Set<entities::ContainerId>>>
      containersOutOfScopeCache_;

  // Cache for object, scope item to expression
  Cache<std::tuple<entities::ScopeItemId, entities::ObjectId>, ExprPtr>
      isAssignedScopeItemCache_;

  // Cache for caching part, container pair to variable (expr)
  Cache<std::tuple<entities::ObjectId, entities::ContainerId>, ExprPtr>
      isAssignedContainerCache_;

  // Cache for getScopeImages(destinationScopeId, sourceScopeId)
  Cache<std::tuple<entities::ScopeId, entities::ScopeId>, ScopeImages>
      scopeImagesCache_;

  Cache<std::pair<ExprPtr, ExprPtr>, ExprPtr> binaryMinCache_;

  Cache<
      std::pair<entities::RoutingConfigId, entities::GroupId>,
      std::shared_ptr<GroupRoutingRing>>
      routingConfigToGroupRoutingRing_;

  Cache<
      std::tuple<
          entities::RoutingConfigId,
          entities::GroupId,
          entities::ScopeItemId>,
      std::shared_ptr<GroupRoutingTrafficLookup>>
      groupRoutingTrafficLookupCache_;

  Cache<
      std::tuple<
          entities::RoutingConfigId,
          entities::GroupId,
          interface::RoutingLatencyMetric,
          std::optional<double>>,
      std::shared_ptr<GroupRoutingLatencyLookup>>
      groupRoutingLatencyLookupCache_;

  // Cache for getNestedImage(outerPartitionId, innerPartitionId, outGroupId)
  Cache<
      std::tuple<
          entities::PartitionId,
          entities::PartitionId,
          entities::GroupId>,
      std::shared_ptr<const entities::Set<entities::GroupId>>>
      nestedPartitionImageCache_;

  // Cache for getObjectPartition(dimensionId, partitionId,
  // normalizeByGroupSize, scopeParams, filteredGroupIds,
  // defaultGroupCoefficient). Only caches when groupLimits is empty.
  // (filteredGroupIds is hashed using commutative_hash_combine_range for cache
  // key)
  Cache<
      std::tuple<
          entities::DimensionId,
          entities::PartitionId,
          bool,
          std::optional<ScopeParams>,
          std::optional<size_t>,
          double>,
      ExprPtr>
      objectPartitionCache_;

  Cache<PartitionInfoCacheKey, std::shared_ptr<const PartitionInfo>>
      partitionInfoCache_;

  // Cache for getObjectPartitionLookup. Only caches when overrides is empty.
  Cache<
      std::tuple<
          UtilMetric,
          entities::ScopeId,
          entities::ScopeItemId,
          ExprPtr,
          ObjectPartitionLookupPenaltyTransform,
          int,
          bool>,
      ExprPtr>
      objectPartitionLookupCache_;

  // Context for computing bounds.
  // Accessed by getUpperBound()/getLowerBound(); guarded by applyfunc.
  Context context_ FOLLY_TS_GUARDED_BY(applyfunc);

  // Includes changes from MovesInProgressSpec. Set once in the constructor and
  // read-only thereafter; no mutex needed.
  Assignment updatedInitialAssignment_;

  std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor_ = nullptr;

  std::shared_ptr<Metrics::Builder> metrics_ = nullptr;
};

template <typename DimensionOrIdx>
  requires IsDimensionOrIndexType<DimensionOrIdx>
folly::coro::Task<ExprPtr> ExpressionBuilder::getAbsoluteUtil(
    UtilMetric metric,
    const Descriptor& descriptor,
    const DimensionOrIdx& objectDimensionOrIdx) FOLLY_TS_REQUIRES(!applyfunc) {
  const auto getObjectDimension =
      [&]() -> const entities::ObjectScalarDimension& {
    if constexpr (std::is_same_v<DimensionOrIdx, int>) {
      return universe_->getObjects()
          .getDimension(descriptor.dimensionId.value())
          .at(objectDimensionOrIdx);
    } else {
      return objectDimensionOrIdx;
    }
  };
  const auto& objectDimension = getObjectDimension();
  // special scalar dimension that is based on an objectPartition and a
  // routingConfig; needs separate handling
  if (objectDimension.isRoutingConfigBased()) {
    co_return co_await getAbsoluteUtil(
        metric,
        descriptor,
        dynamic_cast<const entities::ObjectPartitionRoutingDimension&>(
            objectDimension));
  }

  // Compute the various flavors of utilization expressions
  switch (metric) {
    case UtilMetric::AFTER:
      co_return co_await getAbsoluteUtilAfter(descriptor, objectDimensionOrIdx);
    case UtilMetric::DURING: {
      auto [afterExpr, initialExpr, stayedExpr] =
          co_await folly::coro::collectAll(
              getAbsoluteUtilAfter(descriptor, objectDimensionOrIdx),
              getAbsoluteUtilInitial(descriptor, objectDimensionOrIdx),
              getAbsoluteUtilStayed(descriptor, objectDimensionOrIdx));
      auto during = afterExpr + initialExpr - stayedExpr;
      co_return maybeApplyBoundsOverrideForDuringExpr(
          std::move(during), objectDimension);
    }
    case UtilMetric::NEW: {
      auto [afterExpr, stayedExpr] = co_await folly::coro::collectAll(
          getAbsoluteUtilAfter(descriptor, objectDimensionOrIdx),
          getAbsoluteUtilStayed(descriptor, objectDimensionOrIdx));
      co_return afterExpr - stayedExpr;
    }
    case UtilMetric::OLD: {
      auto [initialExpr, stayedExpr] = co_await folly::coro::collectAll(
          getAbsoluteUtilInitial(descriptor, objectDimensionOrIdx),
          getAbsoluteUtilStayed(descriptor, objectDimensionOrIdx));
      co_return initialExpr - stayedExpr;
    }
    case UtilMetric::INITIAL:
      co_return co_await getAbsoluteUtilInitial(
          descriptor, objectDimensionOrIdx);
    case UtilMetric::STAYED:
      co_return co_await getAbsoluteUtilStayed(
          descriptor, objectDimensionOrIdx);
    case UtilMetric::MOVED: {
      auto [afterExpr, initialExpr, stayedExpr] =
          co_await folly::coro::collectAll(
              getAbsoluteUtilAfter(descriptor, objectDimensionOrIdx),
              getAbsoluteUtilInitial(descriptor, objectDimensionOrIdx),
              getAbsoluteUtilStayed(descriptor, objectDimensionOrIdx));
      co_return afterExpr + initialExpr - 2 * stayedExpr;
    }
  }
}

} // namespace facebook::rebalancer::materializer

// Hash specialization for ScopeParams to enable caching
namespace std {
template <>
struct hash<
    facebook::rebalancer::materializer::ExpressionBuilder::ScopeParams> {
  std::size_t operator()(
      const facebook::rebalancer::materializer::ExpressionBuilder::ScopeParams&
          params) const {
    return std::hash<std::tuple<
        facebook::rebalancer::entities::ScopeId,
        facebook::rebalancer::entities::ScopeItemId>>{}(
        std::make_tuple(params.scopeId, params.scopeItemId));
  }
};
} // namespace std
