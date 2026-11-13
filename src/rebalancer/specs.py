# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# @manual
# @lint-ignore-every BIASEDTERMPY
#
# This file is hand-written, not produced by a code generator. It mirrors the
# Thrift IDL types that the rebalancer Python binding accepts and returns, so
# users get IDE autocomplete and static checks against typos in spec dicts.
#
# If any of the following Thrift files change, this file must be updated by
# hand to match:
#
#   - algopt/rebalancer/interface/thrift/ProblemSolver.thrift
#       ConstraintSpecs, GoalSpecs, SolverSpecs (the union shims),
#       ManifoldBackupParams, ManifoldUploadPolicy.
#   - algopt/rebalancer/interface/thrift/ProblemSpecs.thrift
#       Every constraint/goal arm struct + their nested types and enums.
#   - algopt/rebalancer/interface/thrift/SolverSpecs.thrift
#       LocalSearchSolverSpec, OptimalSolverSpec and friends + move-type
#       specs and their nested types.
#   - algopt/rebalancer/interface/thrift/Types.thrift
#       AssignmentSolution and the supporting result-side structs;
#       ConstraintParams, ConstraintPolicy, MoveStatsSpec, GroupRoutingRings.
#   - algopt/rebalancer/interface/thrift/Metrics.thrift
#       Metrics struct used in AssignmentSolution.
#   - algopt/rebalancer/algopt_common/thrift/Types.thrift
#       PrecisionTolerances, HigherPriorityObjectivesConfig, Threshold,
#       AllowedWorsening, StringListFilterConfig, PerObjectiveValue.
#
# The binding accepts plain ``dict[str, Any]`` at runtime — these TypedDicts
# only constrain the dict shape statically. For union shims (``ConstraintSpec``,
# ``GoalSpec``, ``SolverSpec``, ``UtilizationBound``, ``GenericSpec``,
# ``MoveTypeSpec``, ``DestinationsToExploreOptions``,
# ``ObjectsToExploreOptions``), exactly one arm key must be present at runtime;
# this is validated by the C++ binding, not by TypedDict.

# pyre-strict

from __future__ import annotations

from typing import Literal, NotRequired, TypedDict


# ---------------------------------------------------------------------------
# Enum literals
# (Thrift enums serialize as their string member name in the JSON wire format.)
# ---------------------------------------------------------------------------

# ProblemSpecs.thrift
AggregatedGroupSpecAggType = Literal["MAX", "SUM"]
LimitTypeName = Literal["ABSOLUTE", "RELATIVE"]
BalanceSpecBoundType = Literal["ABSOLUTE", "RELATIVE", "RELATIVE_UTIL"]
BalanceSpecDefinition = Literal["AFTER", "DURING", "NEW", "OLD"]
BalanceSpecFormula = Literal[
    "LINEAR", "SQUARES", "MAX", "IDEAL", "LEGACY", "RELATIVE_UTIL_VARIANCE"
]
BalanceV2SpecBoundType = Literal["ABSOLUTE", "RELATIVE"]
BalanceV2SpecDefinition = Literal["AFTER", "DURING", "NEW"]
BalanceV2SpecFormula = Literal["LINEAR", "SQUARES", "MAX", "IDEAL", "LEGACY"]
CapacitySpecDefinition = Literal[
    "AFTER",
    "DURING_AND_AFTER",
    "DURING",
    "DOUBLE_DURING_AND_AFTER",
    "DOUBLE_DURING",
    "NEW",
    "OLD",
    "MOVED_DATA",
]
CapacitySpecBound = Literal["MAX", "MIN"]
UtilizationBoundType = Literal["UPPER", "LOWER"]
ColocateGroupsSpecBound = Literal["MAX", "MIN"]
ExclusiveScopeItemsFormula = Literal[
    "MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT", "AGGRESSIVE_PACKING"
]
ExclusiveSwapsSpecSubsetDefinition = Literal[
    "AT_LEAST_ONE_IN_SUBSET", "EXACTLY_ONE_IN_SUBSET", "BOTH_SAME_SIDE_OF_SUBSET"
]
FilterTypeName = Literal["SCOPE_ITEM", "GROUP"]
FlowSpecBound = Literal["UPPER", "LOWER"]
GroupCapacitySpecDefinition = Literal["DURING", "DURING_AND_AFTER", "AFTER"]
GroupCapacitySpecBound = Literal["MAX", "EXACT", "MIN"]
GroupCapacitySpecUtilType = Literal["LINEAR", "STEP", "STEP_MOD_K"]
GroupCountSpecDefinition = Literal["AFTER", "DURING_AND_AFTER", "DURING", "STAYED"]
GroupCountSpecBound = Literal["MAX", "MIN", "EXACT", "MULTIPLE"]
GroupCountSpecLimitRelativeTo = Literal[
    "GROUP_SIZE", "SCOPE_ITEM_UTIL", "GROUP_TO_SCOPE_ITEM_ROUTING_TRAFFIC"
]
MinimizeContainerSpecFormula = Literal["LEGACY", "NEW"]
ThrottlingSpecDefinition = Literal["ANY", "IN", "OUT"]
ToFreeSpecFormula = Literal[
    "MINIMIZE_TOTAL_UTILIZATION", "MINIMIZE_OCCUPIED_CONTAINERS"
]
WorkingSetMetric = Literal["AVG", "MAX"]
RoutingLatencyMetric = Literal["AVG", "PERCENTILE", "MAX", "P99"]
GroupDiversityBound = Literal["MIN", "MAX"]
CapacityWithGroupPresenceBound = Literal["MAX", "MIN"]
CapacityWithGroupPresenceUsageIntent = Literal[
    "PER_SCOPE_ITEM", "PER_GROUP_AND_SCOPE_ITEM"
]
CapacityWithGroupPresenceDefinition = Literal["AFTER", "DURING", "DURING_AND_AFTER"]
GroupUtilMultiplierTarget = Literal["PRESENCE_WEIGHT", "UTILIZATION", "COMMON"]

# Types.thrift (algopt_common/thrift)
IntentName = Literal["MAX"]
AlgoptCommonFilterType = Literal["ALLOWLIST", "BLOCKLIST"]

# Types.thrift (interface/thrift)
ConstraintPolicyName = Literal["DEFAULT", "HARD", "SOFT"]
ConstraintTypeName = Literal[
    "AvailabilityConstraint",
    "ConcentrateNewCapacityConstraint",
    "IsolateConstraint",
    "RestrictNetworkStackingConstraint",
    "StackingConstraint",
    "StackingForMaintenanceConstraint",
]
EndReason = Literal[
    "OPTIMAL",
    "HIT_TIME_LIMIT",
    "HIT_MOVE_LIMIT",
    "HIT_STOP_CONSTRAINT",
    "HIT_PLATEAU_TIME",
    "NO_SOLUTION_FOUND",
    "HIT_STAGE_MOVE_LIMIT",
    "HIT_EXCEPTION",
    "UNABLE_TO_FIND_MORE_MOVES",
    "HIT_MIN_CYCLE_OBJECTIVE_IMPROVEMENT",
]

# SolverSpecs.thrift
OptimalSolverPackage = Literal["XPRESS", "GUROBI", "HIGHS"]
MultiObjectiveSolveType = Literal["BLENDED", "HIERARCHICAL"]
MoveStrategyType = Literal[
    "RANDOM_SAMPLING_WITH_REPLACEMENT", "RANDOM_SAMPLING_WITHOUT_REPLACEMENT"
]

