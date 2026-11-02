# Rebalancer Spec Catalog

Complete reference of all rebalancer spec types, their fields, enums, and whether they can be used as constraints, goals, or both.

Source: `algopt/rebalancer/interface/thrift/ProblemSpecs.thrift` and `ProblemSolver.thrift`

## Quick Lookup: Spec Usage

### Constraints Only
- `ExclusiveScopeItemsSpec`, `LogicalOrSpec`, `LogicalAndSpec`

### Goals Only
- `AssignmentAffinitiesSpec`, `MaximizeAllocationSpec`, `MaximizeFreeCapacityUnitsSpec`, `MinimizeContainersSpec`, `MinimizeNthLargestUtilizationSpec`, `MinimizeSquaresSpec`, `ScopeAffinitiesSpec`, `SumOfMaxSpec`, `UtilIncreaseCostSpec`, `WorkingSetSpec`, `ItemsAffinitySpec`, `LargeShardSpec`, `GroupAssignmentAffinitiesSpec`

### Both Constraints AND Goals
- `AggregatedGroupSpec`, `AvoidAssignmentsSpec`, `AvoidMovingSpec`, `BalanceSpec`, `BalanceV2Spec`, `BipartiteSwapsSpec`, `CapacityRatioSpec`, `CapacitySpec`, `CapacityWithGroupPresenceSpec`, `CapacityWithSupplyAndDrSpec`, `ColocateGroupsSpec`, `DisasterRecoveryCapacitySpec`, `DiversifyWithinScopeItemSpec`, `DrainCapacitySpec`, `ExclusiveGroupsSpec`, `ExclusiveObjectsSpec`, `FlowSpec`, `GroupCapacitySpec`, `GroupCountSpec`, `GroupDiversitySpec`, `GroupIsolationLimitSpec`, `MinimizeMovementSpec`, `NestedScopeLimitSpec`, `PairAffinitiesSpec`, `RasRebalancingMovementSpec`, `RoutingLatencySpec`, `SRBufferCapacitySpec`, `ThrottlingSpec`, `ToFreeSpec`

---

## Common Spec Types (Most Frequently Seen in Debugging)

### CapacitySpec

Bounds utilization on scope items. The most common spec type.

```thrift
struct CapacitySpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: CapacitySpecDefinition definition = CapacitySpecDefinition.AFTER;
  5: CapacitySpecBound bound = CapacitySpecBound.MAX;
  6: Limit limit;
  7: Filter filter;
  10: bool zeroAllowed;
  11: bool useLegacyFormula = false;
  12: optional UtilizationBound utilizationBound;
  13: optional GroupBundleUtilization groupBundleUtilization;
}
```

`groupBundleUtilization` counts only complete, per-partition-group bundles for
configured scope items; unconfigured items retain raw utilization. It requires
an `AFTER` definition, a disjoint partition, and cannot be combined with
`utilizationBound` or `zeroAllowed`.

**`CapacitySpecDefinition`** — what is measured:

| Value | Name | Meaning |
|-------|------|---------|
| 1 | `AFTER` | Utilization after all moves are applied |
| 2 | `DURING_AND_AFTER` | Utilization during and after moves (accounts for transient overload) |
| 3 | `DURING` | Utilization only during moves (transient state) |
| 4 | `DOUBLE_DURING_AND_AFTER` | Double utilization during and after |
| 5 | `DOUBLE_DURING` | Double utilization during moves |
| 6 | `NEW` | Utilization from newly assigned objects only |
| 7 | `OLD` | Utilization from objects in their original assignment |
| 8 | `MOVED_DATA` | Utilization from only the data that moved — affects BOTH source and destination containers. Formula: `afterExpr + initialExpr - 2 * stayedExpr`. Includes moves in progress. |

**`CapacitySpecBound`** — direction of the limit:

| Value | Name | Meaning |
|-------|------|---------|
| 1 | `MAX` | Upper bound — utilization must not exceed the limit. Violation = `max(0, utilization - limit)` |
| 2 | `MIN` | Lower bound — utilization must not fall below the limit. Violation = `max(0, limit - utilization)` |

