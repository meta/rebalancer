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

@hack.NamePrefix{prefix = "rebalancer_interface_"}
@hack.LegacyOmitPrefixInNameString
package "meta.com/algopt/rebalancer/fix_me"

namespace hack ""

include "algopt/rebalancer/interface/thrift/Metrics.thrift"
include "thrift/annotation/cpp.thrift"
include "thrift/annotation/python.thrift"
cpp_include "folly/container/F14Map.h"
include "thrift/annotation/thrift.thrift"
include "thrift/annotation/hack.thrift"

namespace cpp2 facebook.rebalancer.interface
namespace py rebalancer.interface.thrift.Types
namespace py3 rebalancer.interface.thrift

// Each field's Thrift default encodes existing behavior. This, for instance,
// allows OSS builds to use the old behavior until the internal rollout is
// complete. Internal rollout defaults are defined in RolloutConfigDefaults.h.
struct RolloutConfig {
  1: bool groupBackedDynamicDimensions = false;

  // Keep continuous local-search penalties separate from user-visible goals,
  // and use them only after all Global values in the active stage tie.
  2: bool useSeparatedLocalSearchPenaltyObjective = false;

  // Skip expression subgraphs that are already optimal while selecting hot
  // containers.
  3: bool pruneOptimalSubgraphs = false;

  4: bool useIncrementalPriorityQueueV2 = false;

  // Local-search solvers without a parallelExecutionConfig use AUTO instead of
  // sliding-window execution.
  5: bool useAutoParallelExecution = false;

  // Materialize constraint-derived invalid move filters.
  6: bool enableInvalidMoveFilter = false;

  // Temporary setting used to test saving, replaying, and logging a MetaConfig
  // value.
  32767: bool testOnlyConfig = false;
}

enum ConstraintPolicy {
  /*
  If a constraint is broken initially, make “fixing it” a goal,
  and “not making it worse” a constraint. If a constraint is not
  broken initially, then make “not breaking it” a constraint.
  */
  DEFAULT = 1,
  /*
  A constraint is always treated as a hard constraint by the solver.
  If the solver can’t manage to fix it completely, that’s an exception.
  */
  HARD = 2,
  /*
  A constraint is treated as a soft constraint. There’s a caveat: making
  it worse triggers a second step function, so making it slightly worse
  than initial is much more costly than leaving it with the same level
  of brokenness.
  */
  SOFT = 3,
}

// default params that apply to all constraints in the problem
// individual constraints can override these default params
struct ConstraintParams {
  // weight given to incrementally improving a broken constraint
  1: double invalidCost = 100;
  // weight given to completely fixing a broken constraint
  // to disable step set invalidState to 0
  2: double invalidState = 10000;
  /*
  If a constraint is initially broken and the ConstraintPolicy is one
  of Default or Soft, then "fix it" by adding it as a goal at tuple index
  'tuplePosIfBroken', where 'tuplePosIfBroken' is non-negative
  */
  3: i32 tuplePosIfBroken = 0;
}

enum ConstraintType {
  AvailabilityConstraint = 1,
  ConcentrateNewCapacityConstraint = 2,
  IsolateConstraint = 3,
  RestrictNetworkStackingConstraint = 4,
  StackingConstraint = 5,
  StackingForMaintenanceConstraint = 6,
}

enum EndReason {
  OPTIMAL = 1,
  HIT_TIME_LIMIT = 2,
  HIT_MOVE_LIMIT = 3,
  HIT_STOP_CONSTRAINT = 4,
  HIT_PLATEAU_TIME = 5,
  NO_SOLUTION_FOUND = 6,
  HIT_STAGE_MOVE_LIMIT = 7,
  HIT_EXCEPTION = 8,
  UNABLE_TO_FIND_MORE_MOVES = 9,
  HIT_MIN_CYCLE_OBJECTIVE_IMPROVEMENT = 10,
  UNABLE_TO_FIND_IMPROVING_MOVES = 11,
}

struct MoveStatsSpec {
  1: bool trackContainers = false;
  2: bool trackObjects = false;
  3: optional list<string> trackObjectsWhitelist;
  // when enabled will print all evals for the object being tracked
  4: bool printTrackedObjectStats = false;
  // print all evals starting at these whitelisted source containers
  5: optional list<string> printSourceContainersWhitelist;
  // by default, when using stageSolver, only the changed objectives of the stage are populated in MovesSummary.objectives;
  // if field below is set to 'true', moveSummary.objectives will have all changed objectives
  6: bool showAllChangedObjectivesInMovesSummary = false;
}

struct SingleObjectiveSummary {
  1: string name;
  2: string desc;
  3: double weight;
  4: double value;
}

struct ObjectiveSummary {
  // unused field: solved
  1: bool solved;
  2: double value;
  3: list<SingleObjectiveSummary> objs;
}

struct GlobalObjectiveSummary {
  1: bool solved;
  2: list<ObjectiveSummary> goals;
}

struct SingleConstraintSummary {
  1: string name;
  2: string desc;
  3: double value;
}

struct ConstraintSummary {
  1: bool solved;
  2: double brokenVal;
  3: i64 brokenCount;
  4: list<SingleConstraintSummary> constraints;
}

