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

#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/tests/utils.h"

#include <folly/Benchmark.h>
#include <folly/container/irange.h>
#include <folly/init/Init.h>
#include <folly/Random.h>

namespace interface = facebook::rebalancer::interface;

static void createAssignment(
    std::unique_ptr<interface::ProblemSolver>& solver,
    int objectsCount,
    int containersCount) {
  // create number of hosts
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(containersCount)) {
    initialAssignment.emplace(
        fmt::format("host{}", i), std::vector<std::string>());
  }

  // create fake_target_host
  initialAssignment.emplace("fake_target_host", std::vector<std::string>());

  // First 240 hosts will get 111 tasks
  int curTask = 0;
  for (const auto i : folly::irange(240)) {
    auto host = fmt::format("host{}", i);
    for (const auto _ : folly::irange(111)) {
      auto task = fmt::format("task{}", curTask);
      initialAssignment[host].push_back(task);
      ++curTask;
    }
  }
  // put rest in remaining hosts except 240 of them
  // This will ensure there will be at least 240 hosts can receive extra tasks
  for (; curTask < objectsCount; curTask++) {
    auto task = fmt::format("task{}", curTask);
    auto host = fmt::format(
        "host{}", folly::Random::rand32(240, containersCount - 240));

    initialAssignment[host].push_back(task);
  }

  solver->setAssignment(initialAssignment);
}

static void addComputeDimensions(
    std::unique_ptr<interface::ProblemSolver>& solver,
    int objectsCount,
    int containersCount) {
  std::map<std::string, double> compute_object_map;
  for (const auto i : folly::irange(objectsCount)) {
    compute_object_map[fmt::format("task{}", i)] = 1;
  }
  solver->addObjectDimension("compute", std::move(compute_object_map));

  std::map<std::string, double> compute_container_map;
  for (const auto i : folly::irange(containersCount)) {
    compute_container_map[fmt::format("host{}", i)] = 110;
  }
  solver->addContainerDimension("compute", compute_container_map);
}

static void addStorageDimension(
    std::unique_ptr<interface::ProblemSolver>& solver,
    int objectsCount,
    int containersCount,
    int index) {
  // Randomize the dimensions of storage,
  // since on average, we won't expect more than 110 tasks per host
  // and the expected value of folly::Random::rand32(1, 10)
  // is around 5.5, average storage utilization should be less than 1000
  auto dimension = fmt::format("storage{}", index);
  std::map<std::string, double> storage_object_map;
  for (const auto i : folly::irange(objectsCount)) {
    storage_object_map[fmt::format("task{}", i)] = folly::Random::rand32(1, 10);
  }
  solver->addObjectDimension(dimension, std::move(storage_object_map));

  std::map<std::string, double> storage_container_map;
  for (const auto i : folly::irange(containersCount)) {
    storage_container_map[fmt::format("host{}", i)] = 1000;
  }
  solver->addContainerDimension(dimension, storage_container_map);
}

static void createStorageCapacityConstraint(
    std::unique_ptr<interface::ProblemSolver>& solver,
    int index) {
  auto dimension = fmt::format("storage{}", index);
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = std::move(dimension);
  capacitySpec.bound() = interface::CapacitySpecBound::MAX;
  capacitySpec.definition() =
      interface::CapacitySpecDefinition::DURING_AND_AFTER;

  interface::Limit limit;
  limit.type() = interface::LimitType::RELATIVE;
  limit.globalLimit() = 1;
  capacitySpec.limit() = std::move(limit);
  capacitySpec.zeroAllowed() = false;

  solver->addConstraint(std::move(capacitySpec));
}

static void createSolvableConstraint(
    std::unique_ptr<interface::ProblemSolver>& solver) {
  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "host";
  capacitySpec.dimension() = "compute";
  capacitySpec.bound() = interface::CapacitySpecBound::MAX;
  capacitySpec.definition() =
      interface::CapacitySpecDefinition::DURING_AND_AFTER;

  interface::Limit limit;
  limit.type() = interface::LimitType::RELATIVE;
  limit.globalLimit() = 1;
  capacitySpec.limit() = std::move(limit);
  capacitySpec.zeroAllowed() = false;

  solver->addConstraint(std::move(capacitySpec));
}

