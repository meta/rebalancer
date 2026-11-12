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

#include "algopt/lp/environment/Environment.h"

#include <gtest/gtest.h>

#if defined(REBALANCER_USE_XPRESS) && !defined(REBALANCER_OSS_BUILD)

#include "algopt/lp/detail/xpress/XpressProblem.h"
#include "algopt/lp/detail/xpress_v2/XpressProblemV2.h"
#include "algopt/lp/detail/xpress_v2/XpressV2Types.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/expressions/Max.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartition.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/solvers/OptimalSolver.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Context.h"
#include "algopt/rebalancer/solver/utils/Problem.h"

#include <fmt/format.h>
#include <folly/coro/BlockingWait.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lp = facebook::algopt::lp;
namespace rb = facebook::rebalancer;

namespace {

struct Capture {
  bool useBcl = false;
  int scalarRows = 0;
  bool useBulk = false;
  bool nativePwl = false;
  bool nativeMax = false;
  std::mutex mutex;
  std::vector<size_t> batches;
  int factoryCalls = 0;
  int indicators = 0;
  int pwls = 0;
  int maxes = 0;
};
struct CaptureRegistry {
  std::mutex mutex;
  std::shared_ptr<Capture> active;
};

CaptureRegistry& captureRegistry() {
  static CaptureRegistry registry;
  return registry;
}

std::shared_ptr<Capture> activeCapture() {
  auto& registry = captureRegistry();
  const std::lock_guard guard(registry.mutex);
  return registry.active;
}

class CaptureScope {
 public:
  explicit CaptureScope(std::shared_ptr<Capture> capture) {
    auto& registry = captureRegistry();
    const std::lock_guard guard(registry.mutex);
    previous_ = std::exchange(registry.active, std::move(capture));
  }
  CaptureScope(const CaptureScope&) = delete;
  CaptureScope& operator=(const CaptureScope&) = delete;
  CaptureScope(CaptureScope&&) = delete;
  CaptureScope& operator=(CaptureScope&&) = delete;
  ~CaptureScope() {
    auto& registry = captureRegistry();
    const std::lock_guard guard(registry.mutex);
    registry.active = std::move(previous_);
  }

 private:
  std::shared_ptr<Capture> previous_;
};

class CapturingBclProblem : public lp::detail::XpressProblem {
 public:
  explicit CapturingBclProblem(std::shared_ptr<Capture> capture)
      : capture_(std::move(capture)) {}

  [[noreturn]] std::vector<std::shared_ptr<lp::ConstraintImpl>> newConstraints(
      const ConstraintRequests&) override {
    throw std::runtime_error("non-native BCL materializer called bulk API");
  }

  std::shared_ptr<lp::ConstraintImpl> newConstraint(
      std::shared_ptr<const lp::RelationImpl> relation,
      const std::string& name) override {
    auto row = XpressProblem::newConstraint(std::move(relation), name);
    ++capture_->scalarRows;
    return row;
  }

 private:
  std::shared_ptr<Capture> capture_;
};

class CapturingProblem : public lp::detail::XpressProblemV2 {
 public:
  explicit CapturingProblem(std::shared_ptr<Capture> capture)
      : capture_(std::move(capture)) {}

  std::vector<std::shared_ptr<lp::ConstraintImpl>> newConstraints(
      const ConstraintRequests& requests) override {
    {
      const std::lock_guard lock(capture_->mutex);
      capture_->batches.push_back(requests.size());
    }
    auto rows = capture_->useBulk ? XpressProblemV2::newConstraints(requests)
                                  : ProblemImpl::newConstraints(requests);
    if (rows.size() != requests.size()) {
      throw std::runtime_error("materializer batch lost handles");
    }
    for (const auto& row : rows) {
      const auto& inequality =
          dynamic_cast<const lp::detail::XpressV2Constraint&>(*row).get();
      if (inequality.getIndex() < 0 || inequality.getProblem() == nullptr) {
        throw std::runtime_error("materializer returned a fake/deleted handle");
      }
    }
    return rows;
  }

