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

#include "algopt/rebalancer/materializer/Materializer.h"

#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/moves/InvalidMoveFilter.h"
#include "algopt/rebalancer/treeprof/EventRecorder.h"

#include <folly/container/irange.h>
#include <folly/coro/BlockingWait.h>
#include <thrift/lib/cpp/util/EnumUtils.h>

#include <algorithm>
#include <stdexcept>

using namespace facebook::rebalancer::entities;
using namespace facebook::rebalancer::interface;

namespace facebook::rebalancer::materializer {

namespace {
void expandGoalsToIndex(
    int index,
    std::vector<GoalInfo>& goals,
    const entities::Universe& universe) {
  const auto targetSize = static_cast<size_t>(index + 1);
  while (goals.size() < targetSize) {
    goals.push_back(
        GoalInfo{
            .objectiveExpr = const_expr(0, universe), .penaltyExpr = nullptr});
  }
}
void inplaceAddToGoalsIndex(
    int index,
    std::vector<GoalInfo>& goals,
    const GoalInfo& goal,
    const entities::Universe& universe) {
  expandGoalsToIndex(index, goals, universe);
  auto& aggregate = goals.at(index);
  if (goal.objectiveExpr) {
    inplace_add(aggregate.objectiveExpr, goal.objectiveExpr);
  }
  if (goal.penaltyExpr) {
    if (!aggregate.penaltyExpr) {
      aggregate.penaltyExpr = const_expr(0, universe);
    }
    inplace_add(aggregate.penaltyExpr, goal.penaltyExpr);
  }
}
} // namespace

const std::shared_ptr<const MaterializedProblem> Materializer::materialize(
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
    std::shared_ptr<const Universe> universe,
    bool continuousExpressions,
    std::shared_ptr<LogCollector> logger,
    bool shouldCollectMetrics,
    bool enableInvalidMoveFilter,
    bool useSeparatedLocalSearchPenaltyObjective,
    bool parallelized) {
  Materializer materializer(
      std::move(executor),
      std::move(universe),
      continuousExpressions,
      std::move(logger),
      shouldCollectMetrics,
      enableInvalidMoveFilter,
      useSeparatedLocalSearchPenaltyObjective,
      parallelized);
  return materializer.materialize();
}

Materializer::Materializer(
    std::shared_ptr<algopt::treeprof::ExecutorWrapper> executor,
    std::shared_ptr<const entities::Universe> universe,
    bool continuousExpressions,
    std::shared_ptr<LogCollector> logger,
    bool shouldCollectMetrics,
    bool enableInvalidMoveFilter,
    bool useSeparatedLocalSearchPenaltyObjective,
    bool parallelized)
    : executor_(std::move(executor)),
      universe_(std::move(universe)),
      specBuilderFactory_(universe_, continuousExpressions, logger),
      logger_(std::move(logger)),
      materialized_(std::make_shared<MaterializedProblem>(*universe_)),
      invalidMoveFilter_(
          enableInvalidMoveFilter
              ? std::make_unique<InvalidMoveFilter>(
                    universe_->getObjects().getObjectIds().size(),
                    universe_->getContainers().getContainerIds().size())
              : nullptr),
      metricsBuilder_(
          shouldCollectMetrics ? std::make_shared<Metrics::Builder>()
                               : nullptr),
      useSeparatedLocalSearchPenaltyObjective_(
          useSeparatedLocalSearchPenaltyObjective),
      parallelized_(parallelized) {}

std::shared_ptr<MaterializedProblem> Materializer::materialize() {
  auto updatesInInitialAssignment = getUpdatesInInitialAssignment();

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    // Create updatedInitialAssignment using updates from spec(s)
    auto& containers = universe_->getContainers();
    for (auto containerId : containers.getContainerIds()) {
      // initialize updatedInitialAssignment for each containerId
      if (!wlockedMaterialized.updatedInitialAssignment.contains(containerId)) {
        wlockedMaterialized.updatedInitialAssignment[containerId] = {};
      }

      for (auto objectId : containers.getInitialObjectIds(containerId)) {
        auto dstContainerId = folly::get_default(
            updatesInInitialAssignment, objectId, containerId);
        wlockedMaterialized.updatedInitialAssignment[dstContainerId].push_back(
            objectId);
      }
    }

    wlockedMaterialized.similarContainers = getSimilarContainers();

    // Initialize the constraint.
    wlockedMaterialized.finalConstraint = any_positive({}, *universe_);
    wlockedMaterialized.userConstraintSum = const_expr(0, *universe_);
  });

  ExpressionBuilder expressionBuilder(
      universe_,
      materialized_.rlock()->updatedInitialAssignment,
      executor_,
      metricsBuilder_);

  algopt::treeprof::EventRecorder ctrMaterialization(
      "Materialize Constraints & Goals");
  const auto constraintIds = universe_->getConstraints().getConstraintIds();
  const auto goalIds = universe_->getGoals().getGoalIds();
  auto* const invalidMoveFilter = invalidMoveFilter_.get();
  std::vector<folly::coro::Task<void>> tasks;
  tasks.reserve(constraintIds.size() + goalIds.size());
  for (auto constraintId : constraintIds) {
    tasks.emplace_back(materializeConstraintCoro(
        expressionBuilder, constraintId, invalidMoveFilter));
  }
  for (auto goalId : goalIds) {
    tasks.emplace_back(materializeGoalCoro(expressionBuilder, goalId));
  }
  folly::coro::blockingWait(
      CoroUtils::runEachTask(
          tasks.begin(),
          tasks.end(),
          [&](auto iter) { return std::move(*iter); },
          executor_));

  if (invalidMoveFilter_ && !invalidMoveFilter_->empty()) {
    materialized_.wlock()->invalidMoveFilter = std::move(invalidMoveFilter_);
  }

  ctrMaterialization.stop();

  // The computation of fixed containers depends on fixed objects being known.
  // Since fixed objects are computed as part of materializeConstraints, it's
  // important for collectFixedContainers to be invoked after.
  collectFixedContainers();

  buildLabeledConstraints();

  buildGlobalObjectiveAndLabeledObjectives();

  if (metricsBuilder_) {
    materialized_.wlock()->metrics =
        std::make_shared<Metrics>(metricsBuilder_->build(universe_));
  }

  return *materialized_.wlockPointer();
}

