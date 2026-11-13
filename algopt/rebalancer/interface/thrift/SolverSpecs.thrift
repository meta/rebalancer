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
include "algopt/rebalancer/algopt_common/thrift/Types.thrift"
// @fb-only: include "configerator/structs/tupperware/ras/resourcepools.thrift"
include "thrift/annotation/hack.thrift"

cpp_include "folly/container/F14Map.h"
cpp_include "folly/container/F14Set.h"

namespace cpp2 facebook.rebalancer.interface
namespace py3 rebalancer.interface.thrift.v2

@cpp.Type{template = "folly::F14FastSet"}
typedef set<string> StringSet

@cpp.Type{template = "folly::F14FastMap"}
typedef map<string, StringSet> StringToStringSetMap

// Intuitively, we can build custom equivalence sets of objects as follows:
// Suppose we assume that initially all objects are equivalent. That is we have
// one singleton equivalence set E with all objects in it.
//
// -- As we consider each goal/constraint, the sets will split depending on
//    how those objects contribute to the goal/constraint. Lets call this collection
//    of sets of objects as E'
//
// -- If we want to artifically split the sets further, we can specfiy a dimension and
//    all objects with different values for that dimension will be in different sets.
//
// -- In a similar way, we can also specify a partition of objects and enforce that two
//    objects that belong to different groups of the partition will be in different sets.
//
struct CustomEquivalenceSetConfig {
  // Equivalence sets E' are created using the following goals and constraint selection config
  1: Types.StringListFilterConfig goalSelectionConfig;
  2: Types.StringListFilterConfig constraintSelectionConfig;
  // Splits E' further to E'' based on the partitions in this list
  3: list<string> partitionNames;
  // Splits E'' further based on the dimension in this list
  // currently not supported, uncomment after T225680790
  // 3: list<string> dimensionNames;
}

///////////////////////////////////////////////////////
///// SPECS AND UTILS RELATED to LocalSearch
///////////////////////////////////////////////////////

// Sampling for a goal over the groups of `partition` (e.g. sleds), such as
// GroupDiversity MAX, in the containers of `scopeItem` of `scope`. The
// occupancy of a group is its number of objects in those containers, and
// follows every applied move. For a move into those containers, the stackable
// multi move types sample, per equivalence set, only the objects of the highest
// occupancy that has a candidate; for a move out of them, the lowest. Objects
// in no group are always sampled. Other moves, and bundles formed across the
// servers of an equivalence set (1:k swaps), are sampled as before. `partition`
// must be disjoint.
struct GroupOccupancySampling {
  1: string partition;
  2: string scope;
  3: string scopeItem;
}

struct RasLocalSearchMetadata {
  1: i32 defaultMultiMoveBundleSize = 1;
  2: map<string, list<i32>> multiMoveBundleSizePerContainer;
  // The following partition will be used to restrict the "search space" for local search
  // Specifically, local search will attempt moving one object from each group of the partition
  3: string searchSpacePartition;
  4: optional string serverPartition;
  // This will be used to further restrict the search space dynamically based on the chosen container
  // Specifically, if an entry exists for a given container C, this will cut down the search space from
  // "|searchSpacePartition|" to "|searchSpaceGroupsPerContainer.at(C)|"
  5: map<string, list<string>> searchSpaceGroupsPerContainer;
  // For every search search space group (groups of servers with same MSB x Subtype in case of stackable reservations),
  // we will try at least k servers from each group. By default, k = 1. And for each server, we attempt to move
  // defaultMultiMoveBundleSize parts
  6: i32 sampleSizePerSearchSpaceGroup = 5;
  // Rebalancer automatically computes a search space for local search based on all goals and constraints called
  // equivalence sets based partitioning. If this flag is true, we will use the equivalence sets to perform local search.
  // the sample size per group and searchSpacePartition will be ignored. Each individual applied move will have better quality
  // but the search may get significantly slower as we are evaluating a lot more moves for each apply.
  7: bool useMostGranularSearchSpace = false;
  // Sets of objectives and constraints that will be used to further split the search space partition
  // In other words, unless useMostGranularSearchSpace is true, this set will be used to compute a custom equivalent set
  // partitioning using a subset of user provided goals and constraints. This "coarser" equivalent set is used by RAS specific
  // local search.
  8: list<string> objectivesToSplitSearchSpacePartition;
  9: list<string> constraintsToSplitSearchSpacePartition;
  // 10 is to be deprecated
  10: bool enableExperimentalLocalSearchStages = false;
  // bundle size => GPU bundle partition name
  11: map<i32, string> gpuBundlePartitionNamePerBundleSize;
  12: map<string, list<string>> conflictContainersPerContainer;
  // set of GPU containers with topology requirements (excluding any special containers)
  13: set<string> gpuContainerNames;
  // adaptive allotment resource limit constraint names
  // These constraints are used to quickly identify servers with exhausted resources
  14: list<string> resourceLimtConstraintNames;
  15: optional string reservationCapacityShapeScopeName;
  16: bool useAdaptiveAllotments = false;
  // Sets of objectives and constraints that will be ignored when partitioning objects into equivalence sets.
  // In other words, when useMostGranularSearchSpace is false and there are no `objectivesToSplitSearchSpacePartition`
  // or `constraintsToSplitSearchSpacePartition`, these sets will be used to compute a custom equivalent set by using
  // all user provided goals and constraints except the ones in the deny lists to partition the search space.
  // This can be used to make the equivalent sets "coarser" and will be used by RAS local search for full server solves.
  @cpp.Type{template = "folly::F14FastSet"}
  17: set<string> objectivesToSplitSearchSpacePartitionDenyList;
  @cpp.Type{template = "folly::F14FastSet"}
  18: set<string> constraintsToSplitSearchSpacePartitionDenyList;
  // All-or-nothing object bundle move hints for containers.
  19: optional ObjectBundleFormationHints objectBundleFormationhints;
  // Configuration for uneven swap ratios in SwapMoveType
  20: optional StringKeyValueMap swapRatioDimension;
  // Single partition with overlapping groups that represent physical GPU
  // topology. Groups may overlap (an object can belong to multiple groups)
  // when server parts share different physical GPUs. Group names are typically
  // in the format "ServerName_GPUId". At most one object from each group can
  // be allocated at once from the special container.
  21: string physicalGpuOverlapPartitionName;
  // Off unless set. Only the stackable solve of RasHybridSolver supports it.
  22: optional GroupOccupancySampling groupOccupancySampling;
}

