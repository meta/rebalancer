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

#include "algopt/rebalancer/solver/utils/Problem.h"

#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/common/log/RebalancerLog.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/ObjectDimension.h"
#include "algopt/rebalancer/entities/Objects.h"
#include "algopt/rebalancer/entities/ObjectScalarDimension.h"
#include "algopt/rebalancer/entities/Partition.h"
#include "algopt/rebalancer/entities/Scope.h"
#include "algopt/rebalancer/entities/Set.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/Constants.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectLookup.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/moves/DestinationsToExploreGenerator.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/solver/moves/MoveStatsAggregator.h"
#include "algopt/rebalancer/solver/moves/ObjectsToExploreGenerator.h"
#include "algopt/rebalancer/solver/solvers/LPStore.h"
#include "algopt/rebalancer/solver/summary/GlobalLabeledObjectives.h"
#include "algopt/rebalancer/solver/summary/LabeledConstraints.h"
#include "algopt/rebalancer/solver/summary/LabeledExpressions.h"
#include "algopt/rebalancer/solver/summary/LabeledObjectives.h"
#include "algopt/rebalancer/solver/summary/metrics/Metrics.h"
#include "algopt/rebalancer/solver/utils/AffectedByChangeInfo.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/ChangeSet.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/EntityAttributesStore.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSetsStore.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/ObjectToContainerAssignmentUtils.h"
#include "algopt/rebalancer/solver/utils/GlobalObjective.h"
#include "algopt/rebalancer/solver/utils/GlobalObjectiveValue.h"
#include "algopt/rebalancer/solver/utils/MaterializedProblem.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/ProblemConfigs.h"
#include "algopt/rebalancer/solver/utils/SimilarContainers.h"
#include "algopt/rebalancer/solver/utils/Util.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashMap.h"
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/CppAttributes.h>
#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/Function.h>
#include <folly/logging/LogStreamProcessor.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <ostream>
#include <ranges>
#include <stdexcept>

