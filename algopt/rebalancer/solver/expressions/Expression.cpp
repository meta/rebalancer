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

#include "algopt/rebalancer/solver/expressions/Expression.h"

#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/expressions/TopToBottomEvaluator.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include <algopt/rebalancer/solver/utils/Problem.h>

#include <fmt/core.h>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace facebook::rebalancer {

namespace {

static const auto kEmptyContainerSet =
    std::make_shared<const PackerSet<entities::ContainerId>>();

using DistinctHashCountAndCandidateSetPair =
    std::pair<int, std::optional<ContainerSetWithHash>>;

// For a given node, consider its unique affected set U and its directly
// affected set D.
// 1. If (both U and D exist and are not empty, and they have the same hash
// values) OR (exactly one of U and D exist and is non empty), then return the
// pair (1,  whichever one of U and D exists and is not empty)
// 2. In all other cases return the pair (C, std::nullopt), where C is the
// number of distinct hash value among U and D (0/2).
DistinctHashCountAndCandidateSetPair
getDistinctHashValuesCountAndCandidateSetIfUnique(Expression* node) {
  auto& uniqueAffectedSet =
      node->getUniqueAffectedContainersInSubgraphIfExists(); // U
  auto& directlyAffectedSet = node->getDirectlyAffectedContainers(); // D

  const bool uniqueAffectedExistsAndNotEmpty =
      (uniqueAffectedSet.exists() && !uniqueAffectedSet.isEmpty());
  const bool directlyAffectedExistsAndNotEmpty =
      (directlyAffectedSet.exists() && !directlyAffectedSet.isEmpty());

  PackerSet<int64_t> hashValues;
  if (uniqueAffectedExistsAndNotEmpty) {
    // insert hash value if the node has a unique affected set that is not empty
    hashValues.insert(uniqueAffectedSet.getHashValue());
  }
  if (directlyAffectedExistsAndNotEmpty) {
    // insert hash value if directly affected set exists and is not empty
    hashValues.insert(directlyAffectedSet.getHashValue());
  }

  if (hashValues.size() == 1) {
    // if number of hash values is 1, then exactly one of {U, D} exist and is
    // non-empty, or both are non-empty and have the same hash values. Return
    // whichever one exists and is not empty
    return directlyAffectedExistsAndNotEmpty
        ? std::make_pair(
              1, static_cast<ContainerSetWithHash>(directlyAffectedSet))
        : std::make_pair(
              1, static_cast<ContainerSetWithHash>(uniqueAffectedSet));
  }

  return std::make_pair(hashValues.size(), std::nullopt);
}

std::optional<size_t> getTotalContainersInUniqueAndDirectlyAffectedSets(
    Expression* expr) {
  auto uniqueAffectedSize =
      expr->getUniqueAffectedContainersInSubgraphIfExists().size();
  auto directlyAffectedSize = expr->getDirectlyAffectedContainers().size();
  if (!uniqueAffectedSize.has_value() && !directlyAffectedSize.has_value()) {
    return std::nullopt;
  }

  // Note that we maybe counting some containers twice (e.g., with
  // GroupRoutingTrafficLookup expression). We are not explicitly checking for
  // this here because this size is just used for some consistent tie-breaking
  // and so as long as the values are consistent that's good enough.
  size_t totalSize = 0;
  if (uniqueAffectedSize.has_value()) {
    totalSize += *uniqueAffectedSize;
  }
  if (directlyAffectedSize.has_value()) {
    totalSize += *directlyAffectedSize;
  }

  return totalSize;
}

bool higherPotentialFirst(
    const Expression::ChildPotential& a,
    const Expression::ChildPotential& b) {
  if (a.potential != b.potential) {
    // a should go before b if it has higher potential value
    return b.potential < a.potential;
  }

  // if an expr does not have any unique or directly affected containers,
  // then we take its size as infinity for comparison below, since we
  // prefer expressions that affect some container
  constexpr auto kInf = std::numeric_limits<size_t>::max();
  const auto affectedSize1 =
      getTotalContainersInUniqueAndDirectlyAffectedSets(a.expr).value_or(kInf);
  const auto affectedSize2 =
      getTotalContainersInUniqueAndDirectlyAffectedSets(b.expr).value_or(kInf);
  if (affectedSize1 != affectedSize2) {
    // if both have same potential values, then we prefer placing the one
    // that affects fewer containers first, since that can potentially
    // help in finding a "unique best hottest container"
    // (https://fburl.com/code/4ueonsdy) quickly (e.g., if expr1 and expr2
    // have the same potential, but expr1 affects only 1 container, while
    // expr2 affects 3 containers, then we prefer exploring expr1 first)
    return affectedSize1 < affectedSize2;
  }

  // if still tied, then just tie-break based on expressions with larger
  // id going first (this choice is arbitrary)
  return b.expr->getId() < a.expr->getId();
}

} // namespace