# ProblemSolver.thrift
ManifoldUploadPolicy = Literal["ALWAYS", "ON_FAILURE", "NEVER", "OUTLIER"]


# ---------------------------------------------------------------------------
# Common building blocks (algopt_common Types.thrift + ProblemSpecs.thrift)
# ---------------------------------------------------------------------------


class AllowedWorsening(TypedDict, total=False):
    percent: float  # default 0.0
    absolute: float  # default 0.0
    intent: IntentName  # default "MAX"


class HigherPriorityObjectivesConfig(TypedDict, total=False):
    tuplePosToAllowedWorsening: dict[int, AllowedWorsening]


class Threshold(TypedDict, total=False):
    percent: float
    absolute: float


class StringListFilterConfig(TypedDict, total=False):
    stringsToFilter: list[str]
    filterType: AlgoptCommonFilterType  # default "ALLOWLIST"


class PerObjectiveValue(TypedDict, total=False):
    objectiveIndexToValue: dict[int, float]
    defaultValue: float


class PrecisionTolerances(TypedDict, total=False):
    """Numeric precision thresholds for floating-point comparisons.

    The absolute epsilon must exceed ``std::numeric_limits<double>::epsilon()``.
    """

    absolute: float  # default 1e-10
    relative: float  # default 1e-10


class Filter(TypedDict, total=False):
    """Allow/blocklist over scope items or groups."""

    itemsBlacklist: list[str]
    itemsWhitelist: list[str]
    type: FilterTypeName  # default "SCOPE_ITEM"


class Limit(TypedDict, total=False):
    """Absolute or relative numeric limits, optionally per scope item / group.

    The Thrift IDL uses RELATIVE as the default ``type``; spec defaults often
    set ``{"type": "ABSOLUTE"}`` so the limit is interpreted directly.
    """

    type: LimitTypeName  # default "RELATIVE"
    globalLimit: float  # default 1
    scopeItemLimits: dict[str, float]
    groupLimits: dict[str, float]
    scopeItemToGroupLimits: dict[str, dict[str, float]]
    isDefaultLimitUnbounded: bool  # default False


class ObjectPair(TypedDict, total=False):
    object1: str
    object2: str


class ScopeItemPair(TypedDict, total=False):
    scopeItem1: str
    scopeItem2: str


# ---------------------------------------------------------------------------
# Constraint / Goal arm structs (ProblemSpecs.thrift)
# ---------------------------------------------------------------------------


class AggregatedGroupSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    partitionName: str
    limit: Limit
    withinGroupAggregationType: AggregatedGroupSpecAggType  # default "SUM"
    groupAggregationType: AggregatedGroupSpecAggType  # default "MAX"
    containerAggregationType: AggregatedGroupSpecAggType  # default "SUM"
    filter: Filter
    contributions: dict[str, Limit]


class AssignmentAffinity(TypedDict, total=False):
    objectName: str
    scopeItemName: str
    affinity: float


class AssignmentAffinitiesSpec(TypedDict, total=False):
    """Make specific objects prefer specific containers."""

    name: str
    scope: str
    affinities: list[AssignmentAffinity]


class GroupScopeItemAffinity(TypedDict, total=False):
    group: str
    scopeItem: str
    targetDimensionValue: float
    affinity: float


class GroupAssignmentAffinitiesSpec(TypedDict, total=False):
    """Make specific groups prefer specific scope items."""

    name: str
    scope: str
    partition: str
    dimension: str
    affinities: list[GroupScopeItemAffinity]


class AvoidAssignment(TypedDict, total=False):
    object: str
    scopeItems: list[str]


class AvoidAssignmentsSpec(TypedDict, total=False):
    """Prevent a set of (object, scope item) assignments."""

    name: str
    scope: str
    assignments: list[AvoidAssignment]


class AvoidMovingSpec(TypedDict, total=False):
    """Do not move any of the listed objects."""

    name: str
    objects: list[str]


class BalanceSpec(TypedDict, total=False):
    """Balance utilization of a resource across items of a scope."""

    name: str
    scope: str
    dimension: str
    upperBound: float  # deprecated; use upperBounds
    softUpperBound: float
    boundType: BalanceSpecBoundType  # default "RELATIVE"
    formula: BalanceSpecFormula  # default "LINEAR"
    filter: Filter
    definition: BalanceSpecDefinition  # default "AFTER"
    fixAverageToInitial: bool
    includeInInitialAverage: list[str]
    useLegacyAverage: bool  # default False
    ignoreUpperBoundForIdealWithAbsOrRelBoundTypes: bool  # default True
    upperBounds: Limit  # default globalLimit 1


class BalanceV2Spec(TypedDict, total=False):
    """Deprecated. Prefer ``BalanceSpec``."""

    name: str
    scope: str
    dimension: str
    upperBound: float  # default 1
    softUpperBound: float
    boundType: BalanceV2SpecBoundType  # default "RELATIVE"
    formula: BalanceV2SpecFormula  # default "LINEAR"
    filter: Filter
    definition: BalanceV2SpecDefinition  # default "AFTER"
    fixAverageToInitial: bool
    includeInInitialAverage: list[str]


class BipartiteSwapsSpec(TypedDict, total=False):
    name: str
    subsetContainers: list[str]


class CapacityRatioSpec(TypedDict, total=False):
    """Limit capacity ratios among scope items (e.g. ``Item1:Item2 <= 1:2``)."""

    name: str
    scope: str
    dimension: str
    ratios: dict[str, dict[str, float]]


class GroupUtilizationBound(TypedDict, total=False):
    partitionName: str
    boundType: UtilizationBoundType  # default "UPPER"
    perGroupValues: Limit
    aggregationScope: str


class GroupBundleUtilization(TypedDict, total=False):
    """Count only complete per-group bundles toward a capacity spec."""

    partitionName: str
    scope: str
    bundleSizes: dict[str, float]


class UtilizationBound(TypedDict, total=False):
    """Union: exactly one arm must be present."""

    groupUtilizationBound: NotRequired[GroupUtilizationBound]


class CapacitySpec(TypedDict, total=False):
    """Cap utilization of a dimension on the items of a scope."""

    name: str
    scope: str
    dimension: str
    definition: CapacitySpecDefinition  # default "AFTER"
    bound: CapacitySpecBound  # default "MAX"
    limit: Limit
    filter: Filter
    zeroAllowed: bool
    useLegacyFormula: bool  # default False
    utilizationBound: UtilizationBound
    groupBundleUtilization: GroupBundleUtilization


class CapacityWithSupplyAndDrSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    partitionName: str
    supplyPartition: str
    prodScope: str
    prodItem: str
    dependencies: dict[str, list[str]]
    drPairs: dict[str, str]
    ratio: float
    exceptions: dict[str, float]
    limit: Limit
    filter: Filter


class ColocateGroupsSpec(TypedDict, total=False):
    name: str
    scope: str
    partitionName: str
    scopeItemWeights: dict[str, float]
    filter: Filter
    bound: ColocateGroupsSpecBound  # default "MAX"
    limits: Limit
    dimension: str
    squares: bool  # default False
    useContinuousPenaltyWithOptimal: bool  # default False
    groupToWeight: dict[str, float]


class DisasterRecoveryCapacitySpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    sharedDisasterGroups: list[list[str]]  # set<string> serialized as list
    primaryToSetOfSecondaryObjects: dict[str, list[str]]


class DrainCapacitySpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    spillDistribution: dict[str, dict[str, float]]


class ExclusiveGroupsSpec(TypedDict, total=False):
    """Different groups may not coexist in the same scope item."""

    scope: str
    partitionName: str
    dimension: str
    name: str


class ExclusiveObjectsSpec(TypedDict, total=False):
    name: str
    scope: str
    pairs: list[ObjectPair]
    filter: Filter
    separate: bool  # default True


class ConflictingScopeItemInfo(TypedDict, total=False):
    conflictingScopeItem: str
    overlap: float  # default 1


class ScopeItemConflictInfo(TypedDict, total=False):
    scopeItem: str
    conflictingScopeItemsWithOverlap: list[ConflictingScopeItemInfo]
    conflictingScopeItems: list[str]  # deprecated


class ExclusiveScopeItemsSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    partitionName: str
    conflictInfoList: list[ScopeItemConflictInfo]
    scopeItemWeights: dict[str, float]
    formula: (
        ExclusiveScopeItemsFormula  # default "MINIMIZE_INVALIDATED_SCOPE_ITEMS_COUNT"
    )
    pairs: list[ScopeItemPair]  # deprecated


class ExclusiveSwapsSpec(TypedDict, total=False):
    name: str
    subsetObjects: list[str]
    subsetDefinition: (
        ExclusiveSwapsSpecSubsetDefinition  # default "AT_LEAST_ONE_IN_SUBSET"
    )


class FlowSpec(TypedDict, total=False):
    """Limit flow capacity for every scope-item pair."""

    name: str
    scope: str
    dimension: str
    bound: FlowSpecBound  # default "UPPER"
    pairs: list[ObjectPair]
    limit: Limit
    sourceFilter: Filter
    destinationFilter: dict[str, Filter]
    coefficients: Limit


class GroupCapacitySpec(TypedDict, total=False):
    name: str
    scope: str
    partitionName: str
    contributionPartition: str
    definition: GroupCapacitySpecDefinition  # default "DURING_AND_AFTER"
    bound: GroupCapacitySpecBound  # default "MAX"
    limit: Limit
    contribution: Limit
    filter: Filter
    utilType: GroupCapacitySpecUtilType  # default "LINEAR"
    bundleConfig: Limit


class GroupCountSpec(TypedDict, total=False):
    name: str
    scope: str
    partitionName: str
    definition: GroupCountSpecDefinition  # default "AFTER"
    bound: GroupCountSpecBound  # default "MAX"
    limit: Limit
    squares: bool
    zeroAllowed: bool
    dimension: str
    filter: Filter
    limitRelativeTo: GroupCountSpecLimitRelativeTo  # default "GROUP_SIZE"
    minimumLimit: float  # default 0
    routingConfigForLimit: str


class GroupIsolationLimitSpec(TypedDict, total=False):
    """Two groups of objects cannot share the same scope item."""

    name: str
    scope: str
    partitionName: str
    limit: Limit
    groupsAllowed: int  # default 1
    filter: Filter


class GroupMoveLimitSpec(TypedDict, total=False):
    """Limit how many objects of the same group can move (container scope only)."""

    name: str
    partitionName: str
    limit: Limit
    sourceScopeItemsAffectingLimitFilter: Filter
    destinationScopeItemsAffectingLimitFilter: Filter
    dimension: str


class MaximizeAllocationSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    filter: Filter


class MaximizeFreeCapacityUnitsSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    unitSize: Limit
    filter: Filter


class ItemsAffinitySpec(TypedDict, total=False):
    scopeItemsOfType1: list[str]
    scopeItemsOfType2: list[str]
    partitionName: str
    scope: str
    dimension: str
    name: str


class LargeShardSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    unassignedScopeItemName: str
    filter: Filter


class MinimizeContainersSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    containerCosts: dict[str, float]
    maxFreeLimit: int
    filter: Filter
    formula: MinimizeContainerSpecFormula  # default "NEW"


class MinimizeMovementSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    magicScaling: bool  # default True
    doNotNormalize: bool  # default False
    allowance: float  # default 0


class MinimizeNthLargestUtilizationSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    n: int
    filter: Filter
    targetUtilization: float


class MinimizeSquaresSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    filter: Filter
    upperBound: float
    lowerBound: float  # default 0
    pieceCount: int  # default 100


class MoveGroupSpec(TypedDict, total=False):
    """Move objects in the same group together across containers."""

    name: str
    partitionName: str


class MoveInProgress(TypedDict, total=False):
    objName: str
    toContainer: str


class MovesInProgressSpec(TypedDict, total=False):
    name: str
    moves: list[MoveInProgress]


class MultipleOrCapacitySpec(TypedDict, total=False):
    """At least one of the contained CapacitySpecs needs to be true."""

    name: str
    capacitySpecs: list[CapacitySpec]


# Forward references for the GenericSpec union (defined after its arms).
class _LogicalOrSpecRef(TypedDict, total=False):
    name: str
    genericSpecs: list[GenericSpec]


class _LogicalAndSpecRef(TypedDict, total=False):
    name: str
    genericSpecs: list[GenericSpec]


class GenericSpec(TypedDict, total=False):
    """Union: exactly one arm must be present.

    Used to nest specs inside ``LogicalOrSpec`` / ``LogicalAndSpec``.
    """

    logicalOrSpec: NotRequired[_LogicalOrSpecRef]
    logicalAndSpec: NotRequired[_LogicalAndSpecRef]
    capacitySpec: NotRequired[CapacitySpec]
    groupCountSpec: NotRequired[GroupCountSpec]
    groupCapacitySpec: NotRequired[GroupCapacitySpec]


LogicalOrSpec = _LogicalOrSpecRef
LogicalAndSpec = _LogicalAndSpecRef


class NonAcceptingSpec(TypedDict, total=False):
    """Scope items not accepting incoming objects."""

    name: str
    scope: str
    items: list[str]


class ObjectAffinity(TypedDict, total=False):
    object0: str
    object1: str
    objectsN: list[str]


class ObjectAffinitiesSpec(TypedDict, total=False):
    name: str
    scope: str
    affinities: list[ObjectAffinity]
    filter: Filter


class PairAffinity(TypedDict, total=False):
    pair: ObjectPair
    affinity: float


class PairAffinitiesSpec(TypedDict, total=False):
    """Pairs of objects that prefer being assigned to the same scope item."""

    name: str
    scope: str
    affinities: list[PairAffinity]
    limit: float  # default 1


class RasRebalancingMovementSpec(TypedDict, total=False):
    name: str
    scope: str
    stayedDimension: str
    incomingDimension: str
    limit: Limit
    filter: Filter


class ScopeAffinitiesSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    affinities: dict[str, float]


class SRBufferCapacitySpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    partitionName: str
    filter: Filter
    scopeItemPairs: list[ScopeItemPair]
    matchingError: float
    lowerBoundMatchingErrors: dict[str, float]
    upperBoundMatchingErrors: dict[str, float]
    addUpperBound: bool
    useHeuristics: bool


class SumOfMaxSpec(TypedDict, total=False):
    name: str
    scope: str
    partitionName: str
    dimension: str
    filter: Filter


class ThrottlingSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    definition: ThrottlingSpecDefinition  # default "ANY"
    limit: Limit
    filter: Filter


