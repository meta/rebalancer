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

@hack.NamePrefix{prefix = "rebalancer_interface_thrift_"}
@hack.LegacyOmitPrefixInNameString
package "meta.com/algopt/rebalancer"

namespace hack ""

include "thrift/annotation/thrift.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/hack.thrift"

namespace cpp2 facebook.rebalancer.interface
namespace py3 rebalancer.interface.thrift.v2
namespace rust problem_solver_types

// WARNING: these types are part of a transition away from SWIG and are not
// yet the proper way of interacting with the solver. For now, use the C++ types
// in //algopt/rebalancer/interface/ProblemSpecs.h or the SWIG types.

// Wiki:
// https://www.internalfb.com/intern/wiki/ReBalancer/API/Goals_and_constraints/
enum AggregatedGroupSpecAggType {
  MAX = 1,
  SUM = 2,
}

enum LimitType {
  ABSOLUTE = 1,
  RELATIVE = 2,
}

// groupAggregationType defines formula type of giving a partition to a
// container
// --> this gives us a formula generated from partition + container
// containerAggregationType defines formula type of a specify scopeItem
// --> giving formulas from containers in that scopeItem, how to generate
// scopeItem formula
// groupAggregationType defines formula how objects within the same group are
// aggregated
//
// Example Usage:
//
// ===== Problem Setup =====
// solver = ProblemSolver()
// solver.setAssignment(
//     {"container1": ["obj1", "obj2", "obj3"], "container2": ["obj4"]}
// )
// solver.addObjectDimension(
//     "size",
//     {"obj1": 1, "obj2": 2, "obj3": 3, "obj4": 4}
// )
// solver.addPartition(
//     "partition",
//     {"obj1": "group1", "obj2": "group1", "obj3": "group2", "obj4": "group1"},
// )
// solver.addScope("scope", {"container1": "item", "container2": "item"})
//
// ===== Example Spec =====
// spec = AggregatedGroupSpec()
// spec.scope = "scope"
// spec.partitionName = "partition"
// spec.dimension = "size"
// coef = Limit()
// coef.setScopeItemLimits({"container1": 0.5, "container2": 0.8})
// spec.setContributions({"item": coef})
// spec.limit.globalLimit = 10
// spec.withinGroupAggregationType = SUM
// spec.groupAggregationType = MAX
// spec.containerAggregationType = SUM
//
// ===== Generated Formula =====
//                       container1      containerAggregationType
//                           |               |
//                  |---------------------|  |
//                 max((0 + 1 + 2), 3) * 0.5 + max(4) * 0.8 <= 10
//                  |   |     | |  | |    |
//                  |   |_____L_|  |_|    |
//  groupAggregationType  |   |     |   scopeItemLimits
//                      group1|    group2
//                            |
//                  withinGroupAggregationType
struct AggregatedGroupSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: string partitionName;
  5: Limit limit = {"type": LimitType.ABSOLUTE};
  10: AggregatedGroupSpecAggType withinGroupAggregationType = AggregatedGroupSpecAggType.SUM;
  6: AggregatedGroupSpecAggType groupAggregationType = AggregatedGroupSpecAggType.MAX;
  7: AggregatedGroupSpecAggType containerAggregationType = AggregatedGroupSpecAggType.SUM;
  8: Filter filter;

  // these are useful when aggregation type is linearsum.
  // for example, if container aggregation type is linearsum
  // it will be a summation of coef * containerExpr
  // similar for groupCoef
  //
  // itemName -> contributions
  // for each Limit:
  // containerName -> groupName -> contribution
  9: optional map<string, Limit> contributions;
}

struct AssignmentAffinity {
  1: string objectName;
  2: string scopeItemName;
  3: double affinity;
}

// Make specific objects prefer specific containers
struct AssignmentAffinitiesSpec {
  1: string name;
  2: string scope;
  3: list<AssignmentAffinity> affinities;
  // Alternative to affinities; the dimension must default to zero.
  4: optional string dimension;
}

struct GroupScopeItemAffinity {
  1: string group;
  2: string scopeItem;
  3: double targetDimensionValue; // What fraction of the group has an affinity to this scope item.
  4: double affinity;
}

// Make specific groups prefer specific scope items.
struct GroupAssignmentAffinitiesSpec {
  1: string name;
  2: string scope;
  3: string partition;
  4: string dimension;
  5: list<GroupScopeItemAffinity> affinities;
}

struct AvoidAssignment {
  1: string object;
  2: list<string> scopeItems;
}

// Prevents a set of assignments from happening. Assignments are defined as a
// list of tuples of object and scope item.
struct AvoidAssignmentsSpec {
  1: string name;
  2: string scope;
  3: list<AvoidAssignment> assignments;
}

// Do not move any of these objects
struct AvoidMovingSpec {
  1: string name;
  2: list<string> objects;
}

enum BalanceSpecBoundType {
  ABSOLUTE = 1, // Absolute offset added to the average relative utilization.
  RELATIVE = 2, // Multiplier of the average utilization.
  RELATIVE_UTIL = 3, // Relative utilization threshold.
}

enum BalanceSpecDefinition {
  AFTER = 1,
  DURING = 2,
  NEW = 3,
  OLD = 4,
}

// Controls what metric is being balanced by a BalanceSpec.
enum BalanceSpecMetric {
  // Balance relative utilization (absUtil / capacity) across scope items.
  // Scope items with zero capacity are excluded.
  RELATIVE_UTIL = 1,
  // Balance average demand per object (absUtil / numObjects) across scope
  // items. Ensures heavy and light objects are evenly distributed regardless
  // of container capacity. By default, numObjects counts ALL objects. Set
  // capacityPerItemCountDimension on BalanceSpec to use a custom dimension for
  // the count.
  CAPACITY_PER_ITEM = 2,
}

enum BalanceSpecFormula {
  LINEAR = 1,
  SQUARES = 2,
  MAX = 3,
  IDEAL = 4,
  LEGACY = 5,
  // Variance-based balancing: penalty = n * Var(relUtil).
  // Focus on balancing the relative utilization without distractions from capacity
  // and efficiency.
  RELATIVE_UTIL_VARIANCE = 6,
}