// The name is slightly misleading and is that way for historical reasons
// Initially, we were planning on using MIPs + Local Search together, hence
// the name HybridSolver, but we found that local search works well enough, so
// building and maintaining an OptimalSubset component would be unnecessary.
// Therefore, the MIP component of "HybridSolver" is no longer valid. This is
// simply a local search based algorithm that makes use of "domain specific"
// knowledge to achieve speed.
@thrift.ReserveIds{ids = [2, 3, 5]} // deprecated fields
struct RasHybridSolverSpec {
  1: string specialContainer = "";
  // by default local search solver will run no longer than 5 minutes
  4: i32 localSearchSolveTime = 300;
  // this is a small number controlling the per-stage time limit for local search
  // explicitly, perStageTimeLimit = perStageTimeLimitMultiplier * (solve_time_limit / numStages)
  6: double perStageTimeLimitMultiplier = 2;
  // Servers with planned capacity loss should move to this special container
  7: string moveOutHoldContainer = "";
  // solver output file to write useful pieces of solver logs
  8: string solverLogFile = "";
  // stackable solve type may use custom move types and custom stages
  9: bool isStackableSolveType = false;
  // Stores relevant metadata which is passed to local search solver
  10: optional RasLocalSearchMetadata rasLocalSearchMetadata;
  // Stores the stages we want for local search
  // @fb-only: 11: list<resourcepools.RasLocalSearchStageSpec> localSearchStages = [];
  // If set, local search stops after this many moves in total, across all stages
  12: optional i32 stopAfterMoves;
}

struct HottestTraversalConfig {
  // if a node is found to be optimal, then do not explore any node in the subgraph rooted at that node during hottest container ordering
  // this is a pure optimization and should ideally be true; flag is set to false only for safe rollout
  // T241049672
  1: bool pruneOptimalSubgraphs = false;
}

struct SlidingWindowExecutionConfig {}

struct BatchingExecutionConfig {
  // Number of items processed by each batch consumer at a time.
  1: i32 batchSize = 32;
  // Maximum concurrent batch consumers, clamped to the executor thread count.
  // If 0, uses all executor threads.
  2: i32 maxConcurrency = 24;
}

struct AutoExecutionConfig {
  1: SlidingWindowExecutionConfig slidingWindow;
  2: BatchingExecutionConfig batching;
}

// IDs 1 and 2 belong to the previous struct representation.
@thrift.ReserveIds{ids = [1, 2]}
union ParallelExecutionConfig {
  3: SlidingWindowExecutionConfig slidingWindow;
  4: BatchingExecutionConfig batching;
  // AUTO times sliding-window and batching on each move type's early
  // evaluation calls, then keeps the one with higher evaluation throughput.
  5: AutoExecutionConfig autoExecution;
}

// Minimum improvement a cycle must achieve to justify starting the next cycle.
// If no objective position improved beyond the threshold (exceeding either
// percent or absolute), solving stops.
struct MinCycleObjectiveImprovementConfig {
  1: Types.Threshold defaultThreshold;
}