struct GlobalObjectiveValueChange {
  1: double newValue;
  2: double change;
  3: i64 tuplePos;
}

struct SingleMove {
  1: string object;
  2: string srcContainer;
  3: string dstContainer;
}

@thrift.ReserveIds{ids = [2]}
struct MovesSummary {
  1: list<SingleMove> moves;
  // only objectives where the value changed as a result of the move are in this map;
  // when using stageSolver, unless MoveStatsSpec.showAllChangedObjectivesInMovesSummary is
  // set to 'true' this map only has objectives that are part of the stage
  3: map<string, GlobalObjectiveValueChange> objectives;
  4: i64 evalsCount;
  5: map<string, i64> constraintInvldCnt;
  6: optional i32 stageId;
  // Local search cycle in which this move was applied (>= 1).
  // Absent for non-local-search solvers and old Manifold data.
  7: optional i32 cycleId;
}

struct SolverEvalStats {
  1: double avgEvalSpeed;
  2: i32 numEvals;
  3: double invalidEvalsPct;
  4: double worseEvalsPct;
  5: double neutralEvalsPct;
  6: double betterEvalsPct;
  7: i32 numEvalTimeouts;
  // time spent in evaluating moves
  8: double evalDurationSecs;
  // time spent in finding the hottest container
  9: double findWorstDurationSecs;
  // Number of local search cycles completed.
  // For single-stage solvers this is the cycle count of that solve.
  // For multi-stage solvers this is the sum across all stages; each stage's
  // own cycle count is available via stagesSummaries[i].evalStats.numCycles.
  10: i32 numCycles;
  // Number of candidate moves skipped by the invalid move filter instead of
  // being evaluated; not included in numEvals or the percentages above. Most
  // move types consult the filter per candidate, but some filter while building
  // candidate lists, so this can exceed the evals the filter actually saved.
  11: i64 numPrunedEvals;
}

// Inference stats for a learned move-filter (rebalancer-net) run that attached.
struct LearnedMoveFilterStats {
  1: string configuredModelPath; // spec value; empty for auto-resolution.
  2: string resolvedModelKey; // resolved Manifold key; empty for a local model.
  3: double pruneThreshold;
  4: i64 numPairsScored;
  5: i64 numPruned;
  6: double pruneRatio; // numPruned / numPairsScored.
  7: double modelLoadSec;
  8: double graphEncodeSec;
  9: double gnnForwardSec;
  10: double scorePairsSec;
}

// Per-solve learned move-filter outcome, logged into the additionalInfo column
// of rebalancer_runs. `stats` is set when the filter attached; otherwise
// `message` explains why it was not used.
struct LearnedMoveFilterInfo {
  1: string fingerprintHash;
  2: optional LearnedMoveFilterStats stats;
  3: optional string message;
}

struct SolverMoveStats {
  // time spent in solver
  1: double durationSecs;
  // total number of moves
  2: i32 numMoves;
  // time bucket to moves count
  3: list<i32> movesHistogram;
  // time spent in applying moves
  4: double applyDurationSecs;
  5: list<string> moveTypes;
}

@thrift.ReserveIds{ids = [4]}
struct StageSummary {
  // name of the stage
  1: optional string name;
  // stage duration
  2: double duration = 0;
  // reason why the stage exited
  3: EndReason endReason;
  // histogram of move times in this stage
  5: optional SolverMoveStats moveStats;
  6: FinalEvaluationSummary finalEvaluationSummary;
  7: optional SolverEvalStats evalStats;
  // additional information about endReason like time limits, global optimum, etc.
  8: string auxEndInfo;
}

struct SolverReport {
  1: EndReason endReason;
  2: optional SolverEvalStats evalStats;
  3: optional SolverMoveStats moveStats;
  4: optional list<StageSummary> stagesSummaries;
}

struct FinalEvaluationStats {
  1: i64 totalCount;
  // For each constraint, how many times an invalid move was evaluated where
  // that constraint was the first broken.
  2: map<string, i64> brokenConstraints;
  // For each objective, how many times a valid move was evaluated producing a
  // quality deterioration, where that objective had the biggest impact.
  3: map<string, i64> worstObjectives;
  // number of containers that are non accepting (no move in allowed). This is populated only for FinalEvaluationSummary.globalStats
  4: optional i32 nonAcceptingContainersCount;
  // number of containers that are fixed (no move in or out allowed). This is populated only for FinalEvaluationSummary.globalStats
  5: optional i32 fixedContainersCount;
  // number of objects that are fixed. This is populated only for FinalEvaluationSummary.globalStats
  6: optional i32 fixedObjectsCount;
}

// Stats on the last round of local search, after the last move applied. Helps
// understand the reasons why the objective couldn't be improved any further.
struct FinalEvaluationSummary {
  1: FinalEvaluationStats globalStats;
  2: map<string, FinalEvaluationStats> sourceContainerStats;
  3: map<string, FinalEvaluationStats> destinationContainerStats;
  4: map<string, FinalEvaluationStats> objectStats;
}

struct SolverStatsForObjective {
  1: OptimalGap gap;
  2: bool solverFoundSolution;
}

