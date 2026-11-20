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

// Correctness tests for native Gurobi operator paths. Each test solves the
// same problem twice — once with the native flag disabled (approximation path)
// and once enabled — and asserts that the resulting objective values are
// numerically identical. Tests are skipped automatically on hosts where Gurobi
// is not available.

#include "algopt/lp/environment/Environment.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/thrift/gen-cpp2/problem_types.h"
#include "algopt/rebalancer/entities/Containers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/entities/Universe.h"
#include "algopt/rebalancer/interface/thrift/gen-cpp2/SolverSpecs_types.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/Operators.h"
#include "algopt/rebalancer/solver/expressions/Orchestrator.h"
#include "algopt/rebalancer/solver/solvers/LPStore.h"
#include "algopt/rebalancer/solver/solvers/OptimalSolver.h"
#include "algopt/rebalancer/solver/tests/ExprProblemCreation.h"
#include "algopt/rebalancer/solver/utils/Assignment.h"
#include "algopt/rebalancer/solver/utils/Problem.h"
#include "algopt/rebalancer/tests/SolverTestUtils.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/irange.h>
#include <folly/container/MapUtil.h>
#include <folly/ExceptionWrapper.h>
#include <folly/ScopeGuard.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/FieldRef.h>

#include <initializer_list>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace algopt = facebook::algopt;

