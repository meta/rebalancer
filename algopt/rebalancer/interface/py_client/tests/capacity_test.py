#!/usr/bin/env python3
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


from collections import Counter

from algopt.rebalancer.interface.py_client.ProblemSolver import ProblemSolver
from facebook.algopt.common.thrift.Types.thrift_types import PrecisionTolerances
from libfb.py.testutil import BaseFacebookTestCase
from rebalancer.interface.thrift.Types.thrift_types import AssignmentSolution
from rebalancer.interface.thrift.v2.ProblemSolver.thrift_types import (
    ConstraintSpecs,
    GoalSpecs,
    SolverSpecs,
)
from rebalancer.interface.thrift.v2.ProblemSpecs.thrift_types import (
    AssignmentAffinitiesSpec,
    AssignmentAffinity,
    CapacitySpec,
    CapacitySpecBound,
    CapacitySpecDefinition,
    Limit,
    LimitType,
)
from rebalancer.interface.thrift.v2.SolverSpecs.thrift_types import (
    KLSearchMoveTypeSpec,
    LocalSearchSolverSpec,
    MoveTypeSpec,
    OptimalSolverSpec,
    SingleMoveTypeSpec,
    SwapMoveTypeSpec,
    TripleLoopMoveTypeSpec,
)


# This test is a Python translation of the C++ test at
# //algopt/rebalancer/interface/tests/CapacityTest.cpp
class TestCapacitySpec(BaseFacebookTestCase):
    def test_parallel_bounds_configuration_is_chainable(self) -> None:
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")

        self.assertIs(solver.enableParallelBoundsComputation(True), solver)
        self.assertIs(solver.enableParallelBoundsComputation(False), solver)

    def test_initially_broken_capacity_constraint(self) -> None:
        # In this example there are 2 hosts and 4 tasks. Each host has capacity for
        # 2 tasks. host0 initially breaks the constraint, as there are 3 tasks in
        # it according to the initial assignment.
        # Rebalancer should detect host0 initially breaks the constraint and fix it
        # to the extent possible. In this case, moving just one task from host0 to
        # host 1 completely fixes the constraint violation.
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setObjectName("task")
        solver.setContainerName("host")
        solver.shouldUseDynamicObjectOrdering(True)

        # host0 initially has 3 tasks in it, and host1 only 1.
        solver.setAssignment(
            {
                "host0": ["task0", "task1", "task2"],
                "host1": ["task3"],
            }
        )

        # Each task needs 10 GB of memory.
        solver.addObjectDimension(
            "memory",
            {
                "task0": 10.0,
                "task1": 10.0,
                "task2": 10.0,
                "task3": 10.0,
            },
        )

        # Each host has 20 GB of memory capacity.
        solver.addContainerDimension(
            dimensionName="memory",
            containerToValue={},
            defaultValue=20.0,
        )

        # Enforce memory capacity on hosts.
        solver.addConstraint(
            ConstraintSpecs(
                capacitySpec=CapacitySpec(
                    name="memory_capacity", scope="host", dimension="memory"
                )
            )
        )

        solver.addSolver(
            SolverSpecs(
                localSearchSolverSpec=LocalSearchSolverSpec(
                    moveTypeList=[
                        MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                        MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
                        MoveTypeSpec(tripleLoopMoveTypeSpec=TripleLoopMoveTypeSpec()),
                        MoveTypeSpec(klSearchMoveTypeSpec=KLSearchMoveTypeSpec()),
                    ],
                )
            )
        )

        # print problem setup
        solver.printProblemSetup()

        # Get a solution from Rebalancer.
        solution: AssignmentSolution = solver.solve()

        # We expect each host to contain exactly 2 tasks in the final assignment.
        tasks_in_host: Counter[str] = Counter()

        for host in solution.assignment.values():
            tasks_in_host[host] += 1

        self.assertEqual(2, tasks_in_host["host0"])
        self.assertEqual(2, tasks_in_host["host1"])

    def test_min_absolute_capacity_constraint(self) -> None:
        # In this example there are 2 hosts and 4 tasks. All tasks start in host0
        # but they have a preference for being placed in host1 instead. A minimum
        # capacity constraint enforces that each host must contain at least 1 task.
        # Rebalancer should move all tasks but 1 from host0 to host1 in order to
        # optimize the preferences without breaking the capacity constraint.
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setObjectName("task")
        solver.setContainerName("host")
        solver.shouldUseDynamicObjectOrdering(True)

        # host0 initially has 4 tasks in it, and host1 has none.
        solver.setAssignment(
            {
                "host0": ["task0", "task1", "task2", "task3"],
                "host1": [],
            }
        )

        # Make all tasks prefer being placed in host1 rather than host0.
        solver.addGoal(
            GoalSpecs(
                assignmentAffinitiesSpec=AssignmentAffinitiesSpec(
                    name="all_tasks_on_host_1",
                    scope="host",
                    affinities=[
                        AssignmentAffinity(
                            objectName="task0", scopeItemName="host1", affinity=1
                        ),
                        AssignmentAffinity(
                            objectName="task1", scopeItemName="host1", affinity=1
                        ),
                        AssignmentAffinity(
                            objectName="task2", scopeItemName="host1", affinity=1
                        ),
                        AssignmentAffinity(
                            objectName="task3", scopeItemName="host1", affinity=1
                        ),
                    ],
                )
            )
        )

        # Note: the dimension task_count is implicit and it's defined as 1 for every
        # task (object). Since we are using absolute limits, it is irrelevant how we
        # define the same dimension on the hosts (scope items).

        # Enforce min absolute capacity on hosts: at least 1 task per host.
        solver.addConstraint(
            ConstraintSpecs(
                capacitySpec=CapacitySpec(
                    name="at_least_1_task_per_host",
                    scope="host",
                    dimension="task_count",
                    bound=CapacitySpecBound.MIN,
                    limit=Limit(type=LimitType.ABSOLUTE, globalLimit=1.0),
                )
            )
        )

        solver.addSolver(
            SolverSpecs(
                localSearchSolverSpec=LocalSearchSolverSpec(
                    moveTypeList=[
                        MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                        MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
                        MoveTypeSpec(tripleLoopMoveTypeSpec=TripleLoopMoveTypeSpec()),
                        MoveTypeSpec(klSearchMoveTypeSpec=KLSearchMoveTypeSpec()),
                    ],
                )
            )
        )

        # print problem setup
        solver.printProblemSetup()

        # Get a solution from Rebalancer.
        solution = solver.solve()

        # We expect host0 to contain a single task, and host1 to contain the rest.
        tasks_in_host: Counter[str] = Counter()

        for host in solution.assignment.values():
            tasks_in_host[host] += 1

        self.assertEqual(1, tasks_in_host["host0"])
        self.assertEqual(3, tasks_in_host["host1"])

    def test_during_capacity_constraint(self) -> None:
        # In this example there are 3 hosts and 5 tasks with the following initial
        # assignment:
        # host0: task0, task1
        # host1: task2, task3
        # host2: task4
        # There's a capacity constraint with the DURING definition, limiting the
        # capacity of hosts to no more than 2 tasks each. task1 has a preference
        # for host1, and task3 has a preference for host2.
        # While it is certainly possible to move both tasks to their preferred host
        # with an AFTER definition without violating capacity (first task3 moves to
        # host2, then task1 moves to host1), it is not possible with the DURING
        # definition: task3 moves to host2, but at that point it contributes to the
        # utilization of both host1 and host2, therefore there's no room in host1 for
        # task1 to come in.
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setObjectName("task")
        solver.setContainerName("host")
        solver.shouldUseDynamicObjectOrdering(True)

        # host0 and host1 start with 2 tasks each, host2 starts with a single task.
        solver.setAssignment(
            {
                "host0": ["task0", "task1"],
                "host1": ["task2", "task3"],
                "host2": ["task4"],
            }
        )

        # task1 has a preference for host1, and task3 has a preference for host2.
        solver.addGoal(
            GoalSpecs(
                assignmentAffinitiesSpec=AssignmentAffinitiesSpec(
                    name="task_to_host_affinity",
                    scope="host",
                    affinities=[
                        AssignmentAffinity(
                            objectName="task1", scopeItemName="host1", affinity=1
                        ),
                        AssignmentAffinity(
                            objectName="task3", scopeItemName="host2", affinity=1
                        ),
                    ],
                )
            )
        )

        # Note: the dimension task_count is implicit and it's defined as 1 for every
        # task (object). Since we are using absolute limits, it is irrelevant how we
        # define the same dimension on the hosts (scope items).

        # Enforce DURING capacity on hosts: at most 2 tasks per host.
        solver.addConstraint(
            ConstraintSpecs(
                capacitySpec=CapacitySpec(
                    name="at_most_2_tasks_per_host",
                    scope="host",
                    dimension="task_count",
                    definition=CapacitySpecDefinition.DURING,
                    limit=Limit(type=LimitType.ABSOLUTE, globalLimit=2.0),
                )
            )
        )

        solver.addSolver(
            SolverSpecs(
                localSearchSolverSpec=LocalSearchSolverSpec(
                    moveTypeList=[
                        MoveTypeSpec(singleMoveTypeSpec=SingleMoveTypeSpec()),
                        MoveTypeSpec(swapMoveTypeSpec=SwapMoveTypeSpec()),
                        MoveTypeSpec(tripleLoopMoveTypeSpec=TripleLoopMoveTypeSpec()),
                        MoveTypeSpec(klSearchMoveTypeSpec=KLSearchMoveTypeSpec()),
                    ],
                )
            )
        )

        # print problem setup
        solver.printProblemSetup()

        # Get a solution from Rebalancer.
        solution = solver.solve()

        # We expect task3 to have moved to host2, but task1 to have stayed in host0.
        expected: dict[str, str] = {
            "task0": "host0",
            "task1": "host0",
            "task2": "host1",
            "task3": "host2",
            "task4": "host2",
        }

        self.assertEqual(expected, solution.assignment)

    def test_capacity_constraint_with_dynamic_dimension(self) -> None:
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setObjectName("task")
        solver.setContainerName("host")

        # host0 initially has 3 tasks in it, and host1 only 1.
        solver.setAssignment(
            {
                "host0": ["task0", "task2"],
                "host1": ["task1", "task3"],
            }
        )

        solver.addPartition(
            "job", {"job1": ["task0", "task2"], "job2": ["task1", "task3"]}
        )

        solver.addDynamicObjectDimensionByPartition(
            dimensionName="memory",
            scope="host",
            partitionName="job",
            scopeItemToGroupToValue={
                "host0": {"job1": 20.0},
                "host1": {"job1": 20.0, "job2": 20.0},
            },
            defaultValue=0.0,
        )

        # Each host has 20 GB of memory capacity.
        solver.addContainerDimension(
            "memory",
            {"host0": 0.0, "host1": 40.0},
        )

        # Enforce memory capacity on hosts.
        solver.addConstraint(
            ConstraintSpecs(
                capacitySpec=CapacitySpec(
                    name="memory_capacity", scope="host", dimension="memory"
                )
            )
        )

        solver.addSolver(SolverSpecs(optimalSolverSpec=OptimalSolverSpec()))

        # Get a solution from Rebalancer.
        solution: AssignmentSolution = solver.solve()

        # We expect host0 to have job2 tasks (task1 and task3) and host1 to have job1 tasks (task0 and task2)
        finalAssignment = solution.assignment
        self.assertEqual("host1", finalAssignment["task0"])
        self.assertEqual("host0", finalAssignment["task1"])
        self.assertEqual("host1", finalAssignment["task2"])
        self.assertEqual("host0", finalAssignment["task3"])

    def test_set_precision(self) -> None:
        # This test verifies that setPrecision correctly passes the precision
        # tolerances to the underlying solver. We verify this by checking that
        # invalid precision values (smaller than machine epsilon) are rejected.
        solver = ProblemSolver(service_name="rebalancer", service_scope="tests")
        solver.setObjectName("task")
        solver.setContainerName("host")
        solver.setAssignment({"host0": ["task0"], "host1": ["task1"]})

        # Set precision with absolute tolerance that is too small (< machine epsilon)
        # This should be rejected by the solver's validation
        invalidPrecisionTolerances = PrecisionTolerances(
            absolute=1e-25,  # Too small - smaller than machine epsilon (~2.22e-16)
            relative=1e-12,
        )

        with self.assertRaises(Exception) as context:
            solver.setPrecision(invalidPrecisionTolerances)

        self.assertIn(
            "absoluteEpsilon must be bigger than std::numeric_limits<double>::epsilon(), but got",
            str(context.exception),
        )