Map<ObjectId, ContainerId> Materializer::getUpdatesInInitialAssignment() {
  /* Parse specs and check if any updates if required in initial assignment */
  Map<ObjectId, ContainerId> updatesInInitialAssignment;
  auto& constraints = universe_->getConstraints();
  for (auto constraintId : constraints.getConstraintIds()) {
    const auto& constraint =
        universe_->getConstraints().getConstraint(constraintId);
    auto specBuilder = specBuilderFactory_.getBuilder(constraint.getSpec());
    auto updates = specBuilder->getUpdatesInInitialAssignment();

    for (auto& [objectId, containerId] : updates) {
      auto existingOverride =
          folly::get_ptr(updatesInInitialAssignment, objectId);
      if (existingOverride && *existingOverride != containerId) {
        throw std::runtime_error(
            fmt::format(
                "object {} cannot be fixed to {} as it is already fixed to {}. Object can be fixed to only one container. Please fix required constraints",
                universe_->getEntityName(objectId),
                universe_->getEntityName(containerId),
                universe_->getEntityName(*existingOverride)));
      } else {
        updatesInInitialAssignment.emplace(objectId, containerId);
      }
    }
  }
  return updatesInInitialAssignment;
}

folly::coro::Task<void> Materializer::materializeConstraintCoro(
    ExpressionBuilder& expressionBuilder,
    entities::ConstraintId constraintId,
    InvalidMoveFilter* invalidMoveFilter) {
  auto& constraint = universe_->getConstraints().getConstraint(constraintId);
  auto specBuilder = specBuilderFactory_.getBuilder(constraint.getSpec());
  auto specType =
      apache::thrift::util::enumNameSafe(constraint.getSpec().getType());
  auto description =
      fmt::format("{}: {}", specType, specBuilder->description());

  algopt::treeprof::EventRecorder materializeCtr(
      fmt::format("[constraint] {}", description));
  const algopt::Timer timer(true);

  auto userConstraint = const_expr(0, *universe_);
  ExprPtr softConstraint;
  ExprPtr constraintPenalty;
  auto hardConstraint = any_positive({}, *universe_);

  auto constraints = co_await specBuilder->constraints(expressionBuilder);

  // Sort by expression memory address for better cache locality
  // when expressions were allocated from multiple threads. Serial runs keep
  // the spec builder order: addresses differ between processes, so sorting by
  // them would make the expressions built below, and the solution, vary.
  if (parallelized_) {
    std::sort(
        constraints.begin(),
        constraints.end(),
        [](const ConstraintInfo& a, const ConstraintInfo& b) {
          return a.constraintExpr.get() < b.constraintExpr.get();
        });
  }

  const auto addComponentTo = [&](const ExprPtr& component,
                                  ExprPtr& aggregate) {
    if (!component) {
      return;
    }
    if (!aggregate) {
      aggregate = const_expr(0, *universe_);
    }
    inplace_add(aggregate, component);
  };

  // TODO: The loop below is quite expensive when there are many expressions in
  // components (since they take significant time to initialize, etc.).
  // Parallelize it after making Context thread-safe.
  for (auto& constraintInfo : constraints) {
    userConstraint += max(0, constraintInfo.constraintExpr);

    auto [hardComponent, softComponent, penaltyComponent] =
        splitConstraintComponent(
            expressionBuilder,
            constraint,
            constraintInfo,
            *specBuilder,
            universe_,
            useSeparatedLocalSearchPenaltyObjective_);

    addComponentTo(softComponent, softConstraint);
    addComponentTo(penaltyComponent, constraintPenalty);

    if (hardComponent != nullptr) {
      inplace_any_positive(hardConstraint, hardComponent);
    }
  }

  // Initially broken constraints are softened and minimized as a goal in
  // the first item in the goals tuple.
  if (softConstraint) {
    softConstraint->description =
        fmt::format("initially broken {}", description);
    softConstraint->setSpecAnnotation(specType);
  }

  if (constraintPenalty) {
    constraintPenalty->setSpecAnnotation(specType);
  }

  hardConstraint->setSpecAnnotation(specType);
  userConstraint->setSpecAnnotation(specType);
  userConstraint->description = description;
  hardConstraint->description = description;
  auto fixedObjects = specBuilder->fixedObjects();
  auto nonAcceptingContainers = specBuilder->nonAcceptingContainers();

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    if (softConstraint || constraintPenalty) {
      inplaceAddToGoalsIndex(
          constraint.getTupleIndex(),
          wlockedMaterialized.finalGoals,
          GoalInfo{
              .objectiveExpr = softConstraint,
              .penaltyExpr = constraintPenalty},
          *universe_);
    }
    if (softConstraint) {
      wlockedMaterialized.softConstraints.emplace(constraintId, softConstraint);
    }
    if (constraintPenalty) {
      wlockedMaterialized.softConstraintPenalties.emplace(
          constraintId, constraintPenalty);
    }
    any_positive_add(wlockedMaterialized.finalConstraint, hardConstraint);
    wlockedMaterialized.userConstraints.emplace(constraintId, userConstraint);
    inplace_add(
        wlockedMaterialized.userConstraintSum, std::move(userConstraint));
    wlockedMaterialized.hardConstraints.emplace(constraintId, hardConstraint);
    wlockedMaterialized.fixedObjects.insert(
        std::move_iterator(fixedObjects.begin()),
        std::move_iterator(fixedObjects.end()));
    wlockedMaterialized.nonAcceptingContainers.insert(
        std::move_iterator(nonAcceptingContainers.begin()),
        std::move_iterator(nonAcceptingContainers.end()));
  });

  if (invalidMoveFilter &&
      constraint.getPolicy() != interface::ConstraintPolicy::SOFT) {
    specBuilder->populateInvalidMoveFilter(
        *invalidMoveFilter, expressionBuilder.getUpdatedInitialAssignment());
  }

  logger_->log(
      SpecUsageInfo{
          .specType = std::move(specType),
          .specParameters = specBuilder->getSpecInfo(),
          .usedAs = "constraint",
          .materializationTime = timer.getSeconds(),
      });
  materializeCtr.stop();
  co_return;
}