static void run(int objectsCount, int containersCount, int numberOfDimensions) {
  /*
  This benchmark was created to mimic the issue that arised out of SM
  zippydb SEV0 (T184979682)
  Scenario:
  - object count is the number of tasks, which is 1785756
  - container count is the number of hosts, which is 27766
  - number of dimension constraints, which is 7, 6 different storage dimensions,
  1 compute constraint

  - all 6 storage dimensions have these properties:
    - max storage capacity for each container is 1000
    - each task will have a storage dimension between 1-10
    - this implies that each task will have an average storage dimension of 5.5
    - because the average storage dimension is 5.5, at 100 taks per container,
  we expect around 55% storage utilization

  - compute constraint will have these properties:
    - max compute capacity for each container is 110
    - each task will have a compute dimension of 1
    - for the first 240 hosts, we will have 111 tasks
    - this means that the relative utilization of compute for the first 240
  hosts will be greater than 1
    - this means we will have at least 240 moves to meet the compute capacity
  constraint

  The reason why this is a problematic scenario for Rebalancer originally is
  because all the constraints uses `during_and_after` definition, which means
  that they will all use the `during` formulation. Since during formulation is
  used, the solver will not be able to improve on the initial lowerbound as
  during can only ever be strictly increasing and can never improve

  With the new `BoundsOverride` expression, we can fix lowerbound to be the
  initial lowerbound as the initial lowerbound is the best possible solution

  With this benchmark, we should expect to see 240 moves in around 2-3~ seconds
  which is much better than the 137~ seconds from the SEV0

  */

  auto solver = interface::initializeTestProblemSolver();
  solver->setObjectName("task");
  solver->setContainerName("host");

  createAssignment(solver, objectsCount, containersCount);

  addComputeDimensions(solver, objectsCount, containersCount);

  for (const auto i : folly::irange(numberOfDimensions)) {
    addStorageDimension(solver, objectsCount, containersCount, i);
    createStorageCapacityConstraint(solver, i);
  }

  std::vector<std::vector<std::string>> scopeItemsList(16);
  for (const auto i : folly::irange(containersCount)) {
    auto host = fmt::format("host{}", i);
    const int randNum = folly::Random::rand32(0, 14);
    scopeItemsList[randNum].push_back(host);
  }
  scopeItemsList[15].emplace_back("fake_target_host");
  solver->addSimilarContainers(scopeItemsList);

  createSolvableConstraint(solver);

  // Since there is 111 objects in 240 hosts
  // and the compute capacity for each host is only 110
  // we will expect to see at least 240 moves
  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec("SINGLE_RANDOM_STRATIFIED"));
  solverSpec.stratifiedSampleSize() = 100;
  solverSpec.solveTime() = 300;
  solverSpec.allowedPlateauTime() = 60;
  solverSpec.exploreMovesFromContainersNotInObjective() = false;

  interface::LocalSearchStageSpec stageSpec;
  stageSpec.solverSpec() = std::move(solverSpec);
  stageSpec.begin() = 0;
  stageSpec.end() = 1;

  interface::LocalSearchStageSolverSpec stageSolverSpec;
  stageSolverSpec.stageSpecs() = {std::move(stageSpec)};

  solver->addSolver(stageSolverSpec);
  // Get a solution from Rebalancer.
  solver->solve();
}

BENCHMARK(MultipleConstraintsThatCantBeImproved) {
  run(1785756, 27766, 6);
}

