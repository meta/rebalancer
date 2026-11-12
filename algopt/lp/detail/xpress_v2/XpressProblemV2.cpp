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

#include "algopt/lp/detail/xpress_v2/XpressProblemV2.h"

#ifdef REBALANCER_USE_XPRESS

#include "algopt/lp/detail/xpress_common/XpressParamCodes.h"
#include "algopt/lp/generic/SolverError.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"

#include <fmt/format.h>
#include <folly/container/irange.h>
#include <folly/logging/xlog.h>
#include <folly/Range.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace facebook::algopt::lp::detail {

namespace {

constexpr double kInfinity = 1.0e20;

xpress::ColumnType toColumnType(VariableImpl::Type type) {
  switch (type) {
    case VariableImpl::Type::BINARY:
      return xpress::ColumnType::Binary;
    case VariableImpl::Type::CONTINUOUS:
      return xpress::ColumnType::Continuous;
    case VariableImpl::Type::INTEGER:
      return xpress::ColumnType::Integer;
    case VariableImpl::Type::SEMI_CONTINUOUS:
      return xpress::ColumnType::SemiContinuous;
    case VariableImpl::Type::SEMI_INTEGER:
      return xpress::ColumnType::SemiInteger;
    case VariableImpl::Type::UNSET:
      break;
  }
  throw std::invalid_argument(
      fmt::format(
          "XpressProblemV2: cannot create a variable of type {}",
          static_cast<int>(type)));
}

const XpressV2Expression& asV2Expression(const ExpressionImpl& expression) {
  const auto* casted = dynamic_cast<const XpressV2Expression*>(&expression);
  if (casted == nullptr) {
    throw std::invalid_argument(
        "XpressProblemV2 expected an XpressV2Expression; a foreign "
        "ExpressionImpl cannot be used with this backend");
  }
  return *casted;
}

void queueName(
    int index,
    std::string name,
    int& first,
    std::vector<std::optional<std::string>>& names) {
  if (names.empty()) {
    first = index;
  }
  if (index < first) {
    throw std::logic_error(
        fmt::format("Name index {} precedes {}", index, first));
  }
  names.resize(static_cast<size_t>(index - first) + 1);
  names.at(static_cast<size_t>(index - first)) = std::move(name);
}

} // namespace

XpressV2NameBuffer::XpressV2NameBuffer(xpress::XPRSProblem* problem)
    : problem_{problem} {}

bool XpressV2NameBuffer::canDefer(const std::string& name) const {
  return !exposed_ && name.find('\0') == std::string::npos;
}

void XpressV2NameBuffer::queueColumn(int index, std::string name) {
  queueName(
      index, std::move(name), firstPendingColumnName_, pendingColumnNames_);
}

void XpressV2NameBuffer::queueRow(int index, std::string name) {
  queueName(index, std::move(name), firstPendingRowName_, pendingRowNames_);
}

void XpressV2NameBuffer::flush() {
  if (problem_ == nullptr) {
    return;
  }
  if (!pendingColumnNames_.empty()) {
    problem_->addNames(
        xpress::Namespaces::Column,
        xpress::SizedArray<const std::optional<std::string>>{
            pendingColumnNames_},
        firstPendingColumnName_,
        firstPendingColumnName_ + static_cast<int>(pendingColumnNames_.size()) -
            1);
    pendingColumnNames_.clear();
  }
  if (!pendingRowNames_.empty()) {
    problem_->addNames(
        xpress::Namespaces::Row,
        xpress::SizedArray<const std::optional<std::string>>{pendingRowNames_},
        firstPendingRowName_,
        firstPendingRowName_ + static_cast<int>(pendingRowNames_.size()) - 1);
    pendingRowNames_.clear();
  }
}

void XpressV2NameBuffer::expose() {
  flush();
  exposed_ = true;
}

void XpressV2NameBuffer::detach() {
  problem_ = nullptr;
  exposed_ = true;
  std::vector<std::optional<std::string>>{}.swap(pendingColumnNames_);
  std::vector<std::optional<std::string>>{}.swap(pendingRowNames_);
}

XpressProblemV2::XpressProblemV2() {
  setTolerances(Tolerances{});
  const int scaling = problem_.getIntControl(XPRS_SCALING);
  problem_.setIntControl(
      XPRS_SCALING, scaling | XPRS_SCALING_SIMPLEX_OBJECTIVE_SCALING);
}
XpressProblemV2::~XpressProblemV2() {
  names_->detach();
}