// Configuration for the learned (GNN) move-legality heuristic (rebalancer-net).
// When set on the AssignmentProblem, the solver loads the exported TorchScript
// model from `modelPath`, scores (object, container) move legality on the
// initial graph, and marks pairs whose predicted probability is below
// `pruneThreshold` as invalid -- pruning them before native move evaluation.
// The filter is enabled by the presence of this spec; leave it unset to disable.
@thrift.ReserveIds{ids = [3]} // 3: bool enabled -- removed in favor of presence-based toggling
struct LearnedMoveFilterSpec {
  // Which exported TorchScript model to load. Leave EMPTY to auto-resolve the
  // latest model published for this problem's (service, fingerprint) from
  // Manifold (rebalancer_net/training:export_model uploads there). Set it to
  // override: a `manifold://<bucket>/<key>` URI or a local filesystem path.
  1: string modelPath;
  // Prune (object, container) candidate moves whose predicted probability is
  // below this threshold. Higher is more conservative: fewer moves pruned, lower
  // risk of dropping a legal improving move (recall), but a smaller eval saving.
  2: double pruneThreshold = 0.5;
  // If set, append one JSON line of inference metrics (timings + prune counts)
  // per solve to this file, for offline plotting/analysis. Populated from a
  // binary-level flag (e.g. the standalone solver) rather than a process-wide
  // flag read inside the solver core. (id 3 is reserved: former `enabled` bool.)
  4: optional string learnedMetricsJson;
}

// 54: learnedHeuristicSpec -- moved to AssignmentProblem.learnedMoveFilterSpec
@thrift.ReserveIds{ids = [9, 14, 15, 21, 22, 23, 27, 37, 38, 54]} // deprecated fields
struct LocalSearchSolverSpec {
  1: optional i32 allowedPlateauTime;
  2: i32 constrainedBoundsCheckMs;
  3: bool enableConstrainedBoundsOptimization;
  4: i32 minHotObjects = 1;
  6: optional i32 objectivesForHottestContainers;
  7: optional string objectOrderingDimension;
  10: i32 randomSeed;
  11: optional i32 solveTime;
  12: optional i32 stopAfterMoves;
  13: i32 stratifiedSampleSize = 10;
  16: optional double timePerMove;
  //  TODO: move this field into HottestTraversalConfig
  31: bool recomputeContainerOrderingAfterEveryMove = true;
  // If the following field is set to 'true' and the sample size for the SINGLE_COLDEST_STRATIFIED move type is k, then
  // when evaluating a move, 2*k samples are considered, where the sample includes k coldest servers and k random servers
  33: bool includeEqualSizeRandomSampleForSingleColdestMoveType = false;
  // TODO (T176926160): remove all optional move type specs and all move type specific options
  // (several fields above like 'replicaDropPartition') from localSearchSpec
  40: optional SingleRandomStratifiedMoveTypeSpec singleRandomStratifiedMoveTypeSpec;
  42: bool enableObjectPotentialSorting;
  // The flag below is used to control whether while performing localSearch the solver should explore moves from containers
  // that are not part of the objective being considered
  43: bool exploreMovesFromContainersNotInObjective = true;
  // if set, the solver will use custom equivalence sets to perform local search
  44: optional CustomEquivalenceSetConfig customEquivalenceSetConfig;

  // A list of MoveTypeSpec elements (a union).
  // This allows for specific, structured definitions of each move type.
  // This field replaces field moveTypes, which is now deprecated.
  50: list<MoveTypeSpec> moveTypeList;

  51: HottestTraversalConfig hottestTraversalConfig;

  // Configuration for parallel execution during move evaluation
  52: optional ParallelExecutionConfig parallelExecutionConfig;

  53: optional MinCycleObjectiveImprovementConfig minCycleObjectiveImprovement;

  // DO NOT USE THESE FIELDS ANYMORE -- THEY ARE DEPRECATED.
  // THEY ARE KEPT HERE TO SUPPORT RUNNING SAVED SCENARIOS.
  // Instead of using moveTypes, please change your code to use moveTypeList.
  5: list<string> moveTypes;
  8: i32 randomContainerBatchSize = 10;
  17: i32 swapNConcurrentObjects = 1;
  18: list<string> swapNSourceObjects;
  19: list<list<string>> swapNDestinationScope;
  20: i32 swapNIterations = 1000000;
  24: optional string replicaDropPartition;
  25: optional string replicaDropScope;
  26: optional string specialContainer;
  28: optional string scopeItemMovesScope;
  29: optional string groupMovesPartition;
  30: i32 nSampleSetsToExplore = 2;
  32: optional i32 fixedSrcDstMoveTypeSampleSize;
  34: optional GroupRoutingMoveTypeSpec groupRoutingMoveTypeSpec;
  35: optional SingleChainMoveTypeSpec singleChainMoveTypeSpec;
  36: optional RasLocalSearchMetadata rasLocalSearchMetadata;
  39: optional SingleChainMoveTypeSpec singleChainFastMoveTypeSpec;
  41: optional SingleFixedSourceMoveTypeSpec singleFixedSourceMoveTypeSpec;
}

