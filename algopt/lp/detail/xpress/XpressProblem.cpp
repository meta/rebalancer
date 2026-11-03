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

#include "algopt/lp/detail/xpress/XpressProblem.h"

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress/XpressConstraint.h"
#include "algopt/lp/detail/xpress/XpressExpression.h"
#include "algopt/lp/detail/xpress/XpressRelation.h"
#include "algopt/lp/detail/xpress/XpressVariable.h"
#include "algopt/lp/detail/xpress_common/XpressParamCodes.h"
#include "algopt/lp/generic/SolverError.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"
#include "algopt/rebalancer/algopt_common/Timer.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/logging/xlog.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

// Throws SolverError if rc != 0, appending the Xpress error string.
// Centralises the repeated XPRSgetlasterror + throw pattern across all new
// native-constraint API calls.
static void throwIfXpressError(int rc, XPRSprob xprob, const std::string& msg) {
  if (rc == 0) {
    return;
  }
  char errBuf[512] = {};
  XPRSgetlasterror(xprob, errBuf);
  throw facebook::algopt::lp::SolverError(
      fmt::format("{} (rc={}): {}", msg, rc, errBuf));
}

} // namespace

namespace facebook::algopt::lp::detail {

XpressProblem::XpressProblem() {
  // Forcing search to continue until optimality using default tolerances
  setTolerances(Tolerances{});
  const int scaling = getParameter("XPRS_SCALING");
  setParameter(
      "XPRS_SCALING", scaling | XPRS_SCALING_SIMPLEX_OBJECTIVE_SCALING);
}

/*
XPRB.PL continuous
XPRB.BV binary
XPRB.UI general integer
XPRB.PI partial integer
XPRB.SC semi-continuous
XPRB.SI semi-continuous integer
*/

std::shared_ptr<VariableImpl> XpressProblem::makeVar(const std::string& name) {
  return makeVar(name, XPRB_PL, -XPRB_INFINITY);
}

std::shared_ptr<VariableImpl> XpressProblem::makeIntVar(
    const std::string& name) {
  return makeVar(name, XPRB_UI, -XPRB_INFINITY);
}

std::shared_ptr<VariableImpl> XpressProblem::makeSemiContVar(
    const std::string& name,
    double threshold) {
  auto variable = makeVar(name, XPRB_SC, 0);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> XpressProblem::makeSemiIntVar(
    const std::string& name,
    double threshold) {
  auto variable = makeVar(name, XPRB_SI, 0);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> XpressProblem::makeBoolVar(
    const std::string& name) {
  return makeVar(name, XPRB_BV, 0);
}

std::shared_ptr<ExpressionImpl> XpressProblem::makeExpression(
    double constant) const {
  return std::make_shared<XpressExpression>(
      dashoptimization::XPRBexpr(constant), constant);
}

std::shared_ptr<VariableImpl>
XpressProblem::makeVar(const std::string& name, int xprbType, double lb) {
  auto variable =
      problem_.newVar(name.empty() ? nullptr : name.c_str(), xprbType, lb);
  auto getVarType = [](int xprbType) {
    switch (xprbType) {
      case XPRB_PL:
        return VariableImpl::Type::CONTINUOUS;
      case XPRB_UI:
        return VariableImpl::Type::INTEGER;
      case XPRB_SC:
        return VariableImpl::Type::SEMI_CONTINUOUS;
      case XPRB_SI:
        return VariableImpl::Type::SEMI_INTEGER;
      case XPRB_BV:
        return VariableImpl::Type::BINARY;
      default:
        throw SolverError(
            fmt::format("unknown xpress variable type {}", xprbType));
    }
  };
  modifiedSinceSolve_ = true;
  return std::make_shared<XpressVariable>(variable, getVarType(xprbType), this);
}

std::shared_ptr<ConstraintImpl> XpressProblem::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) {
  auto constraint = problem_.newCtr(
      name.c_str(), dynamic_cast<const XpressRelation&>(*relation).get());
  modifiedSinceSolve_ = true;
  return std::make_shared<XpressConstraint>(constraint, this);
}

void XpressProblem::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) {
  auto& xpressConstraint = dynamic_cast<XpressConstraint&>(*constraint);
  problem_.delCtr(xpressConstraint.get());
  xpressConstraint.markDeleted();
  modifiedSinceSolve_ = true;
}

int XpressProblem::getObjectiveSize() const {
  return objectives_.size();
}

std::shared_ptr<const ExpressionImpl> XpressProblem::getObjectiveAt(
    int pos) const {
  if (pos >= getObjectiveSize()) {
    throw SolverError(
        fmt::format(
            "There are only {} objectives, but attempt to access objective at pos {}",
            getObjectiveSize(),
            pos));
  }
  return objectives_.at(pos);
}

void XpressProblem::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  objectives_.push_back(expression);
  modifiedSinceSolve_ = true;
}

void XpressProblem::clearObjectives() {
  objectives_.clear();
  // also clear all saved results w.r.t. those objectives
  problemResultPerObjective_.clear();
  modifiedSinceSolve_ = true;
}

void XpressProblem::setObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  problem_.setObj(dynamic_cast<const XpressExpression&>(*expression).get());
}

void XpressProblem::setTolerances(const Tolerances& tol) {
  setParameter("XPRS_FEASTOL", tol.constraint);
  setParameter("XPRS_MIPTOL", tol.integer);
  setParameter("XPRS_MIPRELSTOP", tol.relgap);
  setParameter("XPRS_MIPABSSTOP", tol.absgap);
  setParameter("XPRS_MIPRELCUTOFF", tol.relcut);
  setParameter("XPRS_MIPADDCUTOFF", tol.abscut);
}

Tolerances XpressProblem::getTolerances() {
  return {
      .constraint = getParameter("XPRS_FEASTOL"),
      .integer = getParameter("XPRS_MIPTOL"),
      .absgap = getParameter("XPRS_MIPABSSTOP"),
      .relgap = getParameter("XPRS_MIPRELSTOP"),
      .abscut = getParameter("XPRS_MIPADDCUTOFF"),
      .relcut = getParameter("XPRS_MIPRELCUTOFF")};
}