bool XpressProblemV2::hasLpSensitivity() const {
  return solvedAsLp_ && !modifiedSinceSolve_ &&
      problem_.getIntAttrib(XPRS_LPSTATUS) == XPRS_LP_OPTIMAL;
}

void XpressProblemV2::markModified() {
  modifiedSinceSolve_ = true;
}

std::shared_ptr<VariableImpl> XpressProblemV2::makeTypedVar(
    const std::string& name,
    VariableImpl::Type type,
    double lb) {
  // Xpress rejects a binary whose bounds fall outside [0, 1].
  const double ub = type == VariableImpl::Type::BINARY ? 1.0 : kInfinity;
  markModified();
  auto nativeName = uniqueName(name, usedNames_);
  const bool defer = names_->canDefer(name);
  if (!defer) {
    flushNames();
  }
  auto variable = problem_.addVariable(
      lb, ub, toColumnType(type), defer ? std::nullopt : nativeName);
  if (nativeName && *nativeName != name) {
    originalVariableNames_.emplace(*nativeName, name);
  }
  if (defer && nativeName) {
    names_->queueColumn(variable.getIndex(), std::move(*nativeName));
  }
  return std::make_shared<XpressV2Variable>(
      std::move(variable), type, solution_, this, names_);
}

std::optional<std::string> XpressProblemV2::uniqueName(
    const std::string& name,
    folly::F14FastSet<std::string>& usedNames) {
  if (name.empty()) {
    return std::nullopt;
  }
  if (usedNames.insert(name).second) {
    return name;
  }
  size_t suffix = usedNames.size();
  auto candidate = fmt::format("{}_{}", name, suffix);
  while (!usedNames.insert(candidate).second) {
    candidate = fmt::format("{}_{}", name, ++suffix);
  }
  return candidate;
}

std::shared_ptr<VariableImpl> XpressProblemV2::makeVar(
    const std::string& name) {
  // A plain continuous variable is free below zero.
  return makeTypedVar(name, VariableImpl::Type::CONTINUOUS, -kInfinity);
}

std::shared_ptr<VariableImpl> XpressProblemV2::makeIntVar(
    const std::string& name) {
  return makeTypedVar(name, VariableImpl::Type::INTEGER, -kInfinity);
}

std::shared_ptr<VariableImpl> XpressProblemV2::makeBoolVar(
    const std::string& name) {
  return makeTypedVar(name, VariableImpl::Type::BINARY, 0.0);
}

std::shared_ptr<VariableImpl> XpressProblemV2::makeSemiContVar(
    const std::string& name,
    double threshold) {
  auto variable = makeTypedVar(name, VariableImpl::Type::SEMI_CONTINUOUS, 0.0);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<VariableImpl> XpressProblemV2::makeSemiIntVar(
    const std::string& name,
    double threshold) {
  auto variable = makeTypedVar(name, VariableImpl::Type::SEMI_INTEGER, 0.0);
  variable->setThreshold(threshold);
  return variable;
}

std::shared_ptr<ExpressionImpl> XpressProblemV2::makeExpression(
    double constant) const {
  // Use the constant ctor so the expression stays on the linear fast path
  // rather than becoming an opaque general expression.
  return std::make_shared<XpressV2Expression>(constant, solution_, names_);
}

std::shared_ptr<ConstraintImpl> XpressProblemV2::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) {
  const auto* v2Relation =
      dynamic_cast<const XpressV2Relation*>(relation.get());
  if (v2Relation == nullptr) {
    throw std::invalid_argument(
        "XpressProblemV2 expected an XpressV2Relation; a foreign RelationImpl "
        "cannot be used with this backend");
  }
  markModified();
  auto inequality = problem_.addConstraint(v2Relation->makeDefinition());
  applyConstraintName(inequality, name);
  return std::make_shared<XpressV2Constraint>(
      std::move(inequality), this, names_);
}

