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

#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/PropertiesHelper.h"
#include "algopt/rebalancer/solver/expressions/Step.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/iterators/StlWrapper.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/Enumerate.h>
#include <folly/CPortability.h>

#include <sstream>

using namespace std;

namespace {
const facebook::algopt::SumMap<facebook::rebalancer::entities::ObjectId, double>
    kEmptyObjectWeights = {};
} // namespace

namespace facebook::rebalancer {

namespace {
std::string_view penaltyTransformName(ObjectPartitionLookupPenaltyTransform t) {
  switch (t) {
    case ObjectPartitionLookupPenaltyTransform::IDENTITY:
      return "identity";
    case ObjectPartitionLookupPenaltyTransform::SQUARE:
      return "square";
    case ObjectPartitionLookupPenaltyTransform::STEP:
      return "step";
  }
  throw std::runtime_error("ObjectPartitionLookup: unknown penalty transform");
}
} // namespace

template <typename Policy>
ObjectPartitionLookup<Policy>::ObjectPartitionLookup(
    std::shared_ptr<Expression> objectPartition,
    std::shared_ptr<const PackerSet<entities::ContainerId>> lookupContainersPtr,
    entities::ScopeId scopeId,
    entities::ScopeItemId scopeItemId,
    const entities::Universe& universe,
    const Assignment& initialAssignment,
    PackerMap<entities::GroupId, double> groupLimitOverrides,
    PackerSet<entities::ObjectId> initialDuringObjects,
    std::optional<double> defaultGroupLimitOverride,
    ObjectPartitionLookupPenaltyTransform penaltyTransform,
    int groupsAllowed,
    Bound bound,
    std::conditional_t<
        std::is_same_v<Policy, ObjectPartitionLookupWithMinPresencePolicy>,
        typename ObjectPartitionLookupWithMinPresencePolicy::Data,
        std::monostate> data)
    : Expression(universe),
      objectPartition_((ObjectPartition*)objectPartition.get()),
      lookupContainersPtr_(lookupContainersPtr),
      groupLimitOverrides_(std::move(groupLimitOverrides)),
      defaultGroupLimitOverride_(defaultGroupLimitOverride),
      initialDuringObjects_(std::move(initialDuringObjects)),
      penaltyTransform_(penaltyTransform),
      groupsAllowed_(groupsAllowed),
      bound_(bound),
      scopeId_(scopeId),
      scopeItemId_(scopeItemId),
      dimension_(universe_->getObjects().getDimension(getDimensionId()).only()),
      scopeMatchesDimensionScope_(
          !dimension_.isDynamic() || dimension_.getScopeId() == scopeId_),
      dimensionScope_(
          scopeMatchesDimensionScope_
              ? universe_->getScope(scopeId_)
              : universe_->getScope(dimension_.getScopeId())),
      groupPenalties_(groupsAllowed != 0),
      data_(std::move(data)) {
  if (lookupContainersPtr_ == nullptr) {
    throw std::runtime_error(
        "'lookupContainersPtr' is expected to be non-null");
  }

  if (groupsAllowed_ < 0) {
    throw std::runtime_error(
        "ObjectPartitionLookup: groupsAllowed_ is expected to be non-negative");
  }

  if (!scopeMatchesDimensionScope_ && initialDuringObjects_.size() > 0) {
    // ObjectPartitionLookup does not currently support initialDuringObjects
    // when using a dynamic dimension with a scope differing from the main scope
    throw std::runtime_error(
        "ObjectPartitionLookup: initialDuringObjects_ is expected to be empty when dynamic dimension's scope differs from the main scope");
  }

  add_child(objectPartition);
  set_directly_affected_containers();

  for (auto& objectId : initialDuringObjects_) {
    auto objWeight = objectPartition_->getObjectWeight(objectId, scopeItemId_);
    for (auto groupId : objectPartition_->getObjectGroups(objectId)) {
      auto& [duringPositiveWeight, duringNegativeWeight] =
          groupToDuringObjectsTotalPositiveAndNegativeWeights_[groupId];
      duringPositiveWeight += (objWeight > 0) ? objWeight : 0.0;
      duringNegativeWeight += (objWeight < 0) ? objWeight : 0.0;
    }
  }

  setInitialValue(computeFromAssignment(initialAssignment));
}

template <typename Policy>
const std::string_view& ObjectPartitionLookup<Policy>::getType() const {
  return Policy::typeName;
}

template <typename Policy>
std::optional<AffectedByChange>
ObjectPartitionLookup<Policy>::isAffectedByChange(
    const AffectedByChangeDecisionData& /*data*/) const {
  return AffectedByChange(directlyAffectedContainers.getSetPtr());
}

template <typename Policy>
void ObjectPartitionLookup<Policy>::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets) const {
  PackerMap<entities::ObjectId, int> during;
  for (auto objectId : initialDuringObjects_) {
    during[objectId] = 1;
  }
  equivalenceSets.mappingMerge(during);
}