// Balance utilization of a resource across items of a scope
struct BalanceSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  5: optional double softUpperBound;
  6: BalanceSpecBoundType boundType = BalanceSpecBoundType.RELATIVE;
  7: BalanceSpecFormula formula = BalanceSpecFormula.LINEAR;
  8: Filter filter;
  9: BalanceSpecDefinition definition = BalanceSpecDefinition.AFTER;

  // if set then the average used in balance computation is fixed to the
  // initial value of average (and is not dynamically computed everytime)
  10: bool fixAverageToInitial;

  // include the utilization of these containers in computation of initial
  // average. one instance where this is helpful: we have a ToFree container
  // whose objects will be re-distributed among other containers at the end of
  // load-balancing
  11: list<string> includeInInitialAverage;

  // Currently, the average relative utilization of scope items is calculated
  // by taking the relative utilization of each scope item, summing them up
  // and divide by the total number of scope items
  // In legacy, the average relative utilization is calculated by
  // taking the sum of all absolute util and dividing by the total capacity
  // size of scope items
  12: bool useLegacyAverage = false;

  // Upper bound was previously only supported for RELATIVE_UTIL bound type.
  // This flag enables opt-in upper bound support for other bound types,
  // preserving backward compatibility with existing configurations.
  // Upper bounds are only ignored for Absolute or Relative bound types.
  // This does not affect other configurations of formulas or bound types.
  // Setting it to true will preserve legacy behavior.
  13: bool ignoreUpperBoundForIdealWithAbsOrRelBoundTypes = true;

  // Controls what metric is being balanced. The formula field controls how
  // imbalance is penalized, independently of the metric.
  14: BalanceSpecMetric balanceMetric = BalanceSpecMetric.RELATIVE_UTIL;

  // Optional dimension to use as the count denominator in
  // CAPACITY_PER_ITEM (absUtil / numObjects). When omitted, numObjects counts
  // ALL objects (each contributes 1). When set, the solver sums the values of
  // this dimension instead, so you can exclude objects by giving them value 0
  // or weight objects differently.
  15: optional string capacityPerItemCountDimension;

  // Per-scope-item upper bounds are supported by RELATIVE_UTIL_VARIANCE.
  // Values are interpreted according to boundType. The global limit is used
  // when a scope item has no override.
  16: optional Limit upperBounds;

  // DEPRECATED: use upperBounds instead. Retained so persisted instances can
  // be migrated by BackwardCompatabilityUtils on replay.
  @thrift.Deprecated{message = "Use upperBounds (Limit) instead."}
  4: double upperBound = 1;
}

// Do not use BalanceV2, use Balance instead.
// TODO: remove BalanceV2 once Torch models stop using it.
enum BalanceV2SpecBoundType {
  ABSOLUTE = 1,
  RELATIVE = 2,
}

// Do not use BalanceV2, use Balance instead.
// TODO: remove BalanceV2 once Torch models stop using it.
enum BalanceV2SpecDefinition {
  AFTER = 1,
  DURING = 2,
  NEW = 3,
}

// Do not use BalanceV2, use Balance instead.
// TODO: remove BalanceV2 once Torch models stop using it.
enum BalanceV2SpecFormula {
  LINEAR = 1,
  SQUARES = 2,
  MAX = 3,
  IDEAL = 4,
  LEGACY = 5,
}

// Do not use BalanceV2, use Balance instead.
// TODO: remove BalanceV2 once Torch models stop using it.
struct BalanceV2Spec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: double upperBound = 1;
  5: optional double softUpperBound;
  6: BalanceV2SpecBoundType boundType = BalanceV2SpecBoundType.RELATIVE;
  7: BalanceV2SpecFormula formula = BalanceV2SpecFormula.LINEAR;
  8: Filter filter;
  9: BalanceV2SpecDefinition definition = BalanceV2SpecDefinition.AFTER;

  // if set then the average used in balance computation is fixed to the
  // initial value of average (and is not dynamically computed everytime)
  10: bool fixAverageToInitial;

  // include the utilization of these containers in computation of initial
  // average. one instance where this is helpful: we have a ToFree container
  // whose objects will be re-distributed among other containers at the end of
  // load-balancing
  11: list<string> includeInInitialAverage;
}

struct BipartiteSwapsSpec {
  1: string name;
  2: list<string> subsetContainers;
}

// limit Capacity Ratios among scope items
// For example, Item1 : Item 2 <= 1:2
// Item1 and Item2 are key of ratios, 0.5 is the double value
struct CapacityRatioSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: map<string, map<string, double>> ratios;
}

enum CapacitySpecDefinition {
  AFTER = 1,
  DURING_AND_AFTER = 2,
  DURING = 3,
  DOUBLE_DURING_AND_AFTER = 4,
  DOUBLE_DURING = 5,
  NEW = 6,
  OLD = 7,
  MOVED_DATA = 8,
}

enum CapacitySpecBound {
  MAX = 1,
  MIN = 2,
}

// In Rebalancer, adding an object to a scopeItem typically increases the utilization of the
// scope item linearly. However, in some cases adding new objects may result in decreasing
// additional benefit (upperbound) or adding objects increases the utilization by at least
// a given amount (lowerbound). Concretely,
//  -- Setting an upperbound of { group_1 : x_1, group_2 : x_2}  on a container C ensures
// that utilization of container C from group_1 objects does not go beyond x_1 (resp. x2 for group_2)
//  -- Setting a lowerbound of { group_1 : x_1, group_2 : x_2}  on a container C ensures that
// utilization of container C is at least x_1 if it contains one or more object of group_1
enum UtilizationBoundType {
  UPPER = 1,
  LOWER = 2,
}

struct GroupUtilizationBound {
  1: string partitionName;
  2: UtilizationBoundType boundType = UtilizationBoundType.UPPER;
  // by default, all groups for which utilization was not provided are assumed to be unbounded
  // The limits are applied to utilization of a group but can be
  // specified in any way (using a global default, per scope item default or a
  // specific value for per-group scopeItem combination)
  3: Limit perGroupValues = {
    "type": LimitType.ABSOLUTE,
    "isDefaultLimitUnbounded": true,
  };
  // sometimes we may want to aggregate utilization across a different scope than one provided
  // in the spec definition. For example, if this was used with CapacitySpec and scope was "rack"
  // but we want to aggregate and bound utilization across "host" scope, we can specify aggregationScope
  // as "host". Note that the aggregationScope must be a subset of the scope provided in the spec definition
  4: optional string aggregationScope;
}

// For each configured scope item B, contributes
//   sum_G floor(Util(B, G) / k) * k
// instead of raw utilization. Unconfigured items remain raw.
// Requires AFTER, !zeroAllowed, no utilizationBound, a disjoint partition, and a
// non-negative static scalar dimension. Each B must belong to one CapacitySpec
// scope item.
struct GroupBundleUtilization {
  1: string partitionName;
  2: string scope;
  // Positive bundle size k per `scope` item, in units of the spec dimension.
  3: map<string, double> bundleSizes;
}