void XpressProblemV2::applyConstraintName(
    xpress::objects::Inequality& inequality,
    const std::string& name) {
  if (!name.empty()) {
    auto nativeName = *uniqueName(name, usedConstraintNames_);
    if (nativeName != name) {
      originalConstraintNames_.emplace(nativeName, name);
    }
    if (names_->canDefer(nativeName)) {
      names_->queueRow(inequality.getIndex(), std::move(nativeName));
    } else {
      flushNames();
      inequality.setName(nativeName);
    }
  }
}

void XpressProblemV2::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) {
  flushNames();
  const auto* v2Constraint =
      dynamic_cast<const XpressV2Constraint*>(constraint.get());
  if (v2Constraint == nullptr) {
    throw std::invalid_argument(
        "XpressProblemV2 expected an XpressV2Constraint");
  }
  // The typed API exposes deletion for general/SOS/indicator constraints but
  // not for plain rows, so drop to the inherited raw interface for this one.
  const int rowIndex = v2Constraint->getNative().getIndex();
  markModified();
  problem_.delRows(1, &rowIndex);
}

void XpressProblemV2::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  objectives_.push_back(std::move(expression));
  markModified();
}

int XpressProblemV2::getObjectiveSize() const {
  return static_cast<int>(objectives_.size());
}

void XpressProblemV2::clearObjectives() {
  objectives_.clear();
  problemResultPerObjective_.clear();
  markModified();
}

std::shared_ptr<const ExpressionImpl> XpressProblemV2::getObjectiveAt(
    int pos) const {
  if (pos < 0 || pos >= static_cast<int>(objectives_.size())) {
    throw std::out_of_range(
        fmt::format(
            "XpressProblemV2: objective index {} out of range (size {})",
            pos,
            objectives_.size()));
  }
  return objectives_[pos];
}

void XpressProblemV2::flushNames() {
  names_->flush();
}

void XpressProblemV2::solveForObjectiveAt(
    int pos,
    std::optional<double> timeLimit) {
  flushNames();
  // Runs for the whole solve, not just the callback: the warm-start notify
  // callback reads the elapsed time too, and it can fire during addMipSol.
  // Scoped so a throwing solve still stops the timer -- Timer::start() throws
  // if it is already running, and a multi-objective solve gets here once per
  // objective.
  const algopt::TimerScope solveTimer{timer_};

  // Each solve reports its own warm-start outcome or none: the notify callback
  // below only fires when Xpress actually processes a hint, so without this a
  // later solve would inherit an earlier one's result.
  solvedAsLp_ = false;
  modifiedSinceSolve_ = false;
  solution_->clear();
  warmStartResult_.reset();

  problem_.setObjective(
      asV2Expression(*getObjectiveAt(pos)).getNative(),
      xpress::ObjSense::Minimize);

  if (timeLimit.has_value()) {
    // Pass the limit through as-is. A negated limit -- the "stop even without
    // a solution" convention -- is rejected outright by Xpress 9.9 with
    // "Invalid value for control".
    problem_.setDblControl(XPRS_TIMELIMIT, timeLimit.value());
  }

  if (multiObjConfig_) {
    for (const auto& [name, value] : multiObjConfig_->paramNamesToValues) {
      setParameter(
          name, algopt::common::thriftUtils::getObjectiveValue(pos, value));
    }
  }

  // Unconditionally: a notify callback left over from an earlier solve captures
  // `this` and would still fire on an addMipSol from this solve.
  // applyStartValues() re-registers one when there are hints to submit.
  problem_.callbacks.removeUserSolNotifyCallbacks();
  if (!initialValues_.empty()) {
    saveDebugData(DebugPhase::PreWarmStart);
    applyStartValues();
    saveDebugData(DebugPhase::PostWarmStart);
  }

  callbackAborted_ = false;
  // Unconditionally, before the re-registration below: setCallback() may have
  // cleared callback_ since the last solve, and a registration left over from
  // then would call an empty std::function from inside the solve.
  problem_.callbacks.removeCheckTimeCallbacks();
  if (callback_) {
    // Register through the typed API rather than XPRSaddcbchecktime directly.
    // The C++ layer keeps its own callback registry (XPRScxxinit installs
    // callbackAdded/callbackRemoved hooks for exactly this), and registering a
    // raw callback behind its back leaves that registry inconsistent, which
    // shows up later as heap corruption rather than an error at the call site.
    problem_.callbacks.addCheckTimeCallback(
        [this](xpress::objects::XpressProblem& prob) {
          return callbackImpl(prob);
        },
        /*prio=*/1);
  }

  solvedAsLp_ = initialValues_.empty() &&
      problem_.getIntAttrib(XPRS_MIPENTS) == 0 &&
      problem_.getIntAttrib(XPRS_SETS) == 0 &&
      problem_.getIntAttrib(XPRS_QELEMS) == 0 &&
      problem_.getIntAttrib(XPRS_QCONSTRAINTS) == 0 &&
      problem_.getIntAttrib(XPRS_PWLCONS) == 0 &&
      problem_.getIntAttrib(XPRS_GENCONS) == 0;
  std::string algorithm;
  switch (problem_.getIntControl(XPRS_DEFAULTALG)) {
    case 2:
      algorithm = "d";
      break;
    case 3:
      algorithm = "p";
      break;
    case 4:
      algorithm = "b";
      break;
    default:
      break;
  }

  saveDebugData(DebugPhase::PreMain);
  if (solvedAsLp_) {
    problem_.lpOptimize(algorithm);
  } else {
    problem_.mipOptimize(algorithm.empty() ? "c" : algorithm);
  }
  saveDebugData(DebugPhase::PostMain);
  publishSolution();
}