std::atomic<int64_t> next_id(0);

Expression::Expression(const entities::Universe& universe)
    : universe_(&universe), id(next_id++) {}

Expression::Expression(const entities::Universe& universe, double initialValue)
    : Expression(universe) {
  setInitialValue(initialValue);
}

Expression::~Expression() = default;

int Expression::height(Context& context) const {
  auto cached = context.val().get(id);
  if (cached) {
    return *cached;
  }
  int x = 0;
  for (const auto& child : children_) {
    x = std::max(x, child->height(context));
  }
  return context.val().save(id, x + 1);
}

int Expression::subtree_size(Context& context) const {
  auto cached = context.val().get(id);
  if (cached) {
    return *cached;
  }
  int x = 1;
  for (const auto& child : children_) {
    x += child->subtree_size(context);
  }
  return context.val().save(id, x);
}

double Expression::delta(
    const BottomToTopEvaluator& evaluator,
    const ChangeSet& changes) const {
  return evaluator.evaluate((Expression*)this, changes) - value;
}

double Expression::fullApply(
    const TopToBottomEvaluator& evaluator,
    const Assignment& assignment) {
  // TODO: move all context caching operations to orchestrator/evaluator like it
  // is done for partialApply()
  auto& context = evaluator.getContext();
  auto cached = context.apply().get(id);
  if (!cached) {
    auto result = innerFullApply(evaluator, assignment);
    descendingChildPotentials_.shouldRefresh = true;
    return context.apply().save(id, result);
  }
  return *cached;
}

double Expression::partialApply(
    const BottomToTopEvaluator& evaluator,
    const Assignment& assignment,
    const ChangeSet& changes) {
  throwIfNotProperlyInitialized();
  auto newValue = innerPartialApply(evaluator, assignment, changes);
  descendingChildPotentials_.shouldRefresh = true;
  return newValue;
}

Bounds Expression::lowerAndUpperBounds(
    Context& context,
    const BoundConstraints& bc) const {
  if (bc.isEmpty()) {
    {
      auto rLockedUconstrainedBounds = unconstrainedBounds.rlock();
      if (rLockedUconstrainedBounds->has_value()) {
        return rLockedUconstrainedBounds->value();
      }
    }
    return unconstrainedBounds.withWLock([&](auto& wLockedUconstrainedBounds) {
      if (!wLockedUconstrainedBounds.has_value()) {
        wLockedUconstrainedBounds.emplace(
            innerLowerAndUpperBounds(context, bc));
      }
      return wLockedUconstrainedBounds.value();
    });
  } else {
    // only use context cache when calling lowerAndUpperBounds with
    // BoundConstraints
    return context.bounds().getSavedOrCompute(
        id, [&]() { return innerLowerAndUpperBounds(context, bc); });
  }
}

bool Expression::shouldComputeBounds() const {
  return true;
}

void Expression::init_unconstrained_bounds(Context& context) const {
  // lowerAndUpperBounds() initializes unconstrainedBounds if not already
  lowerAndUpperBounds(context);
}

int64_t Expression::getId() const {
  return id;
}

bool Expression::needsEquivalenceSetBasedPostProcessing() const {
  return false;
}

AbstractContainer<ObjectPotential> Expression::getObjectPotentials(
    bool /* descending */) const {
  throw std::runtime_error(
      fmt::format(
          "Expression::getObjectPotentials not implemented for expression type {}",
          getType()));
}

ExpressionProperties Expression::getProperties() const {
  return ExpressionProperties();
}

void Expression::add_child(std::shared_ptr<Expression> expr) {
  children_.insert(std::move(expr));
}

void Expression::updateEquivalenceSets(EquivalenceSets& /*unused*/) const {
  throw std::runtime_error(
      fmt::format("add updateEquivalenceSets API for {}", getType()));
}