  bool supportsNativePwl() const override {
    return capture_->nativePwl;
  }
  bool supportsNativeMax() const override {
    return capture_->nativeMax;
  }
  bool supportsIndicatorConstraints() const override {
    return true;
  }
  bool setIndicatorOnConstraint(
      lp::Constraint& row,
      const lp::Variable& binary,
      int direction) override {
    const auto& inequality =
        dynamic_cast<const lp::detail::XpressV2Constraint&>(*row.get()).get();
    if (inequality.getIndex() < 0 || inequality.getProblem() == nullptr) {
      throw std::runtime_error("indicator observed a fake handle");
    }
    ++capture_->indicators;
    return XpressProblemV2::setIndicatorOnConstraint(row, binary, direction);
  }
  std::optional<lp::Expression> addNativePwlConstraint(
      const lp::Expression& x,
      const std::vector<std::pair<double, double>>& points) override {
    ++capture_->pwls;
    return XpressProblemV2::addNativePwlConstraint(x, points);
  }
  std::optional<lp::Expression> addNativeMaxConstraint(
      const std::vector<lp::Expression>& inputs) override {
    ++capture_->maxes;
    return XpressProblemV2::addNativeMaxConstraint(inputs);
  }

 private:
  std::shared_ptr<Capture> capture_;
};

} // namespace

lp::Problem makeCapturedXpressProblem() asm(
    "__wrap__ZN8facebook6algopt2lp14ProblemFactory17makeXpressProblemEv");
lp::Problem makeCapturedXpressProblem() {
  const auto capture = activeCapture();
  if (!capture) {
    throw std::runtime_error("unexpected unscoped materializer factory call");
  }
  ++capture->factoryCalls;
  if (capture->useBcl) {
    return lp::Problem{std::make_unique<CapturingBclProblem>(capture)};
  }
  return lp::Problem{std::make_unique<CapturingProblem>(capture)};
}

namespace facebook::rebalancer::packer::tests {
namespace {

class ConstraintMaterializerTest
    : public ::testing::Test,
      public entities::tests::UniverseBuilderTestUtils {
 protected:
  std::shared_ptr<Capture> lastCapture_;

  void SetUp() override {
    lp::lazyLoadXpress();
    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"container1", {"object1", "object2"}}, {"container2", {}}});
  }

  double evaluateLp(
      const ExprPtr& expression,
      bool minimizing,
      bool useBulk,
      bool nativePwl = false,
      bool nativeMax = false,
      bool parallel = false,
      bool useBcl = false) {
    lastCapture_ = std::make_shared<Capture>();
    const CaptureScope captureScope(lastCapture_);
    lastCapture_->useBcl = useBcl;
    lastCapture_->useBulk = useBulk;
    lastCapture_->nativePwl = nativePwl;
    lastCapture_->nativeMax = nativeMax;
    const auto universe = getUniversePtr();
    auto objective = minimizing ? expression : -1 * expression;
    ProblemConfigs configs;
    configs.enableParallelizedLpBuilding = parallel;
    auto problem = createTestProblem(
        universe, {objective}, const_expr(0, *universe), {}, configs);
    auto& dynamicContainers = problem->containers;
    const auto dynamicSets =
        problem->getDynamicEquivalentSets(dynamicContainers);
    const auto children =
        problem->getOrchestrator().getDynamicChildren(dynamicContainers);
    LpContext context(dynamicContainers, dynamicSets, children);
    problem->lp_store.reset(
        interface::OptimalSolverPackage::XPRESS, parallel, context);
    problem->getOrchestrator().buildLp({objective}, {}, *problem, context, {});
    auto& lpProblem = problem->lp_store.getLpProblem();
    lpProblem.setObjective(context.lpMin().at(objective->getId()));
    const Assignment fixedAssignment(
        universe->getContainers().getInitialAssignment());
    for (const auto equivSet : dynamicSets) {
      for (const auto container : dynamicContainers) {
        size_t count = 0;
        for (const auto object :
             problem->getEquivalenceSets().getSet(equivSet)) {
          count += fixedAssignment.getContainer(object) == container;
        }
        lpProblem.newConstraint(
            problem->lp_store.getAssignmentVar(equivSet, container) == count,
            "fixed_assignment");
      }
    }
    lpProblem.solve(false);
    EXPECT_EQ(lpProblem.backendName(), "XPRESS");
    EXPECT_EQ(lpProblem.getStatus(), lp::thrift::ProblemStatus::OPTIMAL_FOUND);
    EXPECT_EQ(lastCapture_->factoryCalls, 1);
    EXPECT_TRUE(
        std::all_of(
            lastCapture_->batches.begin(),
            lastCapture_->batches.end(),
            [](size_t size) { return size > 0 && size <= 128; }));
    const auto& saved = minimizing ? context.lpMin() : context.lpNotMin();
    return saved.contains(expression->getId())
        ? saved.at(expression->getId()).getValue()
        : expression->value;
  }
};

