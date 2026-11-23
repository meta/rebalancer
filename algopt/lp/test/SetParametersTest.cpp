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

// Guards getSetParameters(): after a solve, every numeric setting that differs
// from the library default, read back from Gurobi or Xpress, must be recorded
// with the same value. A parameter set without going through setParameter()
// fails this test.
//
// It reads Xpress controls directly to check what algopt/lp records.
// @lint-ignore-every ASTGREP common/cpp/prefer-opal

#include "algopt/lp/environment/Environment.h" // NOLINT
#include "algopt/lp/factory/ProblemFactory.h"
#include "algopt/lp/generic/Operators.h"
#include "algopt/lp/generic/Problem.h"

#ifdef REBALANCER_USE_XPRESS
#include "algopt/lp/detail/xpress_common/XpressParamCodes.h"

#include <xprs.h>
#endif

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <folly/Conv.h>
#include <folly/testing/TestUtil.h>
#include <gtest/gtest.h>

#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace facebook::algopt::lp::tests {
namespace {

using Parameters = std::map<std::string, double>;

// Solves a small two-objective MIP that sets parameters every way the
// backends do: tolerances, caller overrides, a time limit, a warm start and a
// per-objective parameter.
Problem solveRepresentativeProblem(
    const std::function<Problem()>& factory,
    const Parameters& overrides,
    const std::string& perObjectiveParam) {
  Problem problem = factory();
  problem.setTolerances(Tolerances{.constraint = 1e-7, .integer = 1e-6});
  for (const auto& [name, value] : overrides) {
    problem.setParameter(name, value);
  }
  problem.setMaxSolveTime(60);
  const auto x = problem.makeIntVar("x");
  const auto y = problem.makeIntVar("y");
  problem.newConstraint(x + y >= 2);
  problem.newConstraint(x <= 10);
  problem.newConstraint(y <= 10);
  problem.addStartValue(x, 1);
  problem.addStartValue(y, 1);
  common::thrift::PerObjectiveValue perObjective;
  perObjective.defaultValue() = 1e-3;
  perObjective.objectiveIndexToValue() = {{1, 1e-2}};
  MultiObjConfig config;
  config.paramNamesToValues[perObjectiveParam] = perObjective;
  problem.setMultiObjConfig(config);
  problem.setObjective({x, y});
  problem.solve();
  EXPECT_EQ(problem.getStatus(), thrift::ProblemStatus::OPTIMAL_FOUND);
  return problem;
}

// Every non-default setting the solver reports must be recorded with its
// value, and every recorded setting the solver can report must hold that
// value. `unreadable` lists recorded names getParameter() cannot read back.
void expectRecordedMatchesSolver(
    Problem& problem,
    const Parameters& nonDefault,
    const std::set<std::string>& unreadable) {
  const auto recorded = problem.getSetParameters();
  std::vector<std::string> notRecorded;
  for (const auto& [name, value] : nonDefault) {
    const auto it = recorded.find(name);
    if (it == recorded.end()) {
      notRecorded.push_back(fmt::format("{}={}", name, value));
      continue;
    }
    EXPECT_DOUBLE_EQ(it->second, value) << name;
  }
  EXPECT_TRUE(notRecorded.empty())
      << "Set without setParameter(): "
      << fmt::format("{}", fmt::join(notRecorded, ", "));
  for (const auto& [name, value] : recorded) {
    if (name.find('[') != std::string::npos || unreadable.contains(name)) {
      continue;
    }
    EXPECT_DOUBLE_EQ(problem.getParameter(name), value) << name;
  }
}

#ifdef REBALANCER_USE_GUROBI

// Gurobi writes the parameters that differ from their defaults to a .prm file.
Parameters readGurobiNonDefaultParameters(Problem& problem) {
  const folly::test::TemporaryDirectory dir;
  const auto path = (dir.path() / "params.prm").string();
  problem.saveToFile(path);
  Parameters params;
  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line)) {
    std::istringstream fields(line);
    std::string name;
    std::string value;
    if (!(fields >> name >> value) || name.starts_with("#")) {
      continue;
    }
    if (const auto parsed = folly::tryTo<double>(value)) {
      // Hidden parameters keep their full name; the others get the GRB_
      // prefix setParameter() takes.
      params.emplace(
          name.starts_with("GURO_PAR_") ? name : "GRB_" + name, *parsed);
    }
  }
  return params;
}