class ToFreeSpec(TypedDict, total=False):
    name: str
    containers: list[str]
    dimension: str
    formula: ToFreeSpecFormula  # default "MINIMIZE_TOTAL_UTILIZATION"


class UtilIncreaseCostSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    lowerBound: float
    squares: bool
    filter: Filter


class WorkingUnit(TypedDict, total=False):
    endpoints: list[str]
    weight: float


class WorkingSetSpec(TypedDict, total=False):
    name: str
    scope: str
    dimension: str
    workingUnits: list[WorkingUnit]
    metric: WorkingSetMetric  # default "AVG"


class NestedScopeLimitSpec(TypedDict, total=False):
    name: str
    scope: str
    outerScope: str
    dimension: str
    limit: Limit
    filter: Filter


class RoutingLatencyMetricInfo(TypedDict, total=False):
    type: RoutingLatencyMetric  # default "AVG"
    percentile: float


class RoutingLatencySpec(TypedDict, total=False):
    name: str
    scope: str
    partition: str
    limit: Limit
    routingConfigName: str
    filter: Filter
    includeWeightedAvgLatencyMetricIfLimitViolated: float
    latencyMetric: RoutingLatencyMetricInfo
    metric: RoutingLatencyMetric  # deprecated


class GroupDiversitySpec(TypedDict, total=False):
    name: str
    scope: str
    partition: str
    dimension: str
    limit: Limit
    bound: GroupDiversityBound  # default "MIN"
    filter: Filter


class GroupUtilMultiplier(TypedDict, total=False):
    value: Limit
    target: GroupUtilMultiplierTarget  # default "COMMON"


class CapacityWithGroupPresenceSpec(TypedDict, total=False):
    name: str
    scope: str
    partition: str
    dimension: str
    groupToPresenceWeight: Limit
    scopeItemToLimit: Limit
    bound: CapacityWithGroupPresenceBound  # default "MAX"
    scopeItemFilter: Filter
    roundUpGroupUtilOnScopeItem: bool  # default True
    multiplierList: list[Limit]  # deprecated; prefer groupUtilMultipliers
    aggregationScope: str
    groupToExtraAdditivePenalty: Limit
    groupFilter: Filter
    intent: CapacityWithGroupPresenceUsageIntent  # default "PER_SCOPE_ITEM"
    aggregationPartition: str
    groupUtilMultipliers: list[GroupUtilMultiplier]
    definition: CapacityWithGroupPresenceDefinition  # default "AFTER"


class DiversifyWithinScopeItemSpec(TypedDict, total=False):
    name: str
    scope: str
    partition: str
    dimension: str
    groupToLimit: Limit
    scopeItemFilter: Filter


# ---------------------------------------------------------------------------
# Solver-stage building blocks (SolverSpecs.thrift)
# ---------------------------------------------------------------------------


class CustomEquivalenceSetConfig(TypedDict, total=False):
    goalSelectionConfig: StringListFilterConfig
    constraintSelectionConfig: StringListFilterConfig
    partitionNames: list[str]


class HottestTraversalConfig(TypedDict, total=False):
    pruneOptimalSubgraphs: bool  # default False


class SlidingWindowExecutionConfig(TypedDict, total=False):
    pass


class BatchingExecutionConfig(TypedDict, total=False):
    batchSize: int  # default 32
    maxConcurrency: int  # default 24


class AutoExecutionConfig(TypedDict, total=False):
    slidingWindow: SlidingWindowExecutionConfig
    batching: BatchingExecutionConfig


class ParallelExecutionConfig(TypedDict, total=False):
    """Union: exactly one arm must be present."""

    slidingWindow: NotRequired[SlidingWindowExecutionConfig]
    batching: NotRequired[BatchingExecutionConfig]
    autoExecution: NotRequired[AutoExecutionConfig]


class MinCycleObjectiveImprovementConfig(TypedDict, total=False):
    defaultThreshold: Threshold


class MoveToCurrentScopeItemSpec(TypedDict, total=False):
    scopeNameForExploringMovesToCurrentScopeItem: str


class ScopeItemList(TypedDict, total=False):
    scopeName: str
    scopeItems: list[str]


class GroupToScopeItemList(TypedDict, total=False):
    partitionName: str
    groupToScopeItemList: dict[str, ScopeItemList]


class MoveToScopeItemsSpec(TypedDict, total=False):
    defaultScopeItems: ScopeItemList
    objectToScopeItems: dict[str, ScopeItemList]
    scopeItemsPerGroups: GroupToScopeItemList


class DestinationsToExploreOptions(TypedDict, total=False):
    """Union: exactly one arm must be present."""

    moveToCurrentScopeItem: NotRequired[MoveToCurrentScopeItemSpec]
    moveToScopeItems: NotRequired[MoveToScopeItemsSpec]
    destinationToExploreName: NotRequired[str]


class GroupList(TypedDict, total=False):
    partitionName: str


class ObjectsFromGroupsSpec(TypedDict, total=False):
    groupList: GroupList
    bundleSize: int


class ObjectsToExploreOptions(TypedDict, total=False):
    """Union: exactly one arm must be present."""

    objectsFromGroupsSpec: NotRequired[ObjectsFromGroupsSpec]


class SampleSize(TypedDict, total=False):
    defaultSampleSize: int
    objectToSampleSize: dict[str, int]


class StringKeyValueMap(TypedDict, total=False):
    defaultValue: str
    value: dict[str, str]


# Move-type spec building blocks
class SingleMoveTypeSpec(TypedDict, total=False):
    pass


class SingleFastMoveTypeSpec(TypedDict, total=False):
    destinationsToExplore: DestinationsToExploreOptions
    minHotObjects: int  # default 1


class SingleGreedyMoveTypeSpec(TypedDict, total=False):
    pass


class SwapFullWithEmptyContainersMoveTypeSpec(TypedDict, total=False):
    pass


class SwapNMoveTypeSpec(TypedDict, total=False):
    swapNConcurrentObjects: int  # default 1
    swapNSourceObjects: list[str]
    swapNDestinationScope: list[list[str]]
    swapNIterations: int  # default 1000000


class SingleRandomBatchesMoveTypeSpec(TypedDict, total=False):
    randomContainerBatchSize: int  # default 10


class SwapFullContainersMoveTypeSpec(TypedDict, total=False):
    pass


class SingleEndChainMoveTypeSpec(TypedDict, total=False):
    pass


class ObjectBundleFormationHints(TypedDict, total=False):
    scopeItemToObjectsToExploreOptions: dict[str, ObjectsToExploreOptions]
    scopeName: str
    adjustBundleSizeForIncompleteBundles: bool


class SwapMoveTypeSpec(TypedDict, total=False):
    partitionNameToExploreSwapsWithinObjectGroup: str
    greedyOnSrc: bool  # default False
    greedyOnDst: bool  # default False
    destinationsToExplore: DestinationsToExploreOptions
    sampleSize: SampleSize
    swapRatioDimension: StringKeyValueMap
    objectBundleFormationHints: ObjectBundleFormationHints


class TripleLoopMoveTypeSpec(TypedDict, total=False):
    pass


class KLSearchMoveTypeSpec(TypedDict, total=False):
    pass


class GroupRoutingMoveTypeSpec(TypedDict, total=False):
    routingConfigName: str
    unassignedContainer: str