### BalanceSpec

Balance utilization of a resource across items of a scope.

```thrift
struct BalanceSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: double upperBound = 1;
  5: optional double softUpperBound;
  6: BalanceSpecBoundType boundType = BalanceSpecBoundType.RELATIVE;
  7: BalanceSpecFormula formula = BalanceSpecFormula.LINEAR;
  8: Filter filter;
  9: BalanceSpecDefinition definition = BalanceSpecDefinition.AFTER;
  10: bool fixAverageToInitial;
  11: list<string> includeInInitialAverage;
  12: bool useLegacyAverage = false;
  13: bool ignoreUpperBoundForIdealWithAbsOrRelBoundTypes = true;
}
```

**`BalanceSpecBoundType`**:

| Value | Name | Meaning |
|-------|------|---------|
| 1 | `ABSOLUTE` | Absolute offset added to the average relative utilization |
| 2 | `RELATIVE` | Multiplier of the average utilization. threshold = upperBound × average |
| 3 | `RELATIVE_UTIL` | Relative utilization threshold |

**`BalanceSpecFormula`**:

| Value | Name | Meaning |
|-------|------|---------|
| 1 | `LINEAR` | Sum of excess utilization above threshold |
| 2 | `SQUARES` | Sum of squared excess (penalizes large outliers more) |
| 3 | `MAX` | Maximum excess across all scope items |
| 4 | `IDEAL` | Target ideal utilization |
| 5 | `LEGACY` | Legacy formula |
| 6 | `PURE_BALANCE` | Variance-based: `n × Var(relUtil)`. Supports RELATIVE_UTIL bound. |

**`BalanceSpecDefinition`**:

| Value | Name | Meaning |
|-------|------|---------|
| 1 | `AFTER` | Utilization after moves |
| 2 | `DURING` | Utilization during moves |
| 3 | `NEW` | Utilization from new objects only |
| 4 | `OLD` | Utilization from original objects only |

### GroupMoveLimitSpec

Limits how many objects of the same group can move.

```thrift
struct GroupMoveLimitSpec {
  1: string name;
  2: string partitionName;
  3: Limit limit = {"type": LimitType.ABSOLUTE};
  4: Filter sourceScopeItemsAffectingLimitFilter;
  5: Filter destinationScopeItemsAffectingLimitFilter;
  6: optional string dimension;
}
```

A move contributes to the limit only if BOTH source and destination containers pass their respective filters. If no dimension is specified, defaults to `{object}_count`.

### ColocateGroupsSpec

Controls how many scope items a group can spread across.

```thrift
struct ColocateGroupsSpec {
  1: string name;
  2: string scope;
  3: string partitionName;
  4: map<string, double> scopeItemWeights;
  5: Filter filter;
  7: ColocateGroupsSpecBound bound = ColocateGroupsSpecBound.MAX;
  8: Limit limits = {"type": LimitType.ABSOLUTE, "globalLimit": 1};
  9: optional string dimension;
  10: bool squares = false;
  11: bool useContinuousPenaltyWithOptimal = false;
  12: map<string, double> groupToWeight;
}
```

**`ColocateGroupsSpecBound`**: `MAX` (1) = limit spread, `MIN` (2) = require minimum spread.

### GroupCountSpec

Limits or controls the count of objects from a group on each scope item.

```thrift
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
  12: double minimumLimit = 0;
  13: optional string routingConfigForLimit;
}
```

**`GroupCountSpecBound`**: `MAX` (1), `MIN` (2), `EXACT` (3), `MULTIPLE` (4 — count must be integer multiple of limit).

### GroupCapacitySpec

Limits utilization of a group across scope items. More complex than CapacitySpec — supports contribution partitions, bundle sizes, and step functions.

