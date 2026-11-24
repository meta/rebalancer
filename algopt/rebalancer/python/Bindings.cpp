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

// nanobind-based Python bindings for ProblemSolver. Parallel to the ligen
// bindings in interface/fb/polyglot but with no thrift-python or other
// internal-only dependencies, so this module ships in the OSS build.
//
// Thrift-typed arguments are accepted as Python dicts shaped like the Thrift
// JSON wire format; the binding serializes them via Python's ``json`` module
// and round-trips them through SimpleJSONSerializer to produce Thrift values.
// ``solve()`` returns a Python dict by JSON-loading the serialized solution.
// Setters return ``*this`` (reference_internal) for fluent chaining.

// Thrift headers MUST be included before nanobind. Python's structmember.h
// (transitively included by nanobind) #defines T_STRING, which collides with
// apache::thrift::protocol::T_STRING and breaks compilation if Python is
// included first. clang-format off keeps the ordering stable.
// clang-format off
#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/algopt_common/thrift/gen-cpp2/Types_types.h"
#include "algopt/rebalancer/interface/ProblemSolver.h"
#include "algopt/rebalancer/interface/ProblemSolverFactory.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/ProblemSolver_types.h"

#include <thrift/lib/cpp2/op/Get.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <fmt/core.h>
#include <folly/container/F14Map.h>

#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/unordered_map.h>
#include <nanobind/stl/vector.h>
// clang-format on

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nb = nanobind;

namespace facebook::rebalancer::python {

namespace {

using Serializer = apache::thrift::SimpleJSONSerializer;

// Convert a Python value (dict / list / str / None) to a JSON string. Strings
// pass through unchanged; everything else round-trips through ``json.dumps``.
std::string toJson(nb::handle value) {
  if (value.is_none()) {
    return "";
  }
  if (nb::isinstance<nb::str>(value)) {
    return nb::cast<std::string>(value);
  }
  return nb::cast<std::string>(
      nb::module_::import_("json").attr("dumps")(value));
}

// Convert a JSON string to a Python value via ``json.loads``.
nb::object fromJson(const std::string& json) {
  return nb::module_::import_("json").attr("loads")(nb::cast(json));
}

template <typename T>
T deserialize(nb::handle value) {
  T out;
  Serializer::deserialize(toJson(value), out);
  return out;
}

template <typename T>
std::string serialize(const T& value) {
  std::string out;
  Serializer::serialize(value, &out);
  return out;
}

template <typename T>
void validateSpecName(const T& spec) {
  if (spec.name().value().empty()) {
    throw std::runtime_error(
        fmt::format(
            "Expected name field in '{}' to be non-empty",
            apache::thrift::op::get_class_name<T>()));
  }
}

// Wraps a ProblemSolver and exposes the same surface as the internal
// ProblemSolverBinding. All thrift-typed parameters arrive as Python objects
// (dicts) that match the Thrift JSON wire format.
class ProblemSolverPy {
 public:
  ProblemSolverPy(
      std::string serviceName,
      std::string serviceScope,
      bool canExecuteAsync)
      : solver_(
            interface::ProblemSolverFactory::makeProblemSolver(
                std::move(serviceName),
                std::move(serviceScope),
                canExecuteAsync)) {}

  static int32_t ping() noexcept {
    return 4;
  }

  // Problem building -------------------------------------------------------

  ProblemSolverPy& addConstraint(
      nb::handle spec,
      nb::handle policy,
      std::optional<float> invalidCost,
      std::optional<float> invalidState,
      std::optional<int32_t> tuplePosIfBroken) {
    auto specValue = deserialize<interface::ConstraintSpecs>(spec);
    algopt::utils::throwIfUnionIsEmpty(specValue);

    std::optional<interface::ConstraintPolicy> policyValue;
    if (!policy.is_none()) {
      policyValue = deserialize<interface::ConstraintPolicy>(policy);
    }

    apache::thrift::op::visit_union_with_tag(
        specValue,
        [this, policyValue, invalidCost, invalidState, tuplePosIfBroken](
            auto&&, auto&& specType) {
          validateSpecName(specType);
          if constexpr (rebalancer::interface::isSingleParameterConstraint<
                            decltype(specType)>) {
            solver_->addConstraint(std::move(specType));
          } else {
            solver_->addConstraint(
                std::move(specType),
                policyValue,
                invalidCost,
                invalidState,
                tuplePosIfBroken);
          }
        },
        [] {});
    return *this;
  }