template <typename Policy>
std::optional<entities::ScopeItemId>
ObjectPartitionLookup<Policy>::getDimensionScopeItemId(
    entities::ContainerId container) const {
  return scopeMatchesDimensionScope_
      ? scopeItemId_
      : dimensionScope_.getScopeItemId(container);
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::innerFullApply(
    const TopToBottomEvaluator& /* evaluator */,
    const Assignment& assignment) {
  return computeFromAssignment(assignment);
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::computeFromAssignment(
    const Assignment& assignment) {
  // Step 1: list contributing objects
  contributingObjectIds_ = initialDuringObjects_;
  for (auto containerId : *lookupContainersPtr_) {
    auto& objectIds = assignment.getObjects(containerId);
    for (auto objectId : objectIds) {
      contributingObjectIds_.insert(objectId);
    }
  }

  // Step 2: set group object weights
  groupObjectCounts_.clear();
  groupObjectWeights_.clear();
  objectToAssignmentDimensionScopeItem_.clear();

  // Give an empty entry to every group whose penalty is non-zero even when none
  // of its objects are in the lookup containers
  const auto initialize = [&](const auto& groups) {
    for (const auto& [groupId, _] : groups) {
      groupObjectCounts_[groupId] = {};
      groupObjectWeights_[groupId] = {};
    }
  };

  // MAX: groups with a negative limit; MIN: groups with a positive limit.
  initialize(
      bound_ == Bound::MAX ? objectPartition_->getGroupNegativeLimits()
                           : objectPartition_->getGroupPositiveLimits());

  // When using MinPresencePolicy, MIN bound and makeContinuousPenaltyTerm=true,
  // a group's penalty is its upper bound minus its util. So, every group must
  // be initialized.
  if constexpr (std::is_same_v<
                    Policy,
                    ObjectPartitionLookupWithMinPresencePolicy>) {
    if (getData().makeContinuousPenaltyTerm && bound_ == Bound::MIN) {
      initialize(objectPartition_->getGroupToTotalPositiveAndNegativeWeights());
    }
  }

  // if bound is MAX: initialize any groups with negative limit overrides which
  // may not be known to the object partition (e.g. when all objects in a group
  // are fixed)
  // if bound is MIN: initialize any groups with positive limit overrides which
  // may not be known to the object partition
  for (const auto& [groupId, limit] : groupLimitOverrides_) {
    if ((limit < 0 && bound_ == Bound::MAX) ||
        (limit > 0 && bound_ == Bound::MIN)) {
      groupObjectCounts_[groupId] = {};
      groupObjectWeights_[groupId] = {};
    }
  }

  for (auto objectId : contributingObjectIds_) {
    auto& groupIds = objectPartition_->getObjectGroups(objectId);
    std::optional<entities::ScopeItemId> dimensionScopeItemId;
    if (assignment.hasObject(objectId)) {
      dimensionScopeItemId =
          getDimensionScopeItemId(assignment.getContainer(objectId));

      if (dimensionScopeItemId.has_value()) {
        objectToAssignmentDimensionScopeItem_.emplace(
            objectId, dimensionScopeItemId.value());
      }
    }
    const double objectWeight =
        objectPartition_->getObjectWeight(objectId, dimensionScopeItemId);
    for (auto groupId : groupIds) {
      const int objectCount = ++groupObjectCounts_[groupId][objectId];
      groupObjectWeights_[groupId].update(objectId, objectWeight * objectCount);
    }
  }

  // Step 3: set group penalties
  groupPenalties_.clear();
  for (const auto& [groupId, objectWeights] : groupObjectWeights_) {
    const double penalty = getGroupPenalty(objectWeights.query(), groupId);
    groupPenalties_.assign(groupId, penalty);
  }

  // Step 4: sum group penalties
  value = groupPenalties_.sum_all();
  if (groupsAllowed_ > 0) {
    value -= groupPenalties_.sum_top(groupsAllowed_);
  }
  return value;
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::innerPartialApply(
    const BottomToTopEvaluator& /* evaluator */,
    const Assignment& /* assignment */,
    const ChangeSet& changes) {
  // Step 1: update group counts. Process all removals (-1) before all
  // additions (+1).
  //
  // groupObjectWeights_[g][o] is overwritten on each touch with
  // weight_at_change_container * count. When an object's dimension value
  // differs between its source and destination container (which only
  // happens for cross-scope dynamic dimensions, i.e.
  // scopeMatchesDimensionScope_ == false), processing the addition first
  // would leave count > 0 when the removal runs, so the stored value ends
  // up as source_weight * 1 even though the object is now at the
  // destination. Doing removals first drives count to 0 before the
  // addition writes the destination's weight.
  PackerSet<entities::GroupId> groupsChanged;
  for (const bool removalsPhase : {true, false}) {
    for (const auto& [containerId, containerChanges] :
         changes.getContainerToChanges()) {
      if (!lookupContainersPtr_->contains(containerId)) {
        continue;
      }
      for (const auto& change : containerChanges) {
        const bool isRemoval = change.getValue() < 0;
        if (isRemoval != removalsPhase) {
          continue;
        }
        const auto objectId = change.getObject();
        if (initialDuringObjects_.contains(objectId)) {
          continue;
        }

        const auto& groupIds = objectPartition_->getObjectGroups(objectId);
        const auto dimensionScopeItemId = getDimensionScopeItemId(containerId);
        const double objectWeight =
            objectPartition_->getObjectWeight(objectId, dimensionScopeItemId);
        for (const auto groupId : groupIds) {
          auto& objectCount = groupObjectCounts_[groupId][objectId];
          objectCount += change.getValue();
          groupObjectWeights_[groupId].update(
              objectId, objectWeight * objectCount);
          groupsChanged.insert(groupId);
        }
        if (isRemoval) {
          contributingObjectIds_.erase(objectId);
          objectToAssignmentDimensionScopeItem_.erase(objectId);
        } else {
          contributingObjectIds_.insert(objectId);
          if (dimensionScopeItemId.has_value()) {
            objectToAssignmentDimensionScopeItem_.emplace(
                objectId, dimensionScopeItemId.value());
          }
        }
      }
    }
  }
  // Step 2: update group penalties
  for (const auto groupId : groupsChanged) {
    groupPenalties_.assign(
        groupId,
        getGroupPenalty(groupObjectWeights_.at(groupId).query(), groupId));
  }

  // Step 3: sum group penalties
  value = groupPenalties_.sum_all();
  if (groupsAllowed_ > 0) {
    value -= groupPenalties_.sum_top(groupsAllowed_);
  }
  return value;
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::evaluate(
    const BottomToTopEvaluator& /* evaluator */,
    const ChangeSet& changes) const {
  // Step 1: compute delta of group counts
  static thread_local PackerMap<entities::GroupId, double> groupWeightDeltas;
  groupWeightDeltas.clear();
  for (auto& container : changes.getContainersInChangeSet()) {
    if (!lookupContainersPtr_->contains(container)) {
      continue;
    }
    for (auto& change : changes.getChangesByContainer(container)) {
      auto objectId = change.getObject();
      if (initialDuringObjects_.contains(objectId)) {
        continue;
      }

      auto& groupIds = objectPartition_->getObjectGroups(objectId);
      const double objectWeight = objectPartition_->getObjectWeight(
          objectId, getDimensionScopeItemId(container));
      if (objectWeight == 0) {
        continue;
      }
      for (auto groupId : groupIds) {
        groupWeightDeltas[groupId] += objectWeight * change.getValue();
      }
    }
  }

  // Step 2: compute new total penalty
  double totalPenalty = groupPenalties_.sum_all();
  for (const auto& [groupId, weightDelta] : groupWeightDeltas) {
    const auto& objectWeights = folly::get_ref_default(
        groupObjectWeights_, groupId, kEmptyObjectWeights);
    const double oldWeight = objectWeights.query();
    const double newWeight = oldWeight + weightDelta;
    // getGroupPenalty() is O(1) arithmetic for the default policy but expensive
    // for MinPresence (transformWeight + LimitWrapper lookups). Recompute it
    // for the default policy---far cheaper than the O(log n) SumMap lookup
    // when the expression spans many groups---and read the cached value for
    // MinPresence.
    constexpr auto kIsMinPresence =
        std::is_same_v<Policy, ObjectPartitionLookupWithMinPresencePolicy>;
    const auto oldPenalty = kIsMinPresence
        ? groupPenalties_.get(groupId)
        : getGroupPenalty(oldWeight, groupId);
    const double newPenalty = getGroupPenalty(newWeight, groupId);
    const double penaltyDelta = newPenalty - oldPenalty;
    totalPenalty += penaltyDelta;
  }

  // Shortcut: if no groups allowed, we are done
  if (groupsAllowed_ == 0) {
    return totalPenalty;
  }

  // Step 3: list candidates to be a top penalty from the unchanged groups
  static thread_local std::vector<double> candidates;
  candidates.clear();
  for (const auto& [group, penalty] : groupPenalties_) {
    if (groupWeightDeltas.contains(group)) {
      continue; // skip changed groups
    }
    candidates.push_back(penalty);
    if (static_cast<int>(candidates.size()) >= groupsAllowed_) {
      break;
    }
  }

  // Step 4: list all changed groups as candidates to be a top penalty
  for (const auto& [groupId, weightDelta] : groupWeightDeltas) {
    const auto& objectWeights = folly::get_ref_default(
        groupObjectWeights_, groupId, kEmptyObjectWeights);
    const double oldWeight = objectWeights.query();
    const double newWeight = oldWeight + weightDelta;
    const double newPenalty = getGroupPenalty(newWeight, groupId);
    candidates.push_back(newPenalty);
  }

  // Step 5: sort candidates and subtract largest groupsAllowed_ from the
  // total
  sort(candidates.begin(), candidates.end(), std::greater<double>());
  for (int i = 0; i < static_cast<int>(candidates.size()) && i < groupsAllowed_;
       ++i) {
    totalPenalty -= candidates.at(i);
  }
  return totalPenalty;
}

template <typename Policy>
Bounds ObjectPartitionLookup<Policy>::getBounds(
    const BoundConstraints& bc) const {
  auto rLockedUconstrainedBounds = unconstrainedBounds.rlock();
  if (bc.isEmpty() || !rLockedUconstrainedBounds->has_value()) {
    throw std::runtime_error(
        "Expected to be called only when boundConstraints is given and when unconstrained bounds has already been computed");
  }

  auto bounds = rLockedUconstrainedBounds->value();
  auto& directlyAffectedSet = directlyAffectedContainers.getNonNullSet();

  // if bc is set, then return the current value as lowerBound if
  // a) the bound_ is MAX and the none of the containers are "giving"
  // b) the bound_ is MIN and the none of the containers are "taking"
  if ((bound_ == Bound::MAX && !bc.anyGiving(directlyAffectedSet)) ||
      (bound_ == Bound::MIN && !bc.anyTaking(directlyAffectedSet))) {
    bounds.lower_bound = value;
  }

  // if bc is set, then return the current value as upperBound if
  // a) the bound_ is MAX and the none of the containers are "taking"
  // b) the bound_ is MIN and the none of the containers are "giving"
  if ((bound_ == Bound::MAX && !bc.anyTaking(directlyAffectedSet)) ||
      (bound_ == Bound::MIN && !bc.anyGiving(directlyAffectedSet))) {
    bounds.upper_bound = value;
  }

  return bounds;
}

template <typename Policy>
std::pair<double, double>
ObjectPartitionLookup<Policy>::getMaxAndMinNormalizedDeviationsFromLimit(
    entities::GroupId groupId) const {
  auto [totalPositiveWeight, totalNegativeWeight] =
      objectPartition_->getTotalPositiveAndNegativeWeightsForGroup(groupId);

  auto [duringPositiveWeight, duringNegativeWeight] = folly::get_default(
      groupToDuringObjectsTotalPositiveAndNegativeWeights_,
      groupId,
      std::make_pair(0.0, 0.0));

  const double limit = getGroupLimit(groupId);
  const double coeff = objectPartition_->getGroupCoefficient(groupId);

  auto minNormalizedDeviation =
      (totalNegativeWeight + duringPositiveWeight - limit) * coeff;
  auto maxNormalizedDeviation =
      (totalPositiveWeight + duringNegativeWeight - limit) * coeff;

  return std::make_pair(maxNormalizedDeviation, minNormalizedDeviation);
}

template <typename Policy>
Bounds ObjectPartitionLookup<Policy>::getBounds() const {
  /*
  lowerBound computation:
    -- if bound is MAX: the minimum possible penalty of a group is obtained
  when all objects with negative weights in that group and
  initialDuringObjects with positive weights in that group are present (note
  that initialDuringObjects are always present).

    -- if bound is MIN: the minimum possible penalty is obtained when all
  objects with positive weights in that group and initialDuringObjects with
  negative weights in that group are present.


  upperBound computation:
    -- if bound is MAX: the maximum possible penalty of a group is obtained
  when all objects with positive weights in that group and
  initialDuringObjects with negative weights in that group are present (note
  that initialDuringObjects are always present).

    -- if bound is MIN: the maximum penalty of a group is obtained when all
  objects with negative weights in that group  and initialDuringObjects with
  positive weights in that group are present.

  NOTE on overall computation: ObjectPartition already computes the maximum
  and minimum possible deviations from the limit for every group. However, the
  limits used there could be overridden here, or a group might have
  initialDuringObjects associated with it (which is unknown to the
  ObjectPartition). Therefore, only for such groups, we recompute the
  deviations here. This optimization is useful since for many problems, there
  are very few groups that need recomputation (in the most common uses of this
  expression, there are no overrides and no initialDuringObjects) and several
  ObjectPartitionLookups can share the same ObjectPartition; recomputing the
  deviations here for all groups can be very expensive since it requires
  several hash map lookups per group.
  */
  Bounds bounds{.lower_bound = 0.0, .upper_bound = 0.0};
  auto& groupToMaxAndMinDeviations =
      objectPartition_->getGroupToMaxAndMinNormalizedDeviationsFromLimit();

  for (auto& [groupId, maxAndMinDeviations] : groupToMaxAndMinDeviations) {
    const bool shouldRecomputeDeviations =
        (groupToDuringObjectsTotalPositiveAndNegativeWeights_.contains(
             groupId) ||
         groupLimitOverrides_.contains(groupId) ||
         defaultGroupLimitOverride_.has_value());

    auto [maxDeviation, minDeviation] = shouldRecomputeDeviations
        ? getMaxAndMinNormalizedDeviationsFromLimit(groupId)
        : maxAndMinDeviations;

    if (bound_ == Bound::MAX) {
      bounds.lower_bound += computePenalty(minDeviation);
      bounds.upper_bound += computePenalty(maxDeviation);
    } else {
      bounds.lower_bound += computePenalty(maxDeviation);
      bounds.upper_bound += computePenalty(minDeviation);
    }
  }

  for (const auto& [groupId, _] : groupLimitOverrides_) {
    if (!groupToMaxAndMinDeviations.contains(groupId)) {
      bounds.lower_bound += getGroupPenalty(0, groupId);
      bounds.upper_bound += getGroupPenalty(0, groupId);
    }
  }

  return bounds;
}

template <typename Policy>
Bounds ObjectPartitionLookup<Policy>::innerLowerAndUpperBounds(
    Context& /*context*/,
    const BoundConstraints& bc) const {
  return bc.isEmpty() ? getBounds() : getBounds(bc);
}

template <typename Policy>
algopt::lp::Expression ObjectPartitionLookup<Policy>::lp(
    const LpEvaluator& evaluator,
    bool minimizing,
    const interface::OptimalSolverSpec& /* configs */) {
  if (penaltyTransform_ != ObjectPartitionLookupPenaltyTransform::IDENTITY &&
      penaltyTransform_ != ObjectPartitionLookupPenaltyTransform::STEP) {
    throw std::runtime_error(
        "ObjectPartitionLookup: only IDENTITY and STEP penalty transforms are supported in LP");
  }
  if (groupsAllowed_ > 0 && !minimizing) {
    throw std::runtime_error(
        "ObjectPartitionLookup: groupsAllowed_ > 0 when minimizing=false is not supported in LP");
  }

  auto& problem = evaluator.getProblem();
  bool has_dynamic_container = false;
  for (auto container : *lookupContainersPtr_) {
    if (evaluator.getDynamicContainers().contains(container)) {
      has_dynamic_container = true;
    }
  }
  if (!has_dynamic_container) {
    return evaluator.makeLpExpression(value);
  }

  auto expr = evaluator.makeLpExpression();
  auto limitNumViolators = evaluator.makeLpExpression();

  // initialize groups with values without objects (i.e. only fixed objects)
  PackerSet<entities::GroupId> groupIds;
  for (const auto& [group, limit] : groupLimitOverrides_) {
    if (group.asInt() != -1 && limit < 0) {
      groupIds.insert(group);
    }
  }
  const auto& equivSetGroups =
      objectPartition_->getEquivSetGroups(problem.getEquivalenceSets());
  for (const auto& [group, _] : equivSetGroups) {
    groupIds.insert(group);
  }

  const bool addGroupConstraint = groupsAllowed_ > 0;

  auto addAssignmentToSum = [&evaluator](
                                double weight,
                                const auto& container,
                                const auto& equiv_set_param,
                                int objectCopyCountInGroup_param,
                                auto& sum,
                                double& rawSum) {
    if (auto fixedValue = evaluator.getMaybeFixedAssignmentValue(
            equiv_set_param, container)) {
      rawSum += weight * objectCopyCountInGroup_param * fixedValue.value();
    } else {
      sum += weight * objectCopyCountInGroup_param *
          evaluator.getAssignmentVar(equiv_set_param, container);
    }
  };

  LpConstraintBatch batch(evaluator);

  for (auto groupId : groupIds) {
    double sum_ub = 1.0;

    // value is:
    // max(0, sum - limit) if bound is MAX
    // max(0, limit - sum) if bound is MIN
    auto penalty = lp_cont_var(evaluator, "penalty");
    batch.add(REBALANCER_CTR_REQUEST(penalty >= 0));

    auto sum = evaluator.makeLpExpression(0);
    double rawSum = 0;
    if (auto equiv_sets = folly::get_ptr(equivSetGroups, groupId)) {
      // Note that an object can belong to a group multiple times, so
      // objectCopyCountInGroup represents how many times each object of the
      // equivalence set is in this particular group
      for (const auto& [equiv_set, objectCopyCountInGroup] : *equiv_sets) {
        const size_t numObjectsInEquivSet =
            problem.getEquivalenceSets().getSet(equiv_set).size();
        auto objectId = *problem.getEquivalenceSets().getSet(equiv_set).begin();

        if (!scopeMatchesDimensionScope_) {
          // When scope doesn't match, compute object weight per container
          // since it depends on the container's dimension scope item
          double maxObjectWeight = 0.0;
          for (const auto& container : *lookupContainersPtr_) {
            const double objectWeight = objectPartition_->getObjectWeight(
                objectId, dimensionScope_.getScopeItemId(container));
            maxObjectWeight = std::max(maxObjectWeight, objectWeight);
            addAssignmentToSum(
                objectWeight,
                container,
                equiv_set,
                objectCopyCountInGroup,
                sum,
                rawSum);
          }
          sum_ub +=
              numObjectsInEquivSet * objectCopyCountInGroup * maxObjectWeight;
        } else {
          const double objectWeight =
              objectPartition_->getObjectWeight(objectId, scopeItemId_);

          // Upper bound is if all objects of equiv set are assigned
          sum_ub +=
              numObjectsInEquivSet * objectCopyCountInGroup * objectWeight;
          if (initialDuringObjects_.contains(objectId)) {
            // Case of initial during, just add fixed amount, don't generate
            // expression to add
            rawSum +=
                numObjectsInEquivSet * objectCopyCountInGroup * objectWeight;
          } else {
            // Add expression representing how many objects of equiv set
            // are assigned to containers
            for (const auto& container : *lookupContainersPtr_) {
              addAssignmentToSum(
                  objectWeight,
                  container,
                  equiv_set,
                  objectCopyCountInGroup,
                  sum,
                  rawSum);
            }
          }
        }
      }
    }
    sum += rawSum;

    auto limit = getGroupLimit(groupId);
    if (bound_ == Bound::MAX) {
      sum -= limit;
      sum_ub += std::max(0.0, -limit);
    } else {
      sum = limit - sum;
      sum_ub += std::max(0.0, limit);
    }

    // penalty has to be greater than sum
    // penalty + w*bigM >= sum
    if (addGroupConstraint) {
      auto w = lp_bool_var(evaluator);
      limitNumViolators += w;
      // set w=1 if violation is to be ignored
      batch.add(REBALANCER_CTR_REQUEST(penalty + sum_ub * w >= sum));
    } else {
      batch.add(REBALANCER_CTR_REQUEST(penalty >= sum));
    }

    if (!minimizing) {
      auto z = lp_bool_var(evaluator);
      // z == 0 implies penalty <= 0
      batch.add(REBALANCER_CTR_REQUEST(penalty <= sum_ub * z));
      // z == 1 implies penalty = sum
      batch.add(REBALANCER_CTR_REQUEST(penalty <= sum + sum_ub * (1 - z)));
    }

    switch (penaltyTransform_) {
      case ObjectPartitionLookupPenaltyTransform::IDENTITY:
        expr += penalty;
        break;
      case ObjectPartitionLookupPenaltyTransform::STEP:
        batch.flush();
        expr += Step::encodeLp(
            penalty,
            Bounds{.lower_bound = 0, .upper_bound = sum_ub},
            /*childIsInteger=*/false,
            *this,
            evaluator,
            minimizing);
        break;
      case ObjectPartitionLookupPenaltyTransform::SQUARE:
        throw std::runtime_error(
            "SQUARE penalty transform is not supported in LP");
    }
  }
  batch.flush();
  if (addGroupConstraint) {
    newCtr(
        evaluator,
        folly::to<std::string>("summing"),
        limitNumViolators <= groupsAllowed_);
  }
  return expr;
}

template <typename Policy>
AbstractContainer<ObjectPotential>
ObjectPartitionLookup<Policy>::getObjectPotentials(bool descending) const {
  std::vector<ObjectPotential> potentials;
  for (auto objectId : contributingObjectIds_) {
    auto& groupIds = objectPartition_->getObjectGroups(objectId);
    // Below is a pointer to the dimension scope item ID for the object for
    // the current assignment. This is needed when the util scope != dimension
    // scope.
    const auto dimensionScopeItemPtr =
        folly::get_ptr(objectToAssignmentDimensionScopeItem_, objectId);
    const double objectWeight = scopeMatchesDimensionScope_
        ? objectPartition_->getObjectWeight(objectId, scopeItemId_)
        : objectPartition_->getObjectWeight(
              objectId,
              dimensionScopeItemPtr ? std::optional(*dimensionScopeItemPtr)
                                    : std::nullopt);
    double potential = 0;
    for (auto groupId : groupIds) {
      auto& objectWeights = folly::get_ref_default(
          groupObjectWeights_, groupId, kEmptyObjectWeights);
      const double groupWeight = objectWeights.query();
      potential += getGroupPenalty(groupWeight, groupId) -
          getGroupPenalty(groupWeight - objectWeight, groupId);
    }
    if (potential != 0) {
      potentials.push_back(
          ObjectPotential{.objectId = objectId, .potential = potential});
    }
  }
  std::sort(potentials.begin(), potentials.end());
  if (descending) {
    std::reverse(potentials.begin(), potentials.end());
  }
  return makeStlWrapperContainer(std::move(potentials));
}

template <typename Policy>
std::string ObjectPartitionLookup<Policy>::innerDigest(
    size_t maxChildren) const {
  std::stringstream ss;
  ss << "containers(";
  for (const auto& [idx, container] : folly::enumerate(*lookupContainersPtr_)) {
    if (idx >= maxChildren) {
      ss << "... " << lookupContainersPtr_->size() - maxChildren << " more";
      break;
    }
    if (idx > 0) {
      ss << ", ";
    }
    ss << universe_->getEntityName(container);
  }
  ss << ") ";
  ss << initialDuringObjects_.size();
  ss << " initial objects, ";
  ss << "groupsAllowed_: ";
  ss << groupsAllowed_ << ", ";
  if (groupLimitOverrides_.size() > maxChildren) {
    ss << groupLimitOverrides_.size();
    ss << " groupLimitOverrides_";
  } else {
    ss << "groupLimitOverrides_(" << groupLimitOverrides_ << ")";
  }
  const size_t groupObjectWeightsSize = groupObjectWeights_.size();
  if (groupObjectWeightsSize > 0) {
    ss << ", partition_value:";
    // Sort by group name for deterministic output across hash-map orderings
    // and across platforms (raw GroupId values depend on insertion order
    // through unordered maps and are not portable).
    std::vector<std::pair<std::string, double>> sortedEntries;
    sortedEntries.reserve(groupObjectWeightsSize);
    for (const auto& [groupId, weight] : groupObjectWeights_) {
      sortedEntries.emplace_back(
          universe_->getEntityName(groupId), weight.query());
    }
    std::sort(sortedEntries.begin(), sortedEntries.end());
    for (const auto& [idx, entry] : folly::enumerate(sortedEntries)) {
      const auto& [groupName, weight] = entry;
      if (idx >= maxChildren) {
        if (idx < groupObjectWeightsSize) {
          ss << " ... " << groupObjectWeightsSize - maxChildren << " more";
        }
        break;
      }
      ss << " " << fmt::format("{}={}", groupName, weight);
      if (idx < groupObjectWeightsSize - 1 && idx < maxChildren - 1) {
        ss << ",";
      }
    }
  }
  return ss.str();
}

template <typename Policy>
std::vector<std::pair<Expression*, double>>
ObjectPartitionLookup<Policy>::get_sorted_children(bool /* unused */) const {
  return {};
}

template <typename Policy>
bool ObjectPartitionLookup<Policy>::shouldComputeDescendingChildPotentials()
    const {
  return false;
}

template <typename Policy>
ExprPtr ObjectPartitionLookup<Policy>::get_do_not_make_worse_copy(
    const Assignment& initialAssignment) const {
  // copy current groupLimitOverrides_, and update ones where group count is
  // above the limit.
  // TODO This is too loose for groupsAllowed_ == 0, i.e. we are leaving valid
  // some states that make things worse.
  // It's not enough to even not update the limit for groupsAllowed_ worst
  // ones
  PackerMap<entities::GroupId, double> newGroupLimitOverrides =
      groupLimitOverrides_;
  std::vector<std::pair<entities::GroupId, double>> groupPenalties;
  groupPenalties.reserve(groupObjectWeights_.size());
  for (const auto& [groupId, objectWeights] : groupObjectWeights_) {
    groupPenalties.emplace_back(groupId, objectWeights.query());
  }
  std::sort(
      groupPenalties.begin(),
      groupPenalties.end(),
      [](const auto& lhs, const auto& rhs) {
        if (lhs.second == rhs.second) {
          return lhs.first < rhs.first;
        }
        return lhs.second > rhs.second;
      });
  int skipOverrides = 0;
  for (const auto& [groupId, groupWeight] : groupPenalties) {
    // for the top N (= groupsAllowed_) groups, no need to provide limit
    // overrides
    if (groupsAllowed_ > 0 && skipOverrides < groupsAllowed_) {
      ++skipOverrides;
      continue;
    }
    const double diff = groupWeight - getGroupLimit(groupId);
    if ((bound_ == MAX && diff > 0) || (bound_ == MIN && diff < 0)) {
      newGroupLimitOverrides[groupId] = groupWeight;
    }
  }
  return object_partition_lookup(
      *children().begin(),
      lookupContainersPtr_,
      scopeId_,
      scopeItemId_,
      initialAssignment,
      std::move(newGroupLimitOverrides),
      initialDuringObjects_,
      defaultGroupLimitOverride_,
      penaltyTransform_,
      groupsAllowed_,
      bound_ == MIN);
}

template <typename Policy>
void ObjectPartitionLookup<Policy>::set_directly_affected_containers() {
  directlyAffectedContainers.set(lookupContainersPtr_);
}

template <typename Policy>
ExpressionProperties ObjectPartitionLookup<Policy>::getProperties() const {
  ExpressionProperties properties;
  properties.properties()->emplace(
      "penalty_transform",
      PropertiesHelper::makeStringValue(
          std::string(penaltyTransformName(penaltyTransform_))));
  properties.properties()->emplace(
      "groups_allowed", PropertiesHelper::makeIntValue(groupsAllowed_));
  properties.properties()->emplace(
      "lookup_containers",
      PropertiesHelper::makeContainerIdListValue(*lookupContainersPtr_));
  // TODO: add initial_during_objects which needs support for objectId lists
  // TODO: add limit_overrides which needs groupId to groupName translation
  return properties;
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::getGroupLimit(
    entities::GroupId groupId) const {
  {
    auto it = groupLimitOverrides_.find(groupId);
    if (it != groupLimitOverrides_.end()) {
      return it->second;
    }
  }

  if (defaultGroupLimitOverride_.has_value()) {
    return defaultGroupLimitOverride_.value();
  }

  return objectPartition_->getGroupLimit(groupId);
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::getGroupPenalty(
    double weight,
    entities::GroupId groupId) const {
  const double normalizedDeviationFromLimit =
      (weight - getGroupLimit(groupId)) *
      objectPartition_->getGroupCoefficient(groupId);
  return computePenalty(normalizedDeviationFromLimit);
}

template <typename Policy>
FOLLY_ALWAYS_INLINE double ObjectPartitionLookup<Policy>::computePenalty(
    double deviationFromLimit) const {
  const double penalty = std::max(
      0.0, (bound_ == Bound::MAX) ? deviationFromLimit : -deviationFromLimit);
  switch (penaltyTransform_) {
    case ObjectPartitionLookupPenaltyTransform::IDENTITY:
      return penalty;
    case ObjectPartitionLookupPenaltyTransform::SQUARE:
      return penalty * penalty;
    case ObjectPartitionLookupPenaltyTransform::STEP:
      return getPrecision().isStrictlyGtZero(penalty) ? 1.0 : 0.0;
  }
  throw std::runtime_error("ObjectPartitionLookup: unknown penalty transform");
}

template <typename Policy>
bool ObjectPartitionLookup<Policy>::GroupPenalties::Compare::operator()(
    const std::pair<entities::GroupId, double>& lhs,
    const std::pair<entities::GroupId, double>& rhs) const {
  if (lhs.second == rhs.second) {
    return lhs.first < rhs.first;
  }
  return lhs.second > rhs.second;
}

template <typename Policy>
ObjectPartitionLookup<Policy>::GroupPenalties::GroupPenalties(bool iterable) {
  if (iterable) {
    sorted_map.assign(SortedMap());
  }
}

template <typename Policy>
void ObjectPartitionLookup<Policy>::GroupPenalties::clear() {
  sum_map.clear();
  if (sorted_map) {
    sorted_map->clear();
  }
}

template <typename Policy>
void ObjectPartitionLookup<Policy>::GroupPenalties::assign(
    entities::GroupId group_id,
    double penalty) {
  sum_map.update(group_id, penalty);
  if (sorted_map) {
    sorted_map->assign(group_id, penalty);
  }
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::GroupPenalties::sum_all() const {
  return sum_map.query();
}

template <typename Policy>
double ObjectPartitionLookup<Policy>::GroupPenalties::sum_top(int n) const {
  int count = 0;
  double sum = 0;
  for (auto& group_penalty : *this) {
    if (count++ >= n) {
      break;
    }
    sum += group_penalty.second;
  }
  return sum;
}

template <typename Policy>
ObjectPartitionLookup<Policy>::GroupPenalties::const_iterator
ObjectPartitionLookup<Policy>::GroupPenalties::begin() const {
  return sorted_map->begin();
}

template <typename Policy>
ObjectPartitionLookup<Policy>::GroupPenalties::const_iterator
ObjectPartitionLookup<Policy>::GroupPenalties::end() const {
  return sorted_map->end();
}

template <typename Policy>
bool ObjectPartitionLookup<Policy>::hasNoLpIntent() const {
  return true;
}

template <typename Policy>
const entities::PartitionId ObjectPartitionLookup<Policy>::getPartitionId()
    const {
  return objectPartition_->getPartitionId();
}

template <typename Policy>
const entities::DimensionId ObjectPartitionLookup<Policy>::getDimensionId()
    const {
  return objectPartition_->getDimensionId();
}

template <typename Policy>
const PackerMap<entities::GroupId, algopt::SumMap<entities::ObjectId, double>>&
ObjectPartitionLookup<Policy>::getGroupObjectWeights() const {
  return groupObjectWeights_;
}

template <typename Policy>
const entities::ScopeId ObjectPartitionLookup<Policy>::getScopeId() const {
  return scopeId_;
}

template <typename Policy>
const entities::ScopeItemId ObjectPartitionLookup<Policy>::getScopeItemId()
    const {
  return scopeItemId_;
}

// Explicit template instantiation for the default policy
template class ObjectPartitionLookup<ObjectPartitionLookupDefaultPolicy>;

// Explicit template instantiation for the MinPresence policy
template class ObjectPartitionLookup<
    ObjectPartitionLookupWithMinPresencePolicy>;

} // namespace facebook::rebalancer