SplitConstraint Materializer::splitConstraintComponent(
    ExpressionBuilder& expressionBuilder,
    const entities::Constraint& constraint,
    const ConstraintInfo& constraintInfo,
    const SpecBuilder& specBuilder,
    std::shared_ptr<const entities::Universe> universe,
    bool useSeparatedLocalSearchPenaltyObjective) {
  auto& constraintExpr = constraintInfo.constraintExpr;
  const auto softened = [&]() {
    if (useSeparatedLocalSearchPenaltyObjective) {
      return getSeparatedSoftenedConstraint(
          expressionBuilder, specBuilder, constraintInfo, constraint);
    }
    return GoalInfo{
        .objectiveExpr = getSoftenedConstraint(constraintInfo, constraint),
        .penaltyExpr = nullptr};
  };
  switch (constraint.getPolicy()) {
    case ConstraintPolicy::DEFAULT: {
      const double initialValue = constraintExpr->getInitialValue();
      const bool initiallyBroken =
          universe->getPrecision().compare(initialValue, 0) == 1;
      if (!initiallyBroken) {
        return SplitConstraint{
            .hardComponent = constraintExpr,
            .softComponent = nullptr,
            .penaltyComponent = nullptr};
      }
      auto [objectiveExpr, penaltyExpr] = softened();
      return SplitConstraint{
          .hardComponent = getViolationBeyondInitial(
              constraintExpr,
              initialValue,
              expressionBuilder.getUpdatedInitialAssignment()),
          .softComponent = std::move(objectiveExpr),
          .penaltyComponent = std::move(penaltyExpr)};
    }
    case ConstraintPolicy::HARD: {
      return {
          .hardComponent = constraintExpr,
          .softComponent = nullptr,
          .penaltyComponent = nullptr};
    }
    case ConstraintPolicy::SOFT: {
      auto [objectiveExpr, penaltyExpr] = softened();
      return {
          .hardComponent = nullptr,
          .softComponent = std::move(objectiveExpr),
          .penaltyComponent = std::move(penaltyExpr)};
    }
    default:
      throw std::runtime_error("Unhandled ConstraintPolicy");
  }
}

