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


# pyre-strict

from __future__ import annotations

from typing import Final

from algopt.rebalancer.interface.polyglot.py_bindings.async_manifold_upload_handle import (
    AsyncManifoldUploadHandle,
)
from algopt.rebalancer.interface.polyglot.py_bindings.problem_solver import (
    ProblemSolverBinding,
)
from algopt.rebalancer.interface.py_client.typing import Constraint, Goal, Solver
from facebook.algopt.common.thrift.Types.thrift_types import PrecisionTolerances
from rebalancer.interface.thrift.Types.thrift_types import (
    AssignmentSolution,
    ConstraintParams,
    ConstraintPolicy,
    GroupRoutingRings,
    MoveStatsSpec,
)
from rebalancer.interface.thrift.v2.ProblemSolver.thrift_types import (
    ConstraintSpecs,
    GoalSpecs,
    ManifoldBackupParams,
    SolverSpecs,
)


DEFAULT_INVALID_COST: Final[float] = 100
DEFAULT_INVALID_STATE: Final[float] = 10000


class ProblemSolver:
    def __init__(
        self,
        service_name: str,
        service_scope: str,
        can_execute_async: bool = False,
    ) -> None:
        self._ps: ProblemSolverBinding = ProblemSolverBinding(
            service_name, service_scope, can_execute_async
        )

    def ping(self) -> int:
        return self._ps.ping()

    def getCurrentGoalIndex(self) -> int:
        return self._ps.getCurrentGoalIndex()

    def getRunId(self) -> str:
        return self._ps.getRunId()

    def printProblemSetup(self) -> None:
        self._ps.printProblemSetup()

    def setRunId(self, runId: str) -> ProblemSolver:
        self._ps.setRunId(runId)
        return self

    def setLogLevel(self, level: str) -> ProblemSolver:
        self._ps.setLogLevel(level)
        return self

    def setManifoldBackupParams(
        self,
        params: ManifoldBackupParams,
        manifold_upload_handle: AsyncManifoldUploadHandle | None = None,
    ) -> ProblemSolver:
        self._ps.setManifoldBackupParams(params, manifold_upload_handle)
        return self

    def setLoggingLabel(self, loggingLabel: str) -> ProblemSolver:
        self._ps.setLoggingLabel(loggingLabel)
        return self

    def persistToManifold(
        self,
        manifold_upload_handle: AsyncManifoldUploadHandle | None = None,
    ) -> ProblemSolver:
        self._ps.persistToManifold(manifold_upload_handle)
        return self

    def saveBundle(self, path: str) -> ProblemSolver:
        """Serialize the bundle (problem + any solution) to ``path`` using the
        same format as Manifold (zstd-compressed Thrift Binary), loadable by the
        standalone Rebalancer Explorer. Call after ``solve`` to include the
        solution.
        """
        self._ps.saveBundle(path)
        return self

    ##################
    # Problem building
    ##################
    def addConstraint(
        self,
        spec: Constraint | ConstraintSpecs,
        policy: ConstraintPolicy | None = None,
        invalidCost: float | None = None,
        invalidState: float | None = None,
        tuplePosIfBroken: int | None = None,
    ) -> ProblemSolver:
        constraint_specs = (
            spec
            if isinstance(spec, ConstraintSpecs)
            else ConstraintSpecs.fromValue(spec)
        )
        self._ps.addConstraint(
            spec=constraint_specs,
            policy=policy,
            invalidCost=invalidCost,
            invalidState=invalidState,
            tuplePosIfBroken=tuplePosIfBroken,
        )
        return self

    def addContainerDimension(
        self,
        dimensionName: str,
        containerToValue: dict[str, float],
        defaultValue: float = 1.0,
    ) -> ProblemSolver:
        self._ps.addContainerDimension(dimensionName, containerToValue, defaultValue)
        return self

    def addGoal(
        self, spec: Goal | GoalSpecs, weight: float = 1, tuplePos: int | None = None
    ) -> ProblemSolver:
        goal_specs = spec if isinstance(spec, GoalSpecs) else GoalSpecs.fromValue(spec)
        self._ps.addGoal(
            spec=goal_specs,
            weight=weight,
            tuplePos=tuplePos,
        )
        return self

    def addGoalBoundary(self) -> ProblemSolver:
        self._ps.addGoalBoundary()
        return self

    def addObjectDimension(
        self,
        dimensionName: str,
        objectToValue: dict[str, float],
        defaultValue: float = 0,
    ) -> ProblemSolver:
        self._ps.addObjectDimension(dimensionName, objectToValue, defaultValue)
        return self

    def addObjectDimensionVector(
        self,
        dimensionName: str,
        objectToValues: dict[str, list[float]],
        defaultValue: float = 0.0,
    ) -> ProblemSolver:
        self._ps.addObjectDimensionVector(dimensionName, objectToValues, defaultValue)
        return self

    def addDynamicObjectDimension(
        self,
        dimensionName: str,
        scope: str,
        scopeItemToObjectToValue: dict[str, dict[str, float]],
        defaultValue: float = 0,
    ) -> ProblemSolver:
        self._ps.addDynamicObjectDimension(
            dimensionName,
            scope,
            scopeItemToObjectToValue,
            defaultValue,
        )
        return self

    def addDynamicObjectDimensionByPartition(
        self,
        dimensionName: str,
        scope: str,
        partitionName: str,
        scopeItemToGroupToValue: dict[str, dict[str, float]],
        defaultValue: float = 0,
    ) -> ProblemSolver:
        """
        The value in scopeItemToGroupToValue is the value of the dimension for every object in the group.
        So, for example, scopeItemToGroupToValue["host1"]["group1"] = 10 means that all objects in group1
        have a value of 10 for the dimension in host1.
        """
        self._ps.addDynamicObjectDimensionByPartition(
            dimensionName,
            scope,
            partitionName,
            scopeItemToGroupToValue,
            defaultValue,
        )
        return self

    def addObjectPartitionRoutingDimension(
        self,
        dimensionName: str,
        partitionName: str,
        routingConfigName: str,
        groupToValue: dict[str, float],
        defaultValue: float = 0,
    ) -> ProblemSolver:
        self._ps.addObjectPartitionRoutingDimension(
            dimensionName, partitionName, routingConfigName, groupToValue, defaultValue
        )

        return self

    def addObjectPartitionRoutingDimensionWithStaticValues(
        self,
        dimensionName: str,
        partitionName: str,
        routingConfigName: str,
        groupToValue: dict[str, float],
        groupToStaticValue: dict[str, float],
        defaultValue: float = 0,
        defaultStaticValue: float = 0,
    ) -> ProblemSolver:
        self._ps.addObjectPartitionRoutingDimensionWithStaticValues(
            dimensionName,
            partitionName,
            routingConfigName,
            groupToValue,
            groupToStaticValue,
            defaultValue,
            defaultStaticValue,
        )

        return self

    def addPartition(
        self,
        partitionName: str,
        groupToObjects: dict[str, list[str]],
    ) -> ProblemSolver:
        self._ps.addPartition(partitionName, groupToObjects)
        return self

    def addScope(
        self,
        scopeName: str,
        containerToScopeItem: dict[str, str],
    ) -> ProblemSolver:
        self._ps.addScope(scopeName, containerToScopeItem)
        return self

    def addScopeDimension(
        self,
        dimensionName: str,
        scopeName: str,
        scopeItemToValue: dict[str, float],
    ) -> ProblemSolver:
        self._ps.addScopeDimension(dimensionName, scopeName, scopeItemToValue)
        return self

    def addSimilarContainers(
        self, similarContainerClasses: list[list[str]]
    ) -> ProblemSolver:
        self._ps.addSimilarContainers(similarContainerClasses)
        return self

    def addRoutingConfig(
        self,
        routingConfigName: str,
        scopeName: str,
        partitionName: str,
        groupToRoutingRings: dict[str, GroupRoutingRings],
        originToDestinationLatency: dict[str, dict[str, float]],
        defaultOriginToDestinationScopeItemSets: None
        | (dict[str, list[list[str]]]) = None,
    ) -> ProblemSolver:
        # TODO: ligen uses thrift-python, whereas all the rebalancer thrift structs use thrift-py3.
        # Remove this conversion after migrating to thrift-python (https://www.internalfb.com/intern/wiki/Thrift-Python/Migration_Guide/)
        groupToThriftPythonRoutingRings = {}
        for key, value in groupToRoutingRings.items():
            groupToThriftPythonRoutingRings[key] = value

        self._ps.addRoutingConfig(
            routingConfigName,
            scopeName,
            partitionName,
            groupToThriftPythonRoutingRings,
            originToDestinationLatency,
            defaultOriginToDestinationScopeItemSets,
        )
        return self

    def addSolver(self, spec: Solver | SolverSpecs) -> ProblemSolver:
        solver_specs = (
            spec if isinstance(spec, SolverSpecs) else SolverSpecs.fromValue(spec)
        )
        self._ps.addSolver(solver_specs)
        return self

    ################
    # Solver options
    ################
    def disableSolutionSummary(self) -> ProblemSolver:
        self._ps.disableSolutionSummary()
        return self

    def enableMoveStats(self, spec: MoveStatsSpec) -> ProblemSolver:
        self._ps.enableMoveStats(spec)
        return self

    def enableProfiler(self) -> ProblemSolver:
        self._ps.enableProfiler()
        return self

    def enableRestrictMovingObjectOnlyOnce(self) -> ProblemSolver:
        self._ps.enableRestrictMovingObjectOnlyOnce()
        return self

    def disableScubaLogging(self) -> ProblemSolver:
        self._ps.disableScubaLogging()
        return self

    def enableStableAsMuchAsPossible(self) -> ProblemSolver:
        self._ps.enableStableAsMuchAsPossible()
        return self

    def setAssignment(self, containerToObjects: dict[str, list[str]]) -> ProblemSolver:
        self._ps.setAssignment(containerToObjects)
        return self

    def setConstraintPolicy(self, policy: ConstraintPolicy) -> ProblemSolver:
        self._ps.setConstraintPolicy(policy)
        return self

    def setDefaultConstraintParams(self, params: ConstraintParams) -> ProblemSolver:
        self._ps.setDefaultConstraintParams(params)
        return self

    def setContainerName(self, containerName: str) -> ProblemSolver:
        self._ps.setContainerName(containerName)
        return self

    def setObjectName(self, objectName: str) -> ProblemSolver:
        self._ps.setObjectName(objectName)
        return self

    def useParallelizedNewMaterializer(self) -> ProblemSolver:
        self._ps.useParallelizedNewMaterializer()
        return self

    def enableParallelBoundsComputation(self, enable: bool) -> ProblemSolver:
        """Enable or disable parallel bounds initialization for this problem."""
        self._ps.enableParallelBoundsComputation(enable)
        return self

    def shouldUseDynamicObjectOrdering(
        self, useDynamicObjectOrdering: bool
    ) -> ProblemSolver:
        self._ps.shouldUseDynamicObjectOrdering(useDynamicObjectOrdering)
        return self

    def validateAppliedMoves(self, enable: bool) -> ProblemSolver:
        self._ps.validateAppliedMoves(enable)
        return self

    def setPrecision(self, precisionTolerances: PrecisionTolerances) -> ProblemSolver:
        self._ps.setPrecision(precisionTolerances)
        return self

    def publishMetrics(self) -> ProblemSolver:
        self._ps.publishMetrics()
        return self

    ################
    # The main event
    ################
    def solve(self) -> AssignmentSolution:
        return self._ps.solve()