TEST_F(ConstraintMaterializerTest, ProductFourRowsMatchScalarForBothIntents) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto expression = product(
      variable(object(1), container(1), universe, assignment),
      variable(object(2), container(1), universe, assignment));
  for (const bool minimizing : {false, true}) {
    EXPECT_NEAR(evaluateLp(expression, minimizing, false), 1, 1e-6);
    EXPECT_NEAR(evaluateLp(expression, minimizing, true), 1, 1e-6);
    EXPECT_NE(
        std::find(
            lastCapture_->batches.begin(), lastCapture_->batches.end(), 4),
        lastCapture_->batches.end());
  }
}

TEST_F(
    ConstraintMaterializerTest,
    ProductNestedStepKeepsImmediateIndicatorHandles) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto child =
      2 * variable(object(1), container(1), universe, assignment) - 1;
  const auto expression = product(step(child), child - 2);
  EXPECT_NEAR(evaluateLp(expression, true, false), -1, 1e-6);
  EXPECT_NEAR(evaluateLp(expression, true, true), -1, 1e-6);
  EXPECT_EQ(lastCapture_->indicators, 2);
  EXPECT_NE(
      std::find(lastCapture_->batches.begin(), lastCapture_->batches.end(), 4),
      lastCapture_->batches.end());
}

TEST_F(ConstraintMaterializerTest, PiecewiseBoundedChunksMatchScalar) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  std::vector<std::pair<double, double>> points;
  points.reserve(131);
  for (int i = 0; i <= 130; ++i) {
    points.emplace_back(i, i);
  }
  const auto expression = piecewise(
      points, 65 * variable(object(1), container(1), universe, assignment));
  EXPECT_NEAR(evaluateLp(expression, true, false), 65, 1e-6);
  EXPECT_NEAR(evaluateLp(expression, true, true), 65, 1e-6);
  EXPECT_EQ(lastCapture_->batches, (std::vector<size_t>{128, 128, 9}));
}

TEST_F(
    ConstraintMaterializerTest,
    PiecewiseNestedDiscontinuityFlushesBeforeStep) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto expression = piecewise(
      {{0, 0}, {0, 5}, {5, 0}},
      5 * variable(object(1), container(1), universe, assignment),
      false);
  EXPECT_NEAR(evaluateLp(expression, true, false), 0, 1e-6);
  EXPECT_NEAR(evaluateLp(expression, true, true), 0, 1e-6);
  EXPECT_GT(lastCapture_->indicators, 0);
}

TEST_F(ConstraintMaterializerTest, NativePiecewiseStillRunsEagerly) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto expression = piecewise(
      {{0, 0}, {1, 1}},
      variable(object(1), container(1), universe, assignment));
  EXPECT_NEAR(evaluateLp(expression, true, true, true), 1, 1e-6);
  EXPECT_EQ(lastCapture_->pwls, 1);
  EXPECT_TRUE(lastCapture_->batches.empty());
}