class SingleChainMoveTypeSpec(TypedDict, total=False):
    partitionNameToExploreChainsWithinObjectGroup: str
    specialColdContainer: str


class SingleChainFastMoveTypeSpec(TypedDict, total=False):
    partitionNameToExploreFastChainsWithinObjectGroup: str
    specialFastColdContainer: str


class FixedDestMoveTypeSpec(TypedDict, total=False):
    sampleSize: SampleSize
    specialContainer: str
    objectBundleFormationHints: ObjectBundleFormationHints


class GroupOccupancySampling(TypedDict, total=False):
    partition: str
    scope: str
    scopeItem: str


class RasLocalSearchMetadata(TypedDict, total=False):
    defaultMultiMoveBundleSize: int  # default 1
    multiMoveBundleSizePerContainer: dict[str, list[int]]
    searchSpacePartition: str
    serverPartition: str
    searchSpaceGroupsPerContainer: dict[str, list[str]]
    sampleSizePerSearchSpaceGroup: int  # default 5
    useMostGranularSearchSpace: bool  # default False
    objectivesToSplitSearchSpacePartition: list[str]
    constraintsToSplitSearchSpacePartition: list[str]
    enableExperimentalLocalSearchStages: bool  # default False
    gpuBundlePartitionNamePerBundleSize: dict[int, str]
    conflictContainersPerContainer: dict[str, list[str]]
    gpuContainerNames: list[str]
    resourceLimtConstraintNames: list[str]
    reservationCapacityShapeScopeName: str
    useAdaptiveAllotments: bool  # default False
    objectivesToSplitSearchSpacePartitionDenyList: list[str]
    constraintsToSplitSearchSpacePartitionDenyList: list[str]
    objectBundleFormationhints: ObjectBundleFormationHints
    swapRatioDimension: StringKeyValueMap
    physicalGpuOverlapPartitionName: str
    groupOccupancySampling: GroupOccupancySampling


class FixedSrcMultiMoveTypeSpec(TypedDict, total=False):
    maxSamplesPerEquivSet: int  # default 5
    specialContainer: str
    rasLocalSearchMetadata: RasLocalSearchMetadata


class FixedDestMultiMoveTypeSpec(TypedDict, total=False):
    maxSamplesPerEquivSet: int  # default 5
    specialContainer: str
    rasLocalSearchMetadata: RasLocalSearchMetadata


class FixedDestSwapMultiMoveTypeSpec(TypedDict, total=False):
    greedyOnSrc: bool  # default False
    maxSamplesPerEquivSet: int  # default 5
    maxSampleSizeOnSrc: int
    maxSampleSizeOnDst: int
    specialContainer: str
    rasLocalSearchMetadata: RasLocalSearchMetadata


class SingleRandomStratifiedMoveTypeSpec(TypedDict, total=False):
    destinationsToExplore: DestinationsToExploreOptions
    stratifiedSampleSize: SampleSize
    minObjectsToExplore: int  # default 1


class SingleFixedSourceMoveTypeSpec(TypedDict, total=False):
    scopeItemList: ScopeItemList
    stopEarlyAtScopeItemThatImprovesObjective: bool  # default False
    specialContainer: str
    sampleSize: SampleSize
    objectBundleFormationHints: ObjectBundleFormationHints


class SingleRandomObjectStratifiedMoveTypeSpec(TypedDict, total=False):
    stratifiedSampleSize: SampleSize
    objectsToExploreOptions: ObjectsToExploreOptions


class SecondaryGroupReplacementConfig(TypedDict, total=False):
    secondaryGroupToAllowedReplacements: dict[str, list[str]]


class MoveStrategy(TypedDict, total=False):
    type: MoveStrategyType
    moveSetsGeneratedPerScopeItem: int  # default 1
    moveToScopeItems: MoveToScopeItemsSpec
    tertiaryPartition: str
    numScopeItemsToExplorePerTertiaryGroup: int
    distinctScopeItemsAcrossTertiaryGroups: bool  # default False


class MoveStrategies(TypedDict, total=False):
    groupToMoveStrategy: dict[str, MoveStrategy]


class GroupMoveWithHintStrategiesMoveTypeSpec(TypedDict, total=False):
    primaryPartition: str
    secondaryPartition: str
    moveStrategies: MoveStrategies
    unassignedContainer: str
    secondaryGroupReplacementConfig: SecondaryGroupReplacementConfig


class ReplicaDropMoveTypeSpec(TypedDict, total=False):
    replicaDropPartition: str
    replicaDropScope: str


class GreedyGroupToScopeItemMoveTypeSpec(TypedDict, total=False):
    scopeItemMovesScope: str
    groupMovesPartition: str
    nSampleSetsToExplore: int  # default 2


class ColocateGroupsMoveTypeRelatedGroupsInfo(TypedDict, total=False):
    relatedGroups: list[str]
    destinationScopeItems: list[str]


class ColocateGroupsMoveTypeSpec(TypedDict, total=False):
    partitionName: str
    relatedGroupsList: list[ColocateGroupsMoveTypeRelatedGroupsInfo]
    colocationScopeName: str
    colocationScopeItemToGroupToContainers: dict[str, dict[str, list[str]]]
    defaultSampleSize: int


class MoveTypeSpec(TypedDict, total=False):
    """Union: exactly one arm must be present.

    The string-name variant ``moveTypeName`` is supported for legacy reasons.
    """

    singleMoveTypeSpec: NotRequired[SingleMoveTypeSpec]
    swapMoveTypeSpec: NotRequired[SwapMoveTypeSpec]
    tripleLoopMoveTypeSpec: NotRequired[TripleLoopMoveTypeSpec]
    klSearchMoveTypeSpec: NotRequired[KLSearchMoveTypeSpec]
    fixedDestMoveTypeSpec: NotRequired[FixedDestMoveTypeSpec]
    fixedSrcMultiMoveTypeSpec: NotRequired[FixedSrcMultiMoveTypeSpec]
    fixedDestMultiMoveTypeSpec: NotRequired[FixedDestMultiMoveTypeSpec]
    fixedDestSwapMultiMoveTypeSpec: NotRequired[FixedDestSwapMultiMoveTypeSpec]
    singleRandomObjectStratifiedMoveTypeSpec: NotRequired[
        SingleRandomObjectStratifiedMoveTypeSpec
    ]
    groupRoutingMoveTypeSpec: NotRequired[GroupRoutingMoveTypeSpec]
    singleChainMoveTypeSpec: NotRequired[SingleChainMoveTypeSpec]
    singleChainFastMoveTypeSpec: NotRequired[SingleChainFastMoveTypeSpec]
    singleFixedSourceMoveTypeSpec: NotRequired[SingleFixedSourceMoveTypeSpec]
    singleFastMoveTypeSpec: NotRequired[SingleFastMoveTypeSpec]
    groupMoveWithHintStrategiesMoveTypeSpec: NotRequired[
        GroupMoveWithHintStrategiesMoveTypeSpec
    ]
    singleGreedyMoveTypeSpec: NotRequired[SingleGreedyMoveTypeSpec]
    swapNMoveTypeSpec: NotRequired[SwapNMoveTypeSpec]
    singleRandomBatchesMoveTypeSpec: NotRequired[SingleRandomBatchesMoveTypeSpec]
    swapFullContainersMoveTypeSpec: NotRequired[SwapFullContainersMoveTypeSpec]
    swapFullWithEmptyContainersMoveTypeSpec: NotRequired[
        SwapFullWithEmptyContainersMoveTypeSpec
    ]
    singleEndChainMoveTypeSpec: NotRequired[SingleEndChainMoveTypeSpec]
    replicaDropMoveTypeSpec: NotRequired[ReplicaDropMoveTypeSpec]
    colocateGroupsMoveTypeSpec: NotRequired[ColocateGroupsMoveTypeSpec]
    greedyGroupToScopeItemMoveTypeSpec: NotRequired[GreedyGroupToScopeItemMoveTypeSpec]
    singleRandomStratifiedMoveTypeSpec: NotRequired[SingleRandomStratifiedMoveTypeSpec]
    moveTypeName: NotRequired[str]