union UtilizationBound {
  // In future there might be other ways to bound utilization such as ScopeItemUtilizationBound
  // which can help us cap utilization per container perhaps. For now, we only need to bound
  // utilization on a per-group basis
  1: GroupUtilizationBound groupUtilizationBound;
}

@thrift.ReserveIds{ids = [8, 9]}
struct CapacitySpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: CapacitySpecDefinition definition = CapacitySpecDefinition.AFTER;
  5: CapacitySpecBound bound = CapacitySpecBound.MAX;
  6: Limit limit;
  7: Filter filter;
  10: bool zeroAllowed;
  // With legacy formula, we normalize by dividing with scopeItemCapacity
  // This can be useful in some cases to ensure that contribution of each scopeItem
  // with MAX bound is at most 1 (basically captures what fraction of scopeItemCapacity
  // is allocated). However, this will throw if any of the scopeItem limits are zero.
  11: bool useLegacyFormula = false;
  // if specified, we upper|lower bound the utilization contribution by a group of objects
  // to a scopeItem by a pre-specified limit
  12: optional UtilizationBound utilizationBound;
  // Counts only complete per-group bundles in configured scope items.
  13: optional GroupBundleUtilization groupBundleUtilization;
}

/* this is built for namespace allocation problem
 * formulat looks like:
 * prod_usage + migrating_usage + max{dr_usage} <= (capacity + supply) * ratio
 * scope -> iteration for max{dr_usage}
 * supplyPartition -> supply
 * dependencies -> migrating_usage
 *
 * why has partition here?
 * we are limiting capacities per group
 * so supplyPartition and partitionName need to have same group name
 */
struct CapacityWithSupplyAndDrSpec {
  1: string name;

  // disaster happens at this scope
  2: string scope;
  3: string dimension;

  // capacity is limited for every group, prod partition
  4: string partitionName;
  // this should have same group name as above
  5: string supplyPartition;

  // we want to limit capacity at which scope
  6: string prodScope;
  // we want to limit capacity for which scope item,
  // item specified here must be member of prodScope
  7: string prodItem;

  // specifies cut over objects -> migrating_usage
  8: map<string, list<string>> dependencies;
  // specifies prod and dr objects
  9: map<string, string> drPairs;
  10: double ratio;
  // exceptions ratios
  // key is period, which are groupName
  11: map<string, double> exceptions;

  // specify absolute capacity for each group
  12: Limit limit = {"type": LimitType.ABSOLUTE};
  // items specified here must be members of the scope
  13: Filter filter;
}

enum ColocateGroupsSpecBound {
  MAX = 1,
  MIN = 2,
}

// Decide how many scopeItems a group will appear in
// For example, if we want objects in a group to be
// placed to only one item, use limit = {"type": LimitType.ABSOLUTE, "globalLimit": 1}
@thrift.ReserveIds{ids = [6]}
struct ColocateGroupsSpec {
  1: string name;
  2: string scope;
  3: string partitionName;

  // if nothing specified in scopeItemWeights
  // default value is 1
  // with weight the spec is doing following for each group
  // sum of (weight * groupExistsOnItem) <= limit
  4: map<string, double> scopeItemWeights;
  5: Filter filter;

  // The limit represents the maximum/minimum number of different scope items a group can spread across.
  7: ColocateGroupsSpecBound bound = ColocateGroupsSpecBound.MAX;

  8: Limit limits = {"type": LimitType.ABSOLUTE, "globalLimit": 1};

  // if no dimension is mentioned, then by default it is taken as {object}_count.
  9: optional string dimension;

  // if set, the penalty per group is "squared", for example for MAX bound
  // penalty = MAX(0, (weight * groupExistsOnItem) - limit)
  // We add the term penalty^1.1 to the objective
  // Note that we use 1.1 instead of 2 because it keeps range of resulting values small and
  // enforces the same behavior as squares
  10: bool squares = false;

  // It is possible that even an optimal solver might not be able to
  // fully improve this objective (say if there is a constraint on number
  // of moves). In that case, it may be desirable to make progress
  // towards that goal. Setting this to "true" achieves that behavior.
  //
  // NOTE: continuous penalty is non-linear so it may make the LP
  // a quadratic program and may result in slower solve times.
  11: bool useContinuousPenaltyWithOptimal = false;

  // By default all groups have weight 1; if a weight W_G is specified for a group G, then
  // when using this spec as a
  // a) goal: the excess for each group above/below the limit is penalized by a factor of W_G.
  // b) constraint: the constraint is multiplied by W_G
  @cpp.Type{template = "folly::F14FastMap"}
  12: map<string, double> groupToWeight;
}

@thrift.ReserveIds{ids = [4]}
struct DisasterRecoveryCapacitySpec {
  1: string name;

  // disaster happens at this scope
  2: string scope;
  3: string dimension;
  // 4: deprecated

  // each disastergroup is a viewed as a complete disaster scenario---meaning, all the
  // scopeItems in the group fail together; if not defined, then each scopeItem is
  // considered as a separate sharedDisasterGroup
  5: list<set<string>> sharedDisasterGroups;

  6: map<string, list<string>> primaryToSetOfSecondaryObjects;
}

// DrainCapacitySpec models capacity in scenarios where specific scope items spill a proportion of their own utilization into other scope items.
struct DrainCapacitySpec {
  1: string name;
  2: string scope;
  3: string dimension;

  // originScopeItem -> destinationScopeItem --> proportion
  // TODO map has performance issue
  // while F14FastMap has swig issue
  4: map<string, map<string, double>> spillDistribution;
}

// Objects of different groups may not coexist in the same scope item.
struct ExclusiveGroupsSpec {
  1: string scope;
  2: string partitionName;
  3: string dimension;
  4: string name;
}

// if an item is put in filter, then objects could both be in that item
// if separate == true, we are separating the objects
// else, at least one of the two needs to be in the scopeItem
@thrift.ReserveIds{ids = [6]}
struct ExclusiveObjectsSpec {
  1: string name;
  2: string scope;
  3: list<ObjectPair> pairs;
  4: Filter filter;
  5: bool separate = true;
}

struct ConflictingScopeItemInfo {
  1: string conflictingScopeItem;
  2: double overlap = 1;
}

struct ScopeItemConflictInfo {
  1: string scopeItem;
  // 2: deprecated
  3: list<ConflictingScopeItemInfo> conflictingScopeItemsWithOverlap;

