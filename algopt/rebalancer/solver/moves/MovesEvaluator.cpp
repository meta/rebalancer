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

#include "algopt/rebalancer/solver/moves/MovesEvaluator.h"

#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/solver/utils/ContainerPotential.h"

#include <fmt/format.h>
#include <folly/container/irange.h>

namespace facebook::rebalancer {

MovesEvaluator::DoNotWorsenGoalConfig::DoNotWorsenGoalConfig(
    const Problem& problem,
    int objTupleEnd,
    const std::string& stageName,
    const std::optional<algopt::common::thrift::HigherPriorityObjectivesConfig>&
        higherPriorityObjConfig)
    : goal(problem.getUniverse()), universe_(problem.getUniverse()) {
  if (objTupleEnd == 0) {
    return;
  }

  // doNotWorsen goal is the objective in the range [0, objTupleEnd)
  goal = problem.objective.getRange(0, objTupleEnd);
  if (higherPriorityObjConfig.has_value()) {
    for (auto& [tuplePos, allowedWorsening] :
         *higherPriorityObjConfig->tuplePosToAllowedWorsening()) {
      tuplePosToAllowedWorsenUntilValue[tuplePos] =
          algopt::common::thriftUtils::getAllowedWorsenUntilValue(
              goal.getValueAt(tuplePos), allowedWorsening);
    }
  }

  for (const auto i : folly::irange(goal.size())) {
    auto allowedWorsenUntilValuePtr = getAllowedWorsenUntilValue(i);
    auto doNotWorsenMsg = allowedWorsenUntilValuePtr
        ? fmt::format(
              "cannot become worse than {}", *allowedWorsenUntilValuePtr)
        : "cannot worsen";
    doNotWorsenLabels.push_back(
        std::make_shared<LabeledExpression>(
            fmt::format(
                "doNotWorsenGoal: Objective at position {} {} in {}",
                i,
                std::move(doNotWorsenMsg),
                stageName),
            goal.getObjectiveAt(i),
            1));
  }
}

bool MovesEvaluator::DoNotWorsenGoalConfig::worseningAllowed() const {
  return tuplePosToAllowedWorsenUntilValue.size() > 0;
}

const double* MovesEvaluator::DoNotWorsenGoalConfig::getAllowedWorsenUntilValue(
    int pos) const {
  return folly::get_ptr(tuplePosToAllowedWorsenUntilValue, pos);
}

std::optional<int> MovesEvaluator::DoNotWorsenGoalConfig::getFirstWorseTuplePos(
    Context& context,
    Orchestrator& orchestrator) const {
  if (goal.size() == 0) {
    return std::nullopt;
  }
  const auto currentValue = goal.getValue();
  for (const auto i : folly::irange((int)currentValue.size())) {
    const auto newValue = goal.evaluateObjectiveAt(i, context, orchestrator);
    if (exceedsAllowedWorsening(i, currentValue.get(i), newValue)) {
      return i;
    }
  }

  return std::nullopt;
}

bool MovesEvaluator::DoNotWorsenGoalConfig::hasWorsened(
    const GlobalObjectiveValue& oldValue,
    const GlobalObjectiveValue& newValue) const {
  for (const auto i : folly::irange((int)oldValue.size())) {
    if (exceedsAllowedWorsening(i, oldValue.get(i), newValue.get(i))) {
      return true;
    }
  }

  return false;
}

bool MovesEvaluator::DoNotWorsenGoalConfig::exceedsAllowedWorsening(
    int pos,
    double oldValue,
    double newValue) const {
  const auto& precision = universe_.getPrecision();
  if (!precision.isstrictlyGreater(newValue, oldValue)) {
    return false;
  }

  const auto* allowedWorsenUntilValue = getAllowedWorsenUntilValue(pos);
  // A goal without an allowance cannot worsen. Otherwise, it cannot exceed
  // its allowed value.
  return !allowedWorsenUntilValue ||
      precision.isstrictlyGreater(newValue, *allowedWorsenUntilValue);
}

MovesEvaluator::MovesEvaluator(
    Problem& problem,
    int objTupleBegin,
    int objTupleEnd,
    const std::string& stageName,
    const std::optional<algopt::common::thrift::HigherPriorityObjectivesConfig>&
        higherPriorityObjConfig)
    : problem_(problem),
      objTupleBegin_(objTupleBegin),
      objTupleEnd_(objTupleEnd),
      precision_(problem_.getUniverse().getPrecision()),
      doNotWorsenGoalConfig_(DoNotWorsenGoalConfig(
          problem_,
          objTupleBegin_,
          stageName,
          higherPriorityObjConfig)),
      minimizingGoal_(
          problem.objective,
          problem.penaltyObjective,
          objTupleBegin,
          objTupleEnd,
          problem.getUniverse()) {
  if (objTupleBegin_ < 0 || objTupleEnd_ > problem.objective.size()) {
    throw std::runtime_error(
        fmt::format(
            "Expected 0 <= objTupleBegin and objTupleEnd <= size of goal ({}), but got objTupleBegin = {}, objTupleEnd = {}",
            problem.objective.size(),
            objTupleBegin_,
            objTupleEnd_));
  }

  if (objTupleEnd_ < problem.objective.size()) {
    arbiterGoal_.emplace(
        problem.objective.getRange(objTupleEnd_, problem.objective.size()));
  }
}

bool MovesEvaluator::isPositive(
    const LabeledConstraints& labeledConstraints,
    Context& context) const {
  return std::any_of(
      labeledConstraints.begin(),
      labeledConstraints.end(),
      [this, &context](const auto& labeledConstraint) {
        return isPositive(labeledConstraint->expression.get(), context);
      });
}

bool MovesEvaluator::isPositive(const Expression* expression, Context& context)
    const {
  const auto exprValue =
      problem_.getOrchestrator().evaluate(expression, context);
  return precision_.isStrictlyGtZero(exprValue);
}

bool MovesEvaluator::violatesConstraints() const {
  const auto& constraints = problem_.getLabeledConstraints();
  return std::any_of(
      constraints.begin(), constraints.end(), [this](const auto& constraint) {
        return precision_.isStrictlyGtZero(constraint->expression->value);
      });
}

bool MovesEvaluator::satisfiesConstraints(const MoveSet& moves) const {
  checkMoves(moves);
  static thread_local Context context;
  context.clear();
  context.changes() = moves.getChangeSet();
  return !isPositive(problem_.getLabeledConstraints(), context);
}

bool MovesEvaluator::violatesAny(
    const std::vector<const Expression*>& constraints,
    const Move& move) const {
  if (constraints.empty()) {
    return false;
  }
  static thread_local Context context;
  context.clear();
  context.changes() = move.getChangeSet();
  return std::any_of(
      constraints.begin(), constraints.end(), [&](const Expression* expr) {
        return isPositive(expr, context);
      });
}

MoveResult MovesEvaluator::evaluate(MoveSet&& moves) const {
  checkMoves(moves);
  static thread_local Context context;
  context.clear();
  context.changes() = moves.getChangeSet();

  // Step 1: check constraint. If positive, the move is invalid.
  if (isPositive(problem_.getLabeledConstraints(), context)) {
    return MoveResult::makeInvalid(
        std::move(moves),
        getInvalidConstraints(
            context, /* getOnlyFirstInvalidConstraint=*/true));
  }

  // Step 2: check "do not make worse" goal. If worse, the move is invalid.
  if (auto worseTuplePos = doNotWorsenGoalConfig_.getFirstWorseTuplePos(
          context, problem_.getOrchestrator())) {
    return MoveResult::makeInvalid(
        std::move(moves),
        {{doNotWorsenGoalConfig_.doNotWorsenLabels.at(*worseTuplePos)}});
  }

  // Step 3: evaluate minimizing goal.
  auto currentValue = minimizingGoal_.getValue();
  auto newValue = minimizingGoal_.evaluate(context, problem_.getOrchestrator());
  const auto objIdxEvaluatedUpto =
      objTupleBegin_ + static_cast<int>(newValue.size());
  if (LocalSearchObjectiveValue::unsafeCompare(newValue, currentValue) < 0) {
    // The new value is better than the current. Evaluate the arbiter to
    // disambiguate.
    std::optional<GlobalObjectiveValue> arbiterValue;
    if (arbiterGoal_) {
      // NOTE: It is important to evaluate all tuples when evaluating the
      // arbiterGoal. For example, consider the case when the minimizingGoal is
      // the one at tuple position 0, and rest is the arbiterGoal. Also, assume
      // the current value of entire objective tuple is (10, 0, 10). Now, if we
      // have an improvement w.r.t. the minimizingGoal, we need to know the
      // values for all tuple positions in arbiterGoal because it might be
      // useful in distinguishing between two moves, one that results in
      // objective value (5, 1, 7) and another with objective value (5, 1, 4).
      // For this case, if we do not evaluate all tuples, then the optimization
      // in evaluate() function will stop evaluating after tuple position 1
      // (since at that position, the change is worse than the current state)
      // and incorrectly conclude that both moves are equal, when they are not.
      arbiterValue = arbiterGoal_->evaluate(
          context,
          problem_.getOrchestrator(),
          true /*evaluateAllTuplePositions*/);
    }

    auto [objectiveDeltas, objectiveDeltasTupleBegin] =
        getObjectiveDeltas(context, objIdxEvaluatedUpto);
    return MoveResult::makeValid(
        std::move(moves),
        std::move(currentValue),
        std::move(newValue),
        std::move(arbiterValue),
        std::move(objectiveDeltas),
        objTupleBegin_ - objectiveDeltasTupleBegin);
  }

  auto [objectiveDeltas, objectiveDeltasTupleBegin] =
      getObjectiveDeltas(context, objIdxEvaluatedUpto);
  return MoveResult::makeValid(
      std::move(moves),
      std::move(currentValue),
      std::move(newValue),
      std::nullopt,
      std::move(objectiveDeltas),
      objTupleBegin_ - objectiveDeltasTupleBegin);
}

ApplyStatus MovesEvaluator::tryApply(const MoveResult& moveResult) const {
  const auto changes = moveResult.getMoveSet().getChangeSet();
  if (!problem_.configs.validateAppliedMoves) {
    problem_.apply(changes);
    return ApplyStatus::APPLIED;
  }

  const auto oldDoNotWorsenGoalValue = doNotWorsenGoalConfig_.goal.getValue();
  return problem_.tryApply(changes, [&] {
    if (violatesConstraints()) {
      return ApplyStatus::VIOLATES_CONSTRAINT;
    }
    if (doNotWorsenGoalConfig_.hasWorsened(
            oldDoNotWorsenGoalValue, doNotWorsenGoalConfig_.goal.getValue())) {
      return ApplyStatus::WORSENS_HIGHER_PRIORITY_OBJECTIVE;
    }
    if (!LocalSearchObjectiveValue::isStrictlyBetter(
            minimizingGoal_.getValue(), moveResult.getOldValue(), precision_)) {
      return ApplyStatus::DOES_NOT_IMPROVE_OBJECTIVE;
    }
    return ApplyStatus::APPLIED;
  });
}

Problem& MovesEvaluator::getProblem() const {
  return problem_;
}

void MovesEvaluator::checkMoves(const MoveSet& moves) const {
  for (auto& move : moves) {
    if (problem_.assignment.getContainer(move.getObject()) !=
        move.getSourceContainer()) {
      throw std::runtime_error(
          fmt::format(
              "Attempting to move object {} out of {} but its currently in {}",
              move.getObject(),
              move.getSourceContainer(),
              problem_.assignment.getContainer(move.getObject())));
    }
  }
}

bool MovesEvaluator::shouldCollectMoveStats() const {
  return problem_.moveStatsAggregatorConfig->trackContainers ||
      problem_.moveStatsAggregatorConfig->trackObjects;
}

MovesEvaluator::ObjectiveDeltasResult MovesEvaluator::getObjectiveDeltas(
    Context& context,
    int objIdxEvaluatedUpto) const {
  if (!shouldCollectMoveStats()) {
    return {.deltas = std::nullopt, .tupleBegin = objTupleBegin_};
  }

  int objIdxToEvalBegin = objTupleBegin_;
  int objIdxToEvalEnd = objIdxEvaluatedUpto;
  if (problem_.moveStatsAggregatorConfig
          ->showAllChangedObjectivesInMovesSummary) {
    objIdxToEvalBegin = 0;
    objIdxToEvalEnd = problem_.objective.size();
  }

  auto& orchestrator = problem_.getOrchestrator();
  auto& labeledObjectives = problem_.getLabeledObjectives();
  const auto& labeledPenalties = problem_.getLabeledPenalties();
  ObjectiveDeltaSets objectiveDeltas(objIdxToEvalEnd - objIdxToEvalBegin);
  for (const auto idx : folly::irange(objIdxToEvalBegin, objIdxToEvalEnd)) {
    auto& labeledObjectiveExprs = labeledObjectives.getObjectiveAt(idx);
    auto& myObjectiveDeltas = objectiveDeltas.at(idx - objIdxToEvalBegin);
    const auto* labeledPenaltyExprs =
        idx < static_cast<int>(labeledPenalties.size())
        ? &labeledPenalties.getObjectiveAt(idx)
        : nullptr;
    myObjectiveDeltas.reserve(
        labeledObjectiveExprs.size() +
        (labeledPenaltyExprs ? labeledPenaltyExprs->size() : 0));
    for (auto& labeledObjectiveExpr : labeledObjectiveExprs) {
      const auto& expression = labeledObjectiveExpr->expression;
      const double value = orchestrator.evaluate(expression.get(), context);

      myObjectiveDeltas.emplace_back(
          labeledObjectiveExpr, expression->value, value);
    }
    if (labeledPenaltyExprs != nullptr) {
      for (const auto& labeledPenaltyExpr : *labeledPenaltyExprs) {
        const auto& expression = labeledPenaltyExpr->expression;
        const auto value = orchestrator.evaluate(expression.get(), context);
        myObjectiveDeltas.emplace_back(
            labeledPenaltyExpr,
            expression->value,
            value,
            /*isPenalty=*/true);
      }
    }
  }

  return {
      .deltas = std::move(objectiveDeltas), .tupleBegin = objIdxToEvalBegin};
}

std::optional<LabeledExpressionSet> MovesEvaluator::getInvalidConstraints(
    Context& context,
    bool getOnlyFirstInvalidConstraint) const {
  if (!shouldCollectMoveStats()) {
    return std::nullopt;
  }

  LabeledExpressionSet invalidConstraints;
  for (auto& labeledConstraint : problem_.getLabeledConstraints()) {
    const auto& expression = labeledConstraint->expression;
    if (isPositive(expression.get(), context)) {
      invalidConstraints.push_back(labeledConstraint);
      if (getOnlyFirstInvalidConstraint) {
        break;
      }
    }
  }
  return invalidConstraints;
}

const PackerSet<entities::ContainerId>& MovesEvaluator::getContainers() const {
  return problem_.containers;
}

const ObjectStore& MovesEvaluator::getDynamicObjects(
    entities::ContainerId containerId) const {
  return problem_.assignment.getDynamicObjects(containerId);
}

const entities::ContainerId MovesEvaluator::getContainer(
    entities::ObjectId objectId) const {
  return problem_.assignment.getContainer(objectId);
}

PackerMap<entities::ContainerId, ContainerPotential>
MovesEvaluator::computeContainerPotentials() const {
  const auto& minimizingGoal = minimizingGoal_;
  auto& problem = problem_;
  const auto& precision = problem.getUniverse().getPrecision();

  PackerMap<entities::ContainerId, ContainerPotential> containerPotentials;
  auto& containers = this->getContainers();
  folly::coro::blockingWait(
      CoroUtils::runEachFuncAndUpdate(
          containers.begin(),
          containers.end(),
          [&](auto it) {
            auto containerId = *it;
            ChangeSet removeAllObjectsFromContainer;
            const auto& objectsInContainer =
                problem.assignment.getObjects(containerId);

            for (auto objectId : objectsInContainer) {
              removeAllObjectsFromContainer.insert(
                  Change(objectId, containerId, -1));
            }

            Context context;
            context.changes() = std::move(removeAllObjectsFromContainer);
            const auto& goalValIfAllObjectsRemovedFromContainer =
                minimizingGoal.evaluate(
                    context,
                    problem.getOrchestrator(),
                    /*evaluateAllTuples=*/true);

            const auto containerContributionToGoal =
                LocalSearchObjectiveValue::subtract(
                    minimizingGoal.getValue(),
                    goalValIfAllObjectsRemovedFromContainer,
                    precision);

            ContainerPotential containerPotential(
                containerContributionToGoal,
                objectsInContainer.size(),
                precision);

            return containerPotential;
          },
          // update containerPotentials
          [&](const auto& containerPotential, auto it) {
            containerPotentials.emplace(*it, containerPotential);
          },
          problem_.getExecutor()));

  return containerPotentials;
}

// Note on correctness of containers' contributionToGoal after update: When
// updating the containers' contributionToGoal, we only update the
// contributionToGoal of the source and destination container involved in a
// move. However, it is possible that a move might affect the contributions of
// containers other than the source and the destination. We do not update the
// contributions of such containers and so their contributionToGoal values may
// be incorrect. We can partially get around this by periodically recomputing
// all the containers' contributionToGoal (for instance, when calculating
// contributions is required in LocalSearch, this done after every cycle).
PackerMap<entities::ContainerId, ContainerPotential>
MovesEvaluator::updateContainerPotentialsAfterMove(
    const MoveResult& appliedMoveResult) const {
  const int nMoves = appliedMoveResult.getMoveSet().size();
  if (nMoves == 0) {
    return {};
  }
  // for now we throw an error if there an attempt to compute the changes in
  // containerPotential resulting from performing more than one move
  if (nMoves > 1) {
    throw std::runtime_error(
        "updateContainerPotentials is only currently implemented when one move is performed. Use computeContainerPotentials() to recompute all the potentials.");
  }

  const auto& newGoalVal = appliedMoveResult.getValue();
  const auto& previousGoalVal = appliedMoveResult.getOldValue();
  const auto& minimizingGoal = minimizingGoal_;

  // Note: We are updating the containers' contributionToGoal after the move has
  // been applied. Therefore, the value of 'minimizingGoal' will be the
  // 'newGoalVal' in 'appliedMoveResult'

  PackerMap<entities::ContainerId, ContainerPotential>
      changesInContainerPotentials;
  auto& problem = problem_;
  const auto& precision = problem.getUniverse().getPrecision();

  for (const auto& move : appliedMoveResult.getMoveSet()) {
    auto sourceContainer = move.getSourceContainer();
    auto destinationContainer = move.getDestinationContainer();
    auto object = move.getObject();

    // The change in contributionToGoal of the 'destinationContainer' is the
    // difference between 'newGoalValue' and the value of the assignment where
    // 'object' is not part of 'destinationContainer' (which in turn removes it
    // from the problem). Similarly, the change in 'contributionToGoal' of the
    // 'sourceContainer' is the difference between the value of the assignment
    // where 'object' is removed and 'previousGoalValue'
    Context context;
    context.changes() = ChangeSet({Change(object, destinationContainer, -1)});
    const auto& goalValIfObjectRemoved = minimizingGoal.evaluate(
        context,
        problem.getOrchestrator(),
        /*evaluateAllTuples=*/true);

    auto contributionChangeForDestination = LocalSearchObjectiveValue::subtract(
        newGoalVal, goalValIfObjectRemoved, precision);
    auto contributionChangeForSource = LocalSearchObjectiveValue::subtract(
        goalValIfObjectRemoved, previousGoalVal, precision);

    // Note that we are only considering one move here. So the change w.r.t.
    // sourceContainer is that it lost an object because of the move; the
    // destinationContainer gained one.
    changesInContainerPotentials.emplace(
        sourceContainer,
        ContainerPotential(contributionChangeForSource, -1, precision));
    changesInContainerPotentials.emplace(
        destinationContainer,
        ContainerPotential(contributionChangeForDestination, 1, precision));
  }
  return changesInContainerPotentials;
}

} // namespace facebook::rebalancer