folly::coro::Task<void> Materializer::materializeGoalCoro(
    ExpressionBuilder& expressionBuilder,
    entities::GoalId goalId) {
  auto& goal = universe_->getGoals().getGoal(goalId);

  auto specBuilder = specBuilderFactory_.getBuilder(goal.getSpec());
  auto specType = apache::thrift::util::enumNameSafe(goal.getSpec().getType());
  auto description =
      fmt::format("{}: {}", specType, specBuilder->description());

  algopt::treeprof::EventRecorder materializeGoal(
      fmt::format("[goal] {}", description));
  const algopt::Timer timer(true);

  auto goalInfo = useSeparatedLocalSearchPenaltyObjective_
      ? co_await specBuilder->goal(expressionBuilder)
      : GoalInfo{
            .objectiveExpr = co_await specBuilder->goalCoro(expressionBuilder),
            .penaltyExpr = nullptr};

  goalInfo.objectiveExpr->setSpecAnnotation(specType);

  double weight = goal.getWeight();
  if (weight == 1) {
    goalInfo.objectiveExpr->description = description;
  } else {
    goalInfo.objectiveExpr *= weight;
    if (goalInfo.penaltyExpr != nullptr) {
      goalInfo.penaltyExpr *= weight;
    }
    goalInfo.objectiveExpr->description =
        fmt::format("{} * ({})", weight, description);
  }
  if (goalInfo.penaltyExpr != nullptr) {
    goalInfo.penaltyExpr->setSpecAnnotation(specType);
  }

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    inplaceAddToGoalsIndex(
        goal.getTupleIndex(),
        wlockedMaterialized.finalGoals,
        goalInfo,
        *universe_);
    wlockedMaterialized.userGoals.emplace(goalId, goalInfo.objectiveExpr);
    if (goalInfo.penaltyExpr != nullptr) {
      wlockedMaterialized.userPenalties.emplace(goalId, goalInfo.penaltyExpr);
    }
  });

  logger_->log(
      SpecUsageInfo{
          .specType = std::move(specType),
          .specParameters = specBuilder->getSpecInfo(),
          .usedAs = "goal",
          .materializationTime = timer.getSeconds(),
      });
  materializeGoal.stop();
  co_return;
}