void XpressProblemV2::publishSolution() {
  const int numCols = problem_.getIntAttrib(XPRS_INPUTCOLS);
  if (numCols <= 0) {
    solution_->clear();
    return;
  }
  const auto status = getSolveStatus();
  if (status != thrift::ProblemStatus::OPTIMAL_FOUND &&
      status != thrift::ProblemStatus::SOLUTION_FOUND) {
    solution_->clear();
    return;
  }
  // Variable handles retain input-column indices while a stopped MIP can leave
  // XPRS_COLS reporting the smaller presolved matrix.
  *solution_ = problem_.xpress::XPRSProblem::getSolution();
}

thrift::ProblemStatus XpressProblemV2::getSolveStatus() {
  if (solvedAsLp_) {
    switch (problem_.getIntAttrib(XPRS_LPSTATUS)) {
      case XPRS_LP_OPTIMAL:
        return thrift::ProblemStatus::OPTIMAL_FOUND;
      case XPRS_LP_UNFINISHED:
        return problem_.getIntAttrib(XPRS_PRIMALINFEAS) == 0
            ? thrift::ProblemStatus::SOLUTION_FOUND
            : thrift::ProblemStatus::SOLUTION_NOT_FOUND;
      case XPRS_LP_INFEAS:
      case XPRS_LP_UNBOUNDED:
        return thrift::ProblemStatus::NO_SOLUTION_EXISTS;
      default:
        return thrift::ProblemStatus::SOLUTION_NOT_FOUND;
    }
  }
  const int status = problem_.getIntAttrib(XPRS_MIPSTATUS);
  switch (status) {
    case XPRS_MIP_OPTIMAL:
      return thrift::ProblemStatus::OPTIMAL_FOUND;
    case XPRS_MIP_SOLUTION:
      return thrift::ProblemStatus::SOLUTION_FOUND;
    case XPRS_MIP_INFEAS:
    case XPRS_MIP_UNBOUNDED:
      return thrift::ProblemStatus::NO_SOLUTION_EXISTS;
    case XPRS_MIP_LP_OPTIMAL:
    case XPRS_MIP_NO_SOL_FOUND:
    case XPRS_MIP_LP_NOT_OPTIMAL:
      return thrift::ProblemStatus::SOLUTION_NOT_FOUND;
    default:
      throw std::runtime_error(
          fmt::format(
              "XpressProblemV2: unknown xpress status code {}", status));
  }
}

