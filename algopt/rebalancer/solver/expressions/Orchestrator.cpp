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

#include "algopt/rebalancer/solver/expressions/Orchestrator.h"

#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/common/CoroUtils.h"
#include "algopt/rebalancer/solver/expressions/BottomToTopEvaluator.h"
#include "algopt/rebalancer/solver/expressions/LpEvaluator.h"
#include "algopt/rebalancer/solver/utils/Change.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <folly/container/irange.h>
#include <folly/executors/VirtualExecutor.h>

#include <algorithm>

namespace facebook::rebalancer {

Orchestrator::Orchestrator() = default;

void Orchestrator::init(
    std::vector<Expression*> roots,
    AffectedByChangeDecisionData data,
    const PackerSet<entities::ContainerId>& fixedContainers) {
  roots_ = std::move(roots);
  // nodeRangeByRoots: record the range of nodes in postOrder_ belonging to a
  // root e.g., all the nodes between postOrder[nodeRangeByRoots[0]] and
  // postOrder_[nodeRangeByRoots[1]] belongs to roots_[0]
  std::vector<int> nodeRangeByRoots = {0};
  // initialize priorities of all nodes in the graph
  initOrder(nodeRangeByRoots, fixedContainers);
  initParents();
  // initialize connection between changes and affected leaf nodes, including
  // containerToLeaves_, objectToLeaves_, containerObjectToLeaves_, and
  // toCheckLeaves__
  initIndex(nodeRangeByRoots, data);

  const auto nExprs = postOrder_.size();
  const auto fixedNodePercent = (nFixedNodes_ * 100.0) / nExprs;
  XLOGF(INFO, "Number of nodes in the expression graph = {}", nExprs);
  XLOGF(
      DBG1,
      "Number of fixed nodes = {} ({:.2f}%)",
      nFixedNodes_,
      fixedNodePercent);
}

void Orchestrator::traversePostOrder(
    Expression* node,
    folly::F14VectorSet<Expression*>& visited,
    const PackerSet<entities::ContainerId>& fixedContainers,
    size_t currRootIdx) {
  const auto [_, inserted] = visited.emplace(node);
  if (!inserted) {
    return;
  }

  bool isFixedExpr = true;
  std::size_t maxChildHeight = 0;
  for (const auto& child : node->children()) {
    traversePostOrder(child.get(), visited, fixedContainers, currRootIdx);
    isFixedExpr = isFixedExpr && child->isFixed();
    maxChildHeight =
        std::max(maxChildHeight, nodeToPriority_.at(child.get()).height);
  }

  // push the node to postOrder, after visiting all its children
  // children have higher priority than their parents
  postOrder_.push_back(node);

  nodeToPriority_.emplace(
      node, PriorityInfo{.rootIdx = currRootIdx, .height = maxChildHeight + 1});

  // return early if node is not fixed
  if (!isFixedExpr) {
    return;
  }

  // all children are fixed at this point, so if all directly affected
  // containers are fixed, mark the node as fixed
  const auto directlyAffectedSet =
      node->getDirectlyAffectedContainers().getSetPtr();
  if (directlyAffectedSet) {
    isFixedExpr = std::all_of(
        directlyAffectedSet->begin(),
        directlyAffectedSet->end(),
        [&fixedContainers](const auto& containerId) {
          return fixedContainers.contains(containerId);
        });
  }

  if (isFixedExpr) {
    node->markFixed();
    nFixedNodes_++;
  }
}

void Orchestrator::initOrder(
    std::vector<int>& nodeRangeByRoots,
    const PackerSet<entities::ContainerId>& fixedContainers) {
  folly::F14VectorSet<Expression*> visited;
  // traverse each tree in post order
  // visits a parent after all its children
  for (const auto rootIdx : folly::irange(roots_.size())) {
    traversePostOrder(roots_[rootIdx], visited, fixedContainers, rootIdx);
    // the current size of postOrder, record the range of nodes in postOrder
    // belonging to a root
    nodeRangeByRoots.push_back(postOrder_.size());
  }
}

void Orchestrator::initBoundsBottomUp(
    std::shared_ptr<folly::ThreadPoolExecutor> executor) {
  auto virtualExecutor = folly::VirtualExecutor(executor.get());
  for (auto& expr : postOrder_) {
    if (expr->shouldComputeBounds()) {
      virtualExecutor.add([&, expr]() {
        Context context;
        expr->init_unconstrained_bounds(context);
      });
    }
  }
}

void Orchestrator::initParents() {
  nodeToParents_.reserve(postOrder_.size());
  for (const auto& node : postOrder_) {
    for (const auto& child : node->children()) {
      nodeToParents_[child.get()].push_back(node);
    }
  }
}

void Orchestrator::initIndex(
    const std::vector<int>& nodeRangeByRoots,
    const AffectedByChangeDecisionData& data) {
  for (const auto i : folly::irange(roots_.size())) {
    toCheckLeaves_.emplace_back();
    containerToLeaves_.emplace_back();
    objectToLeaves_.emplace_back();
    containerObjectToLeaves_.emplace_back();

    // for all nodes that belong to ith root, add it to one of
    // {toCheckLeaves_[i], containerToLeaves_[i], objectToLeaves_[i],
    // containerObjectToLeaves_[i]} if the node is affected by change and
    // depending on AffectedByChangeType
    for (int j = nodeRangeByRoots[i]; j < nodeRangeByRoots[i + 1]; ++j) {
      initChangeInfo(postOrder_[j], i, data);
    }
  }
}

Orchestrator::LeafId Orchestrator::LeafStore::add(
    Expression* node,
    ChangeFilterFn filter) {
  const auto id = static_cast<LeafId>(leaves_.size());
  leaves_.push_back(node);
  filters_.push_back(std::move(filter));
  return id;
}

void Orchestrator::initChangeInfo(
    Expression* node,
    int rootId,
    const AffectedByChangeDecisionData& data) {
  // For a given node and rootId i, this function is to used to add the node to
  // one of {toCheckLeaves_[i], containerToLeaves_[i], objectToLeaves_[i],
  // containerObjectToLeaves_[i]} if the node is affected by change and
  // depending on AffectedByChangeType
  auto affectedByChangeInfo = node->isAffectedByChange(data);
  if (!affectedByChangeInfo.has_value()) {
    return;
  }
  const auto changeType = affectedByChangeInfo->getType();
  const auto leafId = leafStore_.add(node, affectedByChangeInfo->getFilter());
  switch (changeType) {
    case AffectedByChangeType::CONTAINERS_ONLY: {
      for (auto containerId : affectedByChangeInfo->getContainers()) {
        containerToLeaves_[rootId][containerId].push_back(leafId);
      }
      break;
    }
    case AffectedByChangeType::OBJECTS_ONLY: {
      for (auto objectId : affectedByChangeInfo->getObjects()) {
        objectToLeaves_[rootId][objectId].push_back(leafId);
      }
      break;
    }
    case AffectedByChangeType::ALL_GIVEN_CONTAINER_OBJECT_PAIRS: {
      for (auto containerId : affectedByChangeInfo->getContainers()) {
        for (auto objectId : affectedByChangeInfo->getObjects()) {
          containerObjectToLeaves_[rootId][containerId][objectId].push_back(
              leafId);
        }
      }
      break;
    }
    case AffectedByChangeType::ALL_CHANGES: {
      toCheckLeaves_[rootId].push_back(leafId);
      break;
    }
    default:
      throw std::runtime_error("unknown type of affectedByChange");
  }
}

void Orchestrator::updateEquivalenceSets(
    EquivalenceSets& equivalenceSets,
    entities::EntityIdType numObjects) const {
  // since we are iterating in post order, when considering a node,
  // equivalenceSets has already been updated by its children. One exception to
  // this is StableStayed which uses an object vector that is not its child to
  // compute equivalent sets. This is why we need to pass context to avoid
  // recomputation for such nodes.
  for (auto& node : postOrder_) {
    if (equivalenceSets.size() == numObjects) {
      // no point updating if all the objects are already deemed to be unequal
      break;
    }

    node->updateEquivalenceSets(equivalenceSets);
  }

  equivalenceSets.finalize();

  XLOGF(INFO, "Number of object equivalence sets = {}", equivalenceSets.size());

  if (XLOG_IS_ON(DBG1)) {
    equivalenceSets.print();
  }
}

// partial apply, apply changed nodes and propagating changes to parents
void Orchestrator::apply(Context& context, const Assignment& assignment) const {
  // push changed leaf nodes to context.readyNodes
  computeAllChangedLeaves(context);
  const BottomToTopEvaluator evaluator(context);
  // applying and poping nodes at the top of the priority queue
  // until the priority queue is empty
  while (!context.readyNodes().empty()) {
    Expression* expr = context.readyNodes().top();
    context.readyNodes().pop();
    const auto oldValue = expr->value;
    const auto cached = context.apply().get(expr->getId());
    const auto newValue = cached
        ? *cached
        : expr->partialApply(evaluator, assignment, *context.changes());
    // if the node changes value,
    // propagating the changes to all its parents
    if (oldValue != newValue) {
      context.apply().save(expr->getId(), newValue);
      notifyChange(context, expr);
    }
  }
}

double Orchestrator::evaluate(const Expression* node, Context& context) const {
  const auto& changes = *context.changes();
  // if no leaf nodes pushed to the readyNodes yet,
  // push the first node's leaf nodes to readyNodes
  if (context.evaluatedRoots == -1) {
    computeChangedLeaves(context);
  }
  // push the leaf nodes from all roots having the higher priority than the node
  // we are evaluating
  const auto& nodePriority = priority(node);
  while (nodePriority < priority(roots_[context.evaluatedRoots])) {
    computeChangedLeaves(context);
  }
  const BottomToTopEvaluator evaluator(context);
  // evaluating and poping nodes at the top of the priority queue
  // until the priority queue is empty or
  // the top node has lower priority than node being evaluated
  while (!context.readyNodes().empty()) {
    const auto expr = context.readyNodes().top();
    if (priority(expr) < nodePriority) {
      break;
    }

    context.readyNodes().pop();
    const auto oldValue = expr->value;
    const auto newValue = expr->evaluate(evaluator, changes);
    if (oldValue != newValue) {
      context.val().save(expr->getId(), newValue);
      notifyChange(context, expr);
    }
    if (expr == node) {
      return newValue;
    }
  }
  // return the newValue if changed. Otherwise, return the oldValue
  return evaluator.evaluate(node, changes);
}

void Orchestrator::buildLpBottomUp(
    const std::vector<ExprPtr>& objectiveNodes,
    const PackerMap<ExprPtr, std::string>& constraintNodesToNames,
    Problem& problem,
    LpContext& context,
    const interface::OptimalSolverSpec& configs) const {
  XLOG(DBG2) << "Orchestrator: building LP bottom-up";
  const LpEvaluator evaluator(context, problem, nodeToPriority_);

  // Compute lp optimization intents top-down for both objective and
  // constraint nodes
  for (const auto& node : objectiveNodes) {
    evaluator.computeLpIntent(node, true /*shouldMinimize*/);
  }
  for (auto& [node, _] : constraintNodesToNames) {
    evaluator.computeLpIntent(node, true /*shouldMinimize*/);
  }

  // Compute lp::Expression for all the relevant nodes bottom-up. Note that
  // topological order imposed in evaluator.optimizationIntents_ ensures that if
  // expr A depends on expr B, then B's lp() function is called before A's lp()
  // function
  auto& lpIntents = evaluator.getOptimizationIntentsInTopologicalOrder();
  folly::coro::blockingWait(
      CoroUtils::runEachFunc(
          lpIntents.begin(),
          lpIntents.end(),
          [&](const auto& it) {
            auto& [expr, intent] = *it;
            evaluator.lp(expr.get(), intent, configs);
            const auto constraintName =
                folly::get_ptr(constraintNodesToNames, expr);
            if (constraintName) {
              const algopt::lp::Expression& lpExpr =
                  context.lpMin().at(expr->getId());
              problem.lp_store.addConstraint(
                  lpExpr <= 0, /*name=*/*constraintName);
            }
            return;
          },
          problem.getExecutorForLpBuilding()));
}

void Orchestrator::buildLp(
    const std::vector<ExprPtr>& objectiveNodes,
    const PackerMap<ExprPtr, std::string>& constraintNodesToNames,
    Problem& problem,
    LpContext& context,
    const interface::OptimalSolverSpec& configs) const {
  return buildLpBottomUp(
      objectiveNodes, constraintNodesToNames, problem, context, configs);
}

void Orchestrator::notifyChange(Context& context, Expression* changedNode)
    const {
  // propagating the changes to its parents
  auto parentsPtr = folly::get_ptr(nodeToParents_, changedNode);
  if (parentsPtr) {
    // check changedNode's parents
    for (auto& parent : *parentsPtr) {
      // if the parent has not been pushed to priority queue yet,
      // push the parent to priority queue
      auto& changedChildren = context.changedChildren()[parent];
      if (changedChildren.empty()) {
        context.readyNodes().emplace(priority(parent), parent);
      }
      // push the child to the parent's changedChildren list
      changedChildren.insert(changedNode);
    }
  }
}

void Orchestrator::computeChangedLeavesByRoot(Context& context, int root)
    const {
  PackerSet<LeafId> leaves;

  auto& readyNodes = context.readyNodes();
  const auto processLeaf = [&](LeafId leafId) {
    if (leaves.insert(leafId).second) {
      auto leaf = leafStore_.at(leafId);
      readyNodes.emplace(priority(leaf), leaf);
    }
  };
  const auto processLeaves = [&](const auto& leafIdRange) {
    for (const auto leafId : leafIdRange) {
      processLeaf(leafId);
    }
  };

  // 1. add leaf nodes affected by all changes
  processLeaves(toCheckLeaves_[root]);

  if (context.changes()) {
    const auto processLeavesIfKeyExists = [&](const auto& keyToLeavesMap,
                                              const auto& key) {
      if (const auto leafIdsPtr = folly::get_ptr(keyToLeavesMap, key)) {
        processLeaves(*leafIdsPtr);
      }
    };
    const auto& changeSet = *context.changes();

    // 2. add leaf nodes affected by specific (container,object)
    const auto& containerObjectToLeaves = containerObjectToLeaves_[root];
    for (const auto& [container, changes] : changeSet.getContainerToChanges()) {
      if (const auto containerObjectToLeavesPtr =
              folly::get_ptr(containerObjectToLeaves, container)) {
        for (auto& change : changes) {
          processLeavesIfKeyExists(
              *containerObjectToLeavesPtr, change.getObject());
        }
      }
    }

    const auto processFilteredLeaves = [&](const std::vector<LeafId>& leafIds,
                                           const std::vector<Change>& changes) {
      for (const auto leafId : leafIds) {
        const auto& filter = leafStore_.filter(leafId);
        if (!filter ||
            std::any_of(changes.begin(), changes.end(), std::cref(filter))) {
          processLeaf(leafId);
        }
      }
    };

    // 3. add leaf nodes affected by changes related to specific container
    const auto& containerToLeaves = containerToLeaves_[root];
    for (const auto container : changeSet.getContainersInChangeSet()) {
      const auto leafIdsPtr = folly::get_ptr(containerToLeaves, container);
      if (leafIdsPtr) {
        processFilteredLeaves(
            *leafIdsPtr, changeSet.getChangesByContainer(container));
      }
    }

    // 4. add leaf nodes affected by changes related to specific object
    const auto& objectToLeaves = objectToLeaves_[root];
    for (const auto object : changeSet.getObjectsInChangeSet()) {
      const auto leafIdsPtr = folly::get_ptr(objectToLeaves, object);
      if (leafIdsPtr) {
        processFilteredLeaves(
            *leafIdsPtr, changeSet.getChangesByObject(object));
      }
    }
  }
}

void Orchestrator::computeAllChangedLeaves(Context& context) const {
  for (const auto i : folly::irange(roots_.size())) {
    computeChangedLeavesByRoot(context, i);
  }
}

void Orchestrator::computeChangedLeaves(Context& context) const {
  ++context.evaluatedRoots;
  computeChangedLeavesByRoot(context, context.evaluatedRoots);
}

folly::F14FastMap<Expression*, folly::F14FastSet<Expression*>>
Orchestrator::getDynamicChildren(
    const PackerSet<entities::ContainerId>& dynamicContainers) const {
  algopt::BucketedPriorityQueue<PriorityInfo, Expression*> dynamicNodes;
  auto pushToDynamicNodes = [&](const auto& leafIds) {
    for (const auto leafId : leafIds) {
      const auto leaf = leafStore_.at(leafId);
      dynamicNodes.emplace(priority(leaf), leaf);
    }
  };

  for (const auto i : folly::irange(roots_.size())) {
    pushToDynamicNodes(toCheckLeaves_[i]);
    // add leaf nodes affected by dynamic containers
    for (auto container : dynamicContainers) {
      const auto containerToLeavesPtr =
          folly::get_ptr(containerToLeaves_[i], container);
      if (containerToLeavesPtr) {
        pushToDynamicNodes(*containerToLeavesPtr);
      }
      auto containerObjectToLeavesPtr =
          folly::get_ptr(containerObjectToLeaves_[i], container);
      if (containerObjectToLeavesPtr) {
        for (auto& [_, objectToLeaves] : *containerObjectToLeavesPtr) {
          pushToDynamicNodes(objectToLeaves);
        }
      }
    }

    // look at the nodes that are affected by objects
    for (const auto& [_, leaves] : objectToLeaves_[i]) {
      for (const auto leafId : leaves) {
        const auto leaf = leafStore_.at(leafId);
        const auto& directlyAffectedContainers =
            leaf->getDirectlyAffectedContainers().getNonNullSet();
        for (const auto container : directlyAffectedContainers) {
          if (dynamicContainers.contains(container)) {
            dynamicNodes.emplace(priority(leaf), leaf);
            break;
          }
        }
      }
    }
  }

  // initialize dynamicChildren
  folly::F14FastMap<Expression*, folly::F14FastSet<Expression*>>
      dynamicChildren;
  while (!dynamicNodes.empty()) {
    Expression* expr = dynamicNodes.top();
    dynamicNodes.pop();
    // propagating the changes to its parents
    auto parentsPtr = folly::get_ptr(nodeToParents_, expr);
    if (parentsPtr) {
      // check dynamicNode's parents
      for (auto& parent : *parentsPtr) {
        // if the parent has not been pushed to priority queue yet,
        // push the parent to priority queue
        if (dynamicChildren[parent].empty()) {
          dynamicNodes.emplace(priority(parent), parent);
        }
        // insert the child to the parent's dynamicChildren list
        dynamicChildren[parent].insert(expr);
      }
    }
  }

  return dynamicChildren;
}

const std::vector<Expression*>& Orchestrator::getNodesInPostorder() const {
  return postOrder_;
}

const folly::F14VectorMap<Expression*, std::vector<Expression*>>&
Orchestrator::getNodeToParents() const {
  return nodeToParents_;
}

} // namespace facebook::rebalancer