  // Deprecated: New use cases should use conflictingScopeItemsWithOverlap instead.
  2: list<string> conflictingScopeItems;
}
// These formulas are used when ExclusiveScopeItemsSpec is used as a goal. Each attempts to pack
// the scope items as much as possible.
//
// MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT is the default and attempts to minimize the count of
// scope items that are invalidated by other exclusive scope items. It is simple to implement and
// works well for many cases, but fails to achieve the best possible packing in other cases.
//
// AGGRESSIVE_PACKING is a more aggressive formula for more complicated scenarios (e.g. IP-Next).
// It requires specifying weights for each scope item and the overlap each one has with other
// scope items. It attempts to maximize the objective score as defined by:
//
//     For each scope item S_i, let w_i specify the size of S_i, default 1. And for each conflicting
//     scope item S_j where S_i conflicts with S_j, let overlap_i_j be the size of the overlap
//     between the two conflicting scope items, S_i and S_j, also default 1. Then the amount that
//     each scope item is "packed", P_i, can be calculated by summing w_i * step(utilization(S_i))
//     and overlap_i_j * step(utilization(S_j)) for each S_j that conflicts with S_i.
//
//     Then the objective score can be calculated as:
//         Sum[w_i * P_i ^ 2] for all i
enum ExclusiveScopeItemsFormula {
  MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT = 1,
  AGGRESSIVE_PACKING = 2,
}

// Define pairs of scope items that cannot be utilized concurrently:
// - by any set of objects
// - or by objects of the same group (if an object partition was provided)
struct ExclusiveScopeItemsSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  // 4: deprecated
  5: optional string partitionName;
  6: list<ScopeItemConflictInfo> conflictInfoList;
  7: map<string, double> scopeItemWeights;
  8: ExclusiveScopeItemsFormula formula = ExclusiveScopeItemsFormula.MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT;

  // Deprecated: New use cases should use conflictInfoList instead.
  4: list<ScopeItemPair> pairs;
}

enum ExclusiveSwapsSpecSubsetDefinition {
  // At least one in subset: a valid swap exchanges an object inside the
  // subset with any other object (inside or outside the subset).
  AT_LEAST_ONE_IN_SUBSET = 1,
  // Exactly one in subset: a valid swap exchanges an object inside the subset
  // with an object outside the subset.
  EXACTLY_ONE_IN_SUBSET = 2,
  // Both same side of subset: objects are only allowed to move via exclusive swaps
  // with other objects on the same side of subset. That means object
  // in_subset can move via exclusive sawps with other objects in_subset and objects
  // outside of subset can move via exclusive swaps with other object outside of subset.
  BOTH_SAME_SIDE_OF_SUBSET = 3,
}

struct ExclusiveSwapsSpec {
  // If subsetObjects is set, then all swaps are required to contain at least
  // one object from the list.
  1: string name;
  2: optional list<string> subsetObjects;
  3: ExclusiveSwapsSpecSubsetDefinition subsetDefinition = ExclusiveSwapsSpecSubsetDefinition.AT_LEAST_ONE_IN_SUBSET;
}

enum FilterType {
  SCOPE_ITEM = 1,
  GROUP = 2,
}

struct Filter {
  1: optional list<string> itemsBlacklist;
  2: optional list<string> itemsWhitelist;
  3: FilterType type = FilterType.SCOPE_ITEM;
}

enum FlowSpecBound {
  UPPER = 1,
  LOWER = 2,
}

// given a scope, and object pairs
// limit flow capacity for every scopeItem pair
// flow capacity means only when object in itemA and its pair in itemB
// dimension value for this object is considered
//
// Complexity:
// This spec could be costly
// It will generate item#^2 formulas
// And each formula need binary_min for all pair on pairs
struct FlowSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: FlowSpecBound bound = FlowSpecBound.UPPER;
  5: list<ObjectPair> pairs;

  // itemA -> itemB -> limit for A to B
  6: Limit limit = {"type": LimitType.ABSOLUTE, "globalLimit": 0};

  // we only limit flow from items below
  // destination for each flow is defined by Filter
  7: Filter sourceFilter;
  // key is sourceItemName
  8: map<string, Filter> destinationFilter;

  // for each expr generated per src+dst pair
  // if there is a coefficient needs to be * to it
  // default is 1
  9: Limit coefficients = {"type": LimitType.ABSOLUTE};
}

enum GroupCapacitySpecDefinition {
  DURING = 1,
  DURING_AND_AFTER = 2,
  AFTER = 3,
}

enum GroupCapacitySpecBound {
  MAX = 1,
  EXACT = 2,
  MIN = 3,
}

enum GroupCapacitySpecUtilType {
  // Utilization value is used as is
  LINEAR = 1,
  // Non-zero utilization is rounded to 1
  STEP = 2,
  // Utilization is considered zero if divisible by K, otherwise it is rounded to 1
  STEP_MOD_K = 3,
}

/*
At a high-level, `GroupCapacitySpec` does the following when used as a constraint: given a scope `S`,
a main partition `P`, a contributing partition `Q`, where every group in `Q` maps to at most one group in `P`,
it limits the utilization of a group `G` in `P` across all the scope items in `S`.

The utilization of each `G` in `P` is computed in terms of the contributions of groups in the set `C_G`,
where each `G_i` in `C_G` is part of contributing partition `Q`  (note that each `G_i` is a subset of `G`),
and is computed using the following expression:

      Util(G) =  \sum_{G_i in C_G} \sum_{SI in S} UTIL_FUNC(G_i, SI) * weight(G_i, SI).

The constraint imposed is `Util(G) <= L_G`, where `L_G` is the limit specified for group `G` in the input.

By default, UTIL_FUNC(G_i, SI) is the LINEAR AFTER/DURING utilization. This can be changed to, for example, only count the precence of
G_i's by changing the 'utilType' field to STEP which in turn results in
         Util(G) = \sum_{G_i in C_G} \sum_{SI in S} STEP(Util(G_i, SI)) * weight(G_i, SI),
         where STEP(x) = 1 when x > 0; it is 0 otherwise

Assumptions -
    - Each object belongs to only one group in partitionName and
  contributionPartition
 */
struct GroupCapacitySpec {
  1: string name;
  2: string scope;

  // constraints are written for this partition i.e. for each group in this
  // partition, capacity is summed over all containers
  3: string partitionName;