const UniqueAffectedContainersInSubgraph&
Expression::getUniqueAffectedContainersInSubgraphIfExists() {
  if (uniqueAffectedContainersInSubgraph.has_value()) {
    return uniqueAffectedContainersInSubgraph.value();
  }

  // has never been computed before, so compute now
  return computeUniqueAffectedContainersInSubgraph();
}

const UniqueAffectedContainersInSubgraph&
Expression::computeUniqueAffectedContainersInSubgraph() {
  uniqueAffectedContainersInSubgraph.emplace();

  // if the current node has directlyAffectedContainers set, then we simply use
  // that set as the unique affected set in subgraph without recursing further.
  // This is ok because directlyAffectedContainers is only set for terminal
  // computation nodes (such as ObjectLookup, ObjectPartitionLookup etc) and we
  // can safely assume that the containers covered by the set of
  // directlyAffectedContainers is a strict superset of containers in the
  // subgraph.
  if (directlyAffectedContainers.exists()) {
    uniqueAffectedContainersInSubgraph->set(
        directlyAffectedContainers.getSetPtr());
    return uniqueAffectedContainersInSubgraph.value();
  }

  std::optional<ContainerSetWithHash> candidateAffectedSet = std::nullopt;
  for (const auto& child : children_) {
    auto& childUniqueAffectedContainersInSubgraph =
        child->getUniqueAffectedContainersInSubgraphIfExists();
    if (!childUniqueAffectedContainersInSubgraph.exists()) {
      //  child does not have unique affected set in its subgraph. Therefore,
      //  current node cannot have a  UniqueAffectedContainerInSubgraph (so will
      //  be nullptr)
      return uniqueAffectedContainersInSubgraph.value();
    }

    auto [distinctHashCountFromChild, uniqueSetFromChild] =
        getDistinctHashValuesCountAndCandidateSetIfUnique(child.get());
    if (distinctHashCountFromChild > 1) {
      // (distinctHashCountFromChild > 1) => the child has distinct hash values
      // for its unique affected set in subgraph and the its directly affected
      // set. Therefore, current node cannot have a
      // UniqueAffectedContainerInSubgraph (so will be nullptr)
      return uniqueAffectedContainersInSubgraph.value();
    } else if (distinctHashCountFromChild == 0) {
      // (childUniqueAffectedContainersInSubgraph.exists() and distinctHashCount
      // == 0) => child has an an empty unique affected set and also does not
      // have a non-empty directly affected set (so it is not affected by any
      // container; e.g., it could be an ObjectVector or sum of constants).
      continue;
    }

    // reaching here implies child has either
    // 1. a unique affected set in subgraph which is empty, and has a directly
    // affected set
    // 2. a unique affected set in subgraph that is NOT empty and that set has
    // the same hash value as the child's directly affected set (if it has one)
    if (!candidateAffectedSet.has_value()) {
      // first time seeing a uniqueSetFromChild, so nothing to compare against
      candidateAffectedSet = std::move(uniqueSetFromChild);
    } else if (
        candidateAffectedSet->getHashValue() !=
        uniqueSetFromChild->getHashValue()) {
      // the hash values seen from other children are different from the current
      // child. Hence, parent cannot have a unique affected set in its subgraph
      return uniqueAffectedContainersInSubgraph.value();
    }
  }

  // reaching here implies the current expression node has a unique affected set
  // in its subgraph. This set is the same as the candidateAffectedSet if it
  // exists and if not, it is just empty
  if (!candidateAffectedSet.has_value()) {
    // happens if the node has no children or if every child of the current node
    // has an empty unique affected set in its subgraph and also none of them
    // have a directly affected set (e.g, this can happen for an ObjectVector,
    // ObjectLookup, or sum of constants).
    uniqueAffectedContainersInSubgraph->set(kEmptyContainerSet);
  } else {
    uniqueAffectedContainersInSubgraph->set(candidateAffectedSet->getSetPtr());
  }

  return uniqueAffectedContainersInSubgraph.value();
}

void Expression::all_objects(
    PackerSet<entities::ObjectId>& to_add,
    std::set<Expression*>& visited) {
  if (!visited.insert(this).second) {
    return;
  }

  for (const auto& child : children_) {
    child->all_objects(to_add, visited);
  }
}

double Expression::scaled_bound(double v) const {
  if (v >= 0) {
    v *= 2;
  } else {
    v /= 2;
  }
  return v + getPrecision().getTolerances().absolute().value();
}