TEST(SetParametersTest, GurobiRecordsEverySetting) {
  auto problem = solveRepresentativeProblem(
      ProblemFactory::makeGurobiProblem,
      {{"GRB_Aggregate", 0},
       {"GRB_Threads", 1},
       {"GURO_PAR_PREPROBE", 0},
       {"GUROBI_WARM_START_OVERRIDE", 2}},
      "GRB_MIPGap");

  const auto nonDefault = readGurobiNonDefaultParameters(problem);
  EXPECT_EQ(nonDefault.at("GRB_TimeLimit"), 60);
  expectRecordedMatchesSolver(
      problem, nonDefault, {"GURO_PAR_PREPROBE", "GUROBI_WARM_START_OVERRIDE"});

  const auto recorded = problem.getSetParameters();
  EXPECT_EQ(recorded.at("GRB_MIPGap[obj0]"), 1e-3);
  EXPECT_EQ(recorded.at("GRB_MIPGap[obj1]"), 1e-2);
  EXPECT_EQ(recorded.at("GUROBI_WARM_START_OVERRIDE"), 2);
  EXPECT_EQ(recorded.at("GURO_PAR_PREPROBE"), 0);
  EXPECT_FALSE(problem.getSolverVersion().empty());
}

TEST(SetParametersTest, GenericProblemReportsRealizedParameters) {
  auto problem = solveRepresentativeProblem(
      [] {
        return ProblemFactory::makeSimplifiedProblem(
            ProblemFactory::makeGurobiProblem);
      },
      {{"GRB_Aggregate", 0}},
      "GRB_MIPGap");
  const auto recorded = problem.getSetParameters();
  EXPECT_EQ(recorded.at("GRB_Aggregate"), 0);
  EXPECT_EQ(recorded.at("GRB_TimeLimit"), 60);
  EXPECT_FALSE(problem.getSolverVersion().empty());
}

#endif

#ifdef REBALANCER_USE_XPRESS

// Controls the Xpress BCL layer sets itself, to fixed values, and so cannot go
// through setParameter(): XPRBnewprob() sets XPRS_MIPLOG, and loading the
// matrix for a solve sets the rest.
const Parameters kBclControls = {
    {"XPRS_BIGM", 1},
    {"XPRS_KEEPBASIS", 1},
    {"XPRS_MIPABSCUTOFF", 1e40},
    {"XPRS_MIPLOG", 2},
    {"XPRS_PENALTY", 1},
};

// Compares every control with a newly created problem's.
Parameters readXpressNonDefaultControls(Problem& problem) {
  XPRSprob fresh = nullptr;
  if (XPRScreateprob(&fresh) != 0 || fresh == nullptr) {
    ADD_FAILURE() << "Could not create an Xpress problem";
    return {};
  }
  Parameters controls;
  for (const auto name : detail::xpress_params::getParamNames()) {
    const std::string nameStr(name);
    double defaultValue = 0;
    if (const auto intCode = detail::xpress_params::getIntParamCode(nameStr)) {
      int value = 0;
      if (XPRSgetintcontrol(fresh, *intCode, &value) != 0) {
        continue;
      }
      defaultValue = value;
    } else if (
        const auto doubleCode =
            detail::xpress_params::getDoubleParamCode(nameStr)) {
      if (XPRSgetdblcontrol(fresh, *doubleCode, &defaultValue) != 0) {
        continue;
      }
    }
    const double value = problem.getParameter(nameStr);
    if (value != defaultValue) {
      controls.emplace(nameStr, value);
    }
  }
  XPRSdestroyprob(fresh);
  return controls;
}

TEST(SetParametersTest, XpressRecordsEverySetting) {
  auto problem = solveRepresentativeProblem(
      ProblemFactory::makeXpressProblem,
      {{"XPRS_HEUREMPHASIS", 0}, {"XPRS_THREADS", 1}},
      "XPRS_MIPRELSTOP");

  auto nonDefault = readXpressNonDefaultControls(problem);
  for (const auto& [name, value] : kBclControls) {
    EXPECT_DOUBLE_EQ(nonDefault.at(name), value) << name;
    nonDefault.erase(name);
  }
  // Each objective gets what is left of the time limit.
  EXPECT_TRUE(nonDefault.contains("XPRS_TIMELIMIT"));
  expectRecordedMatchesSolver(problem, nonDefault, {});
  EXPECT_FALSE(problem.getSolverVersion().empty());
}

TEST(SetParametersTest, XpressV2RecordsEverySetting) {
  auto problem = solveRepresentativeProblem(
      ProblemFactory::makeXpressV2Problem,
      {{"XPRS_HEUREMPHASIS", 0}, {"XPRS_THREADS", 1}},
      "XPRS_MIPRELSTOP");

  const auto nonDefault = readXpressNonDefaultControls(problem);
  // Each objective gets what is left of the time limit.
  EXPECT_TRUE(nonDefault.contains("XPRS_TIMELIMIT"));
  expectRecordedMatchesSolver(problem, nonDefault, {});
  EXPECT_FALSE(problem.getSolverVersion().empty());
}

#endif

} // namespace
} // namespace facebook::algopt::lp::tests