namespace facebook::rebalancer {

using entities::ContainerId;
using entities::DimensionId;
using entities::EquivalenceSetId;
using entities::GroupId;
using entities::ObjectId;

namespace {
std::shared_ptr<const ObjectScores> getObjectOrderingDimensionValuesIfExists(
    std::shared_ptr<const entities::Universe> universe,
    std::optional<DimensionId> objectOrderingDimensionId) {
  if (!objectOrderingDimensionId.has_value()) {
    return nullptr;
  }

  auto& dimension =
      universe->getObjects().getDimension(*objectOrderingDimensionId);
  if (dimension.size() != 1 || dimension.at(0).isDynamic()) {
    throw std::runtime_error(
        fmt::format(
            "objectOrderingDimension \"{}\" cannot be a dynamic dimension and should have only one value per object",
            universe->getEntityName(*objectOrderingDimensionId)));
  }

  return std::make_shared<const ObjectScores>(dimension.at(0).values());
}
} // namespace

Problem::Problem(
    std::shared_ptr<const entities::Universe> universe,
    const std::shared_ptr<const MaterializedProblem> materializedProblem,
    const ProblemConfigs& configs,
    std::shared_ptr<LogCollector> logger)
    : universe_(std::move(universe)),
      objective(materializedProblem->globalObjective),
      penaltyObjective(materializedProblem->penaltyObjective),
      configs(configs),
      materializedProblem_(materializedProblem),
      invalidMoveFilter_(materializedProblem_->invalidMoveFilter.get()),
      logger_(logger),
      wrappedExecutor_(
          std::make_shared<algopt::treeprof::ExecutorWrapper>(
              configs.threadPool)),
      equivalenceSetsStore_(*this) {
  if (!universe_) {
    throw std::runtime_error("universe has not be been initialized in Problem");
  }

  // Union the learned (GNN) filter, if provided, with the constraint-built one
  // so a move is pruned when either marks it invalid. The constraint filter is
  // exact, so it must not be discarded.
  if (configs.learnedInvalidMoveFilter) {
    learnedFilter_ =
        std::make_unique<InvalidMoveFilter>(*configs.learnedInvalidMoveFilter);
    if (const auto* constraintFilter =
            materializedProblem_->invalidMoveFilter.get()) {
      learnedFilter_->mergeFrom(*constraintFilter);
    }
    invalidMoveFilter_ = learnedFilter_.get();
  }

  // setup initial assignment and initialize current assignment with it
  buildInitialAssignment();
  assignment = initial_assignment;
  fixed_objects = materializedProblem_->fixedObjects;
  constraint = materializedProblem_->finalConstraint;

  // Init orchestrator
  algopt::treeprof::EventRecorder initOrchestratorEvent(
      "Initialize orchestrator");
  std::vector<Expression*> roots;
  // the order of roots below is deliberate to enable short-circuiting when
  // evaluating hard constraints
  for (auto& labeled_constraint :
       materializedProblem_->labeledHardConstraints) {
    roots.push_back(labeled_constraint->expression.get());
  }

  roots.push_back(materializedProblem_->finalConstraint.get());

  // Local search compares each objective before its penalty, (G0, P0, G1,
  // P1, ...). Evaluating a root first processes every earlier root, so roots
  // must follow that order for the comparison to short-circuit.
  const auto& materializedObjective = materializedProblem_->globalObjective;
  const auto& materializedPenalty = materializedProblem_->penaltyObjective;
  const bool hasPenalty = materializedPenalty.size() != 0;
  if (hasPenalty &&
      materializedPenalty.size() != materializedObjective.size()) {
    throw std::runtime_error(
        fmt::format(
            "Global and Penalty objectives have different sizes: {} != {}",
            materializedObjective.size(),
            materializedPenalty.size()));
  }
  for (const auto pos : folly::irange(materializedObjective.size())) {
    roots.push_back(materializedObjective.getObjectiveAt(pos).get());
    if (hasPenalty) {
      roots.push_back(materializedPenalty.getObjectiveAt(pos).get());
    }
  }

  if (materializedProblem_->metrics && configs.addMetricsExprsToOrchestrator) {
    auto& metrics = *materializedProblem_->metrics;
    metrics.pushAllExprsTo(roots);
  }

  orchestrator_.init(
      std::move(roots),
      AffectedByChangeDecisionData(
          universe_->getNumObjects(),
          universe_->getContainers().getContainerIds().size()),
      getFixedContainers());
  initOrchestratorEvent.stop();

  log_node_summary(
      orchestrator_.getNumFixedNodes(), orchestrator_.getNumNodes());

  algopt::treeprof::EventRecorder initBoundsEvent(
      "Initialize unconstrained bounds");
  // pre-compute unconstrained bounds for all nodes in the
  // objectives/constraint expression trees and re-compute bounds for any
  // expression that may already have been initialized with lower and upper
  // bounds
  {
    // Parallelization may be slower when there are not enough roots because of
    // lockings and waitings if many child expressions are shared.
    if (configs.enableParallelizedBoundsComputing &&
        configs.threadPool->numThreads() > 1) {
      orchestrator_.initBoundsBottomUp(configs.threadPool);
    } else {
      objective.initUnconstrainedBounds();
      penaltyObjective.initUnconstrainedBounds();
      Context bounds_context;
      if (constraint != nullptr) {
        constraint->init_unconstrained_bounds(bounds_context);
      }
    }
  }
  initBoundsEvent.stop();

  const auto& init_containers = initial_assignment.getContainers();
  containers =
      PackerSet<ContainerId>(init_containers.begin(), init_containers.end());
  not_accepting_containers = materializedProblem_->nonAcceptingContainers;

  XLOG(DBG1) << fmt::format(
      "Total objects {}, fixed: {}",
      universe_->getNumObjects(),
      materializedProblem_->fixedObjects.size());

  XLOG(DBG1) << fmt::format(
      "Total containers {}, fixed : {}, Non-accepting {}",
      containers.size(),
      materializedProblem_->fixedContainers.size(),
      materializedProblem_->nonAcceptingContainers.size());

  if (materializedProblem_->similarContainers) {
    similarContainers =
        SimilarContainers(materializedProblem_->similarContainers.value());
  }

  // log_objective_summary() is expensive, so run only in DBG2.
  if (XLOG_IS_ON(DBG2)) {
    log_objective_summary();
  }

  moveStatsAggregatorConfig = makeMoveStatsConfig();
}

std::shared_ptr<const MoveStatsAggregatorConfig> Problem::makeMoveStatsConfig()
    const {
  auto& moveStatsSpec = configs.moveStatsSpec;
  auto moveStatsConfig = std::make_shared<MoveStatsAggregatorConfig>(
      *moveStatsSpec.trackContainers(),
      *moveStatsSpec.trackObjects(),
      *moveStatsSpec.showAllChangedObjectivesInMovesSummary());

  if (moveStatsSpec.trackObjectsWhitelist()) {
    const auto& equivalenceSets = getEquivalenceSets();
    moveStatsConfig->trackObjectsWhitelist = PackerSet<ObjectId>();

    for (auto& objectName : *moveStatsSpec.trackObjectsWhitelist()) {
      auto objectId = universe_->getObjectId(objectName);

      if (moveStatsConfig->trackObjectsWhitelist->count(objectId) == 1) {
        continue;
      }

      // If an object is optimized away (e.g. a fixed object), then local search
      // won't attempt to move it and there's no need to whitelist it.
      if (!equivalenceSets.hasObject(objectId)) {
        continue;
      }

      // Local search will only move one object of the same equivalence class as
      // an optimization. In order to always capture the move stats for the
      // relevant objects, include any object of the same equivalence class in
      // the whitelist. A post-processing step will propagate the move stats of
      // the only object moved to the relevant objects in the same equivalence
      // class.
      auto equivalenceId = equivalenceSets.at(objectId);
      for (auto objectToTrack : equivalenceSets.getSet(equivalenceId)) {
        moveStatsConfig->trackObjectsWhitelist->insert(objectToTrack);
      }
    }

    moveStatsConfig->printTrackedObjectStats =
        *moveStatsSpec.printTrackedObjectStats();
  }

  if (moveStatsSpec.printSourceContainersWhitelist() &&
      (*moveStatsSpec.printSourceContainersWhitelist()).size() > 0) {
    moveStatsConfig->printSourceContainersWhitelist = PackerSet<ContainerId>();
    for (auto& containerName :
         *moveStatsSpec.printSourceContainersWhitelist()) {
      auto containerId = universe_->getContainerId(containerName);
      moveStatsConfig->printSourceContainersWhitelist->insert(containerId);
    }
  }

  return moveStatsConfig;
}

void Problem::log_objective_summary() {
  XLOG(DBG2) << "Objectives:";
  int pos = 0;
  for (const auto& labeled_objectives_elem :
       materializedProblem_->labeledObjectives) {
    XLOG(DBG2) << fmt::format("Objective_{}", pos);
    for (auto& labeled_objective : labeled_objectives_elem) {
      auto value = labeled_objective->expression->value;
      XLOG(DBG2) << labeled_objective->name << " : "
                 << labeled_objective->expression->description << " : "
                 << value;
    }
    ++pos;
  }

  XLOG(DBG2) << "Constraints:";
  for (auto& labeled_constraint :
       materializedProblem_->labeledHardConstraints) {
    auto value = labeled_constraint->expression->value;
    XLOG(DBG2) << labeled_constraint->name << " : "
               << labeled_constraint->expression->description << " : " << value;
  }

  XLOG(DBG2) << "Objective Root:\n"
             << objective.getFirstObjective()->digest(*this, true);
  XLOG(DBG2) << "Constraint root:\n" << constraint->digest(*this, true);
} // namespace rebalancer

const LabeledConstraints& Problem::getLabeledConstraints() const {
  return materializedProblem_->labeledHardConstraints;
}

const GlobalLabeledObjectives& Problem::getLabeledObjectives() const {
  return materializedProblem_->labeledObjectives;
}

const GlobalLabeledObjectives& Problem::getLabeledPenalties() const {
  return materializedProblem_->labeledPenalties;
}

ApplyResult Problem::apply(const ChangeSet& changes) {
  auto result = applyChanges(changes);
  maybeFixMovedObjects(changes);
  return result;
}

ApplyStatus Problem::tryApply(
    const ChangeSet& changes,
    folly::FunctionRef<ApplyStatus()> validate) {
  applyChanges(changes);
  const auto status = validate();
  if (status != ApplyStatus::APPLIED) {
    // Restore the previous state when validation fails.
    applyChanges(changes.getInverse());
    return status;
  }

  maybeFixMovedObjects(changes);
  return status;
}

ApplyResult Problem::applyChanges(const ChangeSet& changes) {
  // Check for matching removals and additions before changing the assignment.
  static_cast<void>(MoveSet::fromChangeSet(changes));
  auto objectiveBefore = objective.getValue();

  for (const auto& change : changes) {
    if (change.getValue() == 1) {
      assignment.moveTo(change.getObject(), change.getContainer());
    }
  }

  // Note that it is assumed that the current assignment has aleady been
  // applied, so we just need to call 'partial_apply' to handle the changes
  partialApply(changes);

  // now that the changes have been applied and the values updated, let's
  // update the attributes as well
  entityAttributesStore_.updateOnApply(changes);

  return {
      .objectiveBefore = std::move(objectiveBefore),
      .objectiveAfter = objective.getValue()};
}

void Problem::maybeFixMovedObjects(const ChangeSet& changes) {
  if (!universe_->getMoveObjectsOnce()) {
    return;
  }
  for (const auto& change : changes) {
    if (change.getValue() == 1) {
      fixed_objects.insert(change.getObject());
    }
  }
}

void Problem::partialApply(const ChangeSet& changes) {
  apply_context.clear();
  apply_context.changes() = changes;
  orchestrator_.apply(apply_context, assignment);
}

Orchestrator& Problem::getOrchestrator() {
  return orchestrator_;
}

double Problem::fullApplyReusingOrchestratorValues(Expression& expr) {
  Context context;
  for (const auto* node : orchestrator_.getNodesInPostorder()) {
    context.apply().save(node->getId(), node->value);
  }
  return expr.fullApply(TopToBottomEvaluator(context), assignment);
}

const std::string& Problem::containerName(ContainerId containerId) const {
  return universe_->getEntityName(containerId);
}

const std::string& Problem::objectName(ObjectId objectId) const {
  return universe_->getEntityName(objectId);
}

ContainerId Problem::containerId(const std::string& containerName) const {
  return universe_->getContainerId(containerName);
}

ObjectId Problem::objectId(const std::string& objectName) const {
  return universe_->getObjectId(objectName);
}

std::optional<int> Problem::get_maybe_fixed_assignment_value(
    LpContext& context,
    EquivalenceSetId equiv_set,
    ContainerId container) const {
  if (context.dynamicContainers().contains(container) &&
      context.dynamicEquivSets().contains(equiv_set)) {
    return std::nullopt;
  }

  auto& fixedEquivSetValsInContainer =
      context.lpFixedVals().getSavedOrCompute(container, [&]() {
        PackerMap<entities::EquivalenceSetId, int> equivSetToValue;
        for (const auto obj : assignment.getObjects(container)) {
          equivSetToValue[getEquivalenceSets().at(obj)]++;
        }
        return equivSetToValue;
      });

  return folly::get_default(fixedEquivSetValsInContainer, equiv_set, 0);
}

algopt::lp::Expression Problem::lp_assignment_var(
    LpContext& context,
    EquivalenceSetId equivSet,
    ContainerId container) const {
  if (context.dynamicContainers().contains(container) &&
      context.dynamicEquivSets().contains(equivSet)) {
    return lp_store.getAssignmentVar(equivSet, container);
  }
  return lp_store.expression(
      get_maybe_fixed_assignment_value(context, equivSet, container).value());
}

bool Problem::shouldCollectMoveStats() const {
  return moveStatsAggregatorConfig->trackContainers ||
      moveStatsAggregatorConfig->trackObjects;
}

int Problem::getContainerSubproblemId(ContainerId containerId) const {
  if (containerToSubproblemId) {
    return folly::get_default(
        *containerToSubproblemId, containerId, SPECIAL_SUBPROBLEM_ID);
  } else {
    throw std::runtime_error(
        "Mapping of container to subproblemId is required");
  }
}

const PackerMap<ContainerId, int>* FOLLY_NULLABLE
Problem::getContainerToSubproblemIdsPtr() const {
  if (!containerToSubproblemId.has_value()) {
    return nullptr;
  }

  return &containerToSubproblemId.value();
}

const PackerSet<ContainerId>& Problem::getOutOfScopeContainerIds(
    const std::string& scopeName) {
  return scopeNameToOutOfScopeContainerIds_.getSavedOrCompute(scopeName, [&]() {
    auto scopeId = universe_->getScopeId(scopeName);
    auto& scope = universe_->getScope(scopeId);
    entities::Set<ContainerId> scopeContainerIds;
    for (auto scopeItemId : scope.getScopeItemIds()) {
      for (auto containerId : scope.getContainerIds(scopeItemId)) {
        scopeContainerIds.insert(containerId);
      }
    }

    PackerSet<ContainerId> outOfScopeContainerIds;
    for (auto containerId : universe_->getContainers().getContainerIds()) {
      if (!scopeContainerIds.contains(containerId)) {
        outOfScopeContainerIds.emplace(containerId);
      }
    }

    return outOfScopeContainerIds;
  });
}

std::optional<GroupId> Problem::getOnlyGroupIdIfExists(
    const std::string& partitionName,
    ObjectId objectId) const {
  return getOnlyGroupIdIfExists(
      universe_->getPartitionId(partitionName), objectId);
}

std::optional<GroupId> Problem::getOnlyGroupIdIfExists(
    entities::PartitionId partitionId,
    entities::ObjectId objectId) const {
  auto& partition = universe_->getPartition(partitionId);
  auto& objectIdToGroupIds = partition.getObjectIdToGroupIds();
  auto groupIdsPtr = folly::get_ptr(objectIdToGroupIds, objectId);
  if (groupIdsPtr == nullptr) {
    return std::nullopt;
  }

  if (groupIdsPtr->size() > 1) {
    throw std::runtime_error(
        "Unhandled case when an object belongs to multiple groups");
  }

  return *groupIdsPtr->begin();
}

const std::vector<ObjectId>& Problem::getObjectIdsForGroup(
    const std::string& partitionName,
    GroupId groupId) const {
  auto& partition =
      universe_->getPartition(universe_->getPartitionId(partitionName));
  return partition.getObjectIds(groupId);
}

ObjectStore Problem::getDynamicObjects(
    ContainerId containerId,
    const std::string& partitionName,
    GroupId groupId) const {
  auto& objectsInGroup = getObjectIdsForGroup(partitionName, groupId);
  auto dynamicObjectsInGroup = assignment.getObjectStoreFactory().get();
  for (auto object : objectsInGroup) {
    if (assignment.getContainer(object) == containerId &&
        !fixed_objects.contains(object)) {
      dynamicObjectsInGroup.insert(object);
    }
  }

  return dynamicObjectsInGroup;
}

const std::shared_ptr<const entities::Universe>& Problem::getUniversePtr()
    const {
  return universe_;
}

const entities::Universe& Problem::getUniverse() const {
  return *universe_;
}

const std::shared_ptr<const MaterializedProblem>
Problem::getMaterializedProblem() const {
  if (!materializedProblem_) {
    throw std::runtime_error(
        "materializedProblem has not be been initialized in Problem");
  }

  return materializedProblem_;
}

std::vector<const Expression*> Problem::getHardConstraintComponents(
    const std::vector<std::string>& constraintNames) const {
  const auto& constraintIdToHardComponent =
      getMaterializedProblem()->hardConstraints;
  std::vector<const Expression*> components;
  components.reserve(constraintNames.size());
  for (const auto& name : constraintNames) {
    components.push_back(
        constraintIdToHardComponent.at(universe_->getConstraintId(name)).get());
  }
  return components;
}

PackerSet<entities::EquivalenceSetId> Problem::getDynamicEquivalentSets(
    const PackerSet<ContainerId>& containerSet) const {
  PackerSet<entities::EquivalenceSetId> dynamicSets;
  for (auto container : containerSet) {
    for (auto obj : assignment.getDynamicObjects(container)) {
      dynamicSets.insert(getEquivalenceSets().at(obj));
    }
  }
  return dynamicSets;
}

algopt::lp::Variable Problem::assignment_var(
    entities::EquivalenceSetId equiv_set,
    ContainerId container) const {
  return lp_store.getAssignmentVar(equiv_set, container);
}

SimilarContainers& Problem::getSimilarContainers() {
  if (!materializedProblem_->similarContainers.has_value()) {
    throw std::runtime_error(
        "Expected SimilarContainers definition to be provided for sampling");
  }
  return similarContainers.value();
}

std::shared_ptr<algopt::treeprof::ExecutorWrapper>
Problem::getExecutorForLpBuilding() const {
  // If lp building is parallelized, then use the wrapped executor; else, return
  // nullptr, which in turn will result in lp building being serial
  if (configs.enableParallelizedLpBuilding) {
    return wrappedExecutor_;
  }
  return nullptr;
}

std::shared_ptr<algopt::treeprof::ExecutorWrapper> Problem::getExecutor()
    const {
  return wrappedExecutor_;
}

const std::vector<ContainerId>&
Problem::getDescendingHotnessContainersOverride() const {
  return universe_->getDescendingHotnessContainers();
}

void Problem::buildInitialAssignment() {
  auto objectOrderingDimension = universe_->getObjectOrderingDimensionId();
  auto objectOrderingDimensionValues = getObjectOrderingDimensionValuesIfExists(
      universe_, objectOrderingDimension);

  initial_assignment = Assignment(
      materializedProblem_->updatedInitialAssignment,
      getObjectOrderingDimensionValuesIfExists(
          universe_, objectOrderingDimension),
      materializedProblem_->fixedObjects,
      configs.useDynamicObjectOrdering);
}

const PackerSet<ContainerId>& Problem::getFixedContainers() const {
  return materializedProblem_->fixedContainers;
}

DestinationsToExploreGenerator& Problem::getDestinationsGenerator() {
  if (!destinationsGenerator_) {
    destinationsGenerator_ = std::make_shared<DestinationsToExploreGenerator>(
        not_accepting_containers, *universe_);
  }
  return *destinationsGenerator_;
}

ObjectsToExploreGenerator& Problem::getObjectsGenerator() {
  if (!objectsGenerator_) {
    objectsGenerator_ = std::make_shared<ObjectsToExploreGenerator>(*universe_);
  }
  return *objectsGenerator_;
}

void Problem::log_node_summary(size_t fixedNodeCount, size_t nodeCount) {
  logger_->log(
      NodeSummary{.fixedNodeCount = fixedNodeCount, .nodeCount = nodeCount});
}

EntityAttributesStore& Problem::getEntityAttributeStore() {
  return entityAttributesStore_;
}

EquivalenceSetsStore& Problem::getEquivalenceSetsStore() {
  return equivalenceSetsStore_;
}

interface::EquivalenceSetInfo Problem::makeEquivalenceSetInfo() const {
  // we only write the equivalence sets computed by rebalancer
  interface::EquivalenceSetInfo equivSetInfo;
  auto& equivSets = getEquivalenceSets();
  for (auto equivSetId : equivSets.ids()) {
    interface::EquivalenceSetMetadata equivSetMetadata;
    equivSetMetadata.name() =
        fmt::format("{}_{}", interface::kEquivSetNamePrefix.data(), equivSetId);
    for (auto objectId : equivSets.getSet(equivSetId)) {
      equivSetMetadata.objectNames()->push_back(objectName(objectId));
    }
    equivSetInfo.equivalenceSets()->push_back(std::move(equivSetMetadata));
  }
  return equivSetInfo;
}

const EquivalenceSets& Problem::getEquivalenceSets() const {
  return equivalenceSetsStore_.get();
}

PackerMap<entities::ObjectId, entities::ContainerId>
Problem::getStableObjectToContainerAssignment(
    const PackerMap<
        entities::EquivalenceSetId,
        PackerMap<entities::ContainerId, int>>&
        equivSetToContainerToObjectCount,
    const Assignment& initialAssignment,
    const std::optional<std::string>& shuffleSeed) {
  ObjectToContainerAssignmentUtils objectToContainerAssignmentUtils;
  for (auto expr : orchestrator_.getNodesInPostorder()) {
    if (expr->needsEquivalenceSetBasedPostProcessing()) {
      objectToContainerAssignmentUtils.addStableContainerGroup(
          expr->getDirectlyAffectedContainers().getSetPtr());
    }
  }
  return objectToContainerAssignmentUtils.getObjectToContainer(
      getEquivalenceSets(),
      equivSetToContainerToObjectCount,
      initialAssignment,
      shuffleSeed);
}

} // namespace facebook::rebalancer