# Solver specs
class LocalSearchSolverSpec(TypedDict, total=False):
    """Configures local-search solving.

    Many fields are deprecated and kept only for replaying saved scenarios;
    new code should use ``moveTypeList`` rather than the individual move-type
    fields.
    """

    allowedPlateauTime: int
    constrainedBoundsCheckMs: int
    enableConstrainedBoundsOptimization: bool
    minHotObjects: int  # default 1
    objectivesForHottestContainers: int
    objectOrderingDimension: str
    randomSeed: int
    solveTime: int
    stopAfterMoves: int
    stratifiedSampleSize: int  # default 10
    timePerMove: float
    recomputeContainerOrderingAfterEveryMove: bool  # default True
    includeEqualSizeRandomSampleForSingleColdestMoveType: bool  # default False
    singleRandomStratifiedMoveTypeSpec: SingleRandomStratifiedMoveTypeSpec
    enableObjectPotentialSorting: bool
    exploreMovesFromContainersNotInObjective: bool  # default True
    customEquivalenceSetConfig: CustomEquivalenceSetConfig
    moveTypeList: list[MoveTypeSpec]
    hottestTraversalConfig: HottestTraversalConfig
    minCycleObjectiveImprovement: MinCycleObjectiveImprovementConfig
    parallelExecutionConfig: ParallelExecutionConfig
    # Deprecated fields below — present so that saved scenarios still validate.
    moveTypes: list[str]
    randomContainerBatchSize: int
    swapNConcurrentObjects: int
    swapNSourceObjects: list[str]
    swapNDestinationScope: list[list[str]]
    swapNIterations: int
    replicaDropPartition: str
    replicaDropScope: str
    specialContainer: str
    scopeItemMovesScope: str
    groupMovesPartition: str
    nSampleSetsToExplore: int
    fixedSrcDstMoveTypeSampleSize: int
    groupRoutingMoveTypeSpec: GroupRoutingMoveTypeSpec
    singleChainMoveTypeSpec: SingleChainMoveTypeSpec
    rasLocalSearchMetadata: RasLocalSearchMetadata
    singleChainFastMoveTypeSpec: SingleChainFastMoveTypeSpec
    singleFixedSourceMoveTypeSpec: SingleFixedSourceMoveTypeSpec


class LocalSearchStageSpec(TypedDict, total=False):
    name: str
    begin: int
    end: int
    minRuntimeSec: int
    stopAfterMovesTillStage: int
    solverSpec: LocalSearchSolverSpec
    higherPriorityObjConfig: HigherPriorityObjectivesConfig


class MultiStageConfig(TypedDict, total=False):
    stageIds: list[int]
    moveLimit: int
    solveTime: float


class LocalSearchStageSolverSpec(TypedDict, total=False):
    stageSpecs: list[LocalSearchStageSpec]
    solveTime: int
    stopAfterMoves: int
    recomputeContainerOrderingAfterEveryMove: bool
    exploreMovesFromContainersNotInObjective: bool
    multiStageConfigs: list[MultiStageConfig]
    customEquivalenceSetConfig: CustomEquivalenceSetConfig
    hottestTraversalConfig: HottestTraversalConfig
    parallelExecutionConfig: ParallelExecutionConfig


class SolverOutputFiles(TypedDict, total=False):
    solverLogFile: str
    solverProblemFile: str


class MultiObjectiveSolveSettings(TypedDict, total=False):
    solveType: MultiObjectiveSolveType  # default "HIERARCHICAL"
    firstObjectiveIdx: int
    lastObjectiveIdx: int
    higherPriorityObjConfig: HigherPriorityObjectivesConfig
    paramNamesToValues: dict[str, PerObjectiveValue]


class OptimalSolverSpec(TypedDict, total=False):
    """Configures the MIP-based exact solver."""

    printFullLp: bool  # default False
    skipInitialAssignmentHint: bool  # default False
    solveTime: int
    suppressLogs: bool  # default False
    xpressArgs: dict[str, float]
    xpressLogFile: str  # deprecated; use solverOutputFiles
    xpressTolerance: float  # default 1e-8
    skipMipSolveForTesting: bool  # default False
    solverPackage: OptimalSolverPackage  # default "XPRESS"
    lpExprSubstitution: bool  # default False
    enablePartitionHeuristic: bool  # default False
    simplifyLpProblem: bool  # default False
    solverOutputFiles: SolverOutputFiles
    multiObjSolveSettings: MultiObjectiveSolveSettings


class OptimalSubsetSolverSpec(TypedDict, total=False):
    alwaysChosenContainers: list[str]
    containerChoice: dict[str, float]
    maxSubproblemErrors: int  # default 1
    maxSubsetRuns: int
    optimalConfig: OptimalSolverSpec
    overallTime: float  # default 0
    perSubsetTime: float  # default 0.5
    suppressLogs: bool
    assignmentVarBudget: int
    regrettableMoves: dict[str, str]


# ---------------------------------------------------------------------------
# Standalone setters / params (Types.thrift, ProblemSolver.thrift)
# ---------------------------------------------------------------------------


class ConstraintParams(TypedDict, total=False):
    """Default constraint penalties; per-constraint overrides win.

    ``invalidCost`` weights incrementally improving a broken constraint;
    ``invalidState`` weights fully fixing one (set to 0 to disable the step).
    """

    invalidCost: float  # default 100
    invalidState: float  # default 10000
    tuplePosIfBroken: int  # default 0


class MoveStatsSpec(TypedDict, total=False):
    trackContainers: bool  # default False
    trackObjects: bool  # default False
    trackObjectsWhitelist: list[str]
    printTrackedObjectStats: bool  # default False
    printSourceContainersWhitelist: list[str]
    showAllChangedObjectivesInMovesSummary: bool  # default False


class ManifoldBackupParams(TypedDict, total=False):
    uploadPolicy: ManifoldUploadPolicy  # default "ON_FAILURE"
    expectedRuntime: int


class RoutingRing(TypedDict, total=False):
    originScopeItem: str
    originTraffic: float
    destinationScopeItemSets: list[list[str]]


class GroupRoutingRings(TypedDict, total=False):
    """All routing rings applicable to one group (replica set)."""

    routingRings: list[RoutingRing]


# ---------------------------------------------------------------------------
# Public union shims (ProblemSolver.thrift)
# ---------------------------------------------------------------------------