int XpressProblem::callbackImpl(XPRSprob& prob) {
  if (callbackAborted_) {
    return 1;
  }
  // lpOptimize() populates neither MIP attribute, so an LP solve reports no
  // progress rather than whatever an earlier solve left behind.
  double bound = -std::numeric_limits<double>::infinity();
  double objective = std::numeric_limits<double>::infinity();
  if (!solvedAsLp_) {
    XPRSgetdblattrib(prob, XPRS_BESTBOUND, &bound);
    XPRSgetdblattrib(prob, XPRS_MIPBESTOBJVAL, &objective);
  }
  if ((*callback_)(
          {.bound = bound,
           .objective = objective,
           .walltime = timer_.getSeconds()}) == ProblemCallbackAction::ABORT) {
    callbackAborted_ = true;
    return 1;
  }
  return 0;
}

/* static */
int XpressProblem::customCallback(XPRSprob prob, void* data) {
  auto xpressProblem = (XpressProblem*)data;
  return xpressProblem->callbackImpl(prob);
}

void XpressProblem::solveForObjectiveAt(
    int pos,
    std::optional<double> timeLimit) {
  if (usesNativeExpressions()) {
    clearNativeConstraints(problem_.getXPRSprob());
  }
  // set objective
  auto objective = getObjectiveAt(pos);
  setObjective(objective);

  // set timeLimit if given
  if (timeLimit.has_value()) {
    setTimeout(timeLimit.value());
  }

  // set the parameter for objective at position pos
  setMultiObjectiveParameter(pos);

  // start the timer as we start solve
  timer_.start();
  // Cleared before anything below can throw, so a failed solve never leaves
  // the accessors trusting an earlier LP solve's state.
  solvedAsLp_ = false;
  modifiedSinceSolve_ = false;

  // Prevent error "problem has no variables" by adding a dummy constraint with
  // a variable.
  problem_.newCtr(nullptr, problem_.newVar() == 0);

  auto lpConfigs = getAlgorithm();
  // Provide initial assignment as a warm-start hint via XPRSaddmipsol.
  // Note: when native PWL/Max constraints are used, the warm-start only covers
  // the original model variables. Auxiliary variables introduced by
  // addNativePwlConstraint/addNativeMaxConstraint (pwl_x, pwl_y, max_result,
  // max_input_*) are absent from the hint; Xpress will complete or discard
  // the partial warm-start at its discretion.
  if (!initialValues_.empty()) {
    XLOG(DBG1) << "Adding warm-start solution from initial values";
    saveDebugData(DebugPhase::PreWarmStart);
    addMipSolFromInitialValues();
    saveDebugData(DebugPhase::PostWarmStart);
    XLOG(DBG1) << "Warm-start solution added";
  }

  XLOG(DBG1) << "Starting main assignment solve";
  callbackAborted_ = false;
  if (callback_) {
    XPRSaddcbchecktime(
        problem_.getXPRSprob(),
        customCallback,
        this, // user defined data
        1 // priority
    );
  }

  // Perform main solve. When native PWL/Max constraints are registered, we
  // must call loadMat() first to get valid column indices, apply the C API
  // constraints (XPRSaddpwlcons / XPRSaddgencons), then call XPRSmipoptimize
  // directly — bypassing BCL's mipOptimize which would call loadMat() again
  // and wipe the C API additions.
  saveDebugData(DebugPhase::PreMain);
  if (usesNativeExpressions()) {
    problem_.loadMat();
    applyNativeConstraints(problem_.getXPRSprob());
    XPRSprob xprob = problem_.getXPRSprob();
    throwIfXpressError(
        XPRSmipoptimize(xprob, lpConfigs.c_str()),
        xprob,
        "XPRSmipoptimize failed");
    // XPRBgetsol reads BCL's internal solution cache, which is only populated
    // by BCL's own solve path. After a direct XPRSmipoptimize call, we must
    // explicitly load the Xpress MIP solution into BCL so XPRBgetsol works.
    int mipStatus = XPRS_MIP_INFEAS;
    throwIfXpressError(
        XPRSgetintattrib(xprob, XPRS_MIPSTATUS, &mipStatus),
        xprob,
        "XPRSgetintattrib(XPRS_MIPSTATUS) failed");
    if (mipStatus == XPRS_MIP_OPTIMAL || mipStatus == XPRS_MIP_SOLUTION) {
      int ncols = 0;
      throwIfXpressError(
          XPRSgetintattrib(xprob, XPRS_COLS, &ncols),
          xprob,
          "XPRSgetintattrib(XPRS_COLS) failed");
      if (ncols > 0) {
        std::vector<double> solValues(ncols, 0.0);
        // XPRSgetmipsol is deprecated since v44.00; XPRSgetsolution returns
        // the same MIP solution for columns [0, ncols).
        throwIfXpressError(
            XPRSgetsolution(xprob, nullptr, solValues.data(), 0, ncols - 1),
            xprob,
            fmt::format("XPRSgetsolution failed (ncols={})", ncols));
        const bool isOptimal = (mipStatus == XPRS_MIP_OPTIMAL);
        problem_.loadMIPSol(solValues.data(), ncols, isOptimal);
      }
    } else {
      // No incumbent found (infeasible, time-limit, unbounded, etc.).
      // BCL's solution cache is not updated — callers must check
      // XPRS_MIPSTATUS or the solver result status directly rather than
      // relying on BCL's XPRBgetsol for outcome detection.
      XLOG(DBG1) << fmt::format(
          "Native-constraint solve: no incumbent (XPRS_MIPSTATUS={}); "
          "BCL solution cache left unsynced",
          mipStatus);
    }
  } else if (!initialValues_.empty()) {
    // A start value is a MIP-style warm start that lpOptimize() would silently
    // discard. Skipping the loadMat() below also keeps BCL from reloading the
    // matrix over the XPRSaddmipsol() hint.
    problem_.mipOptimize(lpConfigs.c_str());
  } else {
    // BCL only populates duals and reduced costs on the lpOptimize() path --
    // under mipOptimize() it reports every dual as 0. XPRS_MIPENTS counts
    // integer, binary, partial-integer and semi-continuous entities;
    // XPRS_SETS counts SOS sets; usesNativeExpressions() covers PWL and
    // general constraints and is already false in this branch. Quadratic
    // models stay on mipOptimize(), the only path that reaches the global
    // solver for a non-convex one.
    problem_.loadMat();
    XPRSprob xprob = problem_.getXPRSprob();
    int mipEnts = 0;
    int sets = 0;
    int qElems = 0;
    int qConstraints = 0;
    throwIfXpressError(
        XPRSgetintattrib(xprob, XPRS_MIPENTS, &mipEnts),
        xprob,
        "XPRSgetintattrib(XPRS_MIPENTS) failed");
    throwIfXpressError(
        XPRSgetintattrib(xprob, XPRS_SETS, &sets),
        xprob,
        "XPRSgetintattrib(XPRS_SETS) failed");
    throwIfXpressError(
        XPRSgetintattrib(xprob, XPRS_QELEMS, &qElems),
        xprob,
        "XPRSgetintattrib(XPRS_QELEMS) failed");
    throwIfXpressError(
        XPRSgetintattrib(xprob, XPRS_QCONSTRAINTS, &qConstraints),
        xprob,
        "XPRSgetintattrib(XPRS_QCONSTRAINTS) failed");
    solvedAsLp_ = mipEnts == 0 && sets == 0 && qElems == 0 && qConstraints == 0;
    if (solvedAsLp_) {
      // "c" means "continue the global search" to mipOptimize() and is not an
      // lpOptimize() flag; "" is lpOptimize()'s automatic choice.
      problem_.lpOptimize(lpConfigs == "c" ? "" : lpConfigs.c_str());
    } else {
      problem_.mipOptimize(lpConfigs.c_str());
    }
  }
  saveDebugData(DebugPhase::PostMain);

  timer_.stop();

  if (callback_) {
    XPRSremovecbchecktime(
        problem_.getXPRSprob(),
        customCallback,
        this // user defined data
    );
  }
  XLOG(DBG1) << "Completed main assignment solve";
}