  // contribution values depend on this optional partition.
  // contribution partition must be higher resolution partition than partition
  // name i.e. each group in contribution partition must belong to one and only
  // one group in main partition
  // if not given, partitionName is assumed to be contribution partition as well
  4: optional string contributionPartition;
  5: GroupCapacitySpecDefinition definition = GroupCapacitySpecDefinition.DURING_AND_AFTER;
  6: GroupCapacitySpecBound bound = GroupCapacitySpecBound.MAX;

  // the limits are defined based on partitionName
  7: Limit limit = {"type": LimitType.ABSOLUTE, "globalLimit": 0};

  // the contributions are defined based on contributionPartition + scope
  8: Limit contribution = {"type": LimitType.ABSOLUTE, "globalLimit": 0};
  9: Filter filter;
  10: GroupCapacitySpecUtilType utilType = GroupCapacitySpecUtilType.LINEAR;
  // if a bundleSize is specified, the solver will incentivize the objects of the contributionPartition
  // to form bundles of the specified size (as defined via `Limit` struct). That is, say for a given
  // scopeItem, bundleSize = k, then utilization is considered zero if it is a multiple of k,
  // otherwise it is considered 1.
  // This is only applicable when utilType is STEP_MOD_K.
  11: optional Limit bundleConfig;
}

enum GroupCountSpecDefinition {
  AFTER = 1,
  DURING_AND_AFTER = 2,
  DURING = 3,
  STAYED = 4,
}

enum GroupCountSpecBound {
  MAX = 1,
  MIN = 2,
  EXACT = 3,
  // group count is an integer multiple of limit
  MULTIPLE = 4,
}

enum GroupCountSpecLimitRelativeTo {
  GROUP_SIZE = 1,
  SCOPE_ITEM_UTIL = 2,
  GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC = 3,
}

struct GroupCountSpec {
  1: string name;
  2: string scope;
  3: string partitionName;
  4: GroupCountSpecDefinition definition = GroupCountSpecDefinition.AFTER;
  5: GroupCountSpecBound bound = GroupCountSpecBound.MAX;
  6: Limit limit = {"type": LimitType.ABSOLUTE};
  7: bool squares;
  8: bool zeroAllowed;
  9: string dimension;
  10: Filter filter;
  11: GroupCountSpecLimitRelativeTo limitRelativeTo = GroupCountSpecLimitRelativeTo.GROUP_SIZE;
  // Replaces zero when zeroAllowed is true and bound is MIN: allow any amount
  // up to minimumLimit.
  12: double minimumLimit = 0;
  // routing config name that is used to define the limit when limitRelativeTo is of
  // type GroupCountSpecLimitRelativeTo.GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC
  13: optional string routingConfigForLimit;
}

// Two groups of objects cannot share the same scope item
struct GroupIsolationLimitSpec {
  1: string name;
  2: string scope;
  3: string partitionName;
  4: Limit limit = {"type": LimitType.ABSOLUTE, "globalLimit": 0};
  5: i32 groupsAllowed = 1;
  6: Filter filter;
}

// Limit how many objects of the same group can move.
// NOTE: this spec currently only supports 'container' scope and therefore, a scopeItem below is a container.
struct GroupMoveLimitSpec {
  1: string name;
  2: string partitionName;
  3: Limit limit = {"type": LimitType.ABSOLUTE};
  // For 'sourceScopeItemsAffectingLimitFilter' below, if the filter is unspecified, then all scopeItems are considered to affect the limit (and similarly
  // for the filter 'destinationScopeItemsAffectingLimitFilter'). If filters are specified, then only the appropriate scopeItems are considered to affect the limit.
  // For instance, when using container scope (currently, this is the only scope supported), for a group G and an object O in G,
  // a move w.r.t. O contributes to the limit of G if and only if it is between a container C1 and C2, where C1 is a source container
  // affecting the limit and C2 is destination container affecting the limit.
  4: Filter sourceScopeItemsAffectingLimitFilter;
  5: Filter destinationScopeItemsAffectingLimitFilter;
  // if no dimension is mentioned, then by default it is taken as {object}_count.
  6: optional string dimension;
}

struct Limit {
  1: LimitType type = LimitType.RELATIVE;
  2: double globalLimit = 1;
  3: map<string, double> scopeItemLimits;
  4: map<string, double> groupLimits;
  5: map<string, map<string, double>> scopeItemToGroupLimits;
  // if set, we will overwrite 'globalLimit' to positive infinity
  6: bool isDefaultLimitUnbounded = false;
}

// Maximize utilization on a set of scope items
struct MaximizeAllocationSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: Filter filter;
}

/*
For each scope item S:
  available units = max(
      0,
      floor((capacity(S) - utilization(S)) / unitSize(S)))

This goal maximizes the sum of available units across the filtered scope items.
unitSize can have one global value or a different value per scope item.
*/
struct MaximizeFreeCapacityUnitsSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: Limit unitSize = {"type": LimitType.ABSOLUTE, "globalLimit": 1};
  5: Filter filter;
}

// RAS stacking specific spec that tries to colocate scope items
// of both types (think lowCpu / highCpu reservations) on the same
// group (think server partition) in a balanced way
struct ItemsAffinitySpec {
  1: list<string> scopeItemsOfType1;
  2: list<string> scopeItemsOfType2;
  3: string partitionName;
  4: string scope;
  5: string dimension;
  6: string name;
}

// spec to address the large shard problem w.r.t. a dimension
struct LargeShardSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  // name of the scopeItem that has the unassigned shards
  4: string unassignedScopeItemName;
  // use filter to, for example, add  scopeItems that should not be drained
  5: Filter filter;
}

enum MinimizeContainerSpecFormula {
  LEGACY = 1,
  NEW = 2,
}

// Stopping condition for the minimize-containers goal: how far to keep packing.
union MinimizeContainersTarget {
  // Stop once at least this many scope items are freed (emptied).
  1: i32 maxFreeLimit;
  // Stop once the number of occupied scope items drops to this value.
  2: i32 minUsedLimit;
}