@thrift.ReserveIds{ids = [4, 5, 6, 8, 9, 10, 11, 13, 14]} //deprecated fields
struct LocalSearchStageSpec {
  1: optional string name;
  2: i32 begin;
  3: i32 end;
  7: optional i32 minRuntimeSec;
  12: optional i32 stopAfterMovesTillStage;
  15: LocalSearchSolverSpec solverSpec;
  // this stage focuses on improving objective from tuple position ['begin', 'end'); all the objectives in [0, 'begin') are
  // considered as higher priority objectives and by default they will never be worsened
  // however, the user can change this behavior by setting the 'higherPriorityObjConfig' field below
  16: optional Types.HigherPriorityObjectivesConfig higherPriorityObjConfig;
}

struct MultiStageConfig {
  1: set<i32> stageIds;
  2: optional i32 moveLimit;
  3: optional double solveTime;
}

@thrift.ReserveIds{ids = [1, 5]} //deprecated fields
struct LocalSearchStageSolverSpec {
  // Stage specific settings
  2: list<LocalSearchStageSpec> stageSpecs;
  // Master settings
  3: optional i32 solveTime;
  4: optional i32 stopAfterMoves;
  // if a value is set below, then it overrides anything mentioned in any 'stageSpecs.solverSpec.recomputeContainerOrderingAfterEveryMove'
  6: optional bool recomputeContainerOrderingAfterEveryMove;
  // The flag below is used to control whether while performing localSearch the solver should explore moves from containers
  // that are not part of the objective considered in a stage; if a value is set below, then it overrides anything mentioned in
  // any 'stageSpecs.exploreMovesFromContainersNotInObjective'
  7: optional bool exploreMovesFromContainersNotInObjective;
  8: list<MultiStageConfig> multiStageConfigs;
  // if set, the solver will use custom equivalence sets to perform
  // local search across all stages. If this value is overridden in a stage,
  // then the stage specific equivalence sets will be used
  9: optional CustomEquivalenceSetConfig customEquivalenceSetConfig;
  // if a value is set, then it overrides anything mentioned in any 'stageSpecs.solverSpec.hotObjectOrderingOptions'
  10: optional HottestTraversalConfig hottestTraversalConfig;

  // If set, overrides parallel execution config for all stages
  11: optional ParallelExecutionConfig parallelExecutionConfig;
}

///////////////////////////////////////////////////////
///// SPECS AND UTILS RELATED to Optimal Solver
///////////////////////////////////////////////////////

enum OptimalSolverPackage {
  XPRESS = 0,
  GUROBI = 1,
  HIGHS = 2,
}

struct SolverOutputFiles {
  1: string solverLogFile = "";
  // Problem file name including file type. Format: file name and type separated by .
  // File types supported: Xpress (.mps/.svf) Gurobi (.mps/.mst./.hnt/.prm)
  // When suffix (.tar) is specified, a .tar file will be generated with all files compressed
  2: string solverProblemFile = "";
}

// Optimal solver supports specifying multiple objectives
// and will interpret them in following two ways
enum MultiObjectiveSolveType {
  // linearly combine all objectives into one expression
  BLENDED = 0,
  // objectives are listed in order of priority and solver will solve
  // for objectives in that order ensuring that when solving for position i,
  // prior objectives are not worsened
  HIERARCHICAL = 1,
}

struct MultiObjectiveSolveSettings {
  1: MultiObjectiveSolveType solveType = MultiObjectiveSolveType.HIERARCHICAL;
  // if set, the solver will only consider objectives in the range [firstObjectiveIdx, N-1]
  // where N is the number of objectives
  2: optional i32 firstObjectiveIdx;
  // if set, the solver will only consider objectives in the range [irstObjectiveIdx or 0 if former is not given, lastObjectiveIdx]
  3: optional i32 lastObjectiveIdx;
  // when using multiple objectives, the solver considers all the objectives in the interval
  // ['multiObjSolveSettings.firstObjectiveIdx', 'multiObjSolveSettings.lastObjectiveIdx');
  // when solving for objective at position i, by default it will not worsen any of the objectives in tuple position 0 to i-1.
  // however, the user can change this behavior by setting the 'higherPriorityObjConfig' field below.
  4: optional Types.HigherPriorityObjectivesConfig higherPriorityObjConfig;
  5: optional map<string, Types.PerObjectiveValue> paramNamesToValues;
}

@thrift.ReserveIds{ids = [4, 15]} //deprecated fields
struct OptimalSolverSpec {
  1: bool printFullLp = false;
  2: bool skipInitialAssignmentHint = false;
  3: optional i32 solveTime;
  5: bool suppressLogs = false;
  // using xpress definition for keys
  // such as XPRS_FEASTOL, but in string format
  6: map<string, double> xpressArgs;
  // TODO (ztjiang) deprecated xpressLogFile field after migrating to SolverOutputFile
  7: string xpressLogFile = "";
  8: double xpressTolerance = 1e-8;
  9: bool skipMipSolveForTesting = false;
  10: OptimalSolverPackage solverPackage = OptimalSolverPackage.XPRESS;
  11: bool lpExprSubstitution = false;
  12: bool enablePartitionHeuristic = false;
  13: bool simplifyLpProblem = false;
  14: SolverOutputFiles solverOutputFiles;
  16: MultiObjectiveSolveSettings multiObjSolveSettings;
}