ExprPtr Materializer::getSoftenedConstraint(
    const ConstraintInfo& constraintInfo,
    const entities::Constraint& constraint) {
  auto invalidCost = constraint.getInvalidCost();
  auto invalidState = constraint.getInvalidState();
  auto weightedPenalty =
      invalidCost * SpecBuilder::getConstraintViolation(constraintInfo);

  if (invalidState == 0) {
    // TODO: it should be trivial to implement this optimization at the
    // expression level
    return weightedPenalty;
  }

  return invalidState * step(constraintInfo.constraintExpr) +
      std::move(weightedPenalty);
}

GoalInfo Materializer::getSeparatedSoftenedConstraint(
    ExpressionBuilder& expressionBuilder,
    const SpecBuilder& specBuilder,
    const ConstraintInfo& constraintInfo,
    const entities::Constraint& constraint) {
  const auto invalidCost = constraint.getInvalidCost();
  const auto invalidState = constraint.getInvalidState();

  auto separated = specBuilder.getSeparatedConstraintViolation(
      {constraintInfo}, expressionBuilder);
  separated.objectiveExpr *= invalidCost;
  if (separated.penaltyExpr != nullptr) {
    separated.penaltyExpr *= invalidCost;
  }
  if (invalidState == 0) {
    return separated;
  }
  separated.objectiveExpr = invalidState * step(constraintInfo.constraintExpr) +
      std::move(separated.objectiveExpr);
  return separated;
}

ExprPtr Materializer::getViolationBeyondInitial(
    ExprPtr constraint,
    double initialValue,
    const Assignment& updatedInitialAssignment) {
  // ObjectPartitionLookup requires complex handling in order to treat groups
  // independently as they are all conflated under the same expression.
  auto objectPartitionLookup =
      std::dynamic_pointer_cast<ObjectPartitionLookupDefault>(constraint);
  if (objectPartitionLookup != nullptr) {
    return objectPartitionLookup->get_do_not_make_worse_copy(
        updatedInitialAssignment);
  }

  return std::move(constraint) - initialValue;
}

std::optional<std::vector<std::vector<ContainerId>>>
Materializer::getSimilarContainers() {
  return universe_->getSimilarContainers();
}

void Materializer::buildLabeledConstraints() {
  materialized_.withWLock([&](auto& wlockedMaterialized) {
    // build labeledHardConstraints
    wlockedMaterialized.labeledHardConstraints.setRoot(
        wlockedMaterialized.finalConstraint);
    for (auto& [constraintId, expr] : wlockedMaterialized.hardConstraints) {
      wlockedMaterialized.labeledHardConstraints.addSingle(
          universe_->getEntityName(constraintId), expr);
    }

    // build labeledUserConstraints
    wlockedMaterialized.labeledUserConstraints.setRoot(
        wlockedMaterialized.userConstraintSum);
    for (auto& [constraintId, expr] : wlockedMaterialized.userConstraints) {
      wlockedMaterialized.labeledUserConstraints.addSingle(
          universe_->getEntityName(constraintId), expr);
    }
  });
}