thrift::ProblemResult XpressProblemV2::getSolveResult() {
  thrift::ProblemResult result;
  result.status() = getSolveStatus();
  if (warmStartResult_.has_value()) {
    result.warmStartResult() = *warmStartResult_;
  }

  double bestBound;
  double bestSolution;
  if (solvedAsLp_) {
    const auto status = *result.status();
    bestSolution = status == thrift::ProblemStatus::OPTIMAL_FOUND ||
            status == thrift::ProblemStatus::SOLUTION_FOUND
        ? problem_.getDblAttrib(XPRS_LPOBJVAL)
        : XPRS_PLUSINFINITY;
    bestBound = status == thrift::ProblemStatus::OPTIMAL_FOUND
        ? bestSolution
        : XPRS_MINUSINFINITY;
  } else {
    bestBound = problem_.getDblAttrib(XPRS_BESTBOUND);
    bestSolution = problem_.getDblAttrib(XPRS_MIPBESTOBJVAL);
  }

  result.bestBound() = bestBound;
  result.bestObjective() = bestSolution;
  // Signed, matching every other backend; solves are always Minimize, so the
  // incumbent cannot sit below the bound by more than the integer tolerance.
  result.absoluteGap() = bestSolution - bestBound;
  result.problemAttributes() = getProblemAttributes();

  const double denominator =
      std::max(std::abs(bestBound), std::abs(bestSolution));
  result.relativeGap() =
      denominator == 0 ? 0.0 : std::abs(bestSolution - bestBound) / denominator;
  return result;
}

thrift::ProblemAttributes XpressProblemV2::getProblemAttributes() {
  thrift::ProblemAttributes attributes;
  attributes.numVariables() = problem_.getIntAttrib(XPRS_COLS);
  attributes.numConstraints() = problem_.getIntAttrib(XPRS_ROWS);
  attributes.numOfNonZeros() = problem_.getIntAttrib(XPRS_ELEMS);
  return attributes;
}

void XpressProblemV2::disableLogs() {
  logsDisabled_ = true;
  problem_.setIntControl(XPRS_OUTPUTLOG, 0);
}

void XpressProblemV2::setParameter(const std::string& name, double value) {
  if (const auto code = xpress_params::getIntParamCode(name)) {
    // -2^31 is exactly representable as a double, so the upper bound is its
    // negation taken exclusively; static_cast<double> of INT_MAX rounds up and
    // would let 2^31 through.
    constexpr double kIntMin =
        static_cast<double>(std::numeric_limits<int>::min());
    if (std::trunc(value) != value) {
      throw std::runtime_error(
          fmt::format(
              "XpressProblemV2: parameter {} is an integer control but was "
              "given the fractional value {}",
              name,
              value));
    }
    if (value < kIntMin || value >= -kIntMin) {
      throw std::runtime_error(
          fmt::format(
              "XpressProblemV2: parameter {} is an integer control but was "
              "given {}, which is outside the range of an int",
              name,
              value));
    }
    problem_.setIntControl(*code, static_cast<int>(value));
    return;
  }
  if (const auto code = xpress_params::getDoubleParamCode(name)) {
    problem_.setDblControl(*code, value);
    return;
  }
  throw SolverError(fmt::format("XpressProblemV2: unknown parameter {}", name));
}

double XpressProblemV2::getParameter(const std::string& name) {
  if (const auto code = xpress_params::getIntParamCode(name)) {
    return problem_.getIntControl(*code);
  }
  if (const auto code = xpress_params::getDoubleParamCode(name)) {
    return problem_.getDblControl(*code);
  }
  throw SolverError(fmt::format("XpressProblemV2: unknown parameter {}", name));
}

void XpressProblemV2::setTolerances(const Tolerances& tol) {
  // Set the controls directly by code rather than going through the
  // name-to-code lookup table; the typed problem inherits the raw control API.
  problem_.setDblControl(XPRS_FEASTOL, tol.constraint);
  problem_.setDblControl(XPRS_MIPTOL, tol.integer);
  problem_.setDblControl(XPRS_MIPRELSTOP, tol.relgap);
  problem_.setDblControl(XPRS_MIPABSSTOP, tol.absgap);
  problem_.setDblControl(XPRS_MIPRELCUTOFF, tol.relcut);
  problem_.setDblControl(XPRS_MIPADDCUTOFF, tol.abscut);
}

Tolerances XpressProblemV2::getTolerances() {
  return {
      .constraint = problem_.getDblControl(XPRS_FEASTOL),
      .integer = problem_.getDblControl(XPRS_MIPTOL),
      .absgap = problem_.getDblControl(XPRS_MIPABSSTOP),
      .relgap = problem_.getDblControl(XPRS_MIPRELSTOP),
      .abscut = problem_.getDblControl(XPRS_MIPADDCUTOFF),
      .relcut = problem_.getDblControl(XPRS_MIPRELCUTOFF)};
}

void XpressProblemV2::setLogfile(const std::string& filename) {
  problem_.setLogFile(filename);
}