class ConstraintSpec(TypedDict, total=False):
    """Union shim: the ``add_constraint(spec=...)`` argument shape.

    Exactly one arm key must be present at runtime; the binding raises if zero
    or multiple arms are set.
    """

    aggregatedGroupSpec: NotRequired[AggregatedGroupSpec]
    avoidAssignmentsSpec: NotRequired[AvoidAssignmentsSpec]
    avoidMovingSpec: NotRequired[AvoidMovingSpec]
    bipartiteSwapsSpec: NotRequired[BipartiteSwapsSpec]
    capacityRatioSpec: NotRequired[CapacityRatioSpec]
    capacitySpec: NotRequired[CapacitySpec]
    capacityWithSupplyAndDrSpec: NotRequired[CapacityWithSupplyAndDrSpec]
    colocateGroupsSpec: NotRequired[ColocateGroupsSpec]
    disasterRecoveryCapacitySpec: NotRequired[DisasterRecoveryCapacitySpec]
    drainCapacitySpec: NotRequired[DrainCapacitySpec]
    exclusiveObjectsSpec: NotRequired[ExclusiveObjectsSpec]
    exclusiveScopeItemsSpec: NotRequired[ExclusiveScopeItemsSpec]
    exclusiveSwapsSpec: NotRequired[ExclusiveSwapsSpec]
    flowSpec: NotRequired[FlowSpec]
    groupCapacitySpec: NotRequired[GroupCapacitySpec]
    groupCountSpec: NotRequired[GroupCountSpec]
    groupIsolationLimitSpec: NotRequired[GroupIsolationLimitSpec]
    groupMoveLimitSpec: NotRequired[GroupMoveLimitSpec]
    moveGroupSpec: NotRequired[MoveGroupSpec]
    movesInProgressSpec: NotRequired[MovesInProgressSpec]
    multipleOrCapacitySpec: NotRequired[MultipleOrCapacitySpec]
    nonAcceptingSpec: NotRequired[NonAcceptingSpec]
    objectAffinitiesSpec: NotRequired[ObjectAffinitiesSpec]
    pairAffinitiesSpec: NotRequired[PairAffinitiesSpec]
    rasRebalancingMovementSpec: NotRequired[RasRebalancingMovementSpec]
    srBufferCapacitySpec: NotRequired[SRBufferCapacitySpec]
    throttlingSpec: NotRequired[ThrottlingSpec]
    toFreeSpec: NotRequired[ToFreeSpec]
    minimizeMovementSpec: NotRequired[MinimizeMovementSpec]
    exclusiveGroupsSpec: NotRequired[ExclusiveGroupsSpec]
    nestedScopeLimitSpec: NotRequired[NestedScopeLimitSpec]
    routingLatencySpec: NotRequired[RoutingLatencySpec]
    groupDiversitySpec: NotRequired[GroupDiversitySpec]
    logicalOrSpec: NotRequired[LogicalOrSpec]
    logicalAndSpec: NotRequired[LogicalAndSpec]
    capacityWithGroupPresenceSpec: NotRequired[CapacityWithGroupPresenceSpec]


class GoalSpec(TypedDict, total=False):
    """Union shim: the ``add_goal(spec=...)`` argument shape.

    Exactly one arm key must be present at runtime.
    """

    aggregatedGroupSpec: NotRequired[AggregatedGroupSpec]
    assignmentAffinitiesSpec: NotRequired[AssignmentAffinitiesSpec]
    balanceV2Spec: NotRequired[BalanceV2Spec]
    capacityRatioSpec: NotRequired[CapacityRatioSpec]
    capacitySpec: NotRequired[CapacitySpec]
    capacityWithSupplyAndDrSpec: NotRequired[CapacityWithSupplyAndDrSpec]
    colocateGroupsSpec: NotRequired[ColocateGroupsSpec]
    disasterRecoveryCapacitySpec: NotRequired[DisasterRecoveryCapacitySpec]
    drainCapacitySpec: NotRequired[DrainCapacitySpec]
    exclusiveObjectsSpec: NotRequired[ExclusiveObjectsSpec]
    flowSpec: NotRequired[FlowSpec]
    groupCountSpec: NotRequired[GroupCountSpec]
    groupIsolationLimitSpec: NotRequired[GroupIsolationLimitSpec]
    maximizeAllocationSpec: NotRequired[MaximizeAllocationSpec]
    maximizeFreeCapacityUnitsSpec: NotRequired[MaximizeFreeCapacityUnitsSpec]
    minimizeContainersSpec: NotRequired[MinimizeContainersSpec]
    minimizeMovementSpec: NotRequired[MinimizeMovementSpec]
    minimizeNthLargestUtilizationSpec: NotRequired[MinimizeNthLargestUtilizationSpec]
    minimizeSquaresSpec: NotRequired[MinimizeSquaresSpec]
    pairAffinitiesSpec: NotRequired[PairAffinitiesSpec]
    scopeAffinitiesSpec: NotRequired[ScopeAffinitiesSpec]
    sumOfMaxSpec: NotRequired[SumOfMaxSpec]
    utilIncreaseCostSpec: NotRequired[UtilIncreaseCostSpec]
    workingSetSpec: NotRequired[WorkingSetSpec]
    exclusiveGroupsSpec: NotRequired[ExclusiveGroupsSpec]
    balanceSpec: NotRequired[BalanceSpec]
    groupCapacitySpec: NotRequired[GroupCapacitySpec]
    toFreeSpec: NotRequired[ToFreeSpec]
    itemsAffinitySpec: NotRequired[ItemsAffinitySpec]
    largeShardSpec: NotRequired[LargeShardSpec]
    groupAssignmentAffinitiesSpec: NotRequired[GroupAssignmentAffinitiesSpec]
    routingLatencySpec: NotRequired[RoutingLatencySpec]
    groupDiversitySpec: NotRequired[GroupDiversitySpec]
    srBufferCapacitySpec: NotRequired[SRBufferCapacitySpec]
    capacityWithGroupPresenceSpec: NotRequired[CapacityWithGroupPresenceSpec]
    diversifyWithinScopeItemSpec: NotRequired[DiversifyWithinScopeItemSpec]
    exclusiveScopeItemsSpec: NotRequired[ExclusiveScopeItemsSpec]


class SolverSpec(TypedDict, total=False):
    """Union shim: the ``add_solver(spec=...)`` argument shape."""

    localSearchSolverSpec: NotRequired[LocalSearchSolverSpec]
    localSearchStageSolverSpec: NotRequired[LocalSearchStageSolverSpec]
    optimalSolverSpec: NotRequired[OptimalSolverSpec]
    optimalSubsetSolverSpec: NotRequired[OptimalSubsetSolverSpec]


# ConstraintPolicy is a Thrift enum but the binding accepts the JSON-string
# form. We expose the string Literal directly under the public name so callers
# can write ``set_constraint_policy("HARD")``.
ConstraintPolicy = ConstraintPolicyName


# ---------------------------------------------------------------------------
# Result-side types: AssignmentSolution and supporting structs
# (Types.thrift / Metrics.thrift). Exposed so callers can annotate the value
# returned by ``solve()``.
# ---------------------------------------------------------------------------


class SingleObjectiveSummary(TypedDict, total=False):
    name: str
    desc: str
    weight: float
    value: float


class ObjectiveSummary(TypedDict, total=False):
    solved: bool
    value: float
    objs: list[SingleObjectiveSummary]


class GlobalObjectiveSummary(TypedDict, total=False):
    solved: bool
    goals: list[ObjectiveSummary]


class SingleConstraintSummary(TypedDict, total=False):
    name: str
    desc: str
    value: float