// The following spec is called "MinimizeContainersSpec", but actually does something more general.
// It minimizes the number of utilized scope items in the provided `scope`. If an empty scope is
// provided, the spec defaults to the container scope (hence the name).
struct MinimizeContainersSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  // Per-scope-item cost, keyed by scope item name of this spec's `scope`. With an
  // empty `scope` the spec defaults to the container scope, so the keys are
  // container names --- hence the field name; for any other `scope` they are that
  // scope's item names. Defaults to 1.0 for any scope item not listed. Cost breaks
  // ties during packing: among equally-loaded scope items, the higher-cost one is
  // freed first.
  4: map<string, double> containerCosts;
  6: Filter filter;
  // The new formula will maintain the same behavior as the legacy formula
  // however, the new formula will also work while using maxFreeLimit and localsearch
  // while the legacy formula did not.
  // The new formula will now also cases where there exists containers outside of scope
  // see fbcode/algopt/rebalancer/interface/tests/MinimizeContainersTest.cpp for example test cases with new formula
  // Legacy formula will be deprecated once more services onboard to the new formula
  7: MinimizeContainerSpecFormula formula = MinimizeContainerSpecFormula.NEW;
  // Stopping condition for packing (max free or min used). Only supported by the NEW formula.
  8: optional MinimizeContainersTarget target;

  // DEPRECATED: use `target` (field 8) instead. Retained only so that persisted
  // instances can be migrated to `target` by BackwardCompatabilityUtils on
  // replay; no forward code path reads this field.
  @thrift.Deprecated{message = "Use target (MinimizeContainersTarget) instead."}
  5: optional i32 maxFreeLimit;
}

// Minimize number of moves performed
struct MinimizeMovementSpec {
  1: string name;
  2: string scope;
  3: string dimension;

  // if set to false we will not multiply expression
  // with "magic" heuristic weights inside rebalancer
  4: bool magicScaling = true;

  // if set to true the expression will not normalize with
  // i)  number of items
  // ii) capacity of each container in this dimension
  5: bool doNotNormalize = false;

  // if allowance is non-zero, that means we allow up till that much moves
  6: double allowance = 0;
}

// Minimizes the utilization of scope items with the n-th largest utilization
struct MinimizeNthLargestUtilizationSpec {
  1: string name;
  2: string scope;
  3: string dimension;

  // n is a 0-based index
  4: i32 n;
  5: Filter filter;

  // if set, do not minimize below target utilization
  6: double targetUtilization;
}

// Minimize sum of squared utilizations
struct MinimizeSquaresSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: Filter filter;
  5: optional double upperBound;
  6: double lowerBound = 0;
  7: i32 pieceCount = 100;
}

// Move objects in the same group together across containers
struct MoveGroupSpec {
  1: string name;
  2: string partitionName;
}

@thrift.ReserveIds{ids = [1, 3]}
struct MoveInProgress {
  2: string objName;
  4: string toContainer;
}

// Objects accounted as moving from one container to another
struct MovesInProgressSpec {
  1: string name;
  2: list<MoveInProgress> moves;
}

// at least one of the CapacitySpec needs to be true
struct MultipleOrCapacitySpec {
  1: string name;
  // consider all CapacitySpecs' bound to be MAX
  2: list<CapacitySpec> capacitySpecs;
}

// Generic way of referring to a spec.
// Specially useful in nested Specs, such as LogicalOrSpec / LogicalOrSpec.
union GenericSpec {
  1: LogicalOrSpec logicalOrSpec;
  2: LogicalAndSpec logicalAndSpec;
  3: CapacitySpec capacitySpec;
  4: GroupCountSpec groupCountSpec;
  5: GroupCapacitySpec groupCapacitySpec;
}

// At least one of the genericSpecs needs to be true.
struct LogicalOrSpec {
  1: string name;
  2: list<GenericSpec> genericSpecs;
}

// All of the genericSpecs need to be true.
struct LogicalAndSpec {
  1: string name;
  2: list<GenericSpec> genericSpecs;
}

// Scope items not accepting incoming objects
struct NonAcceptingSpec {
  1: string name;
  2: string scope;
  3: list<string> items;
}

// ObjectAffinity has 2 possible behaviors:
// 1. If object1 is the name of an object, then at least one of these 2 conditions must be met:
//    - object0 is placed in the same scope item as object1.
//    - All of objectsN are placed in the same scope item as object1.
// 2. If object1 is the name of a scope item, then at least one of these 2 conditions must be met:
//    - object0 is placed in scope item object1.
//    - All of objectsN are placed in the scope item object1.
struct ObjectAffinity {
  1: string object0;
  2: string object1;
  3: list<string> objectsN;
}

@thrift.ReserveIds{ids = [5]}
struct ObjectAffinitiesSpec {
  1: string name;
  2: string scope;
  3: list<ObjectAffinity> affinities;
  4: Filter filter;
}

struct ObjectPair {
  1: string object1;
  2: string object2;
}

struct PairAffinity {
  1: ObjectPair pair;
  2: double affinity;
}

// Have pairs of objects that prefer being assigned to the same scope item
struct PairAffinitiesSpec {
  1: string name;
  2: string scope;
  3: list<PairAffinity> affinities;
  4: double limit = 1;
}

//  In RAS we have servers in categories of available/unavailability and
//  used/unused. We want to create a constraint for each scope item that is the
//  following: available_stayed + available_unused_new >= requested_capacity
//  If incoming dimension is empty we default to
//  available_stayed >= requested capacity.
struct RasRebalancingMovementSpec {
  1: string name;
  2: string scope;

  // This dimension defines which servers are valid active servers when counting
  // servers stayed in reservation. Currently it is available or used servers.
  3: string stayedDimension;

  // This dimension defines which machines are accepted as valid active
  // replacement. This dimension changes based on the pool/reservation
  // configuration. If incoming dimension is empty we default to
  // available_stayed >= requested capacity.
  4: string incomingDimension;
  5: Limit limit;
  6: Filter filter;
}

struct ScopeAffinitiesSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: map<string, double> affinities;
}

struct ScopeItemPair {
  1: string scopeItem1;
  2: string scopeItem2;
}

// For each failure domain i
//   S_i = # of reservation servers in failure domain i
//    B_i = # of buffer servers in failure domain i
// Implementation 1: MAX over failure scenarios
//  { MAX_i (S_i + B_i) } - SUM(B_i)   [<= 0]
// Implementation 2: SUM over failure scenarios
//  SUM_i { MAX( 0,  (S_i + B_i) - SUM_i(B_i)) }
//
// The alternate implementation is more friendly to local search
// https://docs.google.com/document/d/1MvnuPq_eKAtyXiO3AbZnRGD5GTh6X_jJuFibdUuuGsI/edit?usp=sharing

// Id 12 was `useSumOverFailureScenarios`, removed once the legacy
// non-sum-over-failure-domains path was deleted and nothing read the flag.
@thrift.ReserveIds{ids = [12]}
struct SRBufferCapacitySpec {
  // if empty dimension is provided we use count dimension
  1: string name;
  2: string scope;
  3: string dimension;
  4: string partitionName;
  5: Filter filter;

