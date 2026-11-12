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

#pragma once

#include "algopt/lp/generic/Relation.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/solver/if/gen-cpp2/packer_types.h"
#include "algopt/rebalancer/solver/iterators/Abstract.h"
#include "algopt/rebalancer/solver/utils/AffectedByChangeInfo.h"
#include "algopt/rebalancer/solver/utils/BoundConstraints.h"
#include "algopt/rebalancer/solver/utils/ContainerSetWithHash.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#include <folly/json/dynamic.h>
#include <folly/Optional.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

using LpVarType = algopt::lp::VariableImpl::Type;

class Problem;
class Assignment;
class Evaluator;
class LpEvaluator;
class TopToBottomEvaluator;
class BottomToTopEvaluator;
constexpr int DEFAULT_DEBUG_LENGTH = 3;

// The object potential can be thought of as the gradient of the objective
// function where the variables represent whether an object belongs to a
// container. More specifically, the value of the potential in this struct
// represents how much the objective decreases if we make all the variables
// involving the given object zero.
struct ObjectPotential {
  entities::ObjectId objectId;
  double potential;

  bool operator==(const ObjectPotential& other) const {
    return objectId == other.objectId && potential == other.potential;
  }

  bool operator<(const ObjectPotential& other) const {
    if (potential != other.potential) {
      return potential < other.potential;
    }
    return objectId < other.objectId;
  }
};

class DirectlyAffectedContainersSet : public ContainerSetWithHash {};
class UniqueAffectedContainersInSubgraph : public ContainerSetWithHash {};

class Expression {
 public:
  Expression(const entities::Universe& universe, double initialValue);

  // For subclasses whose initial value depends on members set after the base
  // ctor runs. The subclass MUST call `setInitialValue(...)` in its ctor
  // body; otherwise `partialApply()` throws at first use.
  explicit Expression(const entities::Universe& universe);

  // Called after children are initialized
  void add_child(std::shared_ptr<Expression> expr);

  Expression(const Expression&) = default;
  Expression(Expression&&) = default;
  Expression& operator=(const Expression&) = delete;
  Expression& operator=(Expression&&) = delete;
  virtual ~Expression();

  double delta(const BottomToTopEvaluator& evaluator, const ChangeSet& changes)
      const;

  int height(Context& context) const;

  int subtree_size(Context& context) const;

  // updates the equivalence sets using this expression
  virtual void updateEquivalenceSets(EquivalenceSets& equivSets) const;

  /* visited is a passed down set which saves all address
   * of visited nodes, no need to use std::shared_ptr<Expression>
   */
  void all_objects(
      PackerSet<entities::ObjectId>& to_add,
      std::set<Expression*>& visited);

  virtual std::optional<std::pair<entities::ObjectId, entities::ContainerId>>
  getVar() const;

  std::optional<std::string> getSpecAnnotation() const;
  void setSpecAnnotation(std::string specName);

  /* below is best effort:
   * if the function says the expression is binary/integer,
   * then the expression is guaranteed to be binary/integer.
   * But the other way around is not true:
   * if an expression is binary/integer,
   * these 2 functions may not necessarily return true.
   */
  bool is_binary(Context& context);
  bool is_integer(Context& context);
  virtual bool inner_is_integer(Context& context);

  // Given a value, return a larger one both by multiplying and adding to it
  // This helps with precision issues in large and small bound values
  double scaled_bound(double v) const;

  const DirectlyAffectedContainersSet& getDirectlyAffectedContainers() const;

  // This function is used to determine if all the nodes in the induced subgraph
  // rooted at a node v (that are not the same as v) affect the same set of
  // containers. For an expression node v, UniqueAffectedContainersInSubgraph of
  // v will have a non-null container pointer if all the nodes w (w \neq v) in
  // the subgraph rooted at v the same set of directly affected containers.
  // Rather than comparing the directly affected containers of all w, this
  // function compares their hash values. Doing this avoids a) having to
  // maintain a set of allAffectedContainers at every node which can be
  // extremely expensive in terms of memory usage, and b) avoids potentially
  // expensive union of several sets
  const UniqueAffectedContainersInSubgraph&
  getUniqueAffectedContainersInSubgraphIfExists();

  // Computed on demand and saved if computed for a node once. Can be expensive
  // especially in terms of memory
  const PackerSet<entities::ContainerId>& getAllAffectedContainers() const;

  virtual std::vector<std::pair<Expression*, double>> get_sorted_children(
      bool descending) const;

  int64_t getId() const;