  ProblemSolverPy&
  addGoal(nb::handle spec, float weight, std::optional<int32_t> tuplePos) {
    auto specValue = deserialize<interface::GoalSpecs>(spec);
    algopt::utils::throwIfUnionIsEmpty(specValue);

    apache::thrift::op::visit_union_with_tag(
        specValue,
        [this, weight, tuplePos](auto&&, auto&& specType) {
          validateSpecName(specType);
          solver_->addGoal(std::move(specType), weight, tuplePos);
        },
        [] {});
    return *this;
  }

  ProblemSolverPy& addGoalBoundary() {
    solver_->addGoalBoundary();
    return *this;
  }

  ProblemSolverPy& addSolver(nb::handle spec) {
    auto specValue = deserialize<interface::SolverSpecs>(spec);
    algopt::utils::throwIfUnionIsEmpty(specValue);
    apache::thrift::op::visit_union_with_tag(
        specValue,
        [this](auto&&, auto&& specType) {
          solver_->addSolver(std::move(specType));
        },
        [] {});
    return *this;
  }

  ProblemSolverPy& addContainerDimension(
      const std::string& dimensionName,
      const std::unordered_map<std::string, double>& containerToValue,
      double defaultValue) {
    solver_->addContainerDimension(
        dimensionName, containerToValue, defaultValue);
    return *this;
  }

  ProblemSolverPy& addObjectDimension(
      const std::string& dimensionName,
      std::unordered_map<std::string, double> objectToValue,
      double defaultValue) {
    solver_->addObjectDimension(
        dimensionName, std::move(objectToValue), defaultValue);
    return *this;
  }

  ProblemSolverPy& addObjectDimensionVector(
      const std::string& dimensionName,
      std::unordered_map<std::string, std::vector<double>> objectToValues,
      double defaultValue) {
    solver_->addObjectDimension(
        dimensionName, std::move(objectToValues), defaultValue);
    return *this;
  }

  ProblemSolverPy& addDynamicObjectDimension(
      const std::string& dimensionName,
      const std::string& scope,
      std::unordered_map<std::string, std::unordered_map<std::string, double>>
          scopeItemToObjectToValue,
      double defaultValue) {
    solver_->addDynamicObjectDimension(
        dimensionName,
        scope,
        toF14(std::move(scopeItemToObjectToValue)),
        defaultValue);
    return *this;
  }

  ProblemSolverPy& addDynamicObjectDimensionByPartition(
      const std::string& dimensionName,
      const std::string& scope,
      const std::string& partitionName,
      std::unordered_map<std::string, std::unordered_map<std::string, double>>
          scopeItemToGroupToValue,
      double defaultValue) {
    solver_->addDynamicObjectDimension(
        dimensionName,
        scope,
        partitionName,
        toF14(std::move(scopeItemToGroupToValue)),
        defaultValue);
    return *this;
  }

  ProblemSolverPy& addObjectPartitionRoutingDimension(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      double defaultValue) {
    solver_->addObjectPartitionRoutingDimension(
        dimensionName,
        partitionName,
        routingConfigName,
        groupToValue,
        defaultValue);
    return *this;
  }

  ProblemSolverPy& addObjectPartitionRoutingDimensionWithStaticValues(
      const std::string& dimensionName,
      const std::string& partitionName,
      const std::string& routingConfigName,
      const std::unordered_map<std::string, double>& groupToValue,
      const std::unordered_map<std::string, double>& groupToStaticValue,
      double defaultValue,
      double defaultStaticValue) {
    solver_->addObjectPartitionRoutingDimension(
        dimensionName,
        partitionName,
        routingConfigName,
        groupToValue,
        groupToStaticValue,
        defaultValue,
        defaultStaticValue);
    return *this;
  }