  // scope item pairs: corresponding to reservation and its buffer
  6: list<ScopeItemPair> scopeItemPairs;

  // default matching error
  7: double matchingError;

  // key = main scope item, value = matching error
  // so sr_buffer_requirement >= max - lowerBoundMatchingError
  8: map<string, double> lowerBoundMatchingErrors;

  // key = main scope item, value = matching error
  // so sr_buffer_requirement <= max + upperBoundMatchingError
  9: map<string, double> upperBoundMatchingErrors;

  // we also add upper bound when this config is set
  10: bool addUpperBound;

  // if useHeuristics is false => bounds are estimated
  // using initial assignment
  11: bool useHeuristics;
}

// Add as an objective to minimize the sum of max value across partitions
// For example: when servers are objects, msbs are partitions and reservations
// are scopes (dimension is server_count) this can be used to minimize the
// sum of maximum number of servers across MSBs per reservations.
struct SumOfMaxSpec {
  1: string name;
  2: string scope;
  3: string partitionName;
  4: string dimension;
  5: Filter filter;
}

enum ThrottlingSpecDefinition {
  ANY = 1,
  IN = 2,
  OUT = 3,
}

struct ThrottlingSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: ThrottlingSpecDefinition definition = ThrottlingSpecDefinition.ANY;
  5: Limit limit;
  6: Filter filter;
}

enum ToFreeSpecFormula {
  MINIMIZE_TOTAL_UTILIZATION = 1,
  MINIMIZE_OCCUPIED_CONTAINERS = 2,
}

// List of containers to free (i.e., to make their utilization w.r.t. to the given dimension as zero)
struct ToFreeSpec {
  1: string name;
  2: list<string> containers;
  // if no dimension is mentioned, then by default it is taken as {object}_count,
  // and the intent would be remove all the objects from the container
  3: optional string dimension;
  // choosing a formula is currently only supported when this spec is used as a goal;
  // when used as a constraint, the constraints w.r.t. all the broken containers are
  // aggregated using ToFreeSpecFormula.MINIMIZE_TOTAL_UTILIZATION formula
  4: ToFreeSpecFormula formula = ToFreeSpecFormula.MINIMIZE_TOTAL_UTILIZATION;
}

struct UtilIncreaseCostSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: double lowerBound;
  5: bool squares;
  6: Filter filter;
}

enum WorkingSetMetric {
  AVG = 1, // minimize the average working set size weighted by the dimension
  MAX = 2, // minimize the maximum working set size among all scope items
}

struct WorkingSetSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: list<WorkingUnit> workingUnits;
  5: WorkingSetMetric metric = WorkingSetMetric.AVG;
}

struct WorkingUnit {
  1: list<string> endpoints;
  2: double weight;
}

// Ensure that utilization of a scopeItem in innerScope is within a certain fraction of
// utilization of the corresponding scopeItem in outerScope
struct NestedScopeLimitSpec {
  1: string name;
  2: string scope;
  3: string outerScope;
  4: string dimension;
  5: Limit limit = {"type": LimitType.ABSOLUTE};
  6: Filter filter;
}

enum RoutingLatencyMetric {
  AVG = 1,
  PERCENTILE = 4,

  // deprecated fields; DO NOT USE
  MAX = 2,
  P99 = 3,
}

struct RoutingLatencyMetricInfo {
  1: RoutingLatencyMetric type = RoutingLatencyMetric.AVG;
  // it only make sense to use percentile if type is PERCENTILE
  2: optional double percentile;
}

// Enforces an upper limit on the latency of traffic received by the different objects,
// according to a routing policy and a table of origin-to-destination latencies.
@thrift.ReserveIds{ids = [4, 5]}
struct RoutingLatencySpec {
  1: string name;
  2: string scope;
  3: string partition;
  7: Limit limit = {"type": LimitType.ABSOLUTE, "globalLimit": 0};
  8: string routingConfigName;
  // filter has to be of type FilterType.GROUP
  9: Filter filter = {"type": FilterType.GROUP};
  // if 'includeWeightedAvgLatencyMetricIfLimitViolated' is set to a value v and 'metric' is not RoutingLatencyMetric.AVG,
  // then, for each group in the given partition, an extra term is added that corresponds to (v * avg latency
  // of that group). These extra "avg terms" are useful in guiding local search.
  10: optional double includeWeightedAvgLatencyMetricIfLimitViolated;
  11: RoutingLatencyMetricInfo latencyMetric;

  // deprecated fields; DO NOT USE.
  6: RoutingLatencyMetric metric = RoutingLatencyMetric.AVG;
}

enum GroupDiversityBound {
  MIN = 1,
  MAX = 2,
}

// Enforces that each scope item gets objects of at least or at most N different
// groups, where N is defined by the limit.
struct GroupDiversitySpec {
  1: string name;
  2: string scope;
  3: string partition;
  4: string dimension;
  5: Limit limit;
  6: GroupDiversityBound bound = GroupDiversityBound.MIN;
  7: Filter filter;
}

enum CapacityWithGroupPresenceBound {
  MAX = 1,
  // MIN is supported only for the PER_GROUP_AND_SCOPE_ITEM intent.
  MIN = 2,
}

enum CapacityWithGroupPresenceUsageIntent {
  PER_SCOPE_ITEM = 1,
  PER_GROUP_AND_SCOPE_ITEM = 2,
}

enum CapacityWithGroupPresenceDefinition {
  AFTER = 1,
  DURING = 2,
  DURING_AND_AFTER = 3,
}

// Controls the formula used for the continuous penalty expression in
// CapacityWithGroupPresenceSpec when using local search.
enum ContinuousPenaltyType {
  CONTINUOUS_UTILIZATION = 1,
  NORMALIZED_CONTINUOUS_UTILIZATION = 2,
}

enum GroupUtilMultiplierTarget {
  PRESENCE_WEIGHT = 1,
  UTILIZATION = 2,
  COMMON = 3,
}

struct GroupUtilMultiplier {
  1: Limit value;
  2: GroupUtilMultiplierTarget target = GroupUtilMultiplierTarget.COMMON;
}