BENCHMARK(MarkContainersWithInitialDuringViolationsAsNonAccepting) {
  constexpr int containerCount = 10e3;
  constexpr int tasksPerOccupiedHost = 1000;

  auto solver = interface::initializeTestProblemSolver();
  solver->setObjectName("task");
  solver->setContainerName("host");

  // create number of hosts
  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(containerCount)) {
    initialAssignment.emplace(
        fmt::format("host{}", i), std::vector<std::string>());
  }

  // first 5000 containers will have 1000 tasks each; next 5000 wil have none
  for (const auto i : folly::irange(containerCount / 2)) {
    for (const auto j : folly::irange(tasksPerOccupiedHost)) {
      auto task = fmt::format("task{}_{}", j, i);
      initialAssignment[fmt::format("host{}", i)].push_back(task);
    }
  }

  solver->setAssignment(initialAssignment);

  {
    // this capacity spec results in all occupied containers having a violation
    // (by one object)
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "host";
    capacitySpec.dimension() = "task_count";
    capacitySpec.definition() =
        interface::CapacitySpecDefinition::DURING_AND_AFTER;
    capacitySpec.bound() = interface::CapacitySpecBound::MAX;
    capacitySpec.limit()->globalLimit() = tasksPerOccupiedHost - 1;
    capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;

    solver->addConstraint(std::move(capacitySpec));
  }

  {
    // mark all objects in the first 4000 containers as fixed; this means the
    // during violations of those containers cannot be fixed
    interface::AvoidMovingSpec avoidMovingSpec;
    std::vector<std::string> fixedObjects;
    for (const auto i : folly::irange(4000)) {
      for (const auto j : folly::irange(tasksPerOccupiedHost)) {
        fixedObjects.push_back(fmt::format("task{}_{}", j, i));
      }
    }

    avoidMovingSpec.objects() = std::move(fixedObjects);
    solver->addConstraint(std::move(avoidMovingSpec));
  }

  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleMoveTypeSpec{}));

  solver->addSolver(solverSpec);

  solver->solve();
}

BENCHMARK(UtilizationCapOptimizedRepresentation) {
  // if the materializer does not use the optimized representation for
  // utilization cap, the following problem setup will get stuck in
  // materialization. With the optimized representation, the solve finishes in <
  // 3s.
  auto solver = interface::initializeTestProblemSolver();
  solver->setObjectName("object");
  solver->setContainerName("container");
  // create initial assignment
  std::map<std::string, std::vector<std::string>> initialAssignment;
  constexpr int numContainers = 1000;
  for (const auto i : folly::irange(numContainers)) {
    initialAssignment.emplace(
        fmt::format("container{}", i), std::vector<std::string>());
  }
  std::vector<std::string> objects;
  std::map<std::string, std::string> objectToGroup;
  // 10k groups, each with 100 objects = 1M objects
  constexpr int numGroups = 10000;
  constexpr int numObjectsPerGroup = 100;
  for (const auto i : folly::irange(numGroups)) {
    auto group = fmt::format("group{}", i);
    for (const auto j : folly::irange(numObjectsPerGroup)) {
      auto object = fmt::format("object_{}_{}", i, j);
      objectToGroup[object] = group;
      objects.push_back(object);
    }
  }
  initialAssignment["unassigned"] = std::move(objects);
  solver->setAssignment(initialAssignment);
  solver->addPartition("valid_group", std::move(objectToGroup));

  // one of the 10k groups is invalid, remaing 99 are valid
  int numValidObjects = (numGroups - 1) * numObjectsPerGroup;
  interface::CapacitySpec capacitySpec;
  capacitySpec.bound() = interface::CapacitySpecBound::MAX;
  capacitySpec.scope() = "container";
  capacitySpec.name() = "all containers must have at most 999.9k objects";
  capacitySpec.dimension() = "object_count";
  capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
  capacitySpec.limit()->globalLimit() = numValidObjects;
  interface::GroupUtilizationBound bound;
  bound.partitionName() = "valid_group";
  bound.perGroupValues()->globalLimit() = numValidObjects;
  bound.perGroupValues()->groupLimits() = {{"group0", 0}};
  // setting the default limit to be unbounded is important to ensure that
  // the resulting representation is compact (only "bounds" the utilization of
  // invalid group). Setting it to false will create |groups|* |containers|
  // expression which will cause the materializer to get stuck
  bound.perGroupValues()->isDefaultLimitUnbounded() = true;
  capacitySpec.utilizationBound().emplace();
  capacitySpec.utilizationBound()->set_groupUtilizationBound(std::move(bound));
  // we expect that this constraint can be satisfied because the invalid group
  // utilization is capped at 0
  solver->addConstraint(std::move(capacitySpec));

  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleMoveTypeSpec{}));
  solver->addSolver(solverSpec);
  solver->solve();
}