struct OptimalGap {
  1: double relative;
  2: double absolute;
}

// Problem profile for optimal solvers
struct OptimalSolverProfile {
  // time taken to build express problem
  1: double xpressBuildSec = 0;
  // time taken to apply initial state
  2: double xpressApplyInitialStateSec = 0;
  // a list for optimal subset solver
  3: list<double> xpressMipOptimizeSec = [];
  // time taken to translate xpress solution to an assignment
  4: double postProcessingSec = 0;
  // final gap value of the optimal solver w.r.t. to the first objective
  5: OptimalGap gap;
  // status of the initial solution processed
  6: optional bool isWarmStartSuccessful;
  // time taken to process the initial solution
  7: optional i64 warmStartProcessingTimeSec;
  8: bool noFeasibleSolution = false;
  // number of dynamic containers and dynamic object equivalence sets, which
  // determine the number of variables in the MIP model
  9: i32 dynamicContainers;
  10: i32 dynamicEquivalenceSets;
  // for each objective, shows the optimalGap, on whether the solver found a solution, etc.
  11: list<SolverStatsForObjective> solverStatsForObjectives;
}

struct MoveTypeEvent {
  1: i32 moveTypeIndex;
  2: double duration;
  3: double initialValue;
  4: double finalValue;
}

struct LocalSearchProfile {
  1: list<string> moveTypeNames;
  2: list<MoveTypeEvent> moveTypeEvents;
}

@thrift.ReserveIds{ids = [3, 4]}
struct HierarchicalProfileNode {
  1: string eventName;
  2: double duration;
  5: list<HierarchicalProfileNode> children;
  // maximum amount of memory allocated by this node and its children
  6: double maxInclusiveMemory;
}

// Profiler output for the problem. It can include things like average time to
// evaluate an expression or constraint. Time taken to materialize the problem,
// time taken to solve, etc.
struct ProblemProfile {
  // time spent in materializing the problem
  1: double materializationSec;
  // total time spent in solving the problem
  2: double solveSec;
  // only set for optimal solvers
  3: optional OptimalSolverProfile optimalSolverProfile;
  4: list<LocalSearchProfile> localSearchProfiles;
  // root of the hierarchical profile tree, can obtain the hierarchical profile by
  // standard tree traversals using children nodes
  5: HierarchicalProfileNode hierarchicalProfileRoot;
}

// Describes the internal tagging of scope items to groups performed by
// ExclusiveGroupsSpec.
struct ExclusiveGroupsTagging {
  // Maps scope item name to group name.
  1: map<string, string> scopeItemToGroup;
}

// Describes metadata associated with a spec.
struct SpecMetadata {
  1: string specName;
  2: optional ExclusiveGroupsTagging exclusiveGroupsTagging;
}

// The following struct captures the information associated with one
// equivalence set such as name, member objects, and other attributes.
// We will add more details (such as attributes) as needed.
struct EquivalenceSetMetadata {
  1: string name;
  // list of objects that belong to this equivalence set
  2: list<string> objectNames;
}

struct EquivalenceSetInfo {
  1: list<EquivalenceSetMetadata> equivalenceSets;
}

@python.UseCAPI
@thrift.ReserveIds{ids = [10, 16, 17, 18, 23, 24]}
struct AssignmentSolution {
  @cpp.Type{template = "folly::F14FastMap"}
  1: map<string, string> assignment;
  // initial first objective (to be deprecated: use initialGlobalObjective)
  2: ObjectiveSummary initialObjective;
  // final first objective (to be deprecated: use finalGlobalObjective)
  3: ConstraintSummary initialConstraint;
  4: ObjectiveSummary finalObjective;
  5: ConstraintSummary finalConstraint;
  6: optional list<MovesSummary> movesSummary;
  7: list<SolverReport> solverSummaries;
  8: optional FinalEvaluationSummary finalEvaluationSummary;
  @cpp.Type{template = "folly::F14FastMap"}
  9: map<string, string> initialAssignment;
  11: string runId;
  12: GlobalObjectiveSummary initialGlobalObjective;
  13: GlobalObjectiveSummary finalGlobalObjective;
  14: ProblemProfile problemProfile;
  15: i32 numContainers;
  // Maps an objective or constraint name to its metadata.
  19: map<string, SpecMetadata> specNameToMetadata;
  20: Metrics.Metrics initialMetrics;
  21: Metrics.Metrics finalMetrics;
  22: EquivalenceSetInfo equivalenceSetInfo;
}

// Defines a routing ring as:
// 1. Origin scope item (region)
// 2. Amount of traffic originating in (1)
// 3. Routing policy as a series of rings, each being a set of scope items (regions)
// Traffic from this origin is sent to the first set of scope items with availability,
// and it gets spread evenly among the scope items available in the selected set.
struct RoutingRing {
  1: string originScopeItem;
  2: double originTraffic;
  3: optional list<list<string>> destinationScopeItemSets;
}

// Defines all the routing rings applicable to one group (replica set).
@thrift.ReserveIds{ids = [1]}
struct GroupRoutingRings {
  2: list<RoutingRing> routingRings;
}