/*
For a group G, the group's contribution to utilization of scope item S is computed in the following way:
  (1)
      G's contribution to S = max(
                        groupToPresenceWeight(G, S) if G has non-zero utilization in S,
                        actual utilization of G in S
                      )

If roundUpGroupUtilOnScopeItem is true, then G's contribution to S is the following:
  (2)
      G's rounded-up contribution to S = ceil(G's contribution to S),
                                              where G's contribution to S is the expression in (1)

Now, if a multiplier is specified w.r.t. (G, S) as part of multiplierList, then G's weighted contribution
to S is the following:
  (3)
      G's weighted contribution to S =  {
                                          ceil(G's rounded-up contribution to S * multiplier), if roundUpGroupUtilOnScopeItem is true,

                                          G's contribution to S * multiplier, if roundUpGroupUtilOnScopeItem is false,
                                        },
                                          where G's rounded-up contribution to S is the expression in (2)

    Note that the first expression in (3) has two ceilings, i.e., is ceil(ceil(G's contribution to S) * multiplier)

If more than one multiplier is specified, then the expression in (3) is computed iteratively per multiplier. So, for example, if
two multipliers are specified, then we will have the following final expression
  (4)
      G's weighted contribution to S =  {
                                          ceil(ceil(G's rounded-up contribution to S * multiplier1) * multiplier2), if roundUpGroupUtilOnScopeItem is true,

                                          G's contribution to S * multiplier1 * multiplier2, if roundUpGroupUtilOnScopeItem is false,
                                        },
                                        where G's rounded-up contribution to S is the expression in (2)

If multiplier is specified by groupUtilMultipliers, then the expression in (4) is computed differently per multiplier target:
  (4)
      G's weighted contribution to S =  {
                                          max(ceil(ceil(rounded-up utilization of G in S * multiplierApplyToUtil) * multiplierApplyToBoth),
                                              ceil(ceil(rounded-up presence weight of G in S * multiplierApplyToPresenceWeight) * multiplierApplyToBoth)), if roundUpGroupUtilOnScopeItem is true,

                                          max(utilization of G in S * multiplierApplyToUtil * multiplierApplyToBoth,
                                              presence weight of G in S * multiplierApplyToPresenceWeight * multiplierApplyToBoth), if roundUpGroupUtilOnScopeItem is false,
                                        }.
*/
@thrift.ReserveIds{ids = [16]}
struct CapacityWithGroupPresenceSpec {
  1: string name;
  2: string scope;
  3: string partition;
  4: string dimension;
  // for a group G and scopeItem S, where S is in aggregationScope (defined below), if G is present in S w.r.t. 'dimension', then
  // S's utilization increases by at least the 'groupToPresenceWeight'[G][S]
  5: Limit groupToPresenceWeight = {"type": LimitType.ABSOLUTE};
  // capacity limits are defined on the **main scope** defined above OR
  // on (group, main scope item) when the usage intent is PER_GROUP_AND_SCOPE_ITEM
  6: Limit scopeItemToLimit;
  7: CapacityWithGroupPresenceBound bound = CapacityWithGroupPresenceBound.MAX;
  // Filter is defined on the main scope defined above
  8: Filter scopeItemFilter = {"type": FilterType.SCOPE_ITEM};
  9: bool roundUpGroupUtilOnScopeItem = true;
  // Note that if limits are defined using scopeItems then they are expected to be on the aggregationScope (defined below)
  // Deprecated: New use cases should use groupUtilMultipliers instead.
  10: list<Limit> multiplierList;
  // if aggrgeationScope is not provided, then it is the same as the main scope defined above. if aggregationScope is specified, then
  // for a scope item S of the main scope a group's utilization in S is computed the following way:
  // utilization of G in S = sum_{S_i in R(S)} util(G, S_i),
  // where R(S) is the set of scope items from the aggregation scope such that each S_i in R(S) is a subset of S.
  // Note that all the rounding-up behaviour, etc., is applied at the aggregationScope level.
  // For example, aggregationScope="container", scope="region"
  11: optional string aggregationScope;
  // When using local search and when roundUpGroupUtilOnScopeItem is true, there is an extra penalty term that is added to make the objective
  // expression continuous, which in turn is used to help local search get out of local optima. Penalty term is per group and
  // is equal to the utilization of the group (unrounded). However, in certain cases, we want to add an extra additive penalty
  // term to the objective expression to, for example, account for objects that may never move.
  12: Limit groupToExtraAdditivePenalty = {
    "type": LimitType.ABSOLUTE,
    "globalLimit": 0,
  };

  // filter for main partition defined above; if not explicitly specified, then all groups in the partition are considered when
  // computing the formulas in (1), (2), (3), (4) above
  13: Filter groupFilter = {"type": FilterType.GROUP};

  14: CapacityWithGroupPresenceUsageIntent intent = CapacityWithGroupPresenceUsageIntent.PER_SCOPE_ITEM;

  // this field is only relevant when using intent = CapacityWithGroupPresenceUsageIntent.PER_GROUP_AND_SCOPEITEM;
  // if aggregationPartition is not provided, then it is the same as the main partition defined above. if aggregationPartition is specified,
  // then for a group G in mainPartition and scope item S, G's utilization in S is computed in the following way:
  // utilization of G in S = sum_{G_i in R(G)} util(G_i, S), where G_i is a group in the aggregationPartition and R(G) is the set of groups in
  // the aggregationPartition that are subsets of G in mainPartition and each object in G is part of exactly one group in R(G).
  // Note that all the rounding-up behaviour, multipliers, etc., is applied at the aggregationPartition level. The limit used to defined the constraint
  // per (group, scope item) is expected to be defined on the mainPartition.
  15: optional string aggregationPartition;

  // A more detailed version of multiplier, which allows us to control multiplier behaviors and scopes. If this is set, then multiplierList is ignored.
  17: list<GroupUtilMultiplier> groupUtilMultipliers;

  // Controls the continuous penalty formula used by local search.
  18: ContinuousPenaltyType continuousPenaltyType = ContinuousPenaltyType.NORMALIZED_CONTINUOUS_UTILIZATION;

  // For each listed (scope item -> groups), the group's presence-weight floor is
  // always honored, even when its actual utilization is 0. Empty means no such
  // groups.
  19: map<string, list<string>> scopeItemToAlwaysPresentGroups;

  20: CapacityWithGroupPresenceDefinition definition = CapacityWithGroupPresenceDefinition.AFTER;
}

struct DiversifyWithinScopeItemSpec {
  1: string name;
  2: string scope;
  3: string partition;
  4: string dimension;
  // limit after which if a group is present in scope item, then it will be spread across containers in the scope item
  5: Limit groupToLimit;
  6: Filter scopeItemFilter = {"type": FilterType.SCOPE_ITEM};
}