struct OptimalSubsetSolverSpec {
  1: list<string> alwaysChosenContainers;
  2: map<string, double> containerChoice;
  // this denotes the number of sub problem failures the solver
  // will ignore. Since the subset solver iterates over many sub-problems
  // it is possible that a few of the fail, without affecting complete
  // solve. In case of first maxSubproblemErrors failures, the subproblem
  // is simply ignored.
  3: i32 maxSubproblemErrors = 1;
  // 0 means unlimited runs, so overall time is used
  4: i32 maxSubsetRuns;
  5: OptimalSolverSpec optimalConfig;
  6: double overallTime = 0;
  7: double perSubsetTime = 0.5;
  8: bool suppressLogs;
  9: optional i32 assignmentVarBudget;
  // Map of object name to container name. Represents the moves to be applied
  // when the destination is a frozen container internally.
  10: map<string, string> regrettableMoves;
}

///////////////////////////////////////////////////////
///// UTIL STRUCTS USED BY MOVE TYPE SPECS
///////////////////////////////////////////////////////

/*==========================================================================
 Next few structs define how to specify select bins (destinations) to explore
===========================================================================*/

struct MoveToCurrentScopeItemSpec {
  // for an object O, if S is the scope name specified below, I is the current scopeItem O is present in, and
  // C is the set of containers that are part of I, then the destination set is only the ones in C
  // (which in turn may or may not be sampled depending on the move type this is used with).
  // If O is not present in scope S, then by default all destinations are considered
  1: string scopeNameForExploringMovesToCurrentScopeItem;
}

// Filters candidate scope items by a dimension of the moving object, so a move
// type explores only scope items that are better than the current one, plus
// ties when includeEqual is set. See ScopeItemList.filter.
struct ObjectDimensionBasedFilter {
  // The object dimension whose value is used to compare candidate scope items. It
  // must be a dynamic object dimension defined on the enclosing ScopeItemList's scope (scopeName)
  1: string dimensionName;
  // Whether a lower value should be considered better (e.g. latency). If false, higher is better.
  2: bool preferLower = true;
  // Also keep scope items that tie the current one, not just better ones.
  3: bool includeEqual = false;
}

union ScopeItemFilter {
  1: ObjectDimensionBasedFilter objectDimensionBased;
}

@thrift.ReserveIds{ids = [3]}
struct ScopeItemList {
  1: string scopeName;
  // if scopeItems are not explicitly listed, all scopeItems in the specified scopeName are taken
  2: optional list<string> scopeItems;
  // If set, restrict exploration to candidate scope items that the selected
  // ScopeItemFilter considers better than the moving object's current scope
  // item, plus ties when the filter includes equal values. If unset, or the
  // object has no scope item in this scope, all candidates are explored. See
  // ScopeItemFilter for the strategies.
  4: optional ScopeItemFilter filter;
}

struct GroupToScopeItemList {
  1: string partitionName;
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, ScopeItemList> groupToScopeItemList;
}

struct MoveToScopeItemsSpec {
  // For an object `O`, only attempt moves to the containers that are part some scope item in `S`,
  // where `S = objectToScopeItem[O]` if `O` is in that map, else `S = defaultScopeItems`.
  // If scopeItemsPerGroups is used, then the scopeItems in the map are used instead of defaultScopeItems
  // If the object is in both objectToScopeItem and scopeItemsPerGroups, then the scopeItems in objectToScopeItems are used
  1: ScopeItemList defaultScopeItems;
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, ScopeItemList> objectToScopeItems;
  3: GroupToScopeItemList scopeItemsPerGroups;
}

union DestinationsToExploreOptions {
  1: MoveToCurrentScopeItemSpec moveToCurrentScopeItem;
  2: MoveToScopeItemsSpec moveToScopeItems;

  // destinationToExploreName is used to by the move type to refer which
  // DestinationsToExploreOptions is used in the universe
  // Note: you must save another DestinationsToExploreOptions in universe with
  // this as the name and one of the other options for this field to work
  9999: string destinationToExploreName;
}

/*==========================================================================
 Next few structs define how to specify select objects to attempt moves with
===========================================================================*/

struct GroupList {
  // all groups in the partition are considered
  1: string partitionName;
}

struct ObjectsFromGroupsSpec {
  // `groupList` dictates which objects are eligible for consideration
  1: GroupList groupList;
  // When bundleSize is provided, we will select excatly `bundleSize` objects
  // from each group
  2: optional i32 bundleSize;
}

union ObjectsToExploreOptions {
  1: ObjectsFromGroupsSpec objectsFromGroupsSpec;
}

struct SampleSize {
  1: i32 defaultSampleSize;
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, i32> objectToSampleSize;
}

struct StringKeyValueMap {
  1: string defaultValue;
  @cpp.Type{template = "folly::F14FastMap"}
  2: map<string, string> value;
}