BENCHMARK(ConstraintsShortCircuiting) {
  /*
  This benchmark is to show the usefulness of short circuiting constraint
  evaluations in MovesEvaluator. For details, see D87305036
  */
  folly::BenchmarkSuspender suspender;
  constexpr int containerCount = 500;
  constexpr int objectCount = 1e3;

  auto solver = interface::initializeTestProblemSolver();
  solver->setObjectName("object");
  solver->setContainerName("container");

  folly::F14FastMap<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(containerCount)) {
    initialAssignment.emplace(
        fmt::format("container{}", i), std::vector<std::string>());
  }

  // add all objects to container0
  auto& containerOObjects = initialAssignment["container0"];
  for (const auto i : folly::irange(objectCount)) {
    containerOObjects.push_back(fmt::format("object{}", i));
  }
  solver->setAssignment(initialAssignment);

  constexpr int dimensionCount = objectCount;
  for (const auto i : folly::irange(dimensionCount)) {
    folly::F14FastMap<std::string, double> objectDimension;
    for (const auto j : folly::irange(objectCount)) {
      if (i == 0 || j % i == 0) {
        objectDimension[fmt::format("object{}", j)] = j + 2;
      }
    }
    solver->addObjectDimension(
        fmt::format("dimension_{}", i), objectDimension, /*defaultValue=*/2);
  }

  // add capacity constraint w.r.t. each dimension
  for (const auto d : folly::irange(dimensionCount)) {
    interface::CapacitySpec capacitySpec;
    capacitySpec.scope() = "container";
    capacitySpec.dimension() = fmt::format("dimension_{}", d);
    capacitySpec.definition() = interface::CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = interface::CapacitySpecBound::MAX;
    capacitySpec.limit()->globalLimit() = 0;

    solver->addConstraint(std::move(capacitySpec));
  }

  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleFastMoveTypeSpec()));
  solver->addSolver(solverSpec);
  suspender.dismiss();

  const auto solution = solver->solve();

  // we expect zero moves since every move breaks a constraint
  EXPECT_EQ(solution.assignment(), solution.initialAssignment());
}

BENCHMARK(CapacityMaterializationManyScopeItems) {
  constexpr int containerCount = 1000000;
  constexpr int objectCount = 2000000;

  auto solver = interface::initializeTestProblemSolver();
  solver->setObjectName("object");
  solver->setContainerName("container");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  for (const auto i : folly::irange(containerCount)) {
    initialAssignment[fmt::format("container{}", i)] = {};
  }
  for (const auto i : folly::irange(objectCount)) {
    const int containerId = i % containerCount;
    initialAssignment[fmt::format("container{}", containerId)].push_back(
        fmt::format("object{}", i));
  }
  solver->setAssignment(initialAssignment);

  std::map<std::string, double> objectDimension;
  for (const auto i : folly::irange(objectCount)) {
    objectDimension[fmt::format("object{}", i)] = 1;
  }
  solver->addObjectDimension("compute", std::move(objectDimension));

  std::map<std::string, double> containerDimension;
  for (const auto i : folly::irange(containerCount)) {
    containerDimension[fmt::format("container{}", i)] = 100;
  }
  solver->addContainerDimension("compute", containerDimension);

  interface::CapacitySpec capacitySpec;
  capacitySpec.scope() = "container";
  capacitySpec.dimension() = "compute";
  capacitySpec.bound() = interface::CapacitySpecBound::MAX;
  capacitySpec.definition() =
      interface::CapacitySpecDefinition::DURING_AND_AFTER;

  interface::Limit limit;
  limit.type() = interface::LimitType::RELATIVE;
  limit.globalLimit() = 1;
  capacitySpec.limit() = std::move(limit);

  solver->addConstraint(std::move(capacitySpec));

  // Only measure materialization, not solve time
  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleMoveTypeSpec{}));
  solverSpec.stopAfterMoves() = 0;
  solver->addSolver(solverSpec);

  solver->solve();
}