void Materializer::buildGlobalObjectiveAndLabeledObjectives() {
  // build globalObjective
  GlobalObjective::Builder globalObjectiveBuilder;
  GlobalObjective::Builder penaltyObjectiveBuilder;
  // build labeled objectives
  GlobalLabeledObjectives::Builder globalLabeledObjectivesBuilder;
  GlobalLabeledObjectives::Builder globalLabeledPenaltiesBuilder;
  materialized_.withWLock([&](auto& wlockedMaterialized) {
    // All objective expression roots are finalized before this method runs.
    // Solving updates their values but never adds another Penalty expression.
    const bool hasPenaltyObjective = useSeparatedLocalSearchPenaltyObjective_ &&
        std::any_of(wlockedMaterialized.finalGoals.begin(),
                    wlockedMaterialized.finalGoals.end(),
                    [](const GoalInfo& goalInfo) {
                      return goalInfo.penaltyExpr != nullptr;
                    });
    for (const auto pos :
         folly::irange(wlockedMaterialized.finalGoals.size())) {
      const auto& goalInfo = wlockedMaterialized.finalGoals.at(pos);
      globalObjectiveBuilder.setObjective(
          static_cast<int>(pos), goalInfo.objectiveExpr);
      if (hasPenaltyObjective) {
        penaltyObjectiveBuilder.setObjective(
            static_cast<int>(pos),
            goalInfo.penaltyExpr ? goalInfo.penaltyExpr
                                 : const_expr(0, *universe_));
      }
    }
    wlockedMaterialized.globalObjective =
        globalObjectiveBuilder.build(*universe_);
    if (hasPenaltyObjective) {
      wlockedMaterialized.penaltyObjective =
          penaltyObjectiveBuilder.build(*universe_);
      for (const auto pos :
           folly::irange(wlockedMaterialized.penaltyObjective.size())) {
        globalLabeledPenaltiesBuilder.setRoot(
            pos, wlockedMaterialized.penaltyObjective.getObjectiveAt(pos));
      }
    }

    // if there are no userGoals or softConstraints, then just set a
    // labeledObjective with root to be const_expr(0, *universe_) as
    // finalGoals(0) still exists with expr = const_expr(0, *universe_)
    if (wlockedMaterialized.softConstraints.size() == 0 &&
        wlockedMaterialized.userGoals.size() == 0) {
      globalLabeledObjectivesBuilder.setRoot(0, const_expr(0, *universe_));
    }
  });

  auto& userGoals = materialized_.rlock()->userGoals;
  auto& softConstraints = materialized_.rlock()->softConstraints;
  auto& userPenalties = materialized_.rlock()->userPenalties;
  auto& softConstraintPenalties =
      materialized_.rlock()->softConstraintPenalties;
  for (auto& [goalId, expr] : userGoals) {
    auto& goal = universe_->getGoals().getGoal(goalId);
    auto& name = universe_->getEntityName(goalId);
    globalLabeledObjectivesBuilder.addSingle(
        goal.getTupleIndex(), name, expr, goal.getWeight());
  }

  for (auto& [constraintId, expr] : softConstraints) {
    const int tuplePos =
        universe_->getConstraints().getConstraint(constraintId).getTupleIndex();
    auto& name = universe_->getEntityName(constraintId);
    globalLabeledObjectivesBuilder.addSingle(
        tuplePos, name, expr, 1 /* weight */);
  }

  for (auto& [goalId, expr] : userPenalties) {
    const auto& goal = universe_->getGoals().getGoal(goalId);
    globalLabeledPenaltiesBuilder.addSingle(
        goal.getTupleIndex(), universe_->getEntityName(goalId), expr);
  }

  for (auto& [constraintId, expr] : softConstraintPenalties) {
    const auto tuplePos =
        universe_->getConstraints().getConstraint(constraintId).getTupleIndex();
    globalLabeledPenaltiesBuilder.addSingle(
        tuplePos, universe_->getEntityName(constraintId), expr);
  }

  materialized_.withWLock([&](auto& wlockedMaterialized) {
    wlockedMaterialized.labeledObjectives =
        globalLabeledObjectivesBuilder.build(
            wlockedMaterialized.globalObjective);
    if (wlockedMaterialized.penaltyObjective.size() != 0) {
      wlockedMaterialized.labeledPenalties =
          globalLabeledPenaltiesBuilder.build(
              wlockedMaterialized.penaltyObjective);
    }
  });
}

void Materializer::collectFixedContainers() {
  // Given a set of fixed objects, compute a set of containers which break a
  // constraint if any objects are moved in or out with respect to the initial
  // assignment. This is determined by looking at the set of nonAcceting
  // containers and seeing if all the initial objects in it are fixed.
  auto& containers = universe_->getContainers();
  materialized_.withWLock([&](auto& wlockedMaterialized) {
    auto& fixedObjects = wlockedMaterialized.fixedObjects;
    auto& nonAcceptingContainers = wlockedMaterialized.nonAcceptingContainers;

    for (auto containerId : nonAcceptingContainers) {
      bool allObjectsFixed = true;
      auto& objectIds = containers.getInitialObjectIds(containerId);
      for (auto objectId : objectIds) {
        if (!fixedObjects.contains(objectId)) {
          allObjectsFixed = false;
          break;
        }
      }

      if (allObjectsFixed) {
        wlockedMaterialized.fixedContainers.insert(containerId);
      }
    }
  });
}

} // namespace facebook::rebalancer::materializer