std::string XpressProblem::getAlgorithm() {
  std::string lpConfigs;
  const int32_t algorithm = getParameter("XPRS_DEFAULTALG");
  switch (algorithm) {
    case 1:
      lpConfigs = "c"; // automatic
      break;
    case 2:
      lpConfigs = "d"; // dual simplex
      break;
    case 3:
      lpConfigs = "p"; // primal simplex
      break;
    case 4:
      lpConfigs = "b"; // newton-barrier
      break;
    default:
      lpConfigs = "c"; // automatic
  }
  return lpConfigs;
}

thrift::ProblemStatus XpressProblem::getSolveStatus() {
  int status;
  if (solvedAsLp_) {
    // XPRS_MIPSTATUS is not populated by an lpOptimize(), so reading it here
    // would classify every LP solve from a stale value.
    XPRSgetintattrib(problem_.getXPRSprob(), XPRS_LPSTATUS, &status);
    switch (status) {
      case XPRS_LP_OPTIMAL:
        return thrift::ProblemStatus::OPTIMAL_FOUND;
      case XPRS_LP_UNFINISHED: {
        // SOLSTATUS describes XPRSoptimize(), not the lpOptimize() path.
        // An interrupted LP can still have a primal-feasible basis.
        int primalInfeasibilities = 0;
        XPRSprob xprob = problem_.getXPRSprob();
        throwIfXpressError(
            XPRSgetintattrib(xprob, XPRS_PRIMALINFEAS, &primalInfeasibilities),
            xprob,
            "XPRSgetintattrib(XPRS_PRIMALINFEAS) failed");
        return primalInfeasibilities == 0
            ? thrift::ProblemStatus::SOLUTION_FOUND
            : thrift::ProblemStatus::SOLUTION_NOT_FOUND;
      }
      case XPRS_LP_INFEAS:
      case XPRS_LP_UNBOUNDED:
        return thrift::ProblemStatus::NO_SOLUTION_EXISTS;
      default:
        return thrift::ProblemStatus::SOLUTION_NOT_FOUND;
    }
  }
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_MIPSTATUS, &status);
  switch (status) {
    case XPRS_MIP_OPTIMAL:
      return thrift::ProblemStatus::OPTIMAL_FOUND;
    case XPRS_MIP_SOLUTION:
      return thrift::ProblemStatus::SOLUTION_FOUND;
    case XPRS_MIP_INFEAS:
    case XPRS_MIP_UNBOUNDED:
      return thrift::ProblemStatus::NO_SOLUTION_EXISTS;
    // XPRS_MIP_LP_OPTIMAL: Global search incomplete - the initial continuous
    // relaxation has been solved and no integer solution has been found.
    case XPRS_MIP_LP_OPTIMAL:
    case XPRS_MIP_NO_SOL_FOUND:
    case XPRS_MIP_LP_NOT_OPTIMAL:
      return thrift::ProblemStatus::SOLUTION_NOT_FOUND;
    default:
      throw SolverError(fmt::format("unknown xpress status code {}", status));
  }
}