namespace facebook::rebalancer {

namespace {

// ---- Helpers ----------------------------------------------------------------

struct TestUniverse : public entities::tests::UniverseBuilderTestUtils {
  std::shared_ptr<const entities::Universe> build(
      int nObjects,
      int nContainers) {
    entities::Map<std::string, std::vector<std::string>> init;
    for (const auto i : folly::irange(nContainers)) {
      init[fmt::format("container{}", i)] = {};
    }
    for (const auto i : folly::irange(nObjects)) {
      init["container0"].push_back(fmt::format("object{}", i));
    }
    setInitialAssignment(init);
    return buildUniverse();
  }
};

ExprPtr containerLoad(
    const std::shared_ptr<const entities::Universe>& uni,
    const Assignment& assignment,
    int containerIdx,
    int nObjects) {
  auto load = const_expr(0, *uni);
  const auto contId =
      uni->getContainerId(fmt::format("container{}", containerIdx));
  for (const auto i : folly::irange(nObjects)) {
    const auto objId = uni->getObjectId(fmt::format("object{}", i));
    load += variable(objId, contId, *uni, assignment);
  }
  return load;
}

interface::OptimalSolverSpec makeSilentGurobiSpec() {
  interface::OptimalSolverSpec spec;
  *spec.suppressLogs() = true;
  *spec.skipInitialAssignmentHint() = true;
  *spec.solverPackage() = interface::OptimalSolverPackage::GUROBI;
  return spec;
}

// Builds the LP model and solves it; returns the best objective value.
// The native-operator flag must be set before calling this.
double buildAndSolveGurobi(Problem& p) {
  OptimalSolver solver(makeSilentGurobiSpec());
  solver.solve(p);
  const auto result = p.lp_store.getLpProblem().getOnlyResult();
  const auto status = *result.status();
  EXPECT_TRUE(
      status == algopt::lp::thrift::ProblemStatus::OPTIMAL_FOUND ||
      status == algopt::lp::thrift::ProblemStatus::SOLUTION_FOUND)
      << "Gurobi solve produced no incumbent";
  return static_cast<double>(*result.bestObjective());
}

} // namespace

// ---- Test fixture -----------------------------------------------------------

class NativeOperatorGurobiTests : public testing::Test {};

// ---- Square -----------------------------------------------------------------
// Minimize sum of squares of container loads. 10 objects, 2 containers.
// Optimal splits evenly: 5 in each → objective = 5^2 + 5^2 = 50.

TEST_F(NativeOperatorGurobiTests, NativeQuadratic_Square) {
  REBALANCER_SKIP_IF_NO_GUROBI();

  constexpr int kN = 10;

  auto buildSquareProblem = [&]() {
    auto uni = TestUniverse().build(kN, 2);
    const Assignment assignment(uni->getContainers().getInitialAssignment());
    auto obj = square(containerLoad(uni, assignment, 0, kN)) +
        square(containerLoad(uni, assignment, 1, kN));
    return packer::tests::createTestProblem(uni, {obj}, const_expr(0, *uni));
  };

  // Baseline: approximation path.
  algopt::useGurobiNativeQuadratic() = false;
  const double baseline = buildAndSolveGurobi(*buildSquareProblem());

  // Native: direct quadratic expression.
  algopt::useGurobiNativeQuadratic() = true;
  SCOPE_EXIT {
    algopt::useGurobiNativeQuadratic() = false;
  };
  const double native = buildAndSolveGurobi(*buildSquareProblem());

  EXPECT_NEAR(50.0, native, 1e-3);
  EXPECT_NEAR(baseline, native, 1e-4);
}

// ---- Power ------------------------------------------------------------------
// Same as Square but via Power(exponent=2). Same expected objective.

TEST_F(NativeOperatorGurobiTests, NativeQuadratic_Power) {
  REBALANCER_SKIP_IF_NO_GUROBI();

  constexpr int kN = 10;

  auto buildPowerProblem = [&]() {
    auto uni = TestUniverse().build(kN, 2);
    const Assignment assignment(uni->getContainers().getInitialAssignment());
    auto obj = power(containerLoad(uni, assignment, 0, kN), 2) +
        power(containerLoad(uni, assignment, 1, kN), 2);
    return packer::tests::createTestProblem(uni, {obj}, const_expr(0, *uni));
  };

  algopt::useGurobiNativeQuadratic() = false;
  const double baseline = buildAndSolveGurobi(*buildPowerProblem());

  algopt::useGurobiNativeQuadratic() = true;
  SCOPE_EXIT {
    algopt::useGurobiNativeQuadratic() = false;
  };
  const double native = buildAndSolveGurobi(*buildPowerProblem());

  EXPECT_NEAR(50.0, native, 1e-3);
  EXPECT_NEAR(baseline, native, 1e-4);
}

// ---- Piecewise --------------------------------------------------------------
// 11-point PWL approximation of x^2 over [0, 10]. 10 objects, 2 containers.
// Optimal: 5 in each → x=5, PWL(5)=25 for each → objective ≈ 50.

TEST_F(NativeOperatorGurobiTests, NativePwl) {
  REBALANCER_SKIP_IF_NO_GUROBI();

  constexpr int kN = 10;
  auto makePwlPoints = [&]() {
    std::vector<std::pair<double, double>> pts;
    pts.reserve(11);
    for (const auto i : folly::irange(11)) {
      const double x = static_cast<double>(i);
      pts.emplace_back(x, x * x);
    }
    return pts;
  };

  auto buildPwlProblem = [&]() {
    auto uni = TestUniverse().build(kN, 2);
    const Assignment assignment(uni->getContainers().getInitialAssignment());
    const auto points = makePwlPoints();
    auto obj = piecewise(points, containerLoad(uni, assignment, 0, kN)) +
        piecewise(points, containerLoad(uni, assignment, 1, kN));
    return packer::tests::createTestProblem(uni, {obj}, const_expr(0, *uni));
  };

  algopt::useGurobiNativePwl() = false;
  const double baseline = buildAndSolveGurobi(*buildPwlProblem());

  algopt::useGurobiNativePwl() = true;
  SCOPE_EXIT {
    algopt::useGurobiNativePwl() = false;
  };
  const double native = buildAndSolveGurobi(*buildPwlProblem());

  EXPECT_NEAR(50.0, native, 1e-3);
  EXPECT_NEAR(baseline, native, 1e-4);
}

// ---- Max --------------------------------------------------------------------
// Minimize max(load_0, load_1). 10 objects, 2 containers.
// Optimal: 5 in each → max(5, 5) = 5.

TEST_F(NativeOperatorGurobiTests, NativeMax) {
  REBALANCER_SKIP_IF_NO_GUROBI();

  constexpr int kN = 10;

  auto buildMaxProblem = [&]() {
    auto uni = TestUniverse().build(kN, 2);
    const Assignment assignment(uni->getContainers().getInitialAssignment());
    auto obj =
        max({containerLoad(uni, assignment, 0, kN),
             containerLoad(uni, assignment, 1, kN)},
            *uni);
    return packer::tests::createTestProblem(uni, {obj}, const_expr(0, *uni));
  };

  algopt::useGurobiNativeMax() = false;
  const double baseline = buildAndSolveGurobi(*buildMaxProblem());

  algopt::useGurobiNativeMax() = true;
  SCOPE_EXIT {
    algopt::useGurobiNativeMax() = false;
  };
  const double native = buildAndSolveGurobi(*buildMaxProblem());

  EXPECT_NEAR(5.0, native, 1e-3);
  EXPECT_NEAR(baseline, native, 1e-4);
}

// ---- Step (indicator constraints) -------------------------------------------
// Bin-packing: minimize # active containers subject to per-container capacity.
// 20 objects, 5 containers, capacity 5. Optimal = ceil(20/5) = 4.

TEST_F(NativeOperatorGurobiTests, NativeStep) {
  REBALANCER_SKIP_IF_NO_GUROBI();

  constexpr int kStepN = 20;
  constexpr int kStepK = 5;
  constexpr int kStepCap = 5;

  auto buildStepProblem = [&]() {
    auto uni = TestUniverse().build(kStepN, kStepK);
    const Assignment assignment(uni->getContainers().getInitialAssignment());
    auto obj = const_expr(0, *uni);
    std::vector<ExprPtr> excesses;
    for (const auto k : folly::irange(kStepK)) {
      auto load = containerLoad(uni, assignment, k, kStepN);
      obj = obj + step(load);
      excesses.push_back(load - kStepCap);
    }
    auto constraint = max(excesses, *uni);
    return packer::tests::createTestProblem(uni, {obj}, constraint);
  };

  algopt::useGurobiNativeStep() = false;
  const double baseline = buildAndSolveGurobi(*buildStepProblem());

  algopt::useGurobiNativeStep() = true;
  SCOPE_EXIT {
    algopt::useGurobiNativeStep() = false;
  };
  const double native = buildAndSolveGurobi(*buildStepProblem());

  EXPECT_NEAR(4.0, native, 1e-3);
  EXPECT_NEAR(baseline, native, 1e-4);
}

} // namespace facebook::rebalancer