  // Some expressions such as StableStayed need special handling for equivalence
  // set computation. This information will later be used to recover the object
  // to container assignment from equivalence sets to container assignment in a
  // postrpocessing step for optimal solver. Currently this function only
  // returns true for StableStayed expression.
  virtual bool needsEquivalenceSetBasedPostProcessing() const;

  // Objects sorted by a best-effort computation of their potentials.
  // Objects with zero potential may be excluded.
  // The potential of an object in the context of an expression is defined as
  // the amount the expression's value would decrease by if we removed that
  // object from its current assignment. The potential will be negative when
  // removing an object increases the value of an expression.
  // Note: the traversal may not be valid after a mutation to the expression,
  // do not re-use it after an apply operation.
  virtual AbstractContainer<ObjectPotential> getObjectPotentials(
      bool descending) const;

  virtual ExpressionProperties getProperties() const;

  virtual bool references(entities::ObjectId) const {
    return false;
  }

  virtual bool references(entities::ContainerId) const {
    return false;
  }

  virtual double evaluate(
      const BottomToTopEvaluator& evaluator,
      const ChangeSet& changes) const;

  //'fullApply' refers to applying the assignment recursively using a
  // TopToBottomEvaluator
  double fullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment);

  // partialApply refers to applying the given changes using a
  // BottomToTopEvaluator
  double partialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes);

  virtual algopt::lp::Expression lp(
      const LpEvaluator& evaluator,
      bool minimizing,
      const interface::OptimalSolverSpec& configs);

  virtual std::optional<AffectedByChange> isAffectedByChange(
      const AffectedByChangeDecisionData& data) const;

  virtual double getChildCoefficient(Expression* child) const;

  static algopt::lp::Variable lp_cont_var(
      const LpEvaluator& evaluator,
      const std::string& = "var");

  static algopt::lp::Variable lp_bool_var(
      const LpEvaluator& evaluator,
      const std::string& = "var");

  static algopt::lp::Variable lp_int_var(
      const LpEvaluator& evaluator,
      const std::string& = "var");

  // check if the expression is of a certain type
  virtual bool isAnyPositive() const;
  virtual bool isMax() const;
  virtual bool isLinearSum() const;

  virtual bool hasNoLpIntent() const;

  double getInitialValue() const;

 protected:
  // All expressions such as ObjectLookup, ObjectPartitionLookup etc that
  // directly affect a set of containers MUST set this with an appropriate set
  // of containers.
  DirectlyAffectedContainersSet directlyAffectedContainers;

  std::optional<UniqueAffectedContainersInSubgraph>
      uniqueAffectedContainersInSubgraph = std::nullopt;

 private:
  virtual double innerFullApply(
      const TopToBottomEvaluator& evaluator,
      const Assignment& assignment);

  virtual double innerPartialApply(
      const BottomToTopEvaluator& evaluator,
      const Assignment& assignment,
      const ChangeSet& changes);

  const UniqueAffectedContainersInSubgraph&
  computeUniqueAffectedContainersInSubgraph();

 public:
  std::pair<algopt::lp::Relation, std::string> ctrRequest(
      const std::string& suffix,
      const algopt::lp::Relation& relation,
      const std::vector<int64_t>& relatedExpressionIds = {}) const;

  void newCtr(
      const LpEvaluator& evaluator,
      const std::string& extra,
      const algopt::lp::Relation& relation,
      const std::vector<int64_t>& relatedExpressionIds = {}) const;

  inline const PackerSet<std::shared_ptr<Expression>>& children() const {
    return children_;
  }

  double value = 0.0;

  inline const Bounds& getUnconstrainedBounds() const {
    auto rLockedUnconstrainedBounds = unconstrainedBounds.rlock();
    if (!rLockedUnconstrainedBounds->has_value()) {
      throw std::runtime_error(
          "Expected unconstrained bounds to be initialized");
    }
    return rLockedUnconstrainedBounds->value();
  }

  Bounds lowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc = BoundConstraints()) const;

  virtual bool shouldComputeBounds() const;

  // populate the value for unconstrained bounds on the expression
  void init_unconstrained_bounds(Context& context) const;

  void ensureOnlyOneChildExists() const;
  std::shared_ptr<Expression> getOnlyChild() const;
  Expression* getOnlyChildRawPtr() const;

  virtual void lpIntent(const LpEvaluator& evaluator, bool minimizing);

  // A child entry in the descending-potential cache. `valueAtLastRefresh` is
  // the child's `value` at the time `potential` was computed; the incremental
  // refresh uses it to detect stale entries.
  struct ChildPotential {
    Expression* expr{nullptr};
    double potential{0.0};
    double valueAtLastRefresh{0.0};
  };

  // Cache of children sorted by descending potential plus a dirty bit. The
  // owning Expression sets the bit on apply and rebuilds the cache on refresh.
  struct DescendingChildPotentials {
    std::vector<ChildPotential> potentials;
    bool shouldRefresh = true;

    bool isEmpty() const {
      return potentials.empty();
    }
    size_t size() const {
      return potentials.size();
    }
    auto begin() const {
      return potentials.begin();
    }
    auto end() const {
      return potentials.end();
    }
    auto rbegin() const {
      return potentials.rbegin();
    }
    auto rend() const {
      return potentials.rend();
    }
    void setPotentials(std::vector<ChildPotential> newPotentials) {
      potentials = std::move(newPotentials);
      shouldRefresh = false;
    }
  };
  const DescendingChildPotentials& getDescendingChildPotentials();

  std::string description;

  virtual std::string innerDigest(size_t maxChildren = 10) const;
  [[nodiscard]] std::string digest(
      const Problem& p,
      bool logExprInfo = false,
      int maxChildren = DEFAULT_DEBUG_LENGTH);

  virtual const std::string_view& getType() const = 0;

  /** Depending on the set of fixed containers, the following accesor
  functions allows us to mark an expression as fixed, or determine if the
  expression is fixed */
  void markFixed();
  bool isFixed() const;

  const entities::Universe& getUniverse() const;
  const Precision& getPrecision() const;

 protected:
  virtual bool hasValue() const;

  virtual bool shouldComputeDescendingChildPotentials() const;

  // True once `setInitialValue()` has been called. Must be set before
  // `partialApply()` runs; otherwise `partialApply()` throws.
  bool properlyInitialized = false;

  void setInitialValue(double v);

  double initialValue_ = 0.0;
  void throwIfNotProperlyInitialized() const;

  // Rounds a value within precision tolerance of zero to exactly 0.
  double snapToZero(double val) const;

  // name of the spec which when materialized created this expression, only
  // set for the topmost node that was added to constraint or expression
  // trees
  std::optional<std::string> associatedSpecName = std::nullopt;

  mutable folly::Synchronized<std::optional<Bounds>> unconstrainedBounds;

  const entities::Universe* universe_;

 private:
  std::string digestImpl(
      std::function<std::string(Expression&)> metadata,
      std::string prefix,
      int maxChildren);

  virtual Bounds innerLowerAndUpperBounds(
      Context& context,
      const BoundConstraints& bc) const = 0;

  ChildPotential makeChildPotential(Expression* child, Context& context) const;

  // Builds the cache (when empty) or refreshes it incrementally: re-sorts only
  // the children whose `value` changed since the last refresh and merges them
  // with the unchanged (still-sorted) run.
  void refreshDescendingChildPotentials();

  /** Returns j, if the expression only touches containers in subproblem j.
   * If the expressions touches more subproblems returns nullopt */
  std::optional<int> getOnlyAffectedSubproblemIfExists(const Problem& p) const;

  int64_t id;

  /** We say that a node is dynamic if its value can change during the solve
   process, otherwise it fixed. It is possible that sometimes all the
  containers affected by this node are fixed (can neither give or take
  objects). In that case, the value of this node won't change during the
  solve, so this node should not be expanded while searching for a hot
  container. */
  bool isFixed_ = false;

  // thread-safe
  mutable materializer::SingleEntryCache<PackerSet<entities::ContainerId>>
      allAffectedContainers_;

  DescendingChildPotentials descendingChildPotentials_;

  PackerSet<std::shared_ptr<Expression>> children_;
};

#define REBALANCER_CTR_REQUEST(x) \
  ctrRequest(folly::to<std::string>(getType(), "_", __LINE__), x)

#define REBALANCER_NEWCTR(x) \
  newCtr(evaluator, folly::to<std::string>(getType(), "_", __LINE__), x)

#define REBALANCER_NEWCTR_WITH_RELATED(x, relatedExpressionIds) \
  newCtr(                                                       \
      evaluator,                                                \
      folly::to<std::string>(getType(), "_", __LINE__),         \
      x,                                                        \
      relatedExpressionIds)

class Expression;
using ConstExprPtr = std::shared_ptr<const Expression>;
using ExprPtr = std::shared_ptr<Expression>;

} // namespace facebook::rebalancer
