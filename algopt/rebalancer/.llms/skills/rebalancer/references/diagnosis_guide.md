# Expression Tree Diagnosis Guide

A reference for reasoning about ANY rebalancer expression tree. No spec-specific patterns — only universal materialization logic, node semantics, and a generic process for deriving construction rationale from spec fields.

## 1. How Specs Become Expression Trees

These rules apply to ALL specs, including specs that don't exist yet:

### Goals vs Constraints produce fundamentally different trees

- `goalCoro()` → single `ExprPtr` to minimize. Multiple goals at the same tuple index are **summed**. This is why goal trees are typically `LinearSum` at the root.
- `constraints()` → `vector<ConstraintInfo>`, one per independently-violable thing. All joined by `AnyPositive`. This is why constraint trees have `AnyPositive` at the top — it means "ALL children must be ≤ 0."
- When a spec supports both modes, `goalCoro()` typically wraps each constraint in `max(0, expr)` and sums them: `LinearSum(max(0, violation_1), max(0, violation_2), ...)`.

### Scope-based fan-out

Specs with a `scope` field iterate over scope items and produce one sub-expression per scope item. Each scope item's violation is independent. The fan-out width = number of scope items after filtering. When the spec has no per-item independence (e.g., a global move budget), there's no fan-out.

### Partition/group-based nesting

Specs with a `partitionName` field add an additional nesting level — one sub-expression per group within the partition.

### Definition field → leaf type

The `definition` field (`AFTER`, `DURING`, `MOVED_DATA`, `NEW`, `OLD`, etc.) determines which variant of utilization the leaf `ObjectLookup` measures. `DURING_AND_AFTER` creates two parallel trees.

### Hard/soft constraint splitting