void XpressProblemV2::saveToFile(const std::string& filename) {
  flushNames();
  const std::filesystem::path path(filename);
  const auto extension = path.extension();
  const auto stem = (path.parent_path() / path.stem()).string();
  if (extension == ".lp") {
    problem_.writeProb(stem, "l");
  } else if (extension == ".mps") {
    problem_.writeProb(stem, "");
  } else if (extension == ".svf") {
    problem_.saveAs(stem);
  } else {
    throw SolverError(
        "File type not supported, for Xpress, expected .lp/.mps/.svf");
  }
}

void XpressProblemV2::saveToFileWithObjectiveAt(
    int pos,
    const std::string& filename) {
  markModified();
  problem_.setObjective(
      asV2Expression(*getObjectiveAt(pos)).getNative(),
      xpress::ObjSense::Minimize);
  saveToFile(filename);
}

void XpressProblemV2::saveToFileWithAllObjectives(const std::string& filename) {
  if (!objectives_.empty()) {
    saveToFileWithObjectiveAt(0, filename);
  } else {
    saveToFile(filename);
  }
}

void XpressProblemV2::setDebugPath(const std::string& path) {
  debugPath_ = path;
}

void XpressProblemV2::saveDebugData(DebugPhase phase) {
  if (debugPath_.empty()) {
    return;
  }
  flushNames();
  const bool isWarmStart =
      phase == DebugPhase::PreWarmStart || phase == DebugPhase::PostWarmStart;
  const bool isPre =
      phase == DebugPhase::PreWarmStart || phase == DebugPhase::PreMain;
  const auto* const solveName = isWarmStart ? "warm-start" : "main";

  if (isPre) {
    // Xpress appends the format's extension to an extensionless name, so the
    // shared stem yields .lp and .mps rather than one dump overwriting the
    // other. WritesDebugDataAroundSolve pins both names.
    const auto stem = fmt::format("{}/{}", debugPath_, solveName);
    problem_.writeProb(stem, "l");
    problem_.writeProb(stem, "");
  }
  problem_.saveAs(
      fmt::format("{}/{}-{}", debugPath_, isPre ? "pre" : "post", solveName));
}

void XpressProblemV2::print(
    const std::vector<std::string>& /* substringsToMatch */) {
  flushNames();
  // Xpress has no per-constraint print, so the filter is ignored and the whole
  // model is dumped.
  problem_.writeProb("", "lp");
}

void XpressProblemV2::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback) {
  callback_ = std::move(callback);
}

int XpressProblemV2::callbackImpl(xpress::objects::XpressProblem& prob) {
  if (callbackAborted_) {
    return 1;
  }
  const auto action = callback_(
      {.bound = solvedAsLp_ ? -std::numeric_limits<double>::infinity()
                            : prob.getDblAttrib(XPRS_BESTBOUND),
       .objective = solvedAsLp_ ? std::numeric_limits<double>::infinity()
                                : prob.getDblAttrib(XPRS_MIPBESTOBJVAL),
       .walltime = timer_.getSeconds()});
  if (action == ProblemCallbackAction::ABORT) {
    callbackAborted_ = true;
    return 1;
  }
  return 0;
}

void XpressProblemV2::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) {
  initialValues_[std::move(variable)] = value;
}

void XpressProblemV2::applyStartValues() {
  std::vector<double> values;
  std::vector<int> columns;
  values.reserve(initialValues_.size());
  columns.reserve(initialValues_.size());
  for (const auto& [variable, value] : initialValues_) {
    const auto* v2Variable =
        dynamic_cast<const XpressV2Variable*>(variable.get());
    if (v2Variable == nullptr) {
      throw std::invalid_argument(
          "XpressProblemV2 expected an XpressV2Variable for a start value");
    }
    columns.push_back(v2Variable->getNative().getIndex());
    values.push_back(value);
  }
  // Register before submitting: Xpress may process the hint during the
  // addMipSol call itself, and a callback installed afterwards would miss it.
  problem_.callbacks.removeUserSolNotifyCallbacks();
  problem_.callbacks.addUserSolNotifyCallback(
      [this](
          xpress::objects::XpressProblem& /* prob */,
          const char* solutionName,
          int processingStatus) {
        thrift::WarmStartResult warmResult;
        warmResult.status() = toWarmStartStatus(processingStatus);
        warmResult.processingTimeInSecs() = timer_.getSeconds();
        warmStartResult_ = warmResult;
        XLOG_IF(INFO, !logsDisabled_) << fmt::format(
            "Initial solution supplied through warm-start has been processed."
            " Processing Status: {}, Solution Name: {}",
            processingStatus,
            solutionName == nullptr ? "" : solutionName);
      },
      /*prio=*/0);

  problem_.addMipSol(
      static_cast<int>(values.size()), values, columns, "initial_assignment");
}