///////////////////////////////////////////////////////
///// MOVE TYPE SPEC DEFINITIONS
///////////////////////////////////////////////////////

// **************************************************
// A union containing all supported move types specs.
// **************************************************
@thrift.ReserveIds{ids = [5, 7, 28, 29]} //deprecated fields
union MoveTypeSpec {
  1: SingleMoveTypeSpec singleMoveTypeSpec;
  2: SwapMoveTypeSpec swapMoveTypeSpec;
  3: TripleLoopMoveTypeSpec tripleLoopMoveTypeSpec;
  4: KLSearchMoveTypeSpec klSearchMoveTypeSpec;
  6: FixedDestMoveTypeSpec fixedDestMoveTypeSpec;
  // 8-10 are exclusively used by RAS stackable solve
  8: FixedSrcMultiMoveTypeSpec fixedSrcMultiMoveTypeSpec;
  9: FixedDestMultiMoveTypeSpec fixedDestMultiMoveTypeSpec;
  10: FixedDestSwapMultiMoveTypeSpec fixedDestSwapMultiMoveTypeSpec;
  11: SingleRandomObjectStratifiedMoveTypeSpec singleRandomObjectStratifiedMoveTypeSpec;
  12: GroupRoutingMoveTypeSpec groupRoutingMoveTypeSpec;
  13: SingleChainMoveTypeSpec singleChainMoveTypeSpec;
  14: SingleChainFastMoveTypeSpec singleChainFastMoveTypeSpec;
  15: SingleFixedSourceMoveTypeSpec singleFixedSourceMoveTypeSpec;
  16: SingleFastMoveTypeSpec singleFastMoveTypeSpec;
  17: GroupMoveWithHintStrategiesMoveTypeSpec groupMoveWithHintStrategiesMoveTypeSpec;
  18: SingleGreedyMoveTypeSpec singleGreedyMoveTypeSpec;
  19: SwapNMoveTypeSpec swapNMoveTypeSpec;
  20: SingleRandomBatchesMoveTypeSpec singleRandomBatchesMoveTypeSpec;
  21: SwapFullContainersMoveTypeSpec swapFullContainersMoveTypeSpec;
  22: SwapFullWithEmptyContainersMoveTypeSpec swapFullWithEmptyContainersMoveTypeSpec;
  23: SingleEndChainMoveTypeSpec singleEndChainMoveTypeSpec;
  24: ReplicaDropMoveTypeSpec replicaDropMoveTypeSpec;
  25: ColocateGroupsMoveTypeSpec colocateGroupsMoveTypeSpec;
  26: GreedyGroupToScopeItemMoveTypeSpec greedyGroupToScopeItemMoveTypeSpec;
  27: SingleRandomStratifiedMoveTypeSpec singleRandomStratifiedMoveTypeSpec;

  // We also support a variant of a MoveTypeSpec with just one field, a string name.
  // This variant should go away once we have migrated every move type to a specific struct definition.
  999: string moveTypeName;
}

struct SingleMoveTypeSpec {}

struct SingleFastMoveTypeSpec {
  1: optional DestinationsToExploreOptions destinationsToExplore;
  2: i32 minHotObjects = 1;
}

struct SingleGreedyMoveTypeSpec {}

struct SwapFullWithEmptyContainersMoveTypeSpec {}

struct SwapNMoveTypeSpec {
  1: i32 swapNConcurrentObjects = 1;
  2: list<string> swapNSourceObjects;
  3: list<list<string>> swapNDestinationScope;
  4: i32 swapNIterations = 1000000;
}

struct SingleRandomBatchesMoveTypeSpec {
  1: i32 randomContainerBatchSize = 10;
}

struct SwapFullContainersMoveTypeSpec {}

struct SingleEndChainMoveTypeSpec {}

struct SwapMoveTypeSpec {
  1: optional string partitionNameToExploreSwapsWithinObjectGroup;
  // src = hot container, dst = cold container
  // swap src objects one by one, exit early if successful
  2: bool greedyOnSrc = false;
  // attempt dst objects one by one, exit early if successful
  3: bool greedyOnDst = false;
  // settings that specify how to select the destinations (cold containers)
  // by default, the SWAP may explore all cold containers
  4: optional DestinationsToExploreOptions destinationsToExplore;
  // If provided, sample a subset of objects on both src and dst side
  5: optional SampleSize sampleSize;
  // If provided, this option specifies the dimension name used to determine
  // the swap ratio between hot and cold objects. The swap direction (1:k or
  // k:1) is auto-detected based on which object is heavier:
  //   hot > cold → 1:k swap (bundle k cold objects)
  //   cold > hot → k:1 swap (bundle k hot objects)
  //   equal → 1:1 swap
  6: optional StringKeyValueMap swapRatioDimension;
  // If provided, this option dictates which scope items require objects to be moved in bundles, and information
  // on how those bundles should be formed.
  7: optional ObjectBundleFormationHints objectBundleFormationHints;
}