const PackerSet<entities::ContainerId>& Expression::getAllAffectedContainers()
    const {
  return allAffectedContainers_.getSavedOrCompute([&]() {
    // TODO: avoid unnecessary copying and set operations if all the children
    // of this node affects the same containers (or affect none)

    // first add all the children's allAffectedContainers
    PackerSet<entities::ContainerId> allAffectedContainers;
    for (const auto& child : children_) {
      auto& allAffectedContainersOfChild = child->getAllAffectedContainers();
      allAffectedContainers.insert(
          allAffectedContainersOfChild.begin(),
          allAffectedContainersOfChild.end());
    }

    // add the current node's directlyAffectedContainers
    if (auto directlyAffectedContainerSet =
            directlyAffectedContainers.getSetPtr()) {
      allAffectedContainers.insert(
          directlyAffectedContainerSet->begin(),
          directlyAffectedContainerSet->end());
    }

    return allAffectedContainers;
  });
}

const DirectlyAffectedContainersSet& Expression::getDirectlyAffectedContainers()
    const {
  return directlyAffectedContainers;
}

static double getPotential(Expression* expr, double coeff, Context& context) {
  auto [lb, ub] = expr->lowerAndUpperBounds(context);
  return coeff * (expr->value - (coeff > 0 ? lb : ub));
}

std::vector<std::pair<Expression*, double>> Expression::get_sorted_children(
    bool descending) const {
  std::vector<std::pair<Expression*, double>> sorted_children;
  sorted_children.reserve(children_.size());
  for (const auto& child : children_) {
    sorted_children.emplace_back(child.get(), getChildCoefficient(child.get()));
  }

  const int sign = descending ? -1 : 1;
  Context bounds_context;
  sort(
      sorted_children.begin(),
      sorted_children.end(),
      [sign, &bounds_context](
          const std::pair<Expression*, double>& lhs,
          const std::pair<Expression*, double>& rhs) {
        const auto& [lhs_expr, lhs_coef] = lhs;
        const auto& [rhs_expr, rhs_coef] = rhs;
        // return sorted children in the order of potential for improvement
        // (expressed as the difference from estimated global bound)

        // NOTE: coef * potential is always positive: i.e, both are either
        // positive or both are negative
        auto lhs_potential = getPotential(lhs_expr, lhs_coef, bounds_context);
        auto rhs_potential = getPotential(rhs_expr, rhs_coef, bounds_context);
        if (sign * lhs_potential != sign * rhs_potential) {
          return sign * lhs_potential < sign * rhs_potential;
        }
        // Break ties by coefficient (ascending).
        if (lhs_coef != rhs_coef) {
          return lhs_coef < rhs_coef;
        }
        return false;
      });
  return sorted_children;
}

bool Expression::shouldComputeDescendingChildPotentials() const {
  if (children_.size() == 0) {
    return false;
  }

  return true;
}

const Expression::DescendingChildPotentials&
Expression::getDescendingChildPotentials() {
  if (shouldComputeDescendingChildPotentials() &&
      descendingChildPotentials_.shouldRefresh) {
    refreshDescendingChildPotentials();
  }

  return descendingChildPotentials_;
}

Expression::ChildPotential Expression::makeChildPotential(
    Expression* child,
    Context& context) const {
  const auto coef = getChildCoefficient(child);
  return ChildPotential{
      .expr = child,
      .potential = getPotential(child, coef, context),
      .valueAtLastRefresh = child->value};
}

void Expression::refreshDescendingChildPotentials() {
  Context context;
  std::vector<ChildPotential> unchanged;
  std::vector<ChildPotential> changed;

  if (descendingChildPotentials_.isEmpty()) {
    // Initial build: every child needs a fresh entry, none are "unchanged".
    changed.reserve(children_.size());
    for (const auto& child : children_) {
      changed.push_back(makeChildPotential(child.get(), context));
    }
  } else {
    // Incremental refresh: keep entries whose `value` didn't change.
    unchanged.reserve(descendingChildPotentials_.size());
    for (const auto& cp : descendingChildPotentials_) {
      if (cp.expr->value == cp.valueAtLastRefresh) {
        unchanged.push_back(cp);
      } else {
        changed.push_back(makeChildPotential(cp.expr, context));
      }
    }
    if (changed.empty()) {
      descendingChildPotentials_.shouldRefresh = false;
      return;
    }
  }

  std::sort(changed.begin(), changed.end(), higherPotentialFirst);
  if (unchanged.empty()) {
    descendingChildPotentials_.setPotentials(std::move(changed));
    return;
  }

  std::vector<ChildPotential> merged;
  merged.reserve(unchanged.size() + changed.size());
  std::merge(
      unchanged.begin(),
      unchanged.end(),
      changed.begin(),
      changed.end(),
      std::back_inserter(merged),
      higherPotentialFirst);
  descendingChildPotentials_.setPotentials(std::move(merged));
}