TEST_F(
    ConstraintMaterializerTest,
    LargePartitionIdentityChunksAndStepBarriers) {
  entities::Map<std::string, std::vector<std::string>> groups;
  for (int i = 0; i < 70; ++i) {
    groups[fmt::format("group{}", i)] = {"object1"};
  }
  folly::coro::blockingWait(addPartition("partition", groups));
  folly::coro::blockingWait(addScope("scope", {{"item", {"container1"}}}));
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  PackerMap<entities::GroupId, double> limits;
  for (int i = 0; i < 70; ++i) {
    limits.emplace(
        groupId(partitionId("partition"), fmt::format("group{}", i)), 0);
  }
  const auto partition = object_partition(
      std::make_shared<const PartitionInfo>(universe, partitionId("partition")),
      dimensionId("object_count"),
      limits);
  const auto makeLookup = [&](ObjectPartitionLookupPenaltyTransform transform) {
    return std::make_shared<ObjectPartitionLookupDefault>(
        partition,
        std::make_shared<PackerSet<entities::ContainerId>>(
            PackerSet<entities::ContainerId>{container(1)}),
        scopeId("scope"),
        scopeItemId(scopeId("scope"), "item"),
        universe,
        assignment,
        PackerMap<entities::GroupId, double>{},
        PackerSet<entities::ObjectId>{},
        std::nullopt,
        transform);
  };
  const auto identity =
      makeLookup(ObjectPartitionLookupPenaltyTransform::IDENTITY);
  for (const bool minimizing : {false, true}) {
    EXPECT_NEAR(evaluateLp(identity, minimizing, false), 70, 1e-5);
    EXPECT_NEAR(evaluateLp(identity, minimizing, true), 70, 1e-5);
    EXPECT_NE(
        std::find(
            lastCapture_->batches.begin(), lastCapture_->batches.end(), 128),
        lastCapture_->batches.end());
  }
  const auto stepExpression =
      makeLookup(ObjectPartitionLookupPenaltyTransform::STEP);
  EXPECT_NEAR(evaluateLp(stepExpression, true, false), 70, 1e-5);
  EXPECT_NEAR(evaluateLp(stepExpression, true, true), 70, 1e-5);
  EXPECT_EQ(lastCapture_->indicators, 140);
  EXPECT_TRUE(
      std::all_of(
          lastCapture_->batches.begin(),
          lastCapture_->batches.end(),
          [](size_t size) { return size == 2; }));
}

TEST_F(
    ConstraintMaterializerTest,
    ParallelGenericMaterializerKeepsEagerScalarStrategy) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto expression = product(
      variable(object(1), container(1), universe, assignment),
      variable(object(2), container(1), universe, assignment));
  EXPECT_NEAR(evaluateLp(expression, true, true, false, false, true), 1, 1e-6);
  EXPECT_TRUE(lastCapture_->batches.empty());
}

TEST_F(
    ConstraintMaterializerTest,
    NonminimizingMaxUsesBoundedCompletedChildRows) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto child = variable(object(1), container(1), universe, assignment);
  std::vector<ExprPtr> children;
  children.reserve(130);
  for (int i = 0; i < 130; ++i) {
    children.push_back(2 * child + 0.1 * i);
  }
  const auto expression = std::make_shared<Max>(children, universe);
  EXPECT_NEAR(evaluateLp(expression, false, false), 14.9, 1e-5);
  EXPECT_NEAR(evaluateLp(expression, false, true), 14.9, 1e-5);
  EXPECT_EQ(lastCapture_->batches, (std::vector<size_t>{128, 3}));
}