struct TripleLoopMoveTypeSpec {}

struct KLSearchMoveTypeSpec {}

struct GroupRoutingMoveTypeSpec {
  1: string routingConfigName;
  2: optional string unassignedContainer;
}

struct SingleChainMoveTypeSpec {
  1: optional string partitionNameToExploreChainsWithinObjectGroup;
  2: optional string specialColdContainer;
}

struct SingleChainFastMoveTypeSpec {
  1: optional string partitionNameToExploreFastChainsWithinObjectGroup;
  2: optional string specialFastColdContainer;
}

struct FixedDestMoveTypeSpec {
  // If provided, sample a subset of objects from dest
  1: optional SampleSize sampleSize;
  2: optional string specialContainer;
  // If provided, this option dictates which scope items require objects to be moved in bundles, and information
  // on how those bundles should be formed.
  3: optional ObjectBundleFormationHints objectBundleFormationHints;
}

// the next three move types are exclusively used for RAS stackable solve
struct FixedSrcMultiMoveTypeSpec {
  // Number of samples of objects we should select for each equivalent set
  // (gives better quality for on-demand equivalent sets)
  1: i32 maxSamplesPerEquivSet = 5;
  2: optional string specialContainer;
  @cpp.Ref{type = cpp.RefType.Shared}
  3: optional RasLocalSearchMetadata rasLocalSearchMetadata;
}

struct FixedDestMultiMoveTypeSpec {
  1: i32 maxSamplesPerEquivSet = 5;
  2: optional string specialContainer;
  @cpp.Ref{type = cpp.RefType.Shared}
  3: optional RasLocalSearchMetadata rasLocalSearchMetadata;
}

struct FixedDestSwapMultiMoveTypeSpec {
  // if greedyOnSrc is set to true, we will attempt SWAPS to destination container in a greedy way.
  // Let SRC, DST be set of candidate objects in src and dst containers.
  // Traditionally, we will try the cross product SRC x DST possible swaps
  // If isGreedyOnSrc is set, we will do the following
  //    - for obj in SRC :  try swaps from {obj} x DST, exit early once successful
  1: bool greedyOnSrc = false;
  2: i32 maxSamplesPerEquivSet = 5;
  3: optional i32 maxSampleSizeOnSrc;
  4: optional i32 maxSampleSizeOnDst;
  5: optional string specialContainer;
  @cpp.Ref{type = cpp.RefType.Shared}
  6: optional RasLocalSearchMetadata rasLocalSearchMetadata;
}

@thrift.ReserveIds{ids = [3]}
struct SingleRandomStratifiedMoveTypeSpec {
  1: DestinationsToExploreOptions destinationsToExplore;
  2: SampleSize stratifiedSampleSize;
  // Minimum number of objects in the hot container to fully explore per move
  4: i32 minObjectsToExplore = 1;
}

struct ObjectBundleFormationHints {
  @cpp.Type{template = "folly::F14FastMap"}
  1: optional map<
    string,
    ObjectsToExploreOptions
  > scopeItemToObjectsToExploreOptions;
  2: string scopeName;
  // If this field is set to true, for containers that have a partial initial assignment, bundle size for the move is
  // overridden to complement the initial assignment, such that the final assignment will satisfy the bundle size requirement
  // from `scopeItemToObjectsToExploreOptions`.
  // For example, for a container with a bundle size hint of 4 and an initial assignment of 2 objects in a specifc group,
  // objects will be moved in bundles of size 2 from this group to satisfy the container's bundle size requirement of 4.
  3: optional bool adjustBundleSizeForIncompleteBundles;
}

// This move type aims to improve the objective of a hot container by considering moves from a
// pre-determined subset of source containers to the hot container under consideration.
// The pre-determined source container can be a single `specialContainer`, or containers in
// specific scope items under a scope.
struct SingleFixedSourceMoveTypeSpec {
  // scopeItemsNames order will match the order in which
  // SingleFixedSourceMoveType will try moves
  1: ScopeItemList scopeItemList;
  // When set to true, stops after finding first scope item with a move that improves the objective
  // When set to false, it tries moves from all scope items
  2: bool stopEarlyAtScopeItemThatImprovesObjective = false;
  // Only one of scopeItems and specialContainer will be used. If both present, we will use scopeItems.
  3: optional string specialContainer;
  // If provided, sample a subset of objects from src
  4: optional SampleSize sampleSize;
  // If provided, this option dictates which scope items require objects to be moved in bundles, and information
  // on how those bundles should be formed.
  5: optional ObjectBundleFormationHints objectBundleFormationHints;
}

struct SingleRandomObjectStratifiedMoveTypeSpec {
  1: SampleSize stratifiedSampleSize;
  3: ObjectsToExploreOptions objectsToExploreOptions;
}

@cpp.EnableCustomTypeOrdering
struct SecondaryGroupReplacementConfig {
  1: StringToStringSetMap secondaryGroupToAllowedReplacements;
}