thrift::ProblemResult XpressProblem::getSolveResult() {
  thrift::ProblemResult result;
  result.status() = getSolveStatus();
  if (warmStartResult_.has_value()) {
    result.warmStartResult() = warmStartResult_.value();
  }

  double bestBound;
  double bestSolution;
  if (solvedAsLp_) {
    // No branch-and-bound tree, so XPRS_BESTBOUND / XPRS_MIPBESTOBJVAL are not
    // populated. An optimal LP is its own bound, which makes the gap exactly 0;
    // an LP stopped short of optimality has proved no bound at all, and
    // XPRS_LPOBJVAL means nothing unless a feasible point exists. Xpress's own
    // finite infinities keep the result serializable, as on the MIP path.
    const thrift::ProblemStatus status = *result.status();
    bestSolution = XPRS_PLUSINFINITY;
    if (status == thrift::ProblemStatus::OPTIMAL_FOUND ||
        status == thrift::ProblemStatus::SOLUTION_FOUND) {
      XPRSgetdblattrib(problem_.getXPRSprob(), XPRS_LPOBJVAL, &bestSolution);
    }
    bestBound = status == thrift::ProblemStatus::OPTIMAL_FOUND
        ? bestSolution
        : XPRS_MINUSINFINITY;
  } else {
    XPRSgetdblattrib(problem_.getXPRSprob(), XPRS_BESTBOUND, &bestBound);
    XPRSgetdblattrib(problem_.getXPRSprob(), XPRS_MIPBESTOBJVAL, &bestSolution);
  }

  result.bestBound() = bestBound;
  result.bestObjective() = bestSolution;
  result.absoluteGap() = bestSolution - bestBound;
  result.problemAttributes() = getProblemAttributes();
  // as defined in https://fburl.com/iigtjzpf xpress library
  const double denominator =
      std::max(std::abs(bestBound), std::abs(bestSolution));
  if (denominator == 0) {
    result.relativeGap() = 0;
  } else {
    result.relativeGap() = std::abs(bestSolution - bestBound) / denominator;
  }
  return result;
}

thrift::ProblemAttributes XpressProblem::getProblemAttributes() {
  int xprsCols;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_COLS, &xprsCols);
  int xprsRows;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_ROWS, &xprsRows);
  int xprsElems;
  XPRSgetintattrib(problem_.getXPRSprob(), XPRS_ELEMS, &xprsElems);

  thrift::ProblemAttributes problemAttributes;
  // Xpress adds a dummy row, col and non-zero entry internally
  problemAttributes.numVariables() = xprsCols > 0 ? xprsCols - 1 : xprsCols;
  problemAttributes.numConstraints() = xprsRows > 0 ? xprsRows - 1 : xprsRows;
  problemAttributes.numOfNonZeros() = xprsElems > 0 ? xprsElems - 1 : xprsElems;
  // xpress does not seem to have the notion of`modelFingerprint`
  return problemAttributes;
}

double XpressProblem::getParameter(const std::string& name) {
  const auto intParam = getIntParamCode(name);
  if (intParam) {
    int value;
    if (XPRSgetintcontrol(problem_.getXPRSprob(), *intParam, &value)) {
      throw SolverError(
          fmt::format("could not get int parameter {} {}", name, *intParam));
    }
    return value;
  }
  const auto doubleParam = getDoubleParamCode(name);
  if (doubleParam) {
    double value;
    if (XPRSgetdblcontrol(problem_.getXPRSprob(), *doubleParam, &value)) {
      throw SolverError(
          fmt::format(
              "could not get double parameter {} {}", name, *doubleParam));
    }
    return value;
  }
  throw SolverError(fmt::format("unknown parameter {}", name));
}

void XpressProblem::setMultiObjectiveParameter(int index) {
  if (multiObjConfig_) {
    for (auto& [key, val] : (multiObjConfig_->paramNamesToValues)) {
      setParameter(
          key, algopt::common::thriftUtils::getObjectiveValue(index, val));
    }
  }
}

void XpressProblem::setParameter(const std::string& name, double value) {
  double oldValue = getParameter(name);
  const auto intParam = getIntParamCode(name);
  if (intParam) {
    if (XPRSsetintcontrol(
            problem_.getXPRSprob(), *intParam, static_cast<int>(value))) {
      throw SolverError(
          fmt::format("could not set int parameter {} {}", name, *intParam));
    }
    XLOG(INFO) << fmt::format(
        "Updating XPRESS param {} from {} to {}", name, oldValue, value);
    return;
  }
  const auto doubleParam = getDoubleParamCode(name);
  if (doubleParam) {
    if (XPRSsetdblcontrol(problem_.getXPRSprob(), *doubleParam, value)) {
      throw SolverError(
          fmt::format(
              "could not set double parameter {} {}", name, *doubleParam));
    }
    XLOG(INFO) << fmt::format(
        "Updating XPRESS param {} from {} to {}", name, oldValue, value);
    return;
  }
  throw SolverError(fmt::format("unknown parameter {}", name));
}

void XpressProblem::setLogfile(const std::string& filename) {
  XPRSsetlogfile(problem_.getXPRSprob(), filename.c_str());
}

void XpressProblem::saveToFile(const std::string& filename) {
  const std::filesystem::path path(filename);
  auto extension = path.extension();
  auto extensionless = path.parent_path() / path.stem();

  if (extension == ".lp") {
    problem_.exportProb(XPRB_LP, extensionless.c_str());
  } else if (extension == ".mps") {
    problem_.exportProb(XPRB_MPS, extensionless.c_str());
  } else if (extension == ".svf") {
    XPRSsaveas(problem_.getXPRSprob(), extensionless.c_str());
  } else {
    throw SolverError(
        "File type not supported, for Xpress, expected .lp/.mps/.svf");
  }
}

void XpressProblem::saveToFileWithObjectiveAt(
    int pos,
    const std::string& filename) {
  setObjective(objectives_.at(pos));
  saveToFile(filename);
}

void XpressProblem::saveToFileWithAllObjectives(const std::string& filename) {
  if (!objectives_.empty()) {
    // Xpress's native model only holds a single objective at a time;
    // customMultiObjectiveIterativeSolve() handles multi-objective by repeated
    // single-objective solves with added constraints. For an export snapshot
    // we install only the highest-priority (first) objective.
    setObjective(objectives_.at(0));
  }
  saveToFile(filename);
}