TEST_F(
    ConstraintMaterializerTest,
    BasicAssignmentRowsBatchOnlyInSerialLpBuilds) {
  entities::Map<std::string, std::vector<std::string>> initial;
  initial["container2"] = {};
  for (int i = 1; i <= 130; ++i) {
    initial["container1"].push_back(fmt::format("object{}", i));
  }
  entities::tests::UniverseBuilderTestUtils builder;
  builder.setInitialAssignment(initial);
  const auto universe = builder.buildUniverse();
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  auto objective = const_expr(0, *universe);
  for (int i = 1; i <= 130; ++i) {
    objective += i *
        variable(universe->getObjectId(fmt::format("object{}", i)),
                 universe->getContainerId("container1"),
                 *universe,
                 assignment);
  }
  for (const bool parallel : {false, true}) {
    lastCapture_ = std::make_shared<Capture>();
    const CaptureScope captureScope(lastCapture_);
    lastCapture_->useBulk = true;
    ProblemConfigs configs;
    configs.enableParallelizedLpBuilding = parallel;
    auto problem = createTestProblem(
        universe, {objective}, const_expr(0, *universe), {}, configs);
    interface::OptimalSolverSpec spec;
    *spec.solverPackage() = interface::OptimalSolverPackage::XPRESS;
    *spec.simplifyLpProblem() = parallel;
    *spec.skipInitialAssignmentHint() = true;
    *spec.suppressLogs() = true;
    OptimalSolver solver(spec);
    solver.solve(*problem);
    EXPECT_EQ(problem->lp_store.getLpProblem().backendName(), "XPRESS");
    EXPECT_EQ(lastCapture_->factoryCalls, 1);
    if (parallel) {
      EXPECT_TRUE(lastCapture_->batches.empty());
    } else {
      EXPECT_EQ(lastCapture_->batches, (std::vector<size_t>{128, 2}));
    }
    EXPECT_NEAR(
        *problem->lp_store.getLpProblem().getOnlyResult().bestObjective(),
        0,
        1e-6);
  }
}

TEST_F(
    ConstraintMaterializerTest,
    NonNativeBclMaterializerKeepsScalarRowsEager) {
  buildUniverse();
  const auto& universe = getUniverse();
  const Assignment assignment(universe.getContainers().getInitialAssignment());
  const auto child = variable(object(1), container(1), universe, assignment);
  const auto expression =
      product(child, variable(object(2), container(1), universe, assignment));
  EXPECT_NEAR(
      evaluateLp(expression, true, true, false, false, false, true), 1, 1e-6);
  EXPECT_EQ(
      lastCapture_->scalarRows,
      8); // Four operator rows and four fixed assignments.
  EXPECT_TRUE(lastCapture_->batches.empty());
}

TEST_F(ConstraintMaterializerTest, NonNativeBclBasicRowsDoNotUseBulkApi) {
  buildUniverse();
  const auto universe = getUniversePtr();
  const Assignment assignment(universe->getContainers().getInitialAssignment());
  const auto objective =
      variable(object(1), container(1), *universe, assignment);
  auto problem =
      createTestProblem(universe, {objective}, const_expr(0, *universe));
  lastCapture_ = std::make_shared<Capture>();
  const CaptureScope captureScope(lastCapture_);
  lastCapture_->useBcl = true;
  interface::OptimalSolverSpec spec;
  *spec.solverPackage() = interface::OptimalSolverPackage::XPRESS;
  *spec.skipInitialAssignmentHint() = true;
  *spec.suppressLogs() = true;
  OptimalSolver solver(spec);
  solver.solve(*problem);
  EXPECT_FALSE(problem->lp_store.getLpProblem().supportsConstraintBatching());
  EXPECT_EQ(lastCapture_->factoryCalls, 1);
  EXPECT_GT(lastCapture_->scalarRows, 0);
  EXPECT_TRUE(lastCapture_->batches.empty());
  EXPECT_NEAR(
      *problem->lp_store.getLpProblem().getOnlyResult().bestObjective(),
      0,
      1e-6);
}

} // namespace
} // namespace facebook::rebalancer::packer::tests

namespace facebook::rebalancer {

struct LPStoreConstraintCountTestAccess {
  static int reserve(std::atomic<int>& counter, size_t count) {
    return LPStore::reserveConstraintAttempts(counter, count);
  }

  static void initialize(
      LPStore& store,
      std::unique_ptr<algopt::lp::ProblemImpl> backend,
      int count) {
    store.lpProblem_ =
        std::make_unique<algopt::lp::Problem>(std::move(backend));
    store.lpCtrCounter_.store(count, std::memory_order_relaxed);
  }
};

namespace {
class FailingScalarCounterProblem : public algopt::lp::detail::XpressProblemV2 {
 public:
  std::shared_ptr<algopt::lp::ConstraintImpl> newConstraint(
      std::shared_ptr<const algopt::lp::RelationImpl> relation,
      const std::string& name) override {
    ++calls;
    if (calls == failAt) {
      throw std::runtime_error(
          fmt::format(
              "injected scalar counter test failure at request {}", calls));
    }
    auto row = XpressProblemV2::newConstraint(std::move(relation), name);
    ++successes;
    return row;
  }