  ProblemSolverPy& addPartition(
      const std::string& partitionName,
      std::unordered_map<std::string, std::vector<std::string>>
          groupToObjects) {
    solver_->addPartition(partitionName, std::move(groupToObjects));
    return *this;
  }

  ProblemSolverPy& addScope(
      const std::string& scopeName,
      const std::unordered_map<std::string, std::string>&
          containerToScopeItem) {
    solver_->addScope(scopeName, containerToScopeItem);
    return *this;
  }

  ProblemSolverPy& addScopeDimension(
      const std::string& dimensionName,
      const std::string& scopeName,
      const std::unordered_map<std::string, double>& scopeItemToValue) {
    solver_->addScopeDimension(dimensionName, scopeName, scopeItemToValue);
    return *this;
  }

  ProblemSolverPy& addSimilarContainers(
      const std::vector<std::vector<std::string>>& similarContainerClasses) {
    solver_->addSimilarContainers(similarContainerClasses);
    return *this;
  }

  ProblemSolverPy& addRoutingConfig(
      const std::string& configName,
      const std::string& scopeName,
      const std::string& partitionName,
      const nb::dict& groupToRoutingRings,
      const std::unordered_map<
          std::string,
          std::unordered_map<std::string, double>>& originToDestinationLatency,
      const std::optional<std::unordered_map<
          std::string,
          std::vector<std::vector<std::string>>>>&
          defaultOriginToDestinationScopeItemSets) {
    std::unordered_map<std::string, interface::GroupRoutingRings> rings;
    rings.reserve(groupToRoutingRings.size());
    for (auto [k, v] : groupToRoutingRings) {
      rings.emplace(
          nb::cast<std::string>(k),
          deserialize<interface::GroupRoutingRings>(v));
    }
    solver_->addRoutingConfig(
        configName,
        scopeName,
        partitionName,
        rings,
        originToDestinationLatency,
        defaultOriginToDestinationScopeItemSets);
    return *this;
  }

  // Solver options ---------------------------------------------------------

  ProblemSolverPy& disableSolutionSummary() {
    solver_->disableSolutionSummary();
    return *this;
  }

  ProblemSolverPy& enableMoveStats(nb::handle spec) {
    auto specValue = deserialize<interface::MoveStatsSpec>(spec);
    solver_->enableMoveStats(specValue);
    return *this;
  }

  ProblemSolverPy& enableProfiler() {
    solver_->enableProfiler();
    return *this;
  }

  ProblemSolverPy& enableRestrictMovingObjectOnlyOnce() {
    solver_->enableRestrictMovingObjectOnlyOnce();
    return *this;
  }

  ProblemSolverPy& disableScubaLogging() {
    solver_->disableLogging();
    return *this;
  }

  ProblemSolverPy& enableStableAsMuchAsPossible() {
    solver_->enableStableAsMuchAsPossible();
    return *this;
  }

  // Inspect ----------------------------------------------------------------

  int32_t getCurrentGoalIndex() const {
    return solver_->getCurrentGoalIndex();
  }

  std::string getRunId() const {
    return solver_->getRunId();
  }

  void printProblemSetup() const {
    solver_->printProblemSetup();
  }

  // Admin ------------------------------------------------------------------

  ProblemSolverPy& persistToManifold() {
    solver_->persistToManifold();
    return *this;
  }

  ProblemSolverPy& saveBundle(const std::string& path) {
    solver_->saveBundle(path);
    return *this;
  }

  ProblemSolverPy& publishMetrics() {
    solver_->publishMetrics();
    return *this;
  }

  // More problem building --------------------------------------------------

  ProblemSolverPy& setAssignment(
      const std::unordered_map<std::string, std::vector<std::string>>&
          containerToObjects) {
    solver_->setAssignment(containerToObjects);
    return *this;
  }