class ConstraintSummary(TypedDict, total=False):
    solved: bool
    brokenVal: float
    brokenCount: int
    constraints: list[SingleConstraintSummary]


class GlobalObjectiveValueChange(TypedDict, total=False):
    newValue: float
    change: float
    tuplePos: int


class SingleMove(TypedDict, total=False):
    object: str
    srcContainer: str
    dstContainer: str


class MovesSummary(TypedDict, total=False):
    moves: list[SingleMove]
    objectives: dict[str, GlobalObjectiveValueChange]
    evalsCount: int
    constraintInvldCnt: dict[str, int]
    stageId: int
    cycleId: int


class SolverEvalStats(TypedDict, total=False):
    avgEvalSpeed: float
    numEvals: int
    invalidEvalsPct: float
    worseEvalsPct: float
    neutralEvalsPct: float
    betterEvalsPct: float
    numEvalTimeouts: int
    evalDurationSecs: float
    findWorstDurationSecs: float
    numCycles: int
    numPrunedEvals: int


class SolverMoveStats(TypedDict, total=False):
    durationSecs: float
    numMoves: int
    movesHistogram: list[int]
    applyDurationSecs: float
    moveTypes: list[str]


class FinalEvaluationStats(TypedDict, total=False):
    totalCount: int
    brokenConstraints: dict[str, int]
    worstObjectives: dict[str, int]
    nonAcceptingContainersCount: int
    fixedContainersCount: int
    fixedObjectsCount: int


class FinalEvaluationSummary(TypedDict, total=False):
    globalStats: FinalEvaluationStats
    sourceContainerStats: dict[str, FinalEvaluationStats]
    destinationContainerStats: dict[str, FinalEvaluationStats]
    objectStats: dict[str, FinalEvaluationStats]


class StageSummary(TypedDict, total=False):
    name: str
    duration: float  # default 0
    endReason: EndReason
    moveStats: SolverMoveStats
    finalEvaluationSummary: FinalEvaluationSummary
    evalStats: SolverEvalStats
    auxEndInfo: str


class SolverReport(TypedDict, total=False):
    endReason: EndReason
    evalStats: SolverEvalStats
    moveStats: SolverMoveStats
    stagesSummaries: list[StageSummary]


class OptimalGap(TypedDict, total=False):
    relative: float
    absolute: float


class SolverStatsForObjective(TypedDict, total=False):
    gap: OptimalGap
    solverFoundSolution: bool


class OptimalSolverProfile(TypedDict, total=False):
    xpressBuildSec: float  # default 0
    xpressApplyInitialStateSec: float  # default 0
    xpressMipOptimizeSec: list[float]
    postProcessingSec: float  # default 0
    gap: OptimalGap
    isWarmStartSuccessful: bool
    warmStartProcessingTimeSec: int
    noFeasibleSolution: bool  # default False
    dynamicContainers: int
    dynamicEquivalenceSets: int
    solverStatsForObjectives: list[SolverStatsForObjective]


class MoveTypeEvent(TypedDict, total=False):
    moveTypeIndex: int
    duration: float
    initialValue: float
    finalValue: float


class LocalSearchProfile(TypedDict, total=False):
    moveTypeNames: list[str]
    moveTypeEvents: list[MoveTypeEvent]


class HierarchicalProfileNode(TypedDict, total=False):
    eventName: str
    duration: float
    children: list[HierarchicalProfileNode]
    maxInclusiveMemory: float


class ProblemProfile(TypedDict, total=False):
    materializationSec: float
    solveSec: float
    optimalSolverProfile: OptimalSolverProfile
    localSearchProfiles: list[LocalSearchProfile]
    hierarchicalProfileRoot: HierarchicalProfileNode


class ExclusiveGroupsTagging(TypedDict, total=False):
    scopeItemToGroup: dict[str, str]


class SpecMetadata(TypedDict, total=False):
    specName: str
    exclusiveGroupsTagging: ExclusiveGroupsTagging


class EquivalenceSetMetadata(TypedDict, total=False):
    name: str
    objectNames: list[str]


class EquivalenceSetInfo(TypedDict, total=False):
    equivalenceSets: list[EquivalenceSetMetadata]


class ContainerAssignment(TypedDict, total=False):
    objectsPerContainer: dict[str, dict[str, int]]


# Metrics (Metrics.thrift)
class GroupUtils(TypedDict, total=False):
    groupToValue: dict[str, float]


class PartitionUtils(TypedDict, total=False):
    partitionToGroupUtils: dict[str, GroupUtils]


class ScopeItemUtils(TypedDict, total=False):
    scopeItemToValue: dict[str, float]
    scopeItemToPartitionUtils: dict[str, PartitionUtils]


class DimensionUtils(TypedDict, total=False):
    dimensionToScopeItemUtils: dict[str, ScopeItemUtils]


class ScopeUtils(TypedDict, total=False):
    scopeToDimensionUtils: dict[str, DimensionUtils]


class SourceTrafficMetrics(TypedDict, total=False):
    sourceToDestinationTraffic: dict[str, ScopeItemUtils]


class GroupTrafficMetrics(TypedDict, total=False):
    groupToSourceTraffic: dict[str, SourceTrafficMetrics]


class LatencyMetricValue(TypedDict, total=False):
    metric: RoutingLatencyMetricInfo
    value: float


class GroupLatencyMetrics(TypedDict, total=False):
    groupToMetricValues: dict[str, list[LatencyMetricValue]]


class Metrics(TypedDict, total=False):
    utilMetricToScopeUtils: dict[str, ScopeUtils]
    routingConfigToGroupTrafficMetrics: dict[str, GroupTrafficMetrics]
    routingConfigToGroupLatencyMetrics: dict[str, GroupLatencyMetrics]


class AssignmentSolution(TypedDict, total=False):
    """The dict returned by :py:meth:`rebalancer.ProblemSolver.solve`.

    Object-to-container assignments live under ``assignment``;
    ``compactAssignment`` gives the inverse view (container -> object -> count).
    Initial vs final objective/constraint summaries appear in the
    ``initial*`` / ``final*`` fields.  ``initialMetrics`` and ``finalMetrics``
    are populated when ``publish_metrics()`` was enabled before ``solve()``.
    """

    assignment: dict[str, str]
    initialObjective: ObjectiveSummary
    initialConstraint: ConstraintSummary
    finalObjective: ObjectiveSummary
    finalConstraint: ConstraintSummary
    movesSummary: list[MovesSummary]
    solverSummaries: list[SolverReport]
    finalEvaluationSummary: FinalEvaluationSummary
    initialAssignment: dict[str, str]
    runId: str
    initialGlobalObjective: GlobalObjectiveSummary
    finalGlobalObjective: GlobalObjectiveSummary
    problemProfile: ProblemProfile
    numContainers: int
    specNameToMetadata: dict[str, SpecMetadata]
    initialMetrics: Metrics
    finalMetrics: Metrics
    equivalenceSetInfo: EquivalenceSetInfo
    compactAssignmentInitial: ContainerAssignment
    compactAssignment: ContainerAssignment


# ``Any`` is re-exported so callers writing custom helpers can build dicts that
# match these TypedDicts without an additional ``typing`` import.
__all__: list[str] = [
    name
    for name in globals()
    if not name.startswith("_")
    and name not in {"Any", "Literal", "NotRequired", "TypedDict", "annotations"}
]