thrift::WarmStartStatus XpressProblemV2::toWarmStartStatus(
    int processingStatus) {
  switch (processingStatus) {
    case XPRS_USERSOLSTATUS_NOT_CHECKED:
      return thrift::WarmStartStatus::PROCESSING_ERROR;
    case XPRS_USERSOLSTATUS_ACCEPTED_FEASIBLE:
      return thrift::WarmStartStatus::FEASIBLE_SOLUTION_PROVIDED;
    case XPRS_USERSOLSTATUS_ACCEPTED_OPTIMIZED:
    case XPRS_USERSOLSTATUS_SEARCHED_SOL:
      return thrift::WarmStartStatus::FEASIBLE_SOLUTION_OBTAINED;
    case XPRS_USERSOLSTATUS_SEARCHED_NOSOL:
    case XPRS_USERSOLSTATUS_REJECTED_INFEAS_NOSEARCH:
    case XPRS_USERSOLSTATUS_REJECTED_PARTIAL_NOSEARCH:
    case XPRS_USERSOLSTATUS_REJECTED_FAILED_OPTIMIZE:
    case XPRS_USERSOLSTATUS_REJECTED_CUTOFF:
      return thrift::WarmStartStatus::FEASIBLE_SOLUTION_NOT_FOUND;
    case XPRS_USERSOLSTATUS_DROPPED:
      return thrift::WarmStartStatus::SOLUTION_IS_DROPPED;
    default:
      throw std::runtime_error(
          fmt::format(
              "XpressProblemV2: unknown warm-start processing status {}",
              processingStatus));
  }
}

std::optional<IIS> XpressProblemV2::getIIS() {
  flushNames();
  try {
    if (problem_.firstIIS(1) != 0) {
      return std::nullopt;
    }
    const auto data = problem_.getIISData(1);
    IIS iis;
    iis.constraintIds.reserve(data.rowind.size());
    iis.lowerBoundVars.reserve(data.colind.size());
    iis.upperBoundVars.reserve(data.colind.size());
    for (const auto row : data.rowind) {
      const auto nativeName =
          problem_.getNameList(XPRS_NAMES_ROW, row, row).at(0);
      const auto original = originalConstraintNames_.find(nativeName);
      iis.constraintIds.push_back(
          original == originalConstraintNames_.end() ? nativeName
                                                     : original->second);
    }
    for (const auto i : folly::irange(data.colind.size())) {
      const int column = data.colind[i];
      const auto nativeName =
          problem_.getNameList(XPRS_NAMES_COLUMN, column, column).at(0);
      const auto original = originalVariableNames_.find(nativeName);
      const auto& name = original == originalVariableNames_.end()
          ? nativeName
          : original->second;
      switch (data.bndtype[i]) {
        case 'L':
          iis.lowerBoundVars.push_back(name);
          break;
        case 'U':
          iis.upperBoundVars.push_back(name);
          break;
        case 'F':
          iis.lowerBoundVars.push_back(name);
          iis.upperBoundVars.push_back(name);
          break;
        default:
          XLOGF(
              WARNING,
              "Unexpected Xpress IIS bound type '{}'",
              data.bndtype[i]);
          break;
      }
    }
    return iis;
  } catch (const std::exception& e) {
    XLOGF(WARNING, "Xpress could not compute an IIS: {}", e.what());
    return std::nullopt;
  }
}

bool XpressProblemV2::supportsNativeQuadratic() const {
  return algopt::useXpressNativeQuadratic();
}

bool XpressProblemV2::supportsNativePwl() const {
  return algopt::useXpressNativePwl();
}

bool XpressProblemV2::supportsNativeMax() const {
  return algopt::useXpressNativeMax();
}

bool XpressProblemV2::supportsIndicatorConstraints() const {
  return algopt::useXpressIndicatorConstraints();
}