```thrift
struct GroupCapacitySpec {
  1: string name;
  2: string scope;
  3: string partitionName;
  4: optional string contributionPartition;
  5: GroupCapacitySpecDefinition definition = GroupCapacitySpecDefinition.DURING_AND_AFTER;
  6: GroupCapacitySpecBound bound = GroupCapacitySpecBound.MAX;
  7: Limit limit = {"type": LimitType.ABSOLUTE, "globalLimit": 0};
  8: Limit contribution = {"type": LimitType.ABSOLUTE, "globalLimit": 0};
  9: Filter filter;
  10: GroupCapacitySpecUtilType utilType = GroupCapacitySpecUtilType.LINEAR;
  11: optional Limit bundleConfig;
}
```

**`GroupCapacitySpecUtilType`**: `LINEAR` (1) = raw utilization, `STEP` (2) = non-zero rounded to 1, `STEP_MOD_K` (3) = zero if divisible by K, else 1.

### ThrottlingSpec

Limits move throughput — how many objects can move in/out of scope items.

```thrift
struct ThrottlingSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: ThrottlingSpecDefinition definition = ThrottlingSpecDefinition.ANY;
  5: Limit limit;
  6: Filter filter;
}
```

**`ThrottlingSpecDefinition`**: `ANY` (1) = both directions, `IN` (2) = ingress only, `OUT` (3) = egress only.

### NonAcceptingSpec

Scope items that reject incoming objects.

```thrift
struct NonAcceptingSpec {
  1: string name;
  2: string scope;
  3: list<string> items;
}
```

### ToFreeSpec

Containers to drain (make utilization zero).

```thrift
struct ToFreeSpec {
  1: string name;
  2: list<string> containers;
  3: optional string dimension;
  4: ToFreeSpecFormula formula = ToFreeSpecFormula.MINIMIZE_TOTAL_UTILIZATION;
}
```

### MinimizeMovementSpec

Minimize number of moves performed.

```thrift
struct MinimizeMovementSpec {
  1: string name;
  2: string scope;
  3: string dimension;
  4: bool magicScaling = true;
  5: bool doNotNormalize = false;
  6: double allowance = 0;
}
```

---

## Other Spec Types

### AvoidAssignmentsSpec
Prevents specific object→scope item assignments.

### AvoidMovingSpec
Prevents specific objects from moving at all.

### ExclusiveObjectsSpec
Object pairs that cannot coexist on the same scope item.

### ExclusiveGroupsSpec
Groups that cannot coexist on the same scope item.

### GroupIsolationLimitSpec
Limits how many different groups can share a scope item.

### DisasterRecoveryCapacitySpec
Ensures capacity survives failure of shared disaster groups.

### DrainCapacitySpec
Models capacity when scope items spill utilization into others.

### FlowSpec
Limits flow capacity between scope item pairs for object pairs.

### RoutingLatencySpec
Enforces upper limit on traffic latency based on routing policy and origin-to-destination latency tables.

### GroupDiversitySpec
Enforces that each scope item gets objects from at least/at most N different groups.

### CapacityWithGroupPresenceSpec
Complex spec that accounts for group presence weight in addition to raw utilization. Supports rounding, multipliers, and aggregation across scopes/partitions.

### SRBufferCapacitySpec
Specialized for SR (Shared Resource) buffer capacity with failure domain awareness.

### NestedScopeLimitSpec
Ensures utilization at inner scope is within a fraction of outer scope utilization.

---

## Supporting Types

### Limit

```thrift
struct Limit {
  1: LimitType type = LimitType.RELATIVE;
  2: double globalLimit = 1;
  3: map<string, double> scopeItemLimits;
  4: map<string, double> groupLimits;
  5: map<string, map<string, double>> scopeItemToGroupLimits;
  6: bool isDefaultLimitUnbounded = false;
}
```

**`LimitType`**: `ABSOLUTE` (1) = raw values, `RELATIVE` (2) = relative to capacity.

### Filter

```thrift
struct Filter {
  1: optional list<string> itemsBlacklist;
  2: optional list<string> itemsWhitelist;
  3: FilterType type = FilterType.SCOPE_ITEM;
}
```

**`FilterType`**: `SCOPE_ITEM` (1), `GROUP` (2).