When a constraint uses DEFAULT policy and is initially broken, it splits into hard (don't worsen) + soft goal (minimize violation). This is why some expression names appear in both constraint and goal trees.

### Expression caching

Sub-expressions are shared across specs via `ExpressionBuilder` caching (keyed by dimension + scope + scope item). The same `ObjectLookup` leaf may appear under multiple parent expressions from different specs. This creates a DAG, not a strict tree.

---

## 2. Expression Node Types

Each node type and the computation it performs:

| Node Type | Computation | Notes |
|-----------|-------------|-------|
| `LinearSum` | `value = constant + Σ(coefficient_i × child_i)` | Additive composition. Most common root type for goals. |
| `AnyPositive` | Returns 1 if ANY child is strictly greater than zero, else 0 | Logical conjunction for constraints. |
| `Max` | Maximum of children's values | Worst-case selection. |
| `BinaryOperation` | Two-child arithmetic (addition, subtraction) | Used for `utilization - limit` style computations. |
| `Power` / `Square` | Raises a child to an exponent | Non-linear penalty — larger deviations penalized more heavily. |
| `QuotientOperation` | Division of two children | Used for ratios (e.g., utilization / capacity). |
| `ProductOperation` | Multiplication of two children | Scaling operations. |
| `SumOverThreshold` | `Σ max(0, child_i - threshold)` | Aggregates excess over a threshold. |
| `NthLargest` | N-th largest value among children | Used for percentile-style constraints. |
| `ObjectLookup` | **Leaf** — sums object dimension values for objects in a scope item's containers | The physical measurement. |
| `ObjectPartitionMoveLimit` | **Leaf** — tracks per-group move counts within a partition | Move counting. |
| `Variable` | **Leaf** — binary, 1 if a specific object is in a specific container | Assignment indicator. |
| `StableStayed` | **Leaf** — like ObjectLookup but only counts objects that stayed in their initial container | Stability measurement. |
| Constant | A `LinearSum` with no children | A fixed numeric value (often a limit from the spec). |

---

## 3. How to Derive Construction Rationale from Spec JSON

A generic process for ANY spec — instead of memorizing patterns, derive them:

### Step 1: Read the spec type

From the `eval_move` specs output, identify the spec type (e.g., `CapacitySpec`, `BalanceLoadSpec`, `GroupMoveLimitSpec`). The spec type tells you the mathematical formula class.

### Step 2: Identify the spec's structural parameters

| Spec Field | Expected Tree Effect |
|------------|---------------------|
| `scope` | A fan-out level — one child per scope item |
| `dimension` | Leaf `ObjectLookup` nodes measuring this dimension |
| `bound`/`limit`/`upperBound`/`lowerBound` | Constants in `BinaryOperation` or threshold nodes |
| `definition` (`AFTER`, `DURING`, `MOVED_DATA`, etc.) | A specific utilization variant at the leaf |
| `partitionName` | An additional nesting level per group |
| `formula` (if present) | Different composite structures (e.g., `Power` for quadratic, `SumOverThreshold` for linear) |

### Step 3: Match spec fields to tree nodes top-down

1. **Root node type** tells you the composition:
   - `LinearSum` = summed violations across scope items
   - `AnyPositive` = conjunction of violations (all must be ≤ 0)
   - `Max` = worst-case violation

2. **Fan-out children** correspond to scope items or groups — check child `name` fields for scope item identifiers (e.g., `::host=host0`, `::server=-488245692`).

3. **Intermediate nodes** implement the formula:
   - `Power` = non-linear penalty
   - `BinaryOperation` = difference from limit
   - `Max(0, ...)` = clamping violations to non-negative

4. **Leaf `ObjectLookup` nodes** correspond to the `dimension` and `scope` fields — they measure the actual physical quantity.

### Step 4: Read node names

Node names typically encode the scope item identity. Examples:
- `::host=host0`
- `::server=-488245692`
- `::region=us-east`

This tells you which scope item each branch of the fan-out corresponds to.

### Step 5: Identify threshold nodes using spec limits

Constants in the tree that don't change (`delta=0`) often represent limits from the spec. Compare these constant values to the spec's configured `limit`, `upperBound`, `lowerBound`, or `scopeItemLimits` to confirm which tree node represents which spec parameter.

---

## 4. Reading the Blame Path

The blame path traces the primary contributor from root to leaf:

1. **Start at root**: if `delta != 0`, descend.

2. **For `LinearSum`**: Multiple children contribute additively. Each child's contribution = `coefficient × child_delta`. The child with the largest `|contribution|` is the primary contributor. Report all children with non-trivial contributions.

3. **For `Max`**: The child that became the new maximum is the culprit. Compare each child's `destinationValue` — the one matching the root's `destinationValue` is the current maximum.

4. **For `AnyPositive`**: The child that crossed from ≤ 0 to > 0 (or vice versa) caused the violation change. Check each child's `sourceValue` vs `destinationValue` to find the crossover.

5. **For `BinaryOperation`**: Compare both operands' deltas. One operand is typically the utilization (changing), the other is the limit (constant).

6. **For `Power`/`Square`**: Delta is amplified non-linearly. The input delta matters more at higher values.

7. **At leaf (`ObjectLookup`)**: `sourceValue → destinationValue` is the physical measurement change. This is the ground truth — the actual dimension value changed by the move.

8. **`properties` at any node** carries metadata (scope item, dimension, container). Always report when present — it identifies the physical entity.

---

## 5. `getTreeNodeV2` API Reference

### Request (`TreeNodeRequest`)

| Field | Type | Description |
|-------|------|-------------|
| `expressionId` | `i64` | ID of the expression to inspect |
| `sourceAssignment` | `Assignment` | Source (baseline) assignment |
| `destinationAssignment` | `Assignment` | Destination (override) assignment |
| `childrenPage` | `Page` | Pagination: `offset` (0-based) + `limit` |
| `childrenOrderMetric` | `TreeNodeOrderMetric` | `SOURCE_VALUE=0`, `DESTINATION_VALUE=1`, `DELTA_VALUE=2` |
| `childrenOrderDirection` | `OrderDirection` | `ASCENDING=0`, `DESCENDING=1` |
| `search` | `Search` | Optional query to filter children by entity name |

### Response (`TreeNodeResponse`)

| Field | Type | Description |
|-------|------|-------------|
| `node` | `TreeNode` | The requested expression node |
| `children` | `list<TreeNode>` | Direct children of the node |

### TreeNode Fields

| Field | Type | Description |
|-------|------|-------------|
| `expressionId` | `i64` | Unique ID for this expression |
| `expressionType` | `string` | Computation type (e.g., `LinearSum`, `ObjectLookup`) |
| `name` | `string` | Human-readable name (often encodes scope item identity) |
| `description` | `string` | Description of the expression |
| `sourceValue` | `double` | Value at the source assignment |
| `destinationValue` | `double` | Value at the destination assignment |
| `coefficient` | `double` | Weight of this node in its parent's computation |
| `properties` | `ExpressionProperties` | Key-value metadata (scope item, dimension, etc.) |