struct GroupMoveWithHintStrategiesMoveTypeSpec {
  1: string primaryPartition;
  2: string secondaryPartition;
  3: MoveStrategies moveStrategies;
  4: optional string unassignedContainer;
  // When `unassignedContainer` is specified, an allocated secondary group can be replaced by a different secondary group.
  // If the parameter `secondaryGroupToAllowedReplacements` has an entry w.r.t. secondary group `G` and it maps to a subset of the groups in secondary partition,
  // then it can only be replaced by groups that are part of the specified subset. If there is no entry w.r.t. `G`, then it can be replaced by any group.
  // Note that this field will not be used when `unassignedContainer` is not specified.
  5: SecondaryGroupReplacementConfig secondaryGroupReplacementConfig;
}

struct ReplicaDropMoveTypeSpec {
  1: string replicaDropPartition;
  2: string replicaDropScope;
}

// Prunes candidate moves before full (expensive) evaluation: a candidate that
// would break any of the named hard constraints is dropped early. This only
// steers which candidates are explored; the full evaluator still decides every
// move.
struct MoveCandidatePruningSpec {
  1: list<string> constraintNames;
}

struct GreedyGroupToScopeItemMoveTypeSpec {
  1: string scopeItemMovesScope;
  2: string groupMovesPartition;
  // nSampleSetsToExplore determines how many sample sets are explored per scopeItem. If
  // the value is set to k, we explore moving a group to k sets of containers in a
  // scopeItem, where each set has the same size as the number of objects in the
  // group and the containers included in a set is random.
  3: i32 nSampleSetsToExplore = 2;
  // Specify where each group may move. Use scopeItemsPerGroups to restrict specific
  // groups to a subset of scope items. All scope item in 'scopeItemMovesScope'
  // are explored if this field is not specified.
  4: optional DestinationsToExploreOptions destinationsToExplore;
  // Prune candidate destinations that would break one of the named hard
  // constraints before full (expensive) evaluation. Empty list means no
  // pruning.
  5: MoveCandidatePruningSpec candidatePruning;
}

enum MoveStrategyType {
  RANDOM_SAMPLING_WITH_REPLACEMENT = 0,
  RANDOM_SAMPLING_WITHOUT_REPLACEMENT = 1,
}

struct MoveStrategy {
  1: MoveStrategyType type;
  2: i32 moveSetsGeneratedPerScopeItem = 1;
  3: MoveToScopeItemsSpec moveToScopeItems;
  4: optional string tertiaryPartition;
  // this parameter should only be used when `tertiaryPartition` field is set.
  // If set to k, and there are g groups in tertiaryPartition, then we first create k tuples [ (s_i^1, ..., s_g^1), ..., (s_i^k, ..., s_g^k)],
  // where each s_i^j is a scope item selected at random from the scope items in specified in `moveToScopeItems`
  5: optional i32 numScopeItemsToExplorePerTertiaryGroup;
  // If true, each tuple generated above contains no repeated scope item:
  // two tertiary groups cannot select the same destination scope item.
  6: bool distinctScopeItemsAcrossTertiaryGroups = false;
}

struct MoveStrategies {
  1: map<string, MoveStrategy> groupToMoveStrategy;
}

struct ColocateGroupsMoveTypeRelatedGroupsInfo {
  // each group in the set below is related to others groups in the set.
  1: StringSet relatedGroups;
  // each set of related groups can have their own destinationScopeItems where they can be colocated.
  // If this is not set, then all the scope items in colocationScope will be used
  2: optional StringSet destinationScopeItems;
}

struct ColocateGroupsMoveTypeSpec {
  1: string partitionName;

  // each group in the inner set (ColocateGroupsMoveTypeRelatedGroupsInfo.relatedGroups) is related to
  // others groups in the set. Across all ColocateGroupsMoveTypeRelatedGroupsInfo, the sets need to be disjoint
  2: list<ColocateGroupsMoveTypeRelatedGroupsInfo> relatedGroupsList;

  // for each set of related groups, scope in which they need to be colocated by default (for example, each scope item could be a region).
  // NOTE: for a set in RelatedGroupsInfo defined above, if the destinationScopeItems is set in the corresponding RelatedGroupsInfo, then that is taken
  3: string colocationScopeName;

  // for  each scope item in colocationScopeName and for each group in the given partition, only attempt moves to containers
  // that are part of the scope items listed for that (group, scope item). For example, colocation scope = region,
  // groupToColocationScopeItemToContainers = {group1: {region1: {region1_flow1, region1_flow3}}}}
  // if nothing is provided, then for each group and each colocationScopeItem, all the containers are considered
  @cpp.Type{template = "folly::F14FastMap"}
  4: map<string, StringToStringSetMap> colocationScopeItemToGroupToContainers;

  // for each (group, scope item in colocationScopeItems), allow sampling of containers from the set of valid containers
  // If nothing is provided for a (group, colocationScopeItem), all destinations would be considered
  5: optional i32 defaultSampleSize;
}