void XpressProblem::setDebugPath(const std::string& path) {
  debugPath_ = path;
}

void XpressProblem::print(
    const std::vector<std::string>& /*substringsToMatch*/) {
  problem_.print();
}

void XpressProblem::disableLogs() {
  loggingDisabled_ = true;
  setParameter("XPRS_OUTPUTLOG", 0);
}

bool XpressProblem::isLoggingDisabled() const {
  return loggingDisabled_;
}

void XpressProblem::saveDebugData(DebugPhase phase) {
  if (!debugPath_) {
    return;
  }

  std::string solveName;
  switch (phase) {
    case DebugPhase::PreWarmStart:
    case DebugPhase::PostWarmStart:
      solveName = "warm-start";
      break;
    case DebugPhase::PreMain:
    case DebugPhase::PostMain:
      solveName = "main";
      break;
  }

  std::string stage;
  switch (phase) {
    case DebugPhase::PreWarmStart:
    case DebugPhase::PreMain:
      stage = "pre";
      break;
    case DebugPhase::PostWarmStart:
    case DebugPhase::PostMain:
      stage = "post";
      break;
  }

  if (phase == DebugPhase::PreWarmStart || phase == DebugPhase::PreMain) {
    auto filePath = fmt::format("{}/{}", *debugPath_, solveName);
    problem_.exportProb(XPRB_LP, filePath.c_str());
    problem_.exportProb(XPRB_MPS, filePath.c_str());
  }

  {
    auto filePath = fmt::format("{}/{}-{}", *debugPath_, stage, solveName);
    XPRSsaveas(problem_.getXPRSprob(), filePath.c_str());
  }
}

void XpressProblem::setTimeout(double solveTime) {
  setParameter("XPRS_TIMELIMIT", solveTime);
}

void XpressProblem::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) {
  initialValues_[variable] = value;
}

/* static */ thrift::WarmStartStatus XpressProblem::getWarmStartStatus(
    int32_t processingStatus) {
  // refer to https://fburl.com/gqjj8pkw for the different processing
  // status values. See also XPRS_USERSOLSTATUS_* constants in xprs.h.
  thrift::WarmStartStatus warmStartStatus;
  switch (processingStatus) {
    case XPRS_USERSOLSTATUS_NOT_CHECKED:
      warmStartStatus = thrift::WarmStartStatus::PROCESSING_ERROR;
      break;
    case XPRS_USERSOLSTATUS_ACCEPTED_FEASIBLE:
      warmStartStatus = thrift::WarmStartStatus::FEASIBLE_SOLUTION_PROVIDED;
      break;
    case XPRS_USERSOLSTATUS_ACCEPTED_OPTIMIZED:
    case XPRS_USERSOLSTATUS_SEARCHED_SOL:
      warmStartStatus = thrift::WarmStartStatus::FEASIBLE_SOLUTION_OBTAINED;
      break;
    case XPRS_USERSOLSTATUS_SEARCHED_NOSOL:
    case XPRS_USERSOLSTATUS_REJECTED_INFEAS_NOSEARCH:
    case XPRS_USERSOLSTATUS_REJECTED_PARTIAL_NOSEARCH:
    case XPRS_USERSOLSTATUS_REJECTED_FAILED_OPTIMIZE:
    case XPRS_USERSOLSTATUS_REJECTED_CUTOFF:
      warmStartStatus = thrift::WarmStartStatus::FEASIBLE_SOLUTION_NOT_FOUND;
      break;
    case XPRS_USERSOLSTATUS_DROPPED:
      warmStartStatus = thrift::WarmStartStatus::SOLUTION_IS_DROPPED;
      break;
    default:
      throw SolverError(
          fmt::format(
              "Unknown xpress status code {} for warm-start processing",
              processingStatus));
  }

  return warmStartStatus;
}

/* static */ void XpressProblem::callbackForInitialSolution(
    XPRSprob /* xprsProblem */,
    void* xpressProblem,
    const char* solutionName,
    int32_t processingStatus) {
  auto* xpressProblemPtr = static_cast<XpressProblem*>(xpressProblem);
  thrift::WarmStartResult warmResult;
  warmResult.status() = getWarmStartStatus(processingStatus);
  warmResult.processingTimeInSecs() = xpressProblemPtr->timer_.getSeconds();
  xpressProblemPtr->warmStartResult_ = warmResult;

  auto warmStartMsg = fmt::format(
      "Initial solution supplied through warm-start has been processed."
      " Processing Status: {}, Solution Name: {} ",
      processingStatus,
      solutionName);
  if (xpressProblemPtr->isLoggingDisabled()) {
    // if logging was disabled, only print this information in debug mode
    XLOG(DBG) << warmStartMsg;
  } else {
    XLOG(INFO) << warmStartMsg;
  }
}

void XpressProblem::retainNativeColumn(const VariableImpl& variable) {
  if (!nativeColumns_) {
    nativeColumns_ = problem_.newCtr();
    if (!nativeColumns_->isValid() ||
        nativeColumns_->setIncludeVars(true) != 0) {
      throw SolverError("Could not retain native result columns in BCL");
    }
  }
  // Non-binding expressions retain columns without adding mathematical rows.
  const auto& xpressVariable = dynamic_cast<const XpressVariable&>(variable);
  if (nativeColumns_->addTerm(xpressVariable.get()) != 0) {
    throw SolverError("Could not register native result column in BCL");
  }
}