  ProblemSolverPy& setConstraintPolicy(nb::handle policy) {
    solver_->setConstraintPolicy(
        deserialize<interface::ConstraintPolicy>(policy));
    return *this;
  }

  ProblemSolverPy& setDefaultConstraintParams(nb::handle params) {
    solver_->setDefaultConstraintParams(
        deserialize<interface::ConstraintParams>(params));
    return *this;
  }

  ProblemSolverPy& setContainerName(const std::string& containerName) {
    solver_->setContainerName(containerName);
    return *this;
  }

  ProblemSolverPy& setFeasibilityTolerance(double feasibilityTolerance) {
    solver_->setFeasibilityTolerance(feasibilityTolerance);
    return *this;
  }

  ProblemSolverPy& setLogLevel(const std::string& level) {
    solver_->setLogLevel(level);
    return *this;
  }

  ProblemSolverPy& setManifoldBackupParams(nb::handle params) {
    solver_->setManifoldBackupParams(
        deserialize<interface::ManifoldBackupParams>(params));
    return *this;
  }

  ProblemSolverPy& setLoggingLabel(const std::string& loggingLabel) {
    solver_->setLoggingLabel(loggingLabel);
    return *this;
  }

  ProblemSolverPy& setObjectName(const std::string& objectName) {
    solver_->setObjectName(objectName);
    return *this;
  }

  ProblemSolverPy& setRunId(std::string runId) {
    solver_->setRunId(std::move(runId));
    return *this;
  }

  ProblemSolverPy& useParallelizedNewMaterializer() {
    solver_->useParallelizedNewMaterializer();
    return *this;
  }

  ProblemSolverPy& shouldUseDynamicObjectOrdering(
      bool useDynamicObjectOrdering) {
    solver_->shouldUseDynamicObjectOrdering(useDynamicObjectOrdering);
    return *this;
  }

  ProblemSolverPy& setPrecision(nb::handle tolerances) {
    solver_->setPrecision(
        deserialize<algopt::common::thrift::PrecisionTolerances>(tolerances));
    return *this;
  }

  // Solve ------------------------------------------------------------------

  nb::object solve() {
    return fromJson(serialize(solver_->solve()));
  }

 private:
  static folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>>
  toF14(
      std::unordered_map<std::string, std::unordered_map<std::string, double>>
          source) {
    folly::F14FastMap<std::string, folly::F14FastMap<std::string, double>> out;
    out.reserve(source.size());
    for (auto& [k, inner] : source) {
      out.emplace(
          k,
          folly::F14FastMap<std::string, double>(inner.begin(), inner.end()));
    }
    return out;
  }

