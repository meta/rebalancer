# AvoidAssignments

**Type**: Constraint

Forbid specific objects from being placed on specific scope items. For example,
keep certain tasks off certain hosts (say, because those hosts lack hardware the
task needs).

## Parameters

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `name` | string | No | `""` | Descriptive name for logging/debugging. |
| `scope` | string | Yes | - | The scope the listed scope items belong to (e.g. `"host"`). |
| `assignments` | list of [AvoidAssignment](#avoidassignment-entries) | Yes | empty | The forbidden (object, scope item) pairs. |

### AvoidAssignment entries

Each `AvoidAssignment` forbids one object from a set of scope items:

| Field | Type | Description |
|-------|------|-------------|
| `object` | string | The object being restricted |
| `scopeItems` | list of string | The scope items this object may not be placed on |

## Example

An example use: tasks on hosts, where certain tasks may not go on certain hosts.
Forbid `t0` from `h0` and `h1`, `t1` from `h0` and `h2`, and `t2` from `h0`.

```cpp
solver.setObjectName("task");
solver.setContainerName("host");

solver.setAssignment(std::map<std::string, std::vector<std::string>>{
    {"h0", {"t0", "t1"}},
    {"h1", {"t2"}},
    {"h2", {"t3"}},
});

auto forbid = [](std::string object, std::vector<std::string> scopeItems) {
  AvoidAssignment entry;
  entry.object() = std::move(object);
  entry.scopeItems() = std::move(scopeItems);
  return entry;
};

AvoidAssignmentsSpec avoidSpec;
avoidSpec.scope() = "host";
avoidSpec.assignments() = {
    forbid("t0", {"h0", "h1"}),
    forbid("t1", {"h0", "h2"}),
    forbid("t2", {"h0"}),
};
solver.addConstraint(avoidSpec);
```

`t0` and `t1` both start on `h0`, which is forbidden for both, so they move: `t0` to
`h2` (its only allowed host) and `t1` to `h1`. `t2` is already on an allowed host
(`h1`) and stays. `t3` is unconstrained.

## Constraint only

AvoidAssignmentsSpec cannot be used as a goal; passing it to `addGoal` throws.

If the initial assignment already respects every forbidden pair, so does the final
one. If an object starts on a scope item it is forbidden from, that pair is
initially broken, and the general [constraint policy](../constraint-policy) takes
over: under the default policy Rebalancer moves the offending objects off as best
it can without breaking constraints that held initially.

An initially broken scope item never accepts a new forbidden object, even when
another forbidden object leaves at the same time. Swapping one forbidden object
for another would leave the number of violations unchanged, but it is not
allowed: on such a scope item, Rebalancer adds a second constraint, counting
only forbidden objects that were not there initially, and that constraint holds
initially, so the default policy keeps it hard. Forbidden objects that start on
the scope item may still stay or move off.

## Avoiding many pairs

Listing every pair gets unwieldy when a large share of objects must be kept off a
scope item. An equivalent formulation scales better: add a dynamic object
dimension (`addDynamicObjectDimension`) that is 1 for each disallowed (scope item,
object) placement and 0 otherwise, choosing the `defaultValue` (0 or 1) that
leaves fewer explicit entries. Then add a [CapacitySpec](capacity) on it with an
`ABSOLUTE` `MAX` limit of 0, so no disallowed placement fits.

## Source

- Thrift definition: [`interface/thrift/ProblemSpecs.thrift`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/thrift/ProblemSpecs.thrift) (`AvoidAssignmentsSpec`, `AvoidAssignment`)
- SpecBuilder: [`materializer/spec_builder/AvoidAssignmentsSpecBuilder.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/materializer/spec_builder/AvoidAssignmentsSpecBuilder.cpp)---the code that defines this spec's behavior
- Tests and runnable examples: [`interface/tests/AvoidAssignmentsTest.cpp`](https://github.com/facebook/rebalancer/blob/main/algopt/rebalancer/interface/tests/AvoidAssignmentsTest.cpp)---the unit tests the snippets on this page are drawn from