void XpressProblem::clearNativeConstraints(XPRSprob xprob) {
  // Delete our native additions while their indices are still valid, before
  // an objective change or BCL loadMat() discards the native matrix.
  if (nOwnedPwlCons_ > 0) {
    std::vector<int> pwlInd(nOwnedPwlCons_);
    std::iota(pwlInd.begin(), pwlInd.end(), ownedPwlConsStartIdx_);
    const int delRc = XPRSdelpwlcons(xprob, nOwnedPwlCons_, pwlInd.data());
    throwIfXpressError(
        delRc,
        xprob,
        fmt::format(
            "XPRSdelpwlcons failed (nOwnedPwlCons={})", nOwnedPwlCons_));
    nOwnedPwlCons_ = 0;
  }
  if (nOwnedGenCons_ > 0) {
    std::vector<int> genInd(nOwnedGenCons_);
    std::iota(genInd.begin(), genInd.end(), ownedGenConsStartIdx_);
    throwIfXpressError(
        XPRSdelgencons(xprob, nOwnedGenCons_, genInd.data()),
        xprob,
        fmt::format(
            "XPRSdelgencons failed (nOwnedGenCons={})", nOwnedGenCons_));
    nOwnedGenCons_ = 0;
  }
}

void XpressProblem::applyNativeConstraints(XPRSprob xprob) {
  applyNativePwlConstraints(xprob);
  applyNativeMaxConstraints(xprob);
}

void XpressProblem::applyNativePwlConstraints(XPRSprob xprob) {
  // Record the starting index before our additions so clearNativeConstraints
  // can delete exactly [ownedPwlConsStartIdx_, ownedPwlConsStartIdx_ +
  // nOwnedPwlCons_) on the next re-application. This relies on no other code
  // path adding PWL constraints between this add and the subsequent delete.
  throwIfXpressError(
      XPRSgetintattrib(xprob, XPRS_PWLCONS, &ownedPwlConsStartIdx_),
      xprob,
      "XPRSgetintattrib(XPRS_PWLCONS) failed");
  for (const auto& spec : nativePwlConstraints_) {
    const auto* xVar = dynamic_cast<const XpressVariable*>(spec.xVar.get());
    const auto* yVar = dynamic_cast<const XpressVariable*>(spec.yVar.get());
    if (xVar == nullptr || yVar == nullptr) {
      throw SolverError(
          "Native PWL constraint requires XpressVariable operands, but a "
          "non-Xpress VariableImpl was provided");
    }
    int xcol = xVar->get().getColNum();
    int ycol = yVar->get().getColNum();
    const int npoints = static_cast<int>(spec.points.size());
    // CSR-style start offsets: start[0]=0, start[1]=npoints (1 constraint).
    const int start[2] = {0, npoints};
    std::vector<double> xvals, yvals;
    xvals.reserve(npoints);
    yvals.reserve(npoints);
    for (const auto& [xi, yi] : spec.points) {
      xvals.push_back(xi);
      yvals.push_back(yi);
    }
    throwIfXpressError(
        XPRSaddpwlcons(
            xprob, 1, npoints, &xcol, &ycol, start, xvals.data(), yvals.data()),
        xprob,
        fmt::format("XPRSaddpwlcons failed (xcol={}, ycol={})", xcol, ycol));
    ++nOwnedPwlCons_;
  }
}

void XpressProblem::applyNativeMaxConstraints(XPRSprob xprob) {
  // Record the starting index before our additions so clearNativeConstraints
  // can delete exactly these constraints on the next re-application.
  throwIfXpressError(
      XPRSgetintattrib(xprob, XPRS_GENCONS, &ownedGenConsStartIdx_),
      xprob,
      "XPRSgetintattrib(XPRS_GENCONS) failed");
  for (const auto& spec : nativeMaxConstraints_) {
    const auto* resultVar =
        dynamic_cast<const XpressVariable*>(spec.resultVar.get());
    if (resultVar == nullptr) {
      throw SolverError(
          "Native MAX constraint requires an XpressVariable result, but a "
          "non-Xpress VariableImpl was provided");
    }
    const int resultCol = resultVar->get().getColNum();
    std::vector<int> inputCols;
    inputCols.reserve(spec.inputVars.size());
    for (const auto& inputImpl : spec.inputVars) {
      const auto* inputVar =
          dynamic_cast<const XpressVariable*>(inputImpl.get());
      if (inputVar == nullptr) {
        throw SolverError(
            "Native MAX constraint requires XpressVariable inputs, but a "
            "non-Xpress VariableImpl was provided");
      }
      inputCols.push_back(inputVar->get().getColNum());
    }
    // CSR-style column offsets: colstart[0]=0, colstart[1]=nInputs (1
    // constraint).
    const int colstart[2] = {0, static_cast<int>(inputCols.size())};
    const int contype = XPRS_GENCONS_MAX;
    throwIfXpressError(
        XPRSaddgencons(
            xprob,
            1,
            static_cast<int>(inputCols.size()),
            0,
            &contype,
            &resultCol,
            colstart,
            inputCols.data(),
            nullptr,
            nullptr),
        xprob,
        fmt::format(
            "XPRSaddgencons failed (resultCol={}, nInputs={})",
            resultCol,
            inputCols.size()));
    ++nOwnedGenCons_;
  }
}

void XpressProblem::addMipSolFromInitialValues() {
  // Ensure the problem matrix is loaded so variables have column indices
  problem_.loadMat();

  std::vector<double> sol;
  std::vector<int> idxs;
  sol.reserve(initialValues_.size());
  idxs.reserve(initialValues_.size());

  for (const auto& [variable, value] : initialValues_) {
    const auto& xpressVar =
        (dynamic_cast<const XpressVariable*>(variable.get()))->get();
    idxs.push_back(xpressVar.getColNum());
    sol.push_back(value);
  }

  // Add initial solution as a MIP start hint
  XPRSaddmipsol(
      problem_.getXPRSprob(),
      static_cast<int>(sol.size()),
      sol.data(),
      idxs.data(),
      "initial_assignment");

  // Callback to retrieve status when the initial solution is processed
  XPRSaddcbusersolnotify(
      problem_.getXPRSprob(),
      callbackForInitialSolution,
      (void*)this, // user defined data
      0 // priority
  );
}

std::optional<int> XpressProblem::getIntParamCode(const std::string& name) {
  return xpress_params::getIntParamCode(name);
}