  std::unique_ptr<interface::ProblemSolver> solver_;
};

} // namespace

// NOLINTNEXTLINE(performance-unnecessary-value-param)
NB_MODULE(_rebalancer, m) {
  m.doc() =
      "nanobind bindings for the rebalancer ProblemSolver.\n\n"
      "Rebalancer optimizes the placement of objects (e.g. shards, tasks)\n"
      "into containers (e.g. hosts, racks) subject to constraints and goals.\n"
      "Build a problem by chaining configuration calls, then call solve().\n"
      "Thrift-typed arguments are passed as Python dicts shaped like the\n"
      "Thrift JSON wire format; solve() returns the solution as a dict.";

  constexpr auto self = nb::rv_policy::reference_internal;

  nb::class_<ProblemSolverPy>(
      m,
      "ProblemSolver",
      "Builder for assignment-optimization problems.\n\n"
      "Construct with a service name and scope (used for logging and run\n"
      "identification), then chain ``add_*``/``set_*``/``enable_*`` calls\n"
      "to define the problem. ``solve()`` runs the configured solver(s) and\n"
      "returns the solution as a dict.")
      .def(
          nb::init<std::string, std::string, bool>(),
          nb::arg("service_name"),
          nb::arg("service_scope"),
          nb::arg("can_execute_async") = false,
          "Create a ProblemSolver.\n\n"
          "Args:\n"
          "  service_name: identifier for the calling service (logged in\n"
          "    Scuba and used for run-id namespacing).\n"
          "  service_scope: sub-scope within the service.\n"
          "  can_execute_async: if True, the solver may schedule work on a\n"
          "    background executor; defaults to synchronous execution.")
      .def_static(
          "ping",
          &ProblemSolverPy::ping,
          "Liveness probe. Returns 4 (a number chosen by fair dice roll).")
      // Problem building -----------------------------------------------
      .def(
          "add_constraint",
          &ProblemSolverPy::addConstraint,
          nb::arg("spec"),
          nb::arg("policy") = nb::none(),
          nb::arg("invalid_cost") = nb::none(),
          nb::arg("invalid_state") = nb::none(),
          nb::arg("tuple_pos_if_broken") = nb::none(),
          self,
          "Add a constraint that the solver must respect.\n\n"
          "Args:\n"
          "  spec: dict matching ConstraintSpecs (one key per active arm,\n"
          "    e.g. ``{'capacitySpec': {...}}``).\n"
          "  policy: optional ConstraintPolicy override (DEFAULT, HARD,\n"
          "    SOFT, ...). When omitted, the solver-wide default is used.\n"
          "  invalid_cost: per-violation cost penalty when the constraint\n"
          "    is broken (only meaningful for soft constraints).\n"
          "  invalid_state: extra penalty for being in any infeasible\n"
          "    state for this constraint.\n"
          "  tuple_pos_if_broken: index in the goal tuple to attribute\n"
          "    soft-violation cost to. Single-parameter constraints\n"
          "    (AvoidMovingSpec, MovesInProgressSpec, MoveGroupSpec)\n"
          "    ignore all options other than ``spec``.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_goal",
          &ProblemSolverPy::addGoal,
          nb::arg("spec"),
          nb::arg("weight") = 1.0f,
          nb::arg("tuple_pos") = nb::none(),
          self,
          "Add an optimization goal (objective term).\n\n"
          "Args:\n"
          "  spec: dict matching GoalSpecs (one key per active arm,\n"
          "    e.g. ``{'balanceSpec': {...}}``).\n"
          "  weight: relative weight within its tuple position.\n"
          "  tuple_pos: index in the goal tuple. Goals at the same tuple\n"
          "    position are combined; lower indices are optimized first\n"
          "    (lexicographic). Defaults to the current goal-boundary\n"
          "    index.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_goal_boundary",
          &ProblemSolverPy::addGoalBoundary,
          self,
          "Advance to the next tuple position so subsequent ``add_goal``\n"
          "calls form a strictly lower-priority bucket in the lexicographic\n"
          "objective.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_solver",
          &ProblemSolverPy::addSolver,
          nb::arg("spec"),
          self,
          "Append a solver stage. Multiple stages run sequentially, each\n"
          "starting from the previous stage's solution.\n\n"
          "Args:\n"
          "  spec: dict matching SolverSpecs (e.g.\n"
          "    ``{'localSearchSolverSpec': {...}}`` or\n"
          "    ``{'optimalSolverSpec': {...}}``).\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_container_dimension",
          &ProblemSolverPy::addContainerDimension,
          nb::arg("dimension_name"),
          nb::arg("container_to_value"),
          nb::arg("default_value") = 1.0,
          self,
          "Define a container-side numeric dimension (e.g. host capacity).\n\n"
          "Args:\n"
          "  dimension_name: matches the dimension name used in specs.\n"
          "  container_to_value: per-container values; missing entries\n"
          "    fall back to ``default_value``.\n"
          "  default_value: value applied to containers absent from the\n"
          "    map.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_object_dimension",
          &ProblemSolverPy::addObjectDimension,
          nb::arg("dimension_name"),
          nb::arg("object_to_value"),
          nb::arg("default_value") = 0.0,
          self,
          "Define an object-side numeric dimension (e.g. task memory\n"
          "footprint).\n\n"
          "Args:\n"
          "  dimension_name: identifier referenced by goals/constraints.\n"
          "  object_to_value: per-object scalar values.\n"
          "  default_value: value for objects not listed in the map.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_object_dimension_vector",
          &ProblemSolverPy::addObjectDimensionVector,
          nb::arg("dimension_name"),
          nb::arg("object_to_values"),
          nb::arg("default_value") = 0.0,
          self,
          "Define an object-side vector-valued dimension. Each object maps\n"
          "to a list of doubles (used by specs that operate over\n"
          "multi-component values).\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_dynamic_object_dimension",
          &ProblemSolverPy::addDynamicObjectDimension,
          nb::arg("dimension_name"),
          nb::arg("scope"),
          nb::arg("scope_item_to_object_to_value"),
          nb::arg("default_value") = 0.0,
          self,
          "Define an object dimension whose value depends on which scope\n"
          "item the object is assigned to (e.g. a task's bandwidth use\n"
          "depends on the host).\n\n"
          "Args:\n"
          "  dimension_name: identifier.\n"
          "  scope: scope name (must already be added via ``add_scope``).\n"
          "  scope_item_to_object_to_value: nested dict\n"
          "    ``{scope_item: {object: value}}``.\n"
          "  default_value: fallback when an entry is missing.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_dynamic_object_dimension_by_partition",
          &ProblemSolverPy::addDynamicObjectDimensionByPartition,
          nb::arg("dimension_name"),
          nb::arg("scope"),
          nb::arg("partition_name"),
          nb::arg("scope_item_to_group_to_value"),
          nb::arg("default_value") = 0.0,
          self,
          "Like ``add_dynamic_object_dimension`` but values are keyed by\n"
          "partition group rather than by individual object. Every object\n"
          "in a given group sees the same value at a given scope item.\n\n"
          "Args:\n"
          "  partition_name: must already be added via ``add_partition``.\n"
          "  scope_item_to_group_to_value: ``{scope_item: {group: value}}``.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_object_partition_routing_dimension",
          &ProblemSolverPy::addObjectPartitionRoutingDimension,
          nb::arg("dimension_name"),
          nb::arg("partition_name"),
          nb::arg("routing_config_name"),
          nb::arg("group_to_value"),
          nb::arg("default_value") = 0.0,
          self,
          "Define an object dimension whose value depends on routing\n"
          "decisions made by a previously-registered routing config (see\n"
          "``add_routing_config``). Each partition group has a base value.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_object_partition_routing_dimension_with_static_values",
          &ProblemSolverPy::addObjectPartitionRoutingDimensionWithStaticValues,
          nb::arg("dimension_name"),
          nb::arg("partition_name"),
          nb::arg("routing_config_name"),
          nb::arg("group_to_value"),
          nb::arg("group_to_static_value"),
          nb::arg("default_value") = 0.0,
          nb::arg("default_static_value") = 0.0,
          self,
          "Routing-aware object dimension that combines a routing-dependent\n"
          "value with a static (routing-independent) baseline per group.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_partition",
          &ProblemSolverPy::addPartition,
          nb::arg("partition_name"),
          nb::arg("group_to_objects"),
          self,
          "Define a named partition over the objects, mapping group names\n"
          "to the list of objects that belong to them. Used by group-count\n"
          "specs and partition-routing dimensions.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_scope",
          &ProblemSolverPy::addScope,
          nb::arg("scope_name"),
          nb::arg("container_to_scope_item"),
          self,
          "Define a scope by mapping each container to a single scope\n"
          "item (e.g. ``host -> rack``).\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_scope_dimension",
          &ProblemSolverPy::addScopeDimension,
          nb::arg("dimension_name"),
          nb::arg("scope_name"),
          nb::arg("scope_item_to_value"),
          self,
          "Define a numeric value per scope item (e.g. per-rack capacity).\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_similar_containers",
          &ProblemSolverPy::addSimilarContainers,
          nb::arg("similar_container_classes"),
          self,
          "Declare equivalence classes of containers; the solver may treat\n"
          "containers within a class as interchangeable for symmetry-\n"
          "breaking and to shrink the search space.\n\n"
          "Args:\n"
          "  similar_container_classes: list of lists of container names;\n"
          "    each inner list is one equivalence class.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "add_routing_config",
          &ProblemSolverPy::addRoutingConfig,
          nb::arg("config_name"),
          nb::arg("scope_name"),
          nb::arg("partition_name"),
          nb::arg("group_to_routing_rings"),
          nb::arg("origin_to_destination_latency"),
          nb::arg("default_origin_to_destination_scope_item_sets") = nb::none(),
          self,
          "Register a routing configuration used by\n"
          "``add_object_partition_routing_dimension*``.\n\n"
          "Args:\n"
          "  config_name: identifier referenced by routing dimensions.\n"
          "  scope_name: scope over which routing is defined.\n"
          "  partition_name: partition whose groups are routed.\n"
          "  group_to_routing_rings: per-group GroupRoutingRings dicts.\n"
          "  origin_to_destination_latency: latency table for routing\n"
          "    cost calculations.\n"
          "  default_origin_to_destination_scope_item_sets: optional\n"
          "    fallback routing for unspecified origin/destination pairs.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      // Solver options -------------------------------------------------
      .def(
          "disable_solution_summary",
          &ProblemSolverPy::disableSolutionSummary,
          self,
          "Skip the post-solve summary computation. Useful when the\n"
          "summary is large and the caller doesn't need it.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "enable_move_stats",
          &ProblemSolverPy::enableMoveStats,
          nb::arg("spec"),
          self,
          "Enable per-move statistics collection.\n\n"
          "Args:\n"
          "  spec: dict matching MoveStatsSpec.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "enable_profiler",
          &ProblemSolverPy::enableProfiler,
          self,
          "Enable the hierarchical solver profiler (treeprof). Profiling\n"
          "data is logged via XLOG.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "enable_restrict_moving_object_only_once",
          &ProblemSolverPy::enableRestrictMovingObjectOnlyOnce,
          self,
          "Restrict the search so each object is moved at most once across\n"
          "the entire solve.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "disable_scuba_logging",
          &ProblemSolverPy::disableScubaLogging,
          self,
          "Suppress logging of run metadata to the rebalancer_runs Scuba\n"
          "table.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "enable_stable_as_much_as_possible",
          &ProblemSolverPy::enableStableAsMuchAsPossible,
          self,
          "Enable the StableStayed optimization, which reduces the number\n"
          "of equivalence sets the solver creates. Recommended with\n"
          "OptimalSolver; with LocalSearch it may impair the ability to\n"
          "undo sub-optimal moves.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      // Inspect --------------------------------------------------------
      .def(
          "get_current_goal_index",
          &ProblemSolverPy::getCurrentGoalIndex,
          "Return the current tuple position used by ``add_goal``.")
      .def(
          "get_run_id",
          &ProblemSolverPy::getRunId,
          "Return the unique run identifier assigned to this solve.")
      .def(
          "print_problem_setup",
          &ProblemSolverPy::printProblemSetup,
          "Print a human-readable summary of the problem (scopes,\n"
          "dimensions, goals, constraints). Call after all setup is\n"
          "complete; calling earlier yields an incomplete summary.")
      // Admin ----------------------------------------------------------
      .def(
          "persist_to_manifold",
          &ProblemSolverPy::persistToManifold,
          self,
          "Serialize the configured problem (and any solution) and upload\n"
          "the snapshot to Manifold for later replay via the standalone\n"
          "solver. No-op outside Meta infrastructure.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "save_bundle",
          &ProblemSolverPy::saveBundle,
          nb::arg("path"),
          self,
          "Serialize the bundle (problem + any solution) to ``path`` using the\n"
          "same format as Manifold (zstd-compressed Thrift Binary), loadable\n"
          "by the standalone Rebalancer Explorer. Needs no Manifold. Call\n"
          "after ``solve`` to include the solution.\n\n"
          "Args:\n"
          "  path: file path to write the bundle to.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "publish_metrics",
          &ProblemSolverPy::publishMetrics,
          self,
          "Compute and attach container utilization metrics (and other\n"
          "per-entity values) to ``initial_metrics`` and ``final_metrics``\n"
          "of the returned solution.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      // More problem building ------------------------------------------
      .def(
          "set_assignment",
          &ProblemSolverPy::setAssignment,
          nb::arg("container_to_objects"),
          self,
          "Set the initial assignment in expanded form: container ->\n"
          "list of object names.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_constraint_policy",
          &ProblemSolverPy::setConstraintPolicy,
          nb::arg("policy"),
          self,
          "Set the default constraint policy applied when a constraint is\n"
          "added without an explicit ``policy`` argument.\n\n"
          "Args:\n"
          "  policy: dict matching ConstraintPolicy.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_default_constraint_params",
          &ProblemSolverPy::setDefaultConstraintParams,
          nb::arg("params"),
          self,
          "Set the default ConstraintParams (invalid_cost / invalid_state)\n"
          "applied when not specified per-constraint.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_container_name",
          &ProblemSolverPy::setContainerName,
          nb::arg("container_name"),
          self,
          "Set the human-readable label for containers (e.g. 'host'),\n"
          "used in logging and the solution summary.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_feasibility_tolerance",
          &ProblemSolverPy::setFeasibilityTolerance,
          nb::arg("feasibility_tolerance"),
          self,
          "Set the LP/MIP feasibility tolerance for the optimal solver.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_log_level",
          &ProblemSolverPy::setLogLevel,
          nb::arg("level"),
          self,
          "Set the XLOG verbosity (e.g. 'INFO', 'DBG2'). Affects all\n"
          "ProblemSolver instances in the process.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_manifold_backup_params",
          &ProblemSolverPy::setManifoldBackupParams,
          nb::arg("params"),
          self,
          "Configure when and how the solver auto-uploads problem snapshots\n"
          "to Manifold.\n\n"
          "Args:\n"
          "  params: dict matching ManifoldBackupParams.\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_logging_label",
          &ProblemSolverPy::setLoggingLabel,
          nb::arg("logging_label"),
          self,
          "Tag the run with a free-form label that appears in log lines\n"
          "and Scuba rows.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_object_name",
          &ProblemSolverPy::setObjectName,
          nb::arg("object_name"),
          self,
          "Set the human-readable label for objects (e.g. 'task'), used\n"
          "in logging and the solution summary.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_run_id",
          &ProblemSolverPy::setRunId,
          nb::arg("run_id"),
          self,
          "Override the auto-generated run identifier. Useful for tying a\n"
          "solve to an external trace ID.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "use_parallelized_new_materializer",
          &ProblemSolverPy::useParallelizedNewMaterializer,
          self,
          "Opt into the parallelized version of the new materializer (a\n"
          "performance experiment). Default is the non-parallelized path.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "should_use_dynamic_object_ordering",
          &ProblemSolverPy::shouldUseDynamicObjectOrdering,
          nb::arg("use_dynamic_object_ordering"),
          self,
          "Toggle dynamic object ordering for local search. When True\n"
          "(default), local search is faster and avoids exploring\n"
          "redundant moves; no-op for the optimal solver.\n\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      .def(
          "set_precision",
          &ProblemSolverPy::setPrecision,
          nb::arg("tolerances"),
          self,
          "Set numeric precision tolerances (absolute and relative epsilons)\n"
          "used by internal floating-point comparisons.\n\n"
          "Args:\n"
          "  tolerances: dict matching PrecisionTolerances. The absolute\n"
          "    epsilon must exceed std::numeric_limits<double>::epsilon().\n"
          "Returns:\n"
          "  self, for fluent chaining.")
      // Solve ----------------------------------------------------------
      .def(
          "solve",
          &ProblemSolverPy::solve,
          "Build and solve the configured problem.\n\n"
          "Returns:\n"
          "  AssignmentSolution as a dict (Thrift-JSON shape). Notable\n"
          "  fields include ``assignment`` (object -> container),\n"
          "  ``initialMetrics`` and ``finalMetrics`` (when\n"
          "  ``publish_metrics`` is enabled).");
}

} // namespace facebook::rebalancer::python