static void runCapacityMaterializationBenchmark(
    const bool bundled,
    const int specCount) {
  constexpr int groupSize = 3;
  constexpr int groupCount = 1000;
  constexpr int containerCount = 100;

  auto solver = interface::initializeTestProblemSolver();
  if (solver == nullptr) {
    throw std::runtime_error("failed to initialize benchmark solver");
  }
  solver->setObjectName("object");
  solver->setContainerName("container");

  std::map<std::string, std::vector<std::string>> initialAssignment;
  std::map<std::string, std::string> objectToGroup;
  std::map<std::string, double> bundleSizes;
  for (const auto containerIndex : folly::irange(containerCount)) {
    const auto container = fmt::format("container{}", containerIndex);
    initialAssignment[container] = {};
    bundleSizes[container] = groupSize;
  }
  for (const auto groupIndex : folly::irange(groupCount)) {
    const auto group = fmt::format("group{}", groupIndex);
    auto& objects = initialAssignment[fmt::format(
        "container{}", groupIndex % containerCount)];
    for (const auto objectIndex : folly::irange(groupSize)) {
      const auto object = fmt::format("object{}_{}", groupIndex, objectIndex);
      objects.push_back(object);
      objectToGroup[object] = group;
    }
  }
  solver->setAssignment(initialAssignment);
  solver->addPartition("bundle_group", std::move(objectToGroup));

  for (const auto specIndex : folly::irange(specCount)) {
    interface::CapacitySpec capacitySpec;
    capacitySpec.name() =
        fmt::format("{} capacity {}", bundled ? "bundled" : "raw", specIndex);
    capacitySpec.scope() = "container";
    capacitySpec.dimension() = "object_count";
    capacitySpec.definition() = interface::CapacitySpecDefinition::AFTER;
    capacitySpec.bound() = specIndex % 2 == 0
        ? interface::CapacitySpecBound::MAX
        : interface::CapacitySpecBound::MIN;
    capacitySpec.limit()->type() = interface::LimitType::ABSOLUTE;
    capacitySpec.limit()->globalLimit() = groupSize * groupCount;
    if (bundled) {
      capacitySpec.groupBundleUtilization().emplace();
      capacitySpec.groupBundleUtilization()->partitionName() = "bundle_group";
      capacitySpec.groupBundleUtilization()->scope() = "container";
      capacitySpec.groupBundleUtilization()->bundleSizes() = bundleSizes;
    }
    solver->addConstraint(std::move(capacitySpec));
  }

  interface::LocalSearchSolverSpec solverSpec;
  solverSpec.moveTypeList()->push_back(
      interface::ProblemSolver::makeMoveTypeSpec(
          interface::SingleMoveTypeSpec{}));
  solverSpec.stopAfterMoves() = 0;
  solver->addSolver(solverSpec);
  solver->solve();
}

BENCHMARK(RawCapacityMaterialization1Spec) {
  runCapacityMaterializationBenchmark(false, 1);
}

BENCHMARK(RawCapacityMaterialization10Specs) {
  runCapacityMaterializationBenchmark(false, 10);
}

BENCHMARK(BundledCapacityMaterialization1Spec) {
  runCapacityMaterializationBenchmark(true, 1);
}

BENCHMARK(BundledCapacityMaterialization10Specs) {
  runCapacityMaterializationBenchmark(true, 10);
}

int main(int argc, char** argv) {
  const folly::Init init(&argc, &argv);

  folly::runBenchmarks();

  return 0;
}