std::optional<int> XpressProblem::getDoubleParamCode(const std::string& name) {
  return xpress_params::getDoubleParamCode(name);
}

void XpressProblem::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback) {
  callback_ = callback;
}

std::optional<IIS> XpressProblem::getIIS() {
  // An IIS is diagnostic, so report none rather than failing the solve.
  try {
    XPRSprob xprob = problem_.getXPRSprob();
    int iisStatus = 0;
    // Mode 1 refines the initial infeasible subsystem down to an irreducible
    // one.
    throwIfXpressError(
        XPRSiisfirst(xprob, 1, &iisStatus), xprob, "XPRSiisfirst failed");
    // NO_SOLUTION_EXISTS also covers unbounded models, which have no IIS.
    if (iisStatus != 0) {
      XLOGF(WARNING, "Xpress found no IIS (XPRSiisfirst status {})", iisStatus);
      return std::nullopt;
    }

    int nrows = 0;
    int ncols = 0;
    throwIfXpressError(
        XPRSgetiisdata(
            xprob,
            1,
            &nrows,
            &ncols,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        xprob,
        "XPRSgetiisdata failed");
    std::vector<int> rowind(nrows);
    std::vector<int> colind(ncols);
    std::vector<char> bndtype(ncols);
    // XPRSgetiisdata takes no buffer capacity, so this check cannot prevent an
    // overrun; it only detects, after the fact, that the vectors were sized
    // from a different IIS than the one just read.
    int nrowsRead = 0;
    int ncolsRead = 0;
    throwIfXpressError(
        XPRSgetiisdata(
            xprob,
            1,
            &nrowsRead,
            &ncolsRead,
            rowind.data(),
            colind.data(),
            nullptr,
            bndtype.data(),
            nullptr,
            nullptr,
            nullptr,
            nullptr),
        xprob,
        "XPRSgetiisdata failed");
    if (nrowsRead != nrows || ncolsRead != ncols) {
      throw std::runtime_error(
          fmt::format(
              "XPRSgetiisdata size changed between calls: {}x{} then {}x{}",
              nrows,
              ncols,
              nrowsRead,
              ncolsRead));
    }

    const auto getName = [&](int type, int index) {
      int nbytes = 0;
      throwIfXpressError(
          XPRSgetnamelist(xprob, type, nullptr, 0, &nbytes, index, index),
          xprob,
          "XPRSgetnamelist failed");
      std::string name(nbytes, '\0');
      throwIfXpressError(
          XPRSgetnamelist(
              xprob, type, name.data(), nbytes, &nbytes, index, index),
          xprob,
          "XPRSgetnamelist failed");
      // The buffer holds one null-terminated name.
      name.resize(std::strlen(name.c_str()));
      return name;
    };

    IIS iis;
    iis.constraintIds.reserve(nrows);
    iis.lowerBoundVars.reserve(ncols);
    iis.upperBoundVars.reserve(ncols);
    for (const auto row : rowind) {
      iis.constraintIds.push_back(getName(XPRS_NAMES_ROW, row));
    }
    for (const auto i : folly::irange(ncols)) {
      const auto name = getName(XPRS_NAMES_COLUMN, colind[i]);
      switch (bndtype[i]) {
        case 'L':
          iis.lowerBoundVars.push_back(name);
          break;
        case 'U':
          iis.upperBoundVars.push_back(name);
          break;
        case 'F':
          // A fixed column is pinned from both sides.
          iis.lowerBoundVars.push_back(name);
          iis.upperBoundVars.push_back(name);
          break;
        default:
          // Other codes (e.g. a MIP integrality restriction) are not bound
          // conflicts, so listing them on either side would mislead.
          XLOGF(
              WARNING,
              "Unexpected XPRSgetiisdata bound type '{}' for column {}; "
              "omitting it from the IIS bounds",
              bndtype[i],
              name);
      }
    }
    return iis;
  } catch (const std::exception& e) {
    XLOGF(WARNING, "Xpress could not compute an IIS: {}", e.what());
    return std::nullopt;
  }
}

void XpressProblem::replay(const std::string& /* fileName */) const {
  throw SolverError("read from file is not implemented");
  // Unfortunately, the Xpress C++ API does not seem to provide a direct way to
  // do this
  // https://www.fico.com/fico-xpress-optimization/docs/dms2021-01/bcl/dhtml/Cpp-XPRBprob.html
  // In particular, following could work except it needs a C style struct
  // pointer, so reasonable effort is required to make this work which we will
  // revisit if need be (TODO: @nks)
  //  XPRSreadprob(prob, "problem_from_file", "v")
}

bool XpressProblem::supportsNativeQuadratic() const {
  return algopt::useXpressNativeQuadratic();
}

bool XpressProblem::supportsNativePwl() const {
  return algopt::useXpressNativePwl();
}

bool XpressProblem::supportsNativeMax() const {
  return algopt::useXpressNativeMax();
}

bool XpressProblem::usesNativeExpressions() const {
  return !nativePwlConstraints_.empty() || !nativeMaxConstraints_.empty();
}

bool XpressProblem::supportsIndicatorConstraints() const {
  return algopt::useXpressIndicatorConstraints();
}

bool XpressProblem::setIndicatorOnConstraint(
    Constraint& ctr,
    const Variable& binaryVar,
    int dir) {
  auto* xpressCtr = dynamic_cast<XpressConstraint*>(ctr.get().get());
  const auto* xpressVar =
      dynamic_cast<const XpressVariable*>(binaryVar.get().get());
  if (!xpressCtr || !xpressVar) {
    return false;
  }
  // Indicator constraints require a binary variable (XPRB_BV type with
  // bounds [0, 1]). Reject anything else to prevent silent mis-formulations.
  if (xpressVar->get().getType() != XPRB_BV) {
    throw std::invalid_argument(
        fmt::format(
            "setIndicatorOnConstraint: binaryVar must be a binary variable "
            "(XPRB_BV), got type {}",
            xpressVar->get().getType()));
  }
  // The cross-backend interface uses Gurobi's convention, where dir is the
  // binary value that activates the constraint (0 or 1). BCL instead encodes
  // the direction as +1 ("active when the variable is 1") or -1 ("active when
  // the variable is 0"), and rejects 0 outright with
  // "BCL Error 1575: (setindicator) Unexpected argument value 0". Translate
  // here rather than leaking BCL's encoding into callers.
  if (dir != 0 && dir != 1) {
    throw std::invalid_argument(
        fmt::format(
            "setIndicatorOnConstraint: dir must be 0 or 1, got {}", dir));
  }
  const int bclDir = (dir == 1) ? 1 : -1;
  modifiedSinceSolve_ = true;
  return xpressCtr->get().setIndicator(bclDir, xpressVar->get()) == 0;
}

std::optional<algopt::lp::Expression> XpressProblem::addNativePwlConstraint(
    const algopt::lp::Expression& x,
    const std::vector<std::pair<double, double>>& points) {
  // Create auxiliary input variable x_aux and link it to x via BCL constraint.
  // XPRSaddpwlcons requires the input variable to be within the breakpoint
  // range, so we set explicit bounds. Column indices are invalid until
  // loadMat(); we defer the XPRSaddpwlcons call to the presolve callback.
  if (points.size() < 2) {
    throw std::invalid_argument(
        fmt::format(
            "PWL breakpoints must have at least 2 points, got {}",
            points.size()));
  }
  for (const auto i : folly::irange(size_t{1}, points.size())) {
    if (points[i].first <= points[i - 1].first) {
      throw std::invalid_argument(
          fmt::format(
              "PWL breakpoints must be strictly increasing by x: x[{}]={} <= x[{}]={}",
              i,
              points[i].first,
              i - 1,
              points[i - 1].first));
    }
  }
  auto xAuxImpl = makeVar("pwl_x");
  auto yImpl = makeVar("pwl_y");

  // Bound x_aux to the PWL breakpoint range so Xpress doesn't declare the
  // problem infeasible due to the input variable potentially being
  // out-of-range. The equality constraint x_aux == x (added below) therefore
  // also constrains x itself to this range — unlike the Big-M
  // convex-combination encoding, which clips out-of-range values to the
  // boundary. IMPORTANT: callers must guarantee that x cannot reach a value
  // outside [points.front().x, points.back().x] at any feasible solution.
  // Static expression bounds (from lowerAndUpperBounds) are a necessary but
  // not sufficient check — additional model constraints could push x outside
  // the breakpoint domain at runtime, silently converting a feasible problem
  // to infeasible. Do NOT invoke this path when such runtime exceedance is
  // possible.
  xAuxImpl->setLB(points.front().first);
  xAuxImpl->setUB(points.back().first);

  double yMin = points.front().second;
  double yMax = points.front().second;
  for (const auto& [xi, yi] : points) {
    yMin = std::min(yMin, yi);
    yMax = std::max(yMax, yi);
  }
  yImpl->setLB(yMin);
  yImpl->setUB(yMax);
  retainNativeColumn(*yImpl);

  auto xAuxExprImpl = xAuxImpl->makeExpression(1.0);
  if (const auto xExprImpl = x.get()) {
    auto xNegImpl = xExprImpl->clone();
    xNegImpl->multiply(-1.0);
    xAuxExprImpl->add(xNegImpl);
  } else {
    xAuxExprImpl->add(-x.getConstant());
  }
  newConstraint(xAuxExprImpl->makeEqualZeroRelation(), "pwl_x_link");

  nativePwlConstraints_.push_back({xAuxImpl, yImpl, points});
  return algopt::lp::Expression(yImpl->makeExpression(1.0));
}

std::optional<algopt::lp::Expression> XpressProblem::addNativeMaxConstraint(
    const std::vector<algopt::lp::Expression>& inputs) {
  if (inputs.empty()) {
    throw std::invalid_argument(
        "MAX constraint requires at least 1 input expression");
  }
  // Create auxiliary input variables linked to each input expression, and a
  // result variable. The XPRSaddgencons call is deferred to
  // solveForObjectiveAt() after loadMat() makes column indices valid.
  // makeVar() for a continuous variable defaults to LB=-XPRB_INFINITY, so
  // max_result is already correctly unbounded from below (negative inputs
  // work).
  // TODO: tighten the UB to max(ub_i) across inputs once LP expression bound
  // tracking is available, to strengthen the LP relaxation.
  auto resultImpl = makeVar("max_result");
  retainNativeColumn(*resultImpl);

  std::vector<std::shared_ptr<VariableImpl>> inputVarImpls;
  inputVarImpls.reserve(inputs.size());
  for (const auto i : folly::irange(inputs.size())) {
    auto inputImpl = makeVar(fmt::format("max_input_{}", i));
    auto inputAuxExprImpl = inputImpl->makeExpression(1.0);
    // A pure-constant Expression carries its value in constant_ and leaves
    // expression_ null, so get() must not be dereferenced unconditionally.
    // Link against the constant directly in that case: max_input_i - c == 0.
    if (const auto inputExprImpl = inputs[i].get()) {
      auto inputNegImpl = inputExprImpl->clone();
      inputNegImpl->multiply(-1.0);
      inputAuxExprImpl->add(inputNegImpl);
    } else {
      inputAuxExprImpl->add(-inputs[i].getConstant());
    }
    newConstraint(
        inputAuxExprImpl->makeEqualZeroRelation(),
        fmt::format("max_input_link_{}", i));
    inputVarImpls.push_back(inputImpl);
  }

  nativeMaxConstraints_.push_back({resultImpl, std::move(inputVarImpls)});
  return algopt::lp::Expression(resultImpl->makeExpression(1.0));
}

} // namespace facebook::algopt::lp::detail

#endif