bool XpressProblemV2::setIndicatorOnConstraint(
    Constraint& ctr,
    const Variable& binaryVar,
    int dir) {
  // The shared interface uses Gurobi's convention, where dir is the binary
  // value that activates the constraint. The typed API takes that same meaning
  // as a bool, so the value passes straight through.
  if (dir != 0 && dir != 1) {
    throw std::invalid_argument(
        fmt::format(
            "setIndicatorOnConstraint: dir must be 0 or 1, got {}", dir));
  }
  const auto* v2Constraint =
      dynamic_cast<const XpressV2Constraint*>(ctr.get().get());
  const auto* v2Variable =
      dynamic_cast<const XpressV2Variable*>(binaryVar.get().get());
  // False means "this backend cannot attach an indicator here" and is the
  // caller's decision to act on; a nonsensical argument throws instead.
  if (v2Constraint == nullptr || v2Variable == nullptr) {
    return false;
  }
  // Xpress accepts a non-binary variable here and quietly builds a different
  // model, so reject it rather than emit a silent mis-formulation.
  const auto columnType = v2Variable->getNative().getType();
  if (columnType != xpress::ColumnType::Binary) {
    throw std::invalid_argument(
        fmt::format(
            "setIndicatorOnConstraint: binaryVar must be a binary variable, "
            "got {}",
            xpress::to_string(columnType)));
  }
  markModified();
  problem_.setIndicator(
      v2Variable->getNative(), dir == 1, v2Constraint->getNative());
  return true;
}

xpress::objects::Variable XpressProblemV2::pinToVariable(
    const Expression& expression,
    const std::string& name) {
  auto auxImpl = std::dynamic_pointer_cast<XpressV2Variable>(
      makeTypedVar(name, VariableImpl::Type::CONTINUOUS, -kInfinity));
  auto aux = auxImpl->getNative();

  // aux - expression == 0. A pure constant carries its value in the Expression
  // wrapper with a null impl, so it must not be dereferenced.
  if (const auto impl = expression.get()) {
    problem_.addConstraint(aux - asV2Expression(*impl).getNative() == 0.0);
  } else {
    problem_.addConstraint(aux == expression.getConstant());
  }
  return aux;
}

std::optional<Expression> XpressProblemV2::addNativeMaxConstraint(
    const std::vector<Expression>& inputs) {
  if (inputs.empty()) {
    throw std::invalid_argument(
        "MAX constraint requires at least 1 input expression");
  }

  std::vector<xpress::objects::Variable> inputVars;
  inputVars.reserve(inputs.size());
  for (const auto i : folly::irange(inputs.size())) {
    inputVars.push_back(
        pinToVariable(inputs[i], fmt::format("max_input_{}", i)));
  }

  auto resultImpl = std::dynamic_pointer_cast<XpressV2Variable>(
      makeTypedVar("max_result", VariableImpl::Type::CONTINUOUS, -kInfinity));

  problem_.addConstraint(resultImpl->getNative().maxOf(
      xpress::SizedArray<const xpress::objects::Variable>{inputVars}));

  return Expression(resultImpl->makeExpression(1.0));
}

std::optional<Expression> XpressProblemV2::addNativePwlConstraint(
    const Expression& x,
    const std::vector<std::pair<double, double>>& points) {
  if (points.size() < 2) {
    throw std::invalid_argument(
        fmt::format(
            "PWL constraint requires at least 2 breakpoints, got {}",
            points.size()));
  }

  auto xAux = pinToVariable(x, "pwl_x");

  std::vector<double> xs;
  std::vector<double> ys;
  xs.reserve(points.size());
  ys.reserve(points.size());
  for (const auto& [xi, yi] : points) {
    xs.push_back(xi);
    ys.push_back(yi);
  }

  auto yImpl = std::dynamic_pointer_cast<XpressV2Variable>(
      makeTypedVar("pwl_y", VariableImpl::Type::CONTINUOUS, -kInfinity));
  problem_.addConstraint(yImpl->getNative().pwlOf(
      xAux,
      xpress::SizedArray<const double>{xs},
      xpress::SizedArray<const double>{ys}));

  return Expression(yImpl->makeExpression(1.0));
}

} // namespace facebook::algopt::lp::detail

#endif // REBALANCER_USE_XPRESS