std::string Expression::innerDigest(size_t /*maxChildren*/) const {
  return "";
}

bool Expression::hasValue() const {
  return true;
}

std::string
Expression::digest(const Problem& p, bool logExprInfo, int maxChildren) {
  std::string exprInfo;
  if (logExprInfo) {
    Context context;
    const auto [lb, ub] = lowerAndUpperBounds(context);
    exprInfo = fmt::format(
        "type {} value {} num children {} containers {} height {} "
        "upper bound {} lower bound {} description {}\n",
        getType(),
        value,
        children().size(),
        subtree_size(context),
        height(context),
        ub,
        lb,
        description);
  }

  auto metadata = [&p](Expression& expression) {
    Context initialContext;
    Context finalContext;
    double initial_val = expression.fullApply(
        TopToBottomEvaluator(initialContext), p.initial_assignment);
    double final_val =
        expression.fullApply(TopToBottomEvaluator(finalContext), p.assignment);
    return fmt::format(
        "{} [{} → {}]", expression.getType(), initial_val, final_val);
  };
  return exprInfo + digestImpl(std::move(metadata), "", maxChildren);
}

std::string Expression::digestImpl(
    std::function<std::string(Expression&)> metadata,
    std::string prefix,
    int maxChildren) {
  std::stringstream ss;
  ss << metadata(*this);
  const auto innerDigestStr = innerDigest(maxChildren);
  if (!innerDigestStr.empty()) {
    ss << " " << innerDigestStr;
  }
  if (description != "") {
    ss << "\n" << prefix << " {" << fmt::format("desc: {}", description) << "}";
  }
  ss << "\n";
  std::stringstream ss2;
  ss2 << prefix << "   ";
  int count = 0;

  auto sorted_children = get_sorted_children(true);
  auto maxChildIndex = maxChildren;
  const int sorted_children_size = sorted_children.size();
  if (maxChildIndex >= sorted_children_size) {
    maxChildIndex = sorted_children_size - 1;
  }

  for (auto& it : sorted_children) {
    auto indentMarker = (count < maxChildIndex) ? "├─" : "└─";
    if (count >= maxChildren) {
      ss << ss2.str() << "... " << children_.size() - maxChildren
         << " more children\n";
      break;
    }
    ss << ss2.str() << indentMarker;
    if (it.second != 1) {
      ss << it.second << " * ";
    }
    auto depthMarker = (count < maxChildIndex) ? "│" : " ";
    ss << it.first->digestImpl(metadata, ss2.str() + depthMarker, maxChildren);
    count++;
  }
  return ss.str();
}

std::optional<std::pair<entities::ObjectId, entities::ContainerId>>
Expression::getVar() const {
  return std::nullopt;
}

std::optional<std::string> Expression::getSpecAnnotation() const {
  return associatedSpecName;
}

void Expression::setSpecAnnotation(std::string specName) {
  associatedSpecName = std::move(specName);
}

bool Expression::is_binary(Context& context) {
  return context.binary().getSavedOrCompute(id, [&]() {
    auto [lb, ub] = lowerAndUpperBounds(context);

    return is_integer(context) && lb >= 0 && lb <= 1 && ub >= 0 && ub <= 1;
  });
}

bool Expression::inner_is_integer(Context& /* not used */) {
  return false;
}

bool Expression::is_integer(Context& context) {
  return context.integer().getSavedOrCompute(
      id, [&]() { return inner_is_integer(context); });
}

std::pair<algopt::lp::Relation, std::string> Expression::ctrRequest(
    const std::string& suffix,
    const algopt::lp::Relation& relation,
    const std::vector<int64_t>& relatedExpressionIds) const {
  std::stringstream name;
  name << id;
  for (auto relatedIds : relatedExpressionIds) {
    name << ":" << relatedIds;
  }

  if (!suffix.empty()) {
    name << "_" << suffix;
  }
  return {relation, name.str()};
}