  std::vector<std::shared_ptr<algopt::lp::ConstraintImpl>> newConstraints(
      const ConstraintRequests& requests) override {
    return ProblemImpl::newConstraints(requests);
  }

  int calls = 0;
  int successes = 0;
  int failAt = 2;
};
} // namespace

TEST(
    LPStoreConstraintCountTest,
    FailedCallsRetainSubmittedAttemptsNotSuccesses) {
  algopt::lp::lazyLoadXpress();
  for (const bool batch : {false, true}) {
    LPStore store;
    auto backend = std::make_unique<FailingScalarCounterProblem>();
    const auto* capture = backend.get();
    LPStoreConstraintCountTestAccess::initialize(store, std::move(backend), 0);
    const auto x = store.makeLpVar(LpVarType::CONTINUOUS);
    const algopt::lp::Problem::ConstraintRequests requests{
        {x >= 0, "first"}, {x <= 1, "failed"}, {x <= 2, "unvisited"}};
    if (batch) {
      EXPECT_THROW(store.addConstraints(requests), std::runtime_error);
    } else {
      store.addConstraint(requests[0].first, requests[0].second);
      EXPECT_THROW(
          store.addConstraint(requests[1].first, requests[1].second),
          std::runtime_error);
    }
    EXPECT_EQ(capture->calls, 2);
    EXPECT_EQ(capture->successes, 1);
    EXPECT_EQ(store.getConstraintCount(), batch ? 3 : 2);
  }
}

TEST(
    LPStoreConstraintCountTest,
    ScalarAndBatchOverflowRejectBeforeBackendCall) {
  algopt::lp::lazyLoadXpress();
  for (const bool batch : {false, true}) {
    LPStore store;
    auto backend = std::make_unique<FailingScalarCounterProblem>();
    backend->failAt = 1;
    const auto* capture = backend.get();
    constexpr int kMaximum = std::numeric_limits<int>::max();
    LPStoreConstraintCountTestAccess::initialize(
        store, std::move(backend), kMaximum);
    const algopt::lp::Relation relation{0, algopt::lp::Relation::EQ_ZERO};
    if (batch) {
      EXPECT_THROW(
          store.addConstraints({{relation, "overflow"}}), std::overflow_error);
    } else {
      EXPECT_THROW(
          store.addConstraint(relation, "overflow"), std::overflow_error);
    }
    EXPECT_EQ(capture->calls, 0);
    EXPECT_EQ(store.getConstraintCount(), kMaximum);
  }
}

TEST(LPStoreConstraintCountTest, MaximumReservationAndTotalOverflow) {
  constexpr int kMaximum = std::numeric_limits<int>::max();
  std::atomic<int> counter{kMaximum - 1};
  EXPECT_EQ(LPStoreConstraintCountTestAccess::reserve(counter, 1), kMaximum);
  EXPECT_THROW(
      LPStoreConstraintCountTestAccess::reserve(counter, 1),
      std::overflow_error);
  EXPECT_EQ(counter.load(), kMaximum);
}

TEST(LPStoreConstraintCountTest, OversizedRequestDoesNotChangeCounter) {
  std::atomic<int> counter{7};
  const size_t count = static_cast<size_t>(std::numeric_limits<int>::max()) + 1;
  EXPECT_THROW(
      LPStoreConstraintCountTestAccess::reserve(counter, count),
      std::overflow_error);
  EXPECT_EQ(counter.load(), 7);
}

TEST(LPStoreConstraintCountTest, InvalidPriorCounterDoesNotWrap) {
  std::atomic<int> counter{-1};
  EXPECT_THROW(
      LPStoreConstraintCountTestAccess::reserve(counter, 1),
      std::overflow_error);
  EXPECT_EQ(counter.load(), -1);
}

} // namespace facebook::rebalancer

#endif // REBALANCER_USE_XPRESS