void Expression::newCtr(
    const LpEvaluator& evaluator,
    const std::string& suffix,
    const algopt::lp::Relation& relation,
    const std::vector<int64_t>& relatedExpressionIds) const {
  const auto request = ctrRequest(suffix, relation, relatedExpressionIds);
  evaluator.addLpConstraint(request.first, request.second);
}

void Expression::ensureOnlyOneChildExists() const {
  if (children_.size() != 1) {
    throw std::runtime_error(
        fmt::format("{} has {} children", getType(), children_.size()));
  }
}

ExprPtr Expression::getOnlyChild() const {
  ensureOnlyOneChildExists();
  return *children_.begin();
}

Expression* Expression::getOnlyChildRawPtr() const {
  ensureOnlyOneChildExists();
  return children_.begin()->get();
}

double Expression::evaluate(
    const BottomToTopEvaluator& /* unused */,
    const ChangeSet& /* unused */) const {
  throw std::runtime_error(fmt::format("add evaluate API for {}", getType()));
}
double Expression::innerFullApply(
    const TopToBottomEvaluator& /* unused */,
    const Assignment& /* unused */) {
  throw std::runtime_error(
      fmt::format(
          "add innerFullApply API for expression type '{}'", getType()));
}
double Expression::innerPartialApply(
    const BottomToTopEvaluator& /* unused */,
    const Assignment& /* unused */,
    const ChangeSet& /* unused */) {
  throw std::runtime_error(
      fmt::format(
          "add innerPartialApply API for expression type '{}'", getType()));
}

algopt::lp::Expression Expression::lp(
    const LpEvaluator& /* unused */,
    bool /* unused */,
    const interface::OptimalSolverSpec& /* unused */) {
  throw std::runtime_error(fmt::format("add lp API for {}", getType()));
}

void Expression::lpIntent(const LpEvaluator& /*unused*/, bool /*unused*/) {
  if (hasNoLpIntent()) {
    return;
  }
  throw std::runtime_error(
      fmt::format("lpIntent is not implemented for {} expr", getType()));
}

std::optional<AffectedByChange> Expression::isAffectedByChange(
    const AffectedByChangeDecisionData& /*data*/) const {
  return std::nullopt;
}

double Expression::getChildCoefficient(
    [[maybe_unused]] Expression* child) const {
  return 1;
}

algopt::lp::Variable Expression::lp_cont_var(
    const LpEvaluator& evaluator,
    const std::string& name) {
  return evaluator.makeLpVar(LpVarType::CONTINUOUS, name);
}

algopt::lp::Variable Expression::lp_int_var(
    const LpEvaluator& evaluator,
    const std::string& name) {
  return evaluator.makeLpVar(LpVarType::INTEGER, name);
}

algopt::lp::Variable Expression::lp_bool_var(
    const LpEvaluator& evaluator,
    const std::string& name) {
  return evaluator.makeLpVar(LpVarType::BINARY, name);
}

bool Expression::isAnyPositive() const {
  return false;
}

bool Expression::isMax() const {
  return false;
}

bool Expression::isLinearSum() const {
  return false;
}

bool Expression::hasNoLpIntent() const {
  return false;
}

// this function is used in all partial 'apply' functions---i.e., 'apply'
// functions that work only with the changes to the assignment---to make sure
// it is safe to just look at the changes
void Expression::throwIfNotProperlyInitialized() const {
  if (!properlyInitialized) {
    throw std::runtime_error(
        fmt::format(
            "{} expression is not properly initialized. Has it been initialized using a full assignment at least once?",
            getType()));
  }
}

void Expression::markFixed() {
  isFixed_ = true;
}

bool Expression::isFixed() const {
  return isFixed_;
}

const entities::Universe& Expression::getUniverse() const {
  return *universe_;
}

const Precision& Expression::getPrecision() const {
  return universe_->getPrecision();
}

double Expression::snapToZero(double val) const {
  return getPrecision().isZero(val) ? 0.0 : val;
}

double Expression::getInitialValue() const {
  return initialValue_;
}

void Expression::setInitialValue(double v) {
  initialValue_ = v;
  value = v;
  properlyInitialized = true;
}

} // namespace facebook::rebalancer
