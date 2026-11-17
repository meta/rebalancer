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

#include "algopt/lp/detail/gurobi/GurobiProblem.h"

#ifdef REBALANCER_USE_GUROBI

#include "algopt/lp/detail/generic/impl/thrift/gen-cpp2/model_types.h"
#include "algopt/lp/detail/gurobi/GurobiConstraint.h"
#include "algopt/lp/detail/gurobi/GurobiExpression.h"
#include "algopt/lp/detail/gurobi/GurobiRelation.h"
#include "algopt/lp/detail/gurobi/GurobiVariable.h"
#include "algopt/lp/environment/GurobiEnvironment.h"
#include "algopt/lp/generic/Problem.h"
#include "algopt/lp/generic/Variable.h"
#include "algopt/rebalancer/algopt_common/thrift/ThriftUtils.h"

#include <fmt/format.h>
#include <folly/container/F14Map.h>
#include <folly/container/irange.h>
#include <folly/logging/xlog.h>
#include <folly/MapUtil.h>
#include <folly/synchronization/SanitizeThread.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace {

// https://www.gurobi.com/documentation/9.5/refman/cpp_model_write.html
const std::vector<std::string> kDebugExtensions = {
    "mps",
    "rew",
    "lp",
    "rlp",
    "dua",
    "dlp",
    "ilp",
    "sol",
    "mst",
    "hnt",
    "bas",
    "prm",
    "attr",
    "json"};

} // namespace

namespace {

// Parameters from
// third-party2/gurobi/9.1.1/gurobi911/linux64/include/gurobi_c++.h
// https://www.gurobi.com/documentation/9.1/refman/parameter_descriptions.html
const std::map<std::string, GRB_DoubleParam> kDoubleParamCodes = {
    {"GRB_Cutoff", GRB_DoubleParam::GRB_DoubleParam_Cutoff},
    {"GRB_IterationLimit", GRB_DoubleParam::GRB_DoubleParam_IterationLimit},
    {"GRB_NodeLimit", GRB_DoubleParam::GRB_DoubleParam_NodeLimit},
    {"GRB_TimeLimit", GRB_DoubleParam::GRB_DoubleParam_TimeLimit},
    {"GRB_WorkLimit", GRB_DoubleParam::GRB_DoubleParam_WorkLimit},
    {"GRB_FeasibilityTol", GRB_DoubleParam::GRB_DoubleParam_FeasibilityTol},
    {"GRB_IntFeasTol", GRB_DoubleParam::GRB_DoubleParam_IntFeasTol},
    {"GRB_MarkowitzTol", GRB_DoubleParam::GRB_DoubleParam_MarkowitzTol},
    {"GRB_MIPGap", GRB_DoubleParam::GRB_DoubleParam_MIPGap},
    {"GRB_MIPGapAbs", GRB_DoubleParam::GRB_DoubleParam_MIPGapAbs},
    {"GRB_OptimalityTol", GRB_DoubleParam::GRB_DoubleParam_OptimalityTol},
    {"GRB_PerturbValue", GRB_DoubleParam::GRB_DoubleParam_PerturbValue},
    {"GRB_Heuristics", GRB_DoubleParam::GRB_DoubleParam_Heuristics},
    {"GRB_ObjScale", GRB_DoubleParam::GRB_DoubleParam_ObjScale},
    {"GRB_NodefileStart", GRB_DoubleParam::GRB_DoubleParam_NodefileStart},
    {"GRB_BarConvTol", GRB_DoubleParam::GRB_DoubleParam_BarConvTol},
    {"GRB_BarQCPConvTol", GRB_DoubleParam::GRB_DoubleParam_BarQCPConvTol},
    {"GRB_PSDTol", GRB_DoubleParam::GRB_DoubleParam_PSDTol},
    {"GRB_ImproveStartGap", GRB_DoubleParam::GRB_DoubleParam_ImproveStartGap},
    {"GRB_ImproveStartNodes",
     GRB_DoubleParam::GRB_DoubleParam_ImproveStartNodes},
    {"GRB_ImproveStartTime", GRB_DoubleParam::GRB_DoubleParam_ImproveStartTime},
    {"GRB_FeasRelaxBigM", GRB_DoubleParam::GRB_DoubleParam_FeasRelaxBigM},
    {"GRB_TuneTimeLimit", GRB_DoubleParam::GRB_DoubleParam_TuneTimeLimit},
    {"GRB_TuneCleanup", GRB_DoubleParam::GRB_DoubleParam_TuneCleanup},
    {"GRB_PreSOS1BigM", GRB_DoubleParam::GRB_DoubleParam_PreSOS1BigM},
    {"GRB_PreSOS2BigM", GRB_DoubleParam::GRB_DoubleParam_PreSOS2BigM},
    {"GRB_PoolGap", GRB_DoubleParam::GRB_DoubleParam_PoolGap},
    {"GRB_PoolGapAbs", GRB_DoubleParam::GRB_DoubleParam_PoolGapAbs},
    {"GRB_BestObjStop", GRB_DoubleParam::GRB_DoubleParam_BestObjStop},
    {"GRB_BestBdStop", GRB_DoubleParam::GRB_DoubleParam_BestBdStop},
    {"GRB_CSQueueTimeout", GRB_DoubleParam::GRB_DoubleParam_CSQueueTimeout},
    {"GRB_FuncPieceError", GRB_DoubleParam::GRB_DoubleParam_FuncPieceError},
    {"GRB_FuncPieceLength", GRB_DoubleParam::GRB_DoubleParam_FuncPieceLength},
    {"GRB_FuncPieceRatio", GRB_DoubleParam::GRB_DoubleParam_FuncPieceRatio},
    {"GRB_FuncMaxVal", GRB_DoubleParam::GRB_DoubleParam_FuncMaxVal},
    {"GRB_NoRelHeurTime", GRB_DoubleParam::GRB_DoubleParam_NoRelHeurTime},
    {"GRB_NoRelHeurWor", GRB_DoubleParam::GRB_DoubleParam_NoRelHeurWork},
};

const std::map<std::string, GRB_IntParam> kIntParamCodes = {
    {"GRB_SolutionLimit", GRB_IntParam::GRB_IntParam_SolutionLimit},
    {"GRB_Method", GRB_IntParam::GRB_IntParam_Method},
    {"GRB_ScaleFlag", GRB_IntParam::GRB_IntParam_ScaleFlag},
    {"GRB_SimplexPricing", GRB_IntParam::GRB_IntParam_SimplexPricing},
    {"GRB_Quad", GRB_IntParam::GRB_IntParam_Quad},
    {"GRB_NormAdjust", GRB_IntParam::GRB_IntParam_NormAdjust},
    {"GRB_Sifting", GRB_IntParam::GRB_IntParam_Sifting},
    {"GRB_SiftMethod", GRB_IntParam::GRB_IntParam_SiftMethod},
    {"GRB_SubMIPNodes", GRB_IntParam::GRB_IntParam_SubMIPNodes},
    {"GRB_VarBranch", GRB_IntParam::GRB_IntParam_VarBranch},
    {"GRB_Cuts", GRB_IntParam::GRB_IntParam_Cuts},
    {"GRB_CliqueCuts", GRB_IntParam::GRB_IntParam_CliqueCuts},
    {"GRB_CoverCuts", GRB_IntParam::GRB_IntParam_CoverCuts},
    {"GRB_FlowCoverCuts", GRB_IntParam::GRB_IntParam_FlowCoverCuts},
    {"GRB_FlowPathCuts", GRB_IntParam::GRB_IntParam_FlowPathCuts},
    {"GRB_GUBCoverCuts", GRB_IntParam::GRB_IntParam_GUBCoverCuts},
    {"GRB_ImpliedCuts", GRB_IntParam::GRB_IntParam_ImpliedCuts},
    {"GRB_ProjImpliedCuts", GRB_IntParam::GRB_IntParam_ProjImpliedCuts},
    {"GRB_MIPSepCuts", GRB_IntParam::GRB_IntParam_MIPSepCuts},
    {"GRB_MIRCuts", GRB_IntParam::GRB_IntParam_MIRCuts},
    {"GRB_StrongCGCuts", GRB_IntParam::GRB_IntParam_StrongCGCuts},
    {"GRB_ModKCuts", GRB_IntParam::GRB_IntParam_ModKCuts},
    {"GRB_ZeroHalfCuts", GRB_IntParam::GRB_IntParam_ZeroHalfCuts},
    {"GRB_NetworkCuts", GRB_IntParam::GRB_IntParam_NetworkCuts},
    {"GRB_SubMIPCuts", GRB_IntParam::GRB_IntParam_SubMIPCuts},
    {"GRB_InfProofCuts", GRB_IntParam::GRB_IntParam_InfProofCuts},
    {"GRB_RelaxLiftCuts", GRB_IntParam::GRB_IntParam_RelaxLiftCuts},
    {"GRB_RLTCuts", GRB_IntParam::GRB_IntParam_RLTCuts},
    {"GRB_BQPCuts", GRB_IntParam::GRB_IntParam_BQPCuts},
    {"GRB_PSDCuts", GRB_IntParam::GRB_IntParam_PSDCuts},
    {"GRB_CutAggPasses", GRB_IntParam::GRB_IntParam_CutAggPasses},
    {"GRB_CutPasses", GRB_IntParam::GRB_IntParam_CutPasses},
    {"GRB_GomoryPasses", GRB_IntParam::GRB_IntParam_GomoryPasses},
    {"GRB_NodeMethod", GRB_IntParam::GRB_IntParam_NodeMethod},
    {"GRB_Presolve", GRB_IntParam::GRB_IntParam_Presolve},
    {"GRB_Aggregate", GRB_IntParam::GRB_IntParam_Aggregate},
    {"GRB_IISMethod", GRB_IntParam::GRB_IntParam_IISMethod},
    {"GRB_PreCrush", GRB_IntParam::GRB_IntParam_PreCrush},
    {"GRB_PreDepRow", GRB_IntParam::GRB_IntParam_PreDepRow},
    {"GRB_PrePasses", GRB_IntParam::GRB_IntParam_PrePasses},
    {"GRB_DisplayInterval", GRB_IntParam::GRB_IntParam_DisplayInterval},
    {"GRB_OutputFlag", GRB_IntParam::GRB_IntParam_OutputFlag},
    {"GRB_Threads", GRB_IntParam::GRB_IntParam_Threads},
    {"GRB_BarIterLimit", GRB_IntParam::GRB_IntParam_BarIterLimit},
    {"GRB_Crossover", GRB_IntParam::GRB_IntParam_Crossover},
    {"GRB_CrossoverBasis", GRB_IntParam::GRB_IntParam_CrossoverBasis},
    {"GRB_BarCorrectors", GRB_IntParam::GRB_IntParam_BarCorrectors},
    {"GRB_BarOrder", GRB_IntParam::GRB_IntParam_BarOrder},
    {"GRB_PumpPasses", GRB_IntParam::GRB_IntParam_PumpPasses},
    {"GRB_RINS", GRB_IntParam::GRB_IntParam_RINS},
    {"GRB_Symmetry", GRB_IntParam::GRB_IntParam_Symmetry},
    {"GRB_MIPFocus", GRB_IntParam::GRB_IntParam_MIPFocus},
    {"GRB_NumericFocus", GRB_IntParam::GRB_IntParam_NumericFocus},
    {"GRB_AggFill", GRB_IntParam::GRB_IntParam_AggFill},
    {"GRB_PreDual", GRB_IntParam::GRB_IntParam_PreDual},
    {"GRB_SolutionNumber", GRB_IntParam::GRB_IntParam_SolutionNumber},
    {"GRB_MinRelNodes", GRB_IntParam::GRB_IntParam_MinRelNodes},
    {"GRB_ZeroObjNodes", GRB_IntParam::GRB_IntParam_ZeroObjNodes},
    {"GRB_BranchDir", GRB_IntParam::GRB_IntParam_BranchDir},
    {"GRB_DegenMoves", GRB_IntParam::GRB_IntParam_DegenMoves},
    {"GRB_InfUnbdInfo", GRB_IntParam::GRB_IntParam_InfUnbdInfo},
    {"GRB_DualReductions", GRB_IntParam::GRB_IntParam_DualReductions},
    {"GRB_BarHomogeneous", GRB_IntParam::GRB_IntParam_BarHomogeneous},
    {"GRB_PreQLinearize", GRB_IntParam::GRB_IntParam_PreQLinearize},
    {"GRB_MIQCPMethod", GRB_IntParam::GRB_IntParam_MIQCPMethod},
    {"GRB_NonConvex", GRB_IntParam::GRB_IntParam_NonConvex},
    {"GRB_QCPDual", GRB_IntParam::GRB_IntParam_QCPDual},
    {"GRB_LogToConsole", GRB_IntParam::GRB_IntParam_LogToConsole},
    {"GRB_PreSparsify", GRB_IntParam::GRB_IntParam_PreSparsify},
    {"GRB_PreMIQCPForm", GRB_IntParam::GRB_IntParam_PreMIQCPForm},
    {"GRB_Seed", GRB_IntParam::GRB_IntParam_Seed},
    {"GRB_ConcurrentMIP", GRB_IntParam::GRB_IntParam_ConcurrentMIP},
    {"GRB_ConcurrentJobs", GRB_IntParam::GRB_IntParam_ConcurrentJobs},
    {"GRB_DistributedMIPJobs", GRB_IntParam::GRB_IntParam_DistributedMIPJobs},
    {"GRB_LazyConstraints", GRB_IntParam::GRB_IntParam_LazyConstraints},
    {"GRB_TuneResults", GRB_IntParam::GRB_IntParam_TuneResults},
    {"GRB_TuneTrials", GRB_IntParam::GRB_IntParam_TuneTrials},
    {"GRB_TuneOutput", GRB_IntParam::GRB_IntParam_TuneOutput},
    {"GRB_TuneJobs", GRB_IntParam::GRB_IntParam_TuneJobs},
    {"GRB_TuneCriterion", GRB_IntParam::GRB_IntParam_TuneCriterion},
    {"GRB_Disconnected", GRB_IntParam::GRB_IntParam_Disconnected},
    {"GRB_UpdateMode", GRB_IntParam::GRB_IntParam_UpdateMode},
    {"GRB_Record", GRB_IntParam::GRB_IntParam_Record},
    {"GRB_ObjNumber", GRB_IntParam::GRB_IntParam_ObjNumber},
    {"GRB_MultiObjMethod", GRB_IntParam::GRB_IntParam_MultiObjMethod},
    {"GRB_MultiObjPre", GRB_IntParam::GRB_IntParam_MultiObjPre},
    {"GRB_PoolSolutions", GRB_IntParam::GRB_IntParam_PoolSolutions},
    {"GRB_PoolSearchMode", GRB_IntParam::GRB_IntParam_PoolSearchMode},
    {"GRB_ScenarioNumber", GRB_IntParam::GRB_IntParam_ScenarioNumber},
    {"GRB_StartNumber", GRB_IntParam::GRB_IntParam_StartNumber},
    {"GRB_StartNodeLimit", GRB_IntParam::GRB_IntParam_StartNodeLimit},
    {"GRB_IgnoreNames", GRB_IntParam::GRB_IntParam_IgnoreNames},
    {"GRB_PartitionPlace", GRB_IntParam::GRB_IntParam_PartitionPlace},
    {"GRB_CSPriority", GRB_IntParam::GRB_IntParam_CSPriority},
    {"GRB_CSTLSInsecure", GRB_IntParam::GRB_IntParam_CSTLSInsecure},
    {"GRB_CSIdleTimeout", GRB_IntParam::GRB_IntParam_CSIdleTimeout},
    {"GRB_ServerTimeout", GRB_IntParam::GRB_IntParam_ServerTimeout},
    {"GRB_TSPort", GRB_IntParam::GRB_IntParam_TSPort},
    {"GRB_JSONSolDetail", GRB_IntParam::GRB_IntParam_JSONSolDetail},
    {"GRB_CSBatchMode", GRB_IntParam::GRB_IntParam_CSBatchMode},
    {"GRB_FuncPieces", GRB_IntParam::GRB_IntParam_FuncPieces},
    {"GRB_CSClientLog", GRB_IntParam::GRB_IntParam_CSClientLog},
    {"GRB_IntegralityFocus", GRB_IntParam::GRB_IntParam_IntegralityFocus},
};

const std::map<std::string, GRB_StringParam> kStringParamCodes = {
    {"GRB_LogFile", GRB_StringParam::GRB_StringParam_LogFile},
    {"GRB_NodefileDir", GRB_StringParam::GRB_StringParam_NodefileDir},
    {"GRB_ResultFile", GRB_StringParam::GRB_StringParam_ResultFile},
    {"GRB_WorkerPool", GRB_StringParam::GRB_StringParam_WorkerPool},
    {"GRB_WorkerPassword", GRB_StringParam::GRB_StringParam_WorkerPassword},
    {"GRB_ComputeServer", GRB_StringParam::GRB_StringParam_ComputeServer},
    {"GRB_ServerPassword", GRB_StringParam::GRB_StringParam_ServerPassword},
    {"GRB_CSRouter", GRB_StringParam::GRB_StringParam_CSRouter},
    {"GRB_CSGroup", GRB_StringParam::GRB_StringParam_CSGroup},
    {"GRB_TokenServer", GRB_StringParam::GRB_StringParam_TokenServer},
    {"GRB_CloudAccessID", GRB_StringParam::GRB_StringParam_CloudAccessID},
    {"GRB_CloudSecretKey", GRB_StringParam::GRB_StringParam_CloudSecretKey},
    {"GRB_CloudPool", GRB_StringParam::GRB_StringParam_CloudPool},
    {"GRB_CloudHost", GRB_StringParam::GRB_StringParam_CloudHost},
    {"GRB_JobID", GRB_StringParam::GRB_StringParam_JobID},
    {"GRB_CSManager", GRB_StringParam::GRB_StringParam_CSManager},
    {"GRB_CSAuthToken", GRB_StringParam::GRB_StringParam_CSAuthToken},
    {"GRB_CSAPIAccessID", GRB_StringParam::GRB_StringParam_CSAPIAccessID},
    {"GRB_CSAPISecret", GRB_StringParam::GRB_StringParam_CSAPISecret},
    {"GRB_UserName", GRB_StringParam::GRB_StringParam_UserName},
    {"GRB_CSAppName", GRB_StringParam::GRB_StringParam_CSAppName},
    {"GRB_SolFiles", GRB_StringParam::GRB_StringParam_SolFiles},
    {"GRB_Dummy", GRB_StringParam::GRB_StringParam_Dummy},
};

const std::unordered_set<std::string> kHiddenParameters = {
    "GURO_PAR_FORCEHEUR",
    "GURO_PAR_PREPROBE",
};

constexpr std::string_view kGurobiWarmStartOverride =
    "GUROBI_WARM_START_OVERRIDE";

const std::map<int32_t, facebook::algopt::lp::detail::GurobiWarmStartType>
    kGurobiWarmStartTypesMap = {
        {0, facebook::algopt::lp::detail::GurobiWarmStartType::VARIABLE_HINT},
        {1, facebook::algopt::lp::detail::GurobiWarmStartType::MIP_START},
        {2,
         facebook::algopt::lp::detail::GurobiWarmStartType::
             VARIABLE_HINT_AND_MIP_START}};

const std::map<int, const std::string_view> kModelStatusCodes = {
    {GRB_LOADED, "GRB_LOADED"},
    {GRB_OPTIMAL, "GRB_OPTIMAL"},
    {GRB_INFEASIBLE, "GRB_INFEASIBLE"},
    {GRB_INF_OR_UNBD, "GRB_INF_OR_UNBD"},
    {GRB_UNBOUNDED, "GRB_UNBOUNDED"},
    {GRB_CUTOFF, "GRB_CUTOFF"},
    {GRB_ITERATION_LIMIT, "GRB_ITERATION_LIMIT"},
    {GRB_NODE_LIMIT, "GRB_NODE_LIMIT"},
    {GRB_TIME_LIMIT, "GRB_TIME_LIMIT"},
    {GRB_SOLUTION_LIMIT, "GRB_SOLUTION_LIMIT"},
    {GRB_INTERRUPTED, "GRB_INTERRUPTED"},
    {GRB_NUMERIC, "GRB_NUMERIC"},
    {GRB_SUBOPTIMAL, "GRB_SUBOPTIMAL"},
    {GRB_INPROGRESS, "GRB_INPROGRESS"},
    {GRB_USER_OBJ_LIMIT, "GRB_USER_OBJ_LIMIT"},
    {GRB_WORK_LIMIT, "GRB_WORK_LIMIT"},
};

// constraint tolerance EPS helps determine whether a constraint is satisfied or
// not. so if a <= 0 constraint has a value at most EPS, it is considered
// satisfied. However, setting EPS too small or too large can be problematic.
// The following limits are defined by Gurobi:
// https://docs.gurobi.com/projects/optimizer/en/current/reference/parameters.html#feasibilitytol
constexpr double kGurobiMaxConstraintTolerance = 1e-2;
constexpr double kGurobiMinConstraintTolerance = 1e-9;

// Integer tolerance EPS helps determine whether a variable is integer or not.
// This is only relevant for integer variables in MIP problems. Specifically, if
// an integer variable has value within EPS of the nearest integer, gurobi will
// consider the integrality constraint on that variable satisfied. The following
// limits are defined by Gurobi:
// https://docs.gurobi.com/projects/optimizer/en/current/reference/parameters.html#intfeastol
constexpr double kGurobiMaxIntegerTolerance = 1e-1;
constexpr double kGurobiMinIntegerTolerance = 1e-9;

double checkAndAdjustToleranceWithinRange(
    double tol,
    double min,
    double max,
    const std::string_view name) {
  if (tol < min || tol > max) {
    XLOG(ERR) << fmt::format(
        "Invalid value for {} parameter: {}. Value must be between {} and {}",
        name,
        tol,
        min,
        max);
  }
  return std::clamp(tol, min, max);
}

std::string constrExprToString(
    const GRBLinExpr& expr,
    bool nonzeroValuesOnly = true) {
  std::stringstream ss;
  ss << fmt::format("{}", expr.getConstant());
  int numNonZeroValues = 0;
  for (const auto i : folly::irange(expr.size())) {
    const auto& var = expr.getVar(i);
    std::string valueStr;
    bool isValZero = true;
    try {
      auto val = var.get(GRB_DoubleAttr::GRB_DoubleAttr_X);
      if (val != 0) {
        isValZero = false;
        numNonZeroValues++;
        valueStr = fmt::format("{:.8f}", val);
      }
    } catch (const GRBException&) {
      valueStr = "Unknown value";
    };

    if (nonzeroValuesOnly && isValZero) {
      continue;
    }
    ss << fmt::format(
        "+ ({} * {} [{}])",
        expr.getCoeff(i),
        var.get(GRB_StringAttr_VarName),
        valueStr);
  }
  const auto numHidden = expr.size() - numNonZeroValues;
  if (numHidden > 0) {
    ss << fmt::format("... + {} hidden zero value terms [0]", numHidden);
  }
  return ss.str();
}

} // namespace

namespace facebook::algopt::lp::detail {

GurobiProblem::GurobiProblem() try
    : model_(GurobiEnvironment::get()->getEnv()),
      warmStartType_(GurobiWarmStartType::VARIABLE_HINT) {
  setTolerances(
      Tolerances{
          // based on Gurobi default
          // https://docs.gurobi.com/projects/optimizer/en/current/reference/parameters.html
          .constraint = 1e-6,
          .integer = 1e-5,
          .absgap = 1e-10,
          .relgap = 1e-4});
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<VariableImpl> GurobiProblem::makeVar(
    const std::string& name) try {
  return std::make_shared<GurobiVariable>(
      addVar(-GRB_INFINITY, +GRB_INFINITY, 0, GRB_CONTINUOUS, name),
      VariableImpl::Type::CONTINUOUS);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<VariableImpl> GurobiProblem::makeIntVar(
    const std::string& name) try {
  return std::make_shared<GurobiVariable>(
      addVar(-GRB_INFINITY, +GRB_INFINITY, 0, GRB_INTEGER, name),
      VariableImpl::Type::INTEGER);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<VariableImpl> GurobiProblem::makeSemiContVar(
    const std::string& name,
    double threshold) try {
  return std::make_shared<GurobiVariable>(
      addVar(threshold, +GRB_INFINITY, 0, GRB_SEMICONT, name),
      VariableImpl::Type::SEMI_CONTINUOUS);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<VariableImpl> GurobiProblem::makeSemiIntVar(
    const std::string& name,
    double threshold) try {
  return std::make_shared<GurobiVariable>(
      addVar(threshold, +GRB_INFINITY, 0, GRB_SEMIINT, name),
      VariableImpl::Type::SEMI_INTEGER);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<VariableImpl> GurobiProblem::makeBoolVar(
    const std::string& name) try {
  return std::make_shared<GurobiVariable>(
      addVar(0, 1, 0, GRB_BINARY, name), VariableImpl::Type::BINARY);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<ExpressionImpl> GurobiProblem::makeExpression(
    double constant) const try {
  return std::make_shared<GurobiExpression>(constant);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::shared_ptr<ConstraintImpl> GurobiProblem::newConstraint(
    std::shared_ptr<const RelationImpl> relation,
    const std::string& name) try {
  auto grbRel = dynamic_cast<const GurobiRelation&>(*relation);

  // add a quadratic constraint
  if (!grbRel.isLinear()) {
    return std::make_shared<GurobiConstraint>(
        model_.addQConstr(grbRel.get(), name));
  }

  return std::make_shared<GurobiConstraint>(
      model_.addConstr(grbRel.get(), name));
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::deleteConstraint(
    std::shared_ptr<ConstraintImpl> constraint) try {
  std::visit(
      [this](auto& c) { model_.remove(c); },
      dynamic_cast<GurobiConstraint&>(*constraint).get());
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

int GurobiProblem::getObjectiveSize() const {
  return objectives_.size();
}

std::shared_ptr<const ExpressionImpl> GurobiProblem::getObjectiveAt(
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

void GurobiProblem::clearObjectives() {
  objectives_.clear();
  // also clear all saved results w.r.t. those objectives
  problemResultPerObjective_.clear();
}

void GurobiProblem::addObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  objectives_.push_back(expression);
}

void GurobiProblem::setObjective(
    std::shared_ptr<const ExpressionImpl> expression) {
  auto& grbExpr = dynamic_cast<const GurobiExpression&>(*expression).get();
  if (std::holds_alternative<GRBLinExpr>(grbExpr)) {
    model_.setObjective(std::get<GRBLinExpr>(grbExpr));
  } else {
    model_.setObjective(std::get<GRBQuadExpr>(grbExpr));
  }
}

void GurobiProblem::saveDebugData(DebugPhase phase) {
  if (!debugPath_) {
    return;
  }

  std::string stage;
  switch (phase) {
    case DebugPhase::Pre:
      stage = "pre";
      break;
    case DebugPhase::Post:
      stage = "post";
      break;
  }

  for (auto& extension : kDebugExtensions) {
    try {
      model_.write(
          fmt::format("{}/{}-solve.{}", *debugPath_, stage, extension));
    } catch (const GRBException&) {
      // Not all file types can be generated all the time. Silently ignore the
      // ones which are not available.
      (void)0;
    }
  }
}

GRBVar GurobiProblem::addVar(
    double lb,
    double ub,
    double obj,
    char vtype,
    std::string vname) {
  return model_.addVar(lb, ub, obj, vtype, std::move(vname));
}

bool allExprsLinear(
    const std::vector<std::shared_ptr<const ExpressionImpl>>& exprs) {
  for (auto& expr : exprs) {
    auto& grbExpr = dynamic_cast<const GurobiExpression&>(*expr).get();
    if (!std::holds_alternative<GRBLinExpr>(grbExpr)) {
      return false;
    }
  }
  return true;
}

void GurobiProblem::setMultiObjectiveParameter(
    int index,
    std::optional<GRBEnv*> env) {
  if (!multiObjConfig_) {
    return;
  }
  for (auto& [key, val] : (multiObjConfig_->paramNamesToValues)) {
    const auto paramValue =
        algopt::common::thriftUtils::getObjectiveValue(index, val);
    if (env) {
      auto intParam = getIntParamCode(key);
      if (intParam) {
        const GRB_IntParam paramName = *intParam;
        (*env)->set(paramName, paramValue);
        XLOG(INFO) << fmt::format(
            "Updating Gurobi param {} to {} for objective at {}",
            key,
            (*env)->get(paramName),
            index);
      } else {
        auto doubleParam = getDoubleParamCode(key);
        if (doubleParam) {
          const GRB_DoubleParam paramName = *doubleParam;
          (*env)->set(paramName, paramValue);
          XLOG(INFO) << fmt::format(
              "Updating Gurobi param {} to {} for objective at {}",
              key,
              (*env)->get(paramName),
              index);
        }
      }
    } else {
      setParameter(key, paramValue);
    }
  }
}

void GurobiProblem::setMultiObjective() {
  for (const auto index : folly::irange(objectives_.size())) {
    auto& grbExpr =
        dynamic_cast<const GurobiExpression&>(*objectives_.at(index)).get();
    // For Gurobi, we create separate environment for the multi objective and
    // set the parameters accordingly.
    auto env = model_.getMultiobjEnv(index);
    // Add stopping criteria for individual objectives
    setMultiObjectiveParameter(index, &env);

    double absTol = 0.0;
    double relTol = 0.0;
    if (multiObjConfig_.has_value() &&
        multiObjConfig_->higherPriObjConfig.has_value()) {
      const auto allowedWorseningPtr = folly::get_ptr(
          *multiObjConfig_->higherPriObjConfig->tuplePosToAllowedWorsening(),
          index);
      if (allowedWorseningPtr) {
        switch (*allowedWorseningPtr->intent()) {
          case algopt::common::thrift::Intent::MAX: {
            absTol = *allowedWorseningPtr->absolute();
            relTol = *allowedWorseningPtr->percent() / 100.0;
            break;
          }
        }
      }
    }

    // For Gurobi, higher priority value means that it is more important.
    // So, objective at tuple position i should have higher priority than
    // objective at tuple position j if i < j
    const auto priority = static_cast<int>(objectives_.size() - index);
    model_.setObjectiveN(
        std::get<GRBLinExpr>(grbExpr),
        index,
        priority,
        /*weight=*/1,
        absTol,
        relTol);
  }
}

void GurobiProblem::multiObjectiveSolve() try {
  model_.update();
  if (collectLpShadowPrices_ && !model_.get(GRB_IntAttr_IsMIP)) {
    // Native multi-objective optimization does not expose per-stage Pi.
    return customMultiObjectiveIterativeSolve();
  }
  if (!allExprsLinear(objectives_)) {
    // Gurobi does not support quadratic objectives in multi-objective solves,
    // so use custom implementation for that case
    return customMultiObjectiveIterativeSolve();
  }

  setMultiObjective();

  solveModel(maxSolveTime_);

  auto finalProblemStatus = getSolveStatus();
  auto problemAttributes = getProblemAttributes();
  for (const auto index : folly::irange(objectives_.size())) {
    // gurobi does not provide bestBound, absGap, relGap etc. when using
    // multi-objective solve. So just populate status and objective value. In
    // result-i, we store the value of objective-i and the store the
    // finalProblemStatus as the status for each of them
    thrift::ProblemResult result;
    result.status() = finalProblemStatus;
    result.problemAttributes() = problemAttributes;
    // there might be an exception if the solve was infeasible
    try {
      result.bestObjective() =
          model_.getObjective(static_cast<int>(index)).getValue();
    } catch (const GRBException& e) {
      XLOG(ERR) << fmt::format(
          "GRBException when accessing value of objective at index {}: {}. This is likely because the solve was infeasible",
          index,
          e.getMessage());
    }

    problemResultPerObjective_.push_back(std::move(result));

    if (finalProblemStatus == thrift::ProblemStatus::NO_SOLUTION_EXISTS ||
        finalProblemStatus == thrift::ProblemStatus::SOLUTION_NOT_FOUND) {
      // if the problem is infeasible or no solution was found, just return
      // one problemResult, which is consistent with how
      // customMultiObjectiveIterativeSolve() works
      break;
    }
  }
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

// Returns fingerprint of full Gurobi model (including all goals)
std::optional<uint32_t> GurobiProblem::getModelFingerprint() try {
  if (getObjectiveSize() > 1 && !allExprsLinear(objectives_)) {
    // only linear objectives are supported in Gurobi setObjectiveN
    XLOG(INFO)
        << "Modelfingerprint cannot be generated for non-linear tuple goals";
    return std::nullopt;
  }

  model_.update();
  auto modelNumObj = model_.get(GRB_IntAttr::GRB_IntAttr_NumObj);
  if (modelNumObj > 1) {
    // solved multi-objective model, objectives tuple already added to model,
    // get model fingerprint directly
    return model_.get(GRB_IntAttr::GRB_IntAttr_Fingerprint);
  } else if (modelNumObj == 1) {
    // either unsolved model, model solved with 1 objective, model solved with
    // customMultiObjectiveIterativeSolve

    // replace original goal with all objectives_
    auto mainObj = model_.getObjective();
    model_.set(GRB_IntAttr::GRB_IntAttr_NumObj,
               0); // model with no objective
    model_.update();

    if (getObjectiveSize() == 1) {
      setObjective(objectives_.at(0));
    } else {
      setMultiObjective(); // add objectives to tuple
    }
    model_.update();

    auto modelFingerprint = model_.get(GRB_IntAttr::GRB_IntAttr_Fingerprint);

    // restore original goals
    model_.set(GRB_IntAttr::GRB_IntAttr_NumObj,
               0); // model with no objective
    model_.update();
    model_.setObjective(mainObj);
    model_.update();

    return modelFingerprint;
  } else {
    XLOG(CRITICAL) << fmt::format(
        "Unexpected modelNumObj value {}", modelNumObj);
    return std::nullopt;
  }
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::solveForObjectiveAt(
    int pos,
    std::optional<double> timeLimit) try {
  // set objective
  auto objective = getObjectiveAt(pos);
  setObjective(objective);
  setMultiObjectiveParameter(pos, std::nullopt);
  solveModel(timeLimit);
  captureLpShadowPrices(pos);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::captureLpShadowPrices(int pos) try {
  if (!collectLpShadowPrices_) {
    return;
  }
  if (pos == 0) {
    lpShadowPrice_.reset();
  }
  const auto status = model_.get(GRB_IntAttr_Status);
  if ((status != GRB_OPTIMAL && status != GRB_SUBOPTIMAL) ||
      model_.get(GRB_IntAttr_IsMIP) || model_.get(GRB_IntAttr_NumQConstrs) ||
      model_.get(GRB_IntAttr_NumGenConstrs) || !allExprsLinear(objectives_)) {
    lpShadowPrice_.reset();
    return;
  }
  if (pos != 0 &&
      (!lpShadowPrice_ || lpShadowPrice_->objectives.size() != pos)) {
    return;
  }
  const std::unique_ptr<GRBConstr[]> constraints(model_.getConstrs());
  if (pos == 0) {
    lpShadowPrice_.emplace();
    const auto numConstraints = model_.get(GRB_IntAttr_NumConstrs);
    lpShadowPrice_->constraintNames.reserve(numConstraints);
    for (const auto i : folly::irange(numConstraints)) {
      lpShadowPrice_->constraintNames.push_back(
          constraints[i].get(GRB_StringAttr_ConstrName));
    }
  }
  const auto rowCount = lpShadowPrice_->constraintNames.size();
  const std::unique_ptr<double[]> prices(model_.get(
      GRB_DoubleAttr_Pi, constraints.get(), static_cast<int>(rowCount)));
  LpShadowPrices values;
  values.objectiveValue = model_.get(GRB_DoubleAttr_ObjVal);
  values.shadowPrices.assign(prices.get(), prices.get() + rowCount);
  if (!std::isfinite(values.objectiveValue) ||
      !std::all_of(
          values.shadowPrices.begin(),
          values.shadowPrices.end(),
          [](double price) { return std::isfinite(price); })) {
    lpShadowPrice_.reset();
    return;
  }
  // Each component is the stage's Gurobi Pi, holding earlier objective
  // bounds fixed. SUBOPTIMAL prices are approximate.
  lpShadowPrice_->objectives.push_back(std::move(values));
} catch (const GRBException& e) {
  if (e.getErrorCode() != GRB_ERROR_DATA_NOT_AVAILABLE) {
    throw;
  }
  lpShadowPrice_.reset();
  XLOG(WARN) << "LP shadow prices unavailable: " << e.getMessage();
}

std::optional<GurobiProblem::LpShadowPricesSnapshot>
GurobiProblem::takeLpShadowPrices() {
  if (!lpShadowPrice_ ||
      lpShadowPrice_->objectives.size() != objectives_.size()) {
    return std::nullopt;
  }
  return std::exchange(lpShadowPrice_, std::nullopt);
}

void GurobiProblem::solveModel(std::optional<double> timeLimit) {
  // set timeLimit if given
  if (timeLimit.has_value()) {
    setTimeout(timeLimit.value());
  }

  // The algorithm is not passed in here to optimize, but is set by
  // the parameter GRB_IntParam::GRB_IntParam_Method. This isn't super
  // ideal, as people need to be familiar with the specific API,
  // but it's not common to want to change this either. The docs say
  // the default is usually fine. For MIP this is dual simplex.

  // Always create a callback wrapper so we can capture presolve stats
  // (rows/cols deleted via GRB_CB_PRESOLVE, NNZs via GRB_CB_MESSAGE).
  // If the user provided a callback, wrap it; otherwise pass a no-op.
  auto presolveWrapper = std::make_unique<CallbackWrapper>(callback_.value_or(
      [](ProblemCallbackData) { return ProblemCallbackAction::CONTINUE; }));
  model_.setCallback(presolveWrapper.get());

  saveDebugData(DebugPhase::Pre);
  {
    // Gurobi raises TSAN issues, see T236804704.
    folly::annotate_ignore_thread_sanitizer_guard g(__FILE__, __LINE__);
    model_.optimize();
  }
  saveDebugData(DebugPhase::Post);

  // Extract presolve stats before destroying the wrapper.
  presolveColDel_ = presolveWrapper->getPresolveColDel();
  presolveRowDel_ = presolveWrapper->getPresolveRowDel();
  presolveNnz_ = presolveWrapper->getPresolveNnz();

  model_.setCallback(nullptr);
}

void GurobiProblem::setCallback(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback) {
  callback_ = callback;
}

thrift::ProblemStatus GurobiProblem::getSolveStatus() try {
  // For status code definitions:
  // https://www.gurobi.com/documentation/9.1/refman/optimization_status_codes.html#sec:StatusCodes
  auto status = model_.get(GRB_IntAttr::GRB_IntAttr_Status);
  const int solutionCount = model_.get(GRB_IntAttr::GRB_IntAttr_SolCount);
  switch (status) {
    case GRB_OPTIMAL:
      // Proven optimal solution found (subject to tolerances)
      return thrift::ProblemStatus::OPTIMAL_FOUND;

    case GRB_INTERRUPTED:
      // When solving is interrupted
    case GRB_SUBOPTIMAL:
      // Unable to satisfy optimality tolerances; a sub-optimal solution is
      // available
    case GRB_ITERATION_LIMIT:
      // Optimization terminated because the total number of simplex
      // iterations performed exceeded the value specified in the
      // IterationLimit parameter, or because the total number of barrier
      // iterations exceeded the value specified in the BarIterLimit
      // parameter.
    case GRB_NODE_LIMIT:
      // Optimization terminated because the total number of branch-and-cut
      // nodes explored exceeded the value specified in the NodeLimit
      // parameter.
    case GRB_TIME_LIMIT:
      // Optimization terminated because the time expended exceeded the value
      // specified in the TimeLimit parameter.
    case GRB_WORK_LIMIT:
      // Optimization terminated because the work expended exceeded the value
      // specified in the WorkLimit parameter.
    case GRB_SOLUTION_LIMIT:
      // Optimization terminated because the number of solutions found reached
      // the value specified in the SolutionLimit parameter.
    case GRB_USER_OBJ_LIMIT:
      return solutionCount == 0 ? thrift::ProblemStatus::SOLUTION_NOT_FOUND
                                : thrift::ProblemStatus::SOLUTION_FOUND;

    case GRB_INFEASIBLE:
      // Proven infeasible
    case GRB_INF_OR_UNBD:
      // This isn't technically correct for INF_OR_UNBD because it's
      // inconclusive. The DualReductions param can be set to get a
      // conclusive answer. See docs here:
      // https://www.gurobi.com/documentation/9.1/refman/dualreductions.html#parameter:DualReductions
    case GRB_UNBOUNDED:
      // This also isn't technically correct for UNBOUNDED because it implies
      // there's a ray that will infinitely improve the solution, which
      // means the model has a problem. Docs say to set the objective to
      // zero and try again for info on feasibility.
      return thrift::ProblemStatus::NO_SOLUTION_EXISTS;

    case GRB_CUTOFF:
      // Optimal objective for model was proven to be worse than the value
      // specified in the Cutoff parameter. No solution information is
      // available.
    case GRB_NUMERIC:
      // Optimization was terminated due to unrecoverable numerical
      // difficulties
      return thrift::ProblemStatus::SOLUTION_NOT_FOUND;

    case GRB_LOADED:
      // asking for a status prior to solve is invalid
      throw SolverError("No solve status available before calling solve()");
    case GRB_INPROGRESS:
      // only applicable to async solves
      throw SolverError("Unexpected Gurobi status GRB_INPROGRESS");
    default:
      throw SolverError(fmt::format("Unknown Gurobi status code {}", status));
  }
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

thrift::ProblemResult GurobiProblem::getSolveResult() try {
  double bestBound = std::numeric_limits<double>::lowest();
  double bestObjective = std::numeric_limits<double>::lowest();

  // trying to access these attributes when problem is infeasbile results in a
  // Gurobi exception
  try {
    bestObjective = model_.get(GRB_DoubleAttr::GRB_DoubleAttr_ObjVal);
  } catch (const GRBException& e) {
    XLOG(ERR) << fmt::format(
        "GRBException when accessing ObjVal: {}", e.getMessage());
  }

  // best bound is only available for MIP models
  try {
    bestBound = model_.get(GRB_DoubleAttr::GRB_DoubleAttr_ObjBound);
  } catch (const GRBException& e) {
    if (model_.get(GRB_IntAttr::GRB_IntAttr_IsMIP)) {
      XLOG(ERR) << fmt::format(
          "GRBException when accessing ObjBound: {}", e.getMessage());
    }
  }

  thrift::ProblemResult result;
  result.status() = getSolveStatus();
  result.bestBound() = bestBound;
  result.bestObjective() = bestObjective;
  result.absoluteGap() = bestObjective - bestBound;
  result.problemAttributes() = getProblemAttributes();

  const double denominator =
      std::max(std::abs(bestBound), std::abs(bestObjective));
  if (denominator == 0) {
    result.relativeGap() = 0;
  } else {
    result.relativeGap() = std::abs(bestObjective - bestBound) / denominator;
  }

  return result;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

// Returns attributes from current state of Gurobi model
thrift::ProblemAttributes GurobiProblem::getProblemAttributes() try {
  thrift::ProblemAttributes problemAttributes;
  const auto numVars = model_.get(GRB_IntAttr::GRB_IntAttr_NumVars);
  const auto numConstrs = model_.get(GRB_IntAttr::GRB_IntAttr_NumConstrs);
  problemAttributes.numVariables() = numVars;
  problemAttributes.numConstraints() = numConstrs;
  problemAttributes.numOfNonZeros() =
      model_.get(GRB_IntAttr::GRB_IntAttr_NumNZs);
  std::optional<uint32_t> modelFingerprint =
      model_.get(GRB_IntAttr::GRB_IntAttr_Fingerprint);
  if (modelFingerprint) {
    problemAttributes.modelFingerprint() =
        std::to_string(modelFingerprint.value());
  }
  // Post-presolve dimensions captured during optimize() via callbacks.
  // Row/col counts come from cumulative GRB_CB_PRE_COLDEL/ROWDEL counters,
  // NNZs come from parsing the "Presolved:" message line.
  if (presolveColDel_ > 0 || presolveRowDel_ > 0) {
    problemAttributes.presolvedVariables() = numVars - presolveColDel_;
    problemAttributes.presolvedConstraints() = numConstrs - presolveRowDel_;
  }
  if (presolveNnz_ >= 0) {
    problemAttributes.presolvedNonZeros() = presolveNnz_;
  }
  return problemAttributes;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::optional<double> GurobiProblem::getScaledMaxCoefRatio() {
  try {
    model_.update();

    XLOG(INFO) << "Starting scaled max coef calculating";

    const int numConstrs = model_.get(GRB_IntAttr_NumConstrs);
    const int numVars = model_.get(GRB_IntAttr_NumVars);

    if (numConstrs == 0 || numVars == 0) {
      return std::nullopt;
    }

    std::vector<CoefficientEntry> entries;
    const int nnz = model_.get(GRB_IntAttr_NumNZs);
    entries.reserve(nnz + numVars); // constraints + objective

    // Extract constraint rows.
    for (const auto i : folly::irange(numConstrs)) {
      const auto constr = model_.getConstr(i);
      const auto expr = model_.getRow(constr);
      for (const auto j : folly::irange(static_cast<int>(expr.size()))) {
        const double coeff = std::abs(expr.getCoeff(j));
        if (coeff > 0.0) {
          entries.push_back({i, expr.getVar(j).index(), coeff});
        }
      }
    }

    // Append objective as an additional row.
    {
      const GRBLinExpr objExpr = model_.getObjective().getLinExpr();
      for (const auto j : folly::irange(static_cast<int>(objExpr.size()))) {
        const double coeff = std::abs(objExpr.getCoeff(j));
        if (coeff > 0.0) {
          entries.push_back({numConstrs, objExpr.getVar(j).index(), coeff});
        }
      }
    }

    return computeScaledMaxCoefRatio(entries, numConstrs + 1, numVars);
  } catch (const GRBException& e) {
    XLOG(WARN) << fmt::format(
        "GRBException computing scaled max coef ratio: {}", e.getMessage());
    return std::nullopt;
  }
}

std::optional<NumericalStabilityInfo> GurobiProblem::getNumericalStability(
    bool computeExactKappa) try {
  model_.update();

  XLOG(INFO) << "Computing numerical stability info";

  const int numConstrs = model_.get(GRB_IntAttr_NumConstrs);
  const int numVars = model_.get(GRB_IntAttr_NumVars);

  if (numConstrs == 0 || numVars == 0) {
    return std::nullopt;
  }

  NumericalStabilityInfo info;

  // Extract constraint coefficient range.
  double minConstraintCoef = std::numeric_limits<double>::max();
  double maxConstraintCoef = 0.0;
  for (const auto i : folly::irange(numConstrs)) {
    const auto constr = model_.getConstr(i);
    const auto expr = model_.getRow(constr);
    for (const auto j : folly::irange(expr.size())) {
      const double coeff = std::abs(expr.getCoeff(j));
      if (coeff > 0.0) {
        minConstraintCoef = std::min(minConstraintCoef, coeff);
        maxConstraintCoef = std::max(maxConstraintCoef, coeff);
      }
    }
  }
  if (maxConstraintCoef > 0.0) {
    info.smallestConstraintCoef = minConstraintCoef;
    info.largestConstraintCoef = maxConstraintCoef;
  }

  // Extract objective coefficient range. getObjective() reports only the
  // first objective, so a multi-objective model has to be walked level by
  // level: the spread that matters is the one across the whole hierarchy.
  const int numObjectives = model_.get(GRB_IntAttr::GRB_IntAttr_NumObj);
  double minObjCoef = std::numeric_limits<double>::max();
  double maxObjCoef = 0.0;
  for (const auto n : folly::irange(std::max(numObjectives, 1))) {
    const GRBLinExpr objExpr = numObjectives > 1
        ? model_.getObjective(n)
        : model_.getObjective().getLinExpr();
    for (const auto j : folly::irange(objExpr.size())) {
      const double coeff = std::abs(objExpr.getCoeff(j));
      if (coeff > 0.0) {
        minObjCoef = std::min(minObjCoef, coeff);
        maxObjCoef = std::max(maxObjCoef, coeff);
      }
    }
  }
  if (maxObjCoef > 0.0) {
    info.smallestObjCoef = minObjCoef;
    info.largestObjCoef = maxObjCoef;
  }

  // Extract RHS range.
  double minRhs = std::numeric_limits<double>::max();
  double maxRhs = 0.0;
  for (const auto i : folly::irange(numConstrs)) {
    const auto constr = model_.getConstr(i);
    const double rhs = std::abs(constr.get(GRB_DoubleAttr_RHS));
    if (rhs > 0.0) {
      minRhs = std::min(minRhs, rhs);
      maxRhs = std::max(maxRhs, rhs);
    }
  }
  if (maxRhs > 0.0) {
    info.smallestRhs = minRhs;
    info.largestRhs = maxRhs;
  }

  // Extract variable bound range (finite, nonzero bounds only).
  double minBound = std::numeric_limits<double>::max();
  double maxBound = 0.0;
  for (const auto j : folly::irange(numVars)) {
    const auto var = model_.getVar(j);
    const double lb = std::abs(var.get(GRB_DoubleAttr_LB));
    const double ub = std::abs(var.get(GRB_DoubleAttr_UB));
    if (lb > 0.0 && lb < GRB_INFINITY) {
      minBound = std::min(minBound, lb);
      maxBound = std::max(maxBound, lb);
    }
    if (ub > 0.0 && ub < GRB_INFINITY) {
      minBound = std::min(minBound, ub);
      maxBound = std::max(maxBound, ub);
    }
  }
  if (maxBound > 0.0) {
    info.smallestBound = minBound;
    info.largestBound = maxBound;
  }

  // Get Kappa (condition number) - requires model to be solved.
  try {
    info.normalKappa = model_.get(GRB_DoubleAttr_Kappa);
  } catch (const GRBException&) {
    // Kappa not available (e.g., model not solved or no basis).
  }

  if (computeExactKappa) {
    try {
      info.exactKappa = model_.get(GRB_DoubleAttr_KappaExact);
    } catch (const GRBException&) {
      // Exact kappa not available.
    }
  }

  XLOG(INFO) << fmt::format(
      "Numerical stability: constraintCoef=[{:.6e}, {:.6e}], "
      "objCoef=[{:.6e}, {:.6e}], rhs=[{:.6e}, {:.6e}], "
      "bound=[{:.6e}, {:.6e}], normalKappa={:.6e}, exactKappa={:.6e}",
      info.smallestConstraintCoef,
      info.largestConstraintCoef,
      info.smallestObjCoef,
      info.largestObjCoef,
      info.smallestRhs,
      info.largestRhs,
      info.smallestBound,
      info.largestBound,
      info.normalKappa,
      info.exactKappa);

  return info;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

template <class M, class K = typename M::key_type>
static std::optional<typename M::mapped_type> mapGetOptional(
    const M& container,
    const K& key) {
  auto it = container.find(key);
  if (it != container.end()) {
    return std::make_optional(it->second);
  } else {
    return std::nullopt;
  }
}

std::optional<GRB_IntParam> GurobiProblem::getIntParamCode(
    const std::string& name) {
  return mapGetOptional(kIntParamCodes, name);
}

std::optional<GRB_DoubleParam> GurobiProblem::getDoubleParamCode(
    const std::string& name) {
  return mapGetOptional(kDoubleParamCodes, name);
}

std::optional<GurobiWarmStartType> GurobiProblem::getGurobiWarmStartType(
    int32_t value) {
  return mapGetOptional(kGurobiWarmStartTypesMap, value);
}

bool GurobiProblem::isValidHiddenParam(const std::string& name) {
  return kHiddenParameters.contains(name);
}

double GurobiProblem::getParameter(const std::string& name) try {
  auto intParam = getIntParamCode(name);
  if (intParam) {
    return model_.get(*intParam);
  }

  auto doubleParam = getDoubleParamCode(name);
  if (doubleParam) {
    return model_.get(*doubleParam);
  }

  throw SolverError(fmt::format("Unknown parameter {}", name));
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::setParameter(const std::string& name, double value) try {
  auto intParam = getIntParamCode(name);
  if (intParam) {
    auto oldValue = model_.get(*intParam);
    model_.set(*intParam, static_cast<int>(value));

    XLOG(DBG) << fmt::format(
        "Updating Gurobi param {} from {} to {}", name, oldValue, value);
    return;
  }

  auto doubleParam = getDoubleParamCode(name);
  if (doubleParam) {
    auto oldValue = model_.get(*doubleParam);
    model_.set(*doubleParam, value);

    XLOG(DBG) << fmt::format(
        "Updating Gurobi param {} from {} to {}", name, oldValue, value);
    return;
  }

  if (isValidHiddenParam(name)) {
    model_.set(name, folly::to<std::string>(static_cast<int>(value)));

    XLOG(DBG) << fmt::format("Updating Gurobi param {} to {}", name, value);
    return;
  }

  if (name == kGurobiWarmStartOverride) {
    auto warmStartOverride = static_cast<int>(value);
    auto warmStartTypeOpt = getGurobiWarmStartType(warmStartOverride);
    if (!warmStartTypeOpt) {
      throw SolverError(
          fmt::format(
              "Unknown value {} for parameter {}", warmStartOverride, name));
    }
    warmStartType_ = *warmStartTypeOpt;
    XLOG(INFO) << fmt::format(
        "Updating gurobi warm start type to {}", warmStartOverride);
    return;
  }

  throw SolverError(fmt::format("Unknown parameter {}", name));
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::setTolerances(const Tolerances& tol) try {
  const std::string constraintTolParamName = "GRB_FeasibilityTol";
  auto constraintTolVal = checkAndAdjustToleranceWithinRange(
      tol.constraint,
      kGurobiMinConstraintTolerance,
      kGurobiMaxConstraintTolerance,
      constraintTolParamName);
  setParameter(constraintTolParamName, constraintTolVal);

  const std::string integerTolParamName = "GRB_IntFeasTol";
  auto integerTolVal = checkAndAdjustToleranceWithinRange(
      tol.integer,
      kGurobiMinIntegerTolerance,
      kGurobiMaxIntegerTolerance,
      integerTolParamName);
  setParameter(integerTolParamName, integerTolVal);

  setParameter("GRB_MIPGap", tol.relgap);
  setParameter("GRB_MIPGapAbs", tol.absgap);
  setParameter("GRB_FeasibilityTol", tol.constraint);
  // intentionally ignoring tol.relcut
  // intentionally ignoring tol.abscut
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

Tolerances GurobiProblem::getTolerances() try {
  return {
      .constraint = getParameter("GRB_FeasibilityTol"),
      .integer = getParameter("GRB_IntFeasTol"),
      .absgap = getParameter("GRB_MIPGapAbs"),
      .relgap = getParameter("GRB_MIPGap"),
      // intentionally ignoring tol.relcut
      // intentionally ignoring tol.abscut
  };
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::setLogfile(const std::string& filename) try {
  model_.set(GRB_StringParam::GRB_StringParam_LogFile, filename);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::saveToFile(const std::string& filename) try {
  model_.write(filename);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}
void GurobiProblem::saveToFileWithObjectiveAt(
    int pos,
    const std::string& filename) try {
  setObjective(objectives_.at(pos));
  saveToFile(filename);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::saveToFileWithAllObjectives(
    const std::string& filename) try {
  if (!objectives_.empty()) {
    // Clear any objective already on the model so that re-installing is
    // idempotent and the previous objective doesn't linger as objective 0.
    model_.set(GRB_IntAttr::GRB_IntAttr_NumObj, 0);
    model_.update();
    if (objectives_.size() == 1) {
      setObjective(objectives_.at(0));
    } else {
      setMultiObjective();
    }
    model_.update();
  }
  saveToFile(filename);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}
void GurobiProblem::setDebugPath(const std::string& path) {
  debugPath_ = path;
}

void GurobiProblem::print(
    const std::vector<std::string>& substringsToMatch) try {
  // Gurobi follows lazy-update, that is the model is not updated
  // until explicitly asked to or if optimize is called.
  // Ensure that variable/constraint information is loaded to the model
  model_.update();

  // some statistics about variables and constraints
  int numVars = model_.get(GRB_IntAttr_NumVars);
  int numConstraints = model_.get(GRB_IntAttr_NumConstrs);
  int numOccurrences = 0;
  auto tolerances = getTolerances();
  XLOG(INFO) << fmt::format(
      "Tolerances: constraint feasibility {}, integer feasibility {}, absolute gap {}, relative gap {}",
      tolerances.constraint,
      tolerances.integer,
      tolerances.absgap,
      tolerances.relgap);

  auto printThisConstraint = [&](const auto& constrName) {
    if (substringsToMatch.empty()) {
      return true;
    }
    for (const auto& substr : substringsToMatch) {
      if (constrName.find(substr) != std::string::npos) {
        return true;
      }
    }
    return false;
  };

  for (const auto i : folly::irange(numConstraints)) {
    const auto constr = model_.getConstr(i);
    const auto expr = model_.getRow(constr);
    numOccurrences += expr.size();

    // constraint is specified as:
    // constrExpr [SENSE] RHS
    // SENSE is one of {<, >, =}
    const auto constrName = constr.get(GRB_StringAttr_ConstrName);
    const auto rhsVal = constr.get(GRB_DoubleAttr_RHS);
    const auto sense = constr.get(GRB_CharAttr_Sense);

    // a constraint in LEQ form is considered standard form
    const int stdFormMultiplier = sense == '>' ? -1 : 1;

    const auto constrVal = (expr.getValue() - rhsVal) * stdFormMultiplier;

    if (printThisConstraint(constrName)) {
      XLOG(INFO) << fmt::format(
          "[Name: {} Val: {}] {} {} {}",
          constr.get(GRB_StringAttr_ConstrName),
          constrVal,
          constrExprToString(expr, /*nonzeroValuesOnly=*/true),
          sense,
          rhsVal);
    }
  }
  XLOG(INFO) << fmt::format(
      "Total {} variables and {} constraints with avg {} variables per constraint",
      numVars,
      numConstraints,
      (1.0 * numOccurrences) / numConstraints);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::disableLogs() try {
  model_.set(GRB_IntParam::GRB_IntParam_OutputFlag, 0);
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::setTimeout(double solveTime) {
  model_.set(GRB_DoubleParam::GRB_DoubleParam_TimeLimit, solveTime);
}

void GurobiProblem::addStartValue(
    std::shared_ptr<const VariableImpl> variable,
    double value) try {
  // Get the VariableImpl* from the shared_ptr,
  // cast to a GurobiVariable*,
  // then get the actual GRBVar from that
  auto gurobiVariable =
      (dynamic_cast<const GurobiVariable*>(variable.get()))->get();

  if (warmStartType_ == GurobiWarmStartType::MIP_START) {
    gurobiVariable.set(GRB_DoubleAttr::GRB_DoubleAttr_Start, value);
  } else if (warmStartType_ == GurobiWarmStartType::VARIABLE_HINT) {
    gurobiVariable.set(GRB_DoubleAttr::GRB_DoubleAttr_VarHintVal, value);
  } else {
    gurobiVariable.set(GRB_DoubleAttr::GRB_DoubleAttr_VarHintVal, value);
    gurobiVariable.set(GRB_DoubleAttr::GRB_DoubleAttr_Start, value);
  }
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

int GurobiProblem::updatePresolvedNonZeros(
    int current,
    std::string_view message) {
  // Hierarchical multi-objective solves presolve again in every objective
  // pass and print a "Presolved:" line each time; a later pass can shrink to a
  // handful of rows, so a last-line-wins value mixes dimensions across passes.
  if (current >= 0 || message.find("Presolved:") == std::string_view::npos) {
    return current;
  }
  const auto nnzPos = message.find("nonzeros");
  if (nnzPos == std::string_view::npos) {
    return current;
  }
  const auto end = message.rfind(' ', nnzPos - 1);
  if (end == std::string_view::npos) {
    return current;
  }
  auto start = message.rfind(' ', end - 1);
  if (start == std::string_view::npos) {
    start = message.rfind(',', end - 1);
  }
  if (start == std::string_view::npos) {
    return current;
  }
  try {
    return std::stoi(std::string(message.substr(start + 1, end - start - 1)));
  } catch (const std::exception&) {
    return current;
  }
}

GurobiProblem::CallbackWrapper::CallbackWrapper(
    std::function<ProblemCallbackAction(ProblemCallbackData)> callback)
    : callback_(std::move(callback)),
      bound_(-INFINITY),
      objective_(INFINITY),
      timer_(true),
      aborted_(false),
      presolveColDel_(0),
      presolveRowDel_(0),
      presolveNnz_(-1) {}

void GurobiProblem::CallbackWrapper::callback() {
  if (aborted_) {
    return;
  }
  if (where == GRB_CB_PRESOLVE) {
    // Track cumulative deletions across presolve passes.
    presolveColDel_ = getIntInfo(GRB_CB_PRE_COLDEL);
    presolveRowDel_ = getIntInfo(GRB_CB_PRE_ROWDEL);
    return;
  }
  if (where == GRB_CB_MESSAGE) {
    presolveNnz_ = GurobiProblem::updatePresolvedNonZeros(
        presolveNnz_, getStringInfo(GRB_CB_MSG_STRING));
    return;
  }
  if (where == GRB_CB_MIPNODE) {
    objective_ = getDoubleInfo(GRB_CB_MIPNODE_OBJBST);
    bound_ = getDoubleInfo(GRB_CB_MIPNODE_OBJBND);
  } else if (where == GRB_CB_MIPSOL) {
    objective_ = getDoubleInfo(GRB_CB_MIPSOL_OBJBST);
    bound_ = getDoubleInfo(GRB_CB_MIPSOL_OBJBND);
  } else if (where == GRB_CB_MIP) {
    objective_ = getDoubleInfo(GRB_CB_MIP_OBJBST);
    bound_ = getDoubleInfo(GRB_CB_MIP_OBJBND);
  }
  if (callback_(
          {.bound = bound_,
           .objective = objective_,
           .walltime = timer_.getSeconds()}) == ProblemCallbackAction::ABORT) {
    abort();
    aborted_ = true;
  }
}

int GurobiProblem::CallbackWrapper::getPresolveColDel() const {
  return presolveColDel_;
}

int GurobiProblem::CallbackWrapper::getPresolveRowDel() const {
  return presolveRowDel_;
}

int GurobiProblem::CallbackWrapper::getPresolveNnz() const {
  return presolveNnz_;
}

std::optional<IIS> GurobiProblem::getIIS() try {
  auto status = model_.get(GRB_IntAttr::GRB_IntAttr_Status);
  XLOG(INFO) << fmt::format(
      "Computing IIS, model status is {} ({})",
      status,
      kModelStatusCodes.at(status));
  if (status == GRB_INF_OR_UNBD) {
    // https://support.gurobi.com/hc/en-us/articles/4402704428177-How-do-I-resolve-the-error-Model-is-infeasible-or-unbounded
    XLOG(INFO) << fmt::format(
        "Model status {} ({}) is ambiguous, trying to pinpoint actual value",
        status,
        kModelStatusCodes.at(status));
    model_.set(GRB_IntParam_DualReductions, 0);
    model_.reset();
    model_.optimize();
    status = model_.get(GRB_IntAttr::GRB_IntAttr_Status);
    XLOG(INFO) << fmt::format(
        "Actual model status is {} ({})", status, kModelStatusCodes.at(status));
  }
  if (status != GRB_INFEASIBLE) {
    throw SolverError(
        fmt::format(
            "getIIS should only be called for status GRB_INFEASIBLE, but status is {} ({})",
            status,
            kModelStatusCodes.at(status)));
  }

  model_.computeIIS();

  // From the docs
  // (https://www.gurobi.com/documentation/current/refman/cpp_model_getconstrs.html):
  // An array of all linear constraints in the model. Note that this array is
  // heap-allocated, and must be returned to the heap by the user.
  auto constraints = model_.getConstrs();

  // From the docs
  // (https://www.gurobi.com/documentation/current/refman/cpp_model_getvars.html):
  // An array of all variables in the model. Note that this array is
  // heap-allocated, and must be returned to the heap by the user.
  auto variables = model_.getVars();

  IIS iis;
  for (const auto i : folly::irange(model_.get(GRB_IntAttr_NumConstrs))) {
    if (constraints[i].get(GRB_IntAttr_IISConstr) == 1) {
      auto constName = constraints[i].get(GRB_StringAttr_ConstrName);
      iis.constraintIds.push_back(constName);
    }
  }
  for (const auto i : folly::irange(model_.get(GRB_IntAttr_NumVars))) {
    if (variables[i].get(GRB_IntAttr_IISLB) == 1) {
      auto varName = variables[i].get(GRB_StringAttr_VarName);
      iis.lowerBoundVars.push_back(varName);
    }
    if (variables[i].get(GRB_IntAttr_IISUB) == 1) {
      auto varName = variables[i].get(GRB_StringAttr_VarName);
      iis.upperBoundVars.push_back(varName);
    }
  }

  // Return to the heap.
  delete[] constraints;
  delete[] variables;

  return iis;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

void GurobiProblem::replay(const std::string& fileName) const try {
  GRBModel model(model_.getEnv(), fileName);
  model.optimize();
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

bool GurobiProblem::supportsNativeQuadratic() const {
  return algopt::useGurobiNativeQuadratic();
}

bool GurobiProblem::supportsNativePwl() const {
  return algopt::useGurobiNativePwl();
}

bool GurobiProblem::supportsNativeMax() const {
  return algopt::useGurobiNativeMax();
}

bool GurobiProblem::supportsIndicatorConstraints() const {
  return algopt::useGurobiNativeStep();
}

bool GurobiProblem::setIndicatorOnConstraint(
    Constraint& ctr,
    const Variable& binaryVar,
    int dir) try {
  // Validate arguments before mutating the model so an invalid dir cannot
  // leave the model inconsistent (linear constraint removed, no indicator
  // added, caller's Constraint holding a stale handle).
  if (dir != 0 && dir != 1) {
    throw std::invalid_argument(
        fmt::format(
            "setIndicatorOnConstraint: dir must be 0 or 1, got {}", dir));
  }
  auto* gurobiCtr = dynamic_cast<GurobiConstraint*>(ctr.get().get());
  const auto* gurobiVar =
      dynamic_cast<const GurobiVariable*>(binaryVar.get().get());
  if (!gurobiCtr || !gurobiVar) {
    return false;
  }
  auto& variant = gurobiCtr->get();
  if (!std::holds_alternative<GRBConstr>(variant)) {
    return false; // only linear constraints can be converted to indicators
  }
  auto linCtr = std::get<GRBConstr>(variant);
  // Gurobi batches model changes and requires update() before attributes of
  // a newly-added constraint (getRow, Sense, RHS) become readable.
  model_.update();
  // Capture row, sense, and RHS before touching the model.
  const GRBLinExpr linExpr = model_.getRow(linCtr);
  const char sense = linCtr.get(GRB_CharAttr_Sense);
  const double rhs = linCtr.get(GRB_DoubleAttr_RHS);
  // Add the indicator before removing the original linear constraint. If
  // addGenConstrIndicator throws (e.g. the variable is not actually binary, or
  // Gurobi rejects the sense/rhs combination), the model still holds the
  // original constraint and the caller's GurobiConstraint keeps its valid
  // GRBConstr handle, so both stay consistent (basic exception guarantee).
  const GRBGenConstr gc =
      model_.addGenConstrIndicator(gurobiVar->get(), dir, linExpr, sense, rhs);
  model_.remove(linCtr);
  gurobiCtr->setGenConstr(gc);
  return true;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::optional<algopt::lp::Expression> GurobiProblem::addNativePwlConstraint(
    const algopt::lp::Expression& x,
    const std::vector<std::pair<double, double>>& points) try {
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
              "PWL breakpoints must be strictly increasing by x: "
              "x[{}]={} <= x[{}]={}",
              i,
              points[i].first,
              i - 1,
              points[i - 1].first));
    }
  }
  const auto* grbExpr = dynamic_cast<const GurobiExpression*>(x.get().get());
  if (!grbExpr || !std::holds_alternative<GRBLinExpr>(grbExpr->get())) {
    return std::nullopt; // quadratic or non-Gurobi inputs unsupported
  }
  const auto& xLinExpr = std::get<GRBLinExpr>(grbExpr->get());

  const double xMin = points.front().first;
  const double xMax = points.back().first;
  double yMin = points.front().second;
  double yMax = points.front().second;
  for (const auto i : folly::irange(size_t{1}, points.size())) {
    yMin = std::min(yMin, points[i].second);
    yMax = std::max(yMax, points[i].second);
  }

  // Bound xAux to the PWL breakpoint domain. The linking equality
  // (xAux == xLinExpr) propagates this bound back to the caller's expression,
  // so the caller must guarantee xLinExpr stays within [xMin, xMax] at every
  // feasible solution. Piecewise::lp() enforces this via a proven-bounds gate
  // (childLb >= xMin && childUb <= xMax) before calling this method.
  const GRBVar xAux = addVar(xMin, xMax, 0, GRB_CONTINUOUS, "pwl_x");
  const GRBVar yVar = addVar(yMin, yMax, 0, GRB_CONTINUOUS, "pwl_y");

  // Link xAux to the input expression: xAux - xLinExpr == 0.
  GRBLinExpr linkExpr(xAux);
  linkExpr -= xLinExpr;
  model_.addConstr(linkExpr == 0, "pwl_x_link");

  // addGenConstrPWL requires x-coordinates to be strictly increasing; the
  // validation above guarantees this.
  const int npts = static_cast<int>(points.size());
  std::vector<double> xpts, ypts;
  xpts.reserve(npts);
  ypts.reserve(npts);
  for (const auto& [xi, yi] : points) {
    xpts.push_back(xi);
    ypts.push_back(yi);
  }
  model_.addGenConstrPWL(xAux, yVar, npts, xpts.data(), ypts.data(), "pwl");

  return algopt::lp::Expression(
      std::make_shared<GurobiExpression>(GRBLinExpr(yVar)));
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

std::optional<algopt::lp::Expression> GurobiProblem::addNativeMaxConstraint(
    const std::vector<algopt::lp::Expression>& inputs) try {
  if (inputs.empty()) {
    throw std::invalid_argument(
        "MAX constraint requires at least 1 input expression");
  }

  // Validate all inputs before mutating the model. A failed cast on a later
  // input must not leave orphan auxiliary vars/constraints behind, since the
  // caller falls back to the approximation path on std::nullopt.
  std::vector<GRBLinExpr> inputLinExprs;
  inputLinExprs.reserve(inputs.size());
  for (const auto& input : inputs) {
    const auto* grbExpr =
        dynamic_cast<const GurobiExpression*>(input.get().get());
    if (!grbExpr || !std::holds_alternative<GRBLinExpr>(grbExpr->get())) {
      return std::nullopt;
    }
    inputLinExprs.push_back(std::get<GRBLinExpr>(grbExpr->get()));
  }

  const GRBVar resultVar =
      addVar(-GRB_INFINITY, GRB_INFINITY, 0, GRB_CONTINUOUS, "max_result");

  std::vector<GRBVar> inputAuxVars;
  inputAuxVars.reserve(inputs.size());
  for (const auto i : folly::irange(inputLinExprs.size())) {
    const auto& linExpr = inputLinExprs.at(i);
    const GRBVar inputAux = addVar(
        -GRB_INFINITY,
        GRB_INFINITY,
        0,
        GRB_CONTINUOUS,
        fmt::format("max_input_{}", i));
    GRBLinExpr linkExpr(inputAux);
    linkExpr -= linExpr;
    model_.addConstr(linkExpr == 0, fmt::format("max_input_link_{}", i));
    inputAuxVars.push_back(inputAux);
  }
  model_.addGenConstrMax(
      resultVar,
      inputAuxVars.data(),
      static_cast<int>(inputAuxVars.size()),
      -GRB_INFINITY,
      "max_gencons");

  return algopt::lp::Expression(
      std::make_shared<GurobiExpression>(GRBLinExpr(resultVar)));
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

namespace {

thrift::VariableType gurobiVtypeToThrift(char vtype) {
  switch (vtype) {
    case GRB_CONTINUOUS:
      return thrift::VariableType::CONTINUOUS;
    case GRB_BINARY:
      return thrift::VariableType::BINARY;
    case GRB_INTEGER:
      return thrift::VariableType::INTEGER;
    case GRB_SEMICONT:
      return thrift::VariableType::SEMI_CONTINUOUS;
    case GRB_SEMIINT:
      return thrift::VariableType::SEMI_INTEGER;
    default:
      throw SolverError(
          fmt::format("Unknown Gurobi variable type '{}'", vtype));
  }
}

char mapVarType(VariableImpl::Type type) {
  switch (type) {
    case VariableImpl::Type::CONTINUOUS:
      return GRB_CONTINUOUS;
    case VariableImpl::Type::INTEGER:
      return GRB_INTEGER;
    case VariableImpl::Type::BINARY:
      return GRB_BINARY;
    case VariableImpl::Type::SEMI_CONTINUOUS:
      return GRB_SEMICONT;
    case VariableImpl::Type::SEMI_INTEGER:
      return GRB_SEMIINT;
    case VariableImpl::Type::UNSET:
      throw SolverError("Variable type UNSET in loadFromFastProblem");
  }
  throw SolverError(
      fmt::format("Unknown variable type: {}", static_cast<int>(type)));
}

char mapConstrSense(Relation::ConstantType sense) {
  switch (sense) {
    case Relation::LE_ZERO:
      return GRB_LESS_EQUAL;
    case Relation::GE_ZERO:
      return GRB_GREATER_EQUAL;
    case Relation::EQ_ZERO:
      return GRB_EQUAL;
  }
  throw SolverError(
      fmt::format("Unknown constraint sense: {}", static_cast<int>(sense)));
}

thrift::ConstraintType gurobiSenseToThrift(char sense) {
  switch (sense) {
    case GRB_EQUAL:
      return thrift::ConstraintType::EQUALS_ZERO;
    case GRB_LESS_EQUAL:
      return thrift::ConstraintType::LEQ_ZERO;
    case GRB_GREATER_EQUAL:
      return thrift::ConstraintType::GEQ_ZERO;
    default:
      throw SolverError(
          fmt::format("Unknown Gurobi constraint sense '{}'", sense));
  }
}

thrift::GenericExpression linExprToThrift(
    const GRBLinExpr& expr,
    double constantOffset) {
  thrift::GenericExpression thriftExpr;
  thriftExpr.constant() = expr.getConstant() + constantOffset;
  for (const auto j : folly::irange(expr.size())) {
    const double coeff = expr.getCoeff(j);
    if (coeff != 0) {
      (*thriftExpr.linearCoeffs())[expr.getVar(j).index()] = coeff;
    }
  }
  return thriftExpr;
}

thrift::GenericExpression quadExprToThrift(
    const GRBQuadExpr& expr,
    double constantOffset) {
  // Extract the linear part.
  const auto linExpr = expr.getLinExpr();
  auto thriftExpr = linExprToThrift(linExpr, constantOffset);

  // Extract the quadratic part.
  for (const auto j : folly::irange(expr.size())) {
    const double coeff = expr.getCoeff(j);
    if (coeff != 0) {
      (*thriftExpr.quadraticCoeffs())[expr.getVar1(j).index()]
                                     [expr.getVar2(j).index()] = coeff;
    }
  }
  return thriftExpr;
}

} // namespace

lp::thrift::GenericProblem GurobiProblem::toThrift() const try {
  model_.update();

  lp::thrift::GenericProblem problem;

  // Extract variables.
  const int numVars = model_.get(GRB_IntAttr_NumVars);
  problem.variables()->reserve(numVars);
  for (const auto i : folly::irange(numVars)) {
    const auto var = model_.getVar(i);
    thrift::GenericVariable thriftVar;
    thriftVar.name() = var.get(GRB_StringAttr_VarName);
    const char vtype = var.get(GRB_CharAttr_VType);
    thriftVar.type() = gurobiVtypeToThrift(vtype);

    const double lb = var.get(GRB_DoubleAttr_LB);
    const double ub = var.get(GRB_DoubleAttr_UB);

    if (vtype == GRB_SEMICONT || vtype == GRB_SEMIINT) {
      // For semi-continuous/semi-integer variables, Gurobi stores the
      // threshold as the lower bound.
      thriftVar.threshold() = lb;
    } else if (lb > -GRB_INFINITY) {
      thriftVar.lowerBound() = lb;
    }
    if (ub < GRB_INFINITY) {
      thriftVar.upperBound() = ub;
    }

    problem.variables()->push_back(std::move(thriftVar));
  }

  // Extract linear constraints.
  const int numConstrs = model_.get(GRB_IntAttr_NumConstrs);
  problem.constraints()->reserve(numConstrs);
  for (const auto i : folly::irange(numConstrs)) {
    const auto constr = model_.getConstr(i);
    const auto row = model_.getRow(constr);
    const double rhs = constr.get(GRB_DoubleAttr_RHS);
    const char sense = constr.get(GRB_CharAttr_Sense);

    // Convert from Gurobi format (row SENSE rhs) to thrift format
    // ((row - rhs) TYPE 0), so the constant offset is -rhs.
    thrift::GenericConstraint thriftConstr;
    thriftConstr.expr() = linExprToThrift(row, -rhs);
    thriftConstr.type() = gurobiSenseToThrift(sense);
    thriftConstr.name() = constr.get(GRB_StringAttr_ConstrName);

    problem.constraints()->push_back(std::move(thriftConstr));
  }

  // Extract quadratic constraints.
  const int numQConstrs = model_.get(GRB_IntAttr_NumQConstrs);
  if (numQConstrs > 0) {
    auto* qconstrs = model_.getQConstrs();
    for (const auto i : folly::irange(numQConstrs)) {
      const auto& qconstr = qconstrs[i];
      const auto row = model_.getQCRow(qconstr);
      const double rhs = qconstr.get(GRB_DoubleAttr_QCRHS);
      const char sense = qconstr.get(GRB_CharAttr_QCSense);

      thrift::GenericConstraint thriftConstr;
      thriftConstr.expr() = quadExprToThrift(row, -rhs);
      thriftConstr.type() = gurobiSenseToThrift(sense);
      thriftConstr.name() = qconstr.get(GRB_StringAttr_QCName);

      problem.constraints()->push_back(std::move(thriftConstr));
    }
    delete[] qconstrs;
  }

  // Extract objectives.
  for (const auto& objective : objectives_) {
    const auto& grbExpr =
        dynamic_cast<const GurobiExpression&>(*objective).get();
    if (std::holds_alternative<GRBLinExpr>(grbExpr)) {
      problem.objectives()->push_back(
          linExprToThrift(std::get<GRBLinExpr>(grbExpr), 0));
    } else {
      problem.objectives()->push_back(
          quadExprToThrift(std::get<GRBQuadExpr>(grbExpr), 0));
    }
  }

  return problem;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

namespace {

GRBLinExpr buildLinExpr(
    const std::vector<Term>& terms,
    const double constant,
    const std::vector<int>& fastToGurobiVarId,
    const GRBVar* grbVars,
    std::vector<double>& coeffs,
    std::vector<GRBVar>& vars) {
  GRBLinExpr expr(constant);
  if (!terms.empty()) {
    const auto& numTerms = static_cast<int>(terms.size());
    coeffs.resize(numTerms);
    vars.resize(numTerms);
    for (const auto t : folly::irange(numTerms)) {
      coeffs[t] = terms[t].coefficient;
      vars[t] = grbVars[fastToGurobiVarId[terms[t].variableId]];
    }
    expr.addTerms(coeffs.data(), vars.data(), numTerms);
  }
  return expr;
}

} // namespace

void GurobiProblem::loadFromFastProblem(const FastProblemImpl& fast) try {
  if (!objectives_.empty()) {
    throw std::invalid_argument(
        "loadFromFastProblem must be called on a fresh GurobiProblem");
  }

  const auto& sortedVarIds = fast.buildSortedVarIds();
  const auto& sortedConstrIds = fast.buildSortedConstrIds();
  const auto& numVars = static_cast<int>(fast.getVariableCount());

  // Add variables in sorted order.
  // Keep grbVarArray alive for the entire function to avoid per-term
  // model.getVar() lookups in buildLinExpr.
  std::vector<double> lbs(numVars);
  std::vector<double> ubs(numVars);
  std::vector<char> varTypes(numVars);
  std::vector<std::string> varNames(numVars);
  std::vector<double> initialValues(numVars, GRB_UNDEFINED);
  bool hasInitialValues = false;
  for (const auto grbVarId : folly::irange(numVars)) {
    const auto& var = fast.getVariable(sortedVarIds[grbVarId]);
    lbs[grbVarId] = var.lb;
    ubs[grbVarId] = var.ub;
    varTypes[grbVarId] = mapVarType(var.type);
    varNames[grbVarId] = var.name;
    if (var.initialValue.has_value()) {
      initialValues[grbVarId] = *var.initialValue;
      hasInitialValues = true;
    }
  }
  const std::unique_ptr<GRBVar[]> grbVarArray(model_.addVars(
      lbs.data(),
      ubs.data(),
      nullptr,
      varTypes.data(),
      varNames.data(),
      numVars));
  if (hasInitialValues) {
    if (warmStartType_ != GurobiWarmStartType::MIP_START) {
      model_.set(
          GRB_DoubleAttr::GRB_DoubleAttr_VarHintVal,
          grbVarArray.get(),
          initialValues.data(),
          numVars);
    }
    if (warmStartType_ != GurobiWarmStartType::VARIABLE_HINT) {
      model_.set(
          GRB_DoubleAttr::GRB_DoubleAttr_Start,
          grbVarArray.get(),
          initialValues.data(),
          numVars);
    }
  }
  model_.update();
  // Keep the load-order names for getSolvedVariableValues (avoids re-fetching
  // them from Gurobi after the solve).
  loadedVarNames_ = std::move(varNames);

  // Build mapping: FastProblemImpl creation-order index -> Gurobi position.
  std::vector<int> fastToGrbVarId(numVars);
  for (const auto grbVarId : folly::irange(numVars)) {
    fastToGrbVarId[sortedVarIds[grbVarId]] = grbVarId;
  }

  // Reusable scratch buffers for buildLinExpr to avoid repeated heap
  // allocations in the constraint and objective loops.
  std::vector<double> coeffs;
  std::vector<GRBVar> vars;

  // Add constraints in sorted order.
  {
    const auto& numConstrs = static_cast<int>(sortedConstrIds.size());
    std::vector<GRBLinExpr> constrExprs(numConstrs);
    std::vector<char> constrSenses(numConstrs);
    std::vector<std::string> constrNames(numConstrs);
    for (const auto constrId : folly::irange(numConstrs)) {
      const auto& constr = fast.getConstraint(sortedConstrIds[constrId]);
      constrExprs[constrId] = buildLinExpr(
          constr.terms,
          -constr.rhs,
          fastToGrbVarId,
          grbVarArray.get(),
          coeffs,
          vars);
      constrSenses[constrId] = mapConstrSense(constr.sense);
      constrNames[constrId] = constr.name;
    }
    const std::unique_ptr<GRBConstr[]> grbConstrs(model_.addConstrs(
        constrExprs.data(),
        constrSenses.data(),
        nullptr,
        constrNames.data(),
        numConstrs));
  }

  // Add objectives.
  uint64_t totalObjTerms = 0;
  const auto& numObjectives = fast.getObjectiveSize();
  objectives_.reserve(numObjectives);
  for (const auto objIdx : folly::irange(numObjectives)) {
    const auto& objImpl =
        static_cast<const FastExpressionImpl&>(*fast.getObjectiveAt(objIdx));
    const auto& terms = objImpl.getTerms();
    totalObjTerms += terms.size();
    objectives_.push_back(
        std::make_shared<GurobiExpression>(buildLinExpr(
            terms,
            objImpl.getConstant(),
            fastToGrbVarId,
            grbVarArray.get(),
            coeffs,
            vars)));
  }
  XLOG(INFO) << "Fast build transfer: " << numObjectives << " goals, "
             << totalObjTerms << " total goal terms";
  model_.update();
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

folly::F14FastMap<std::string, double> GurobiProblem::getSolvedVariableValues()
    const try {
  const auto numVars = static_cast<int>(loadedVarNames_.size());
  CHECK_EQ(numVars, model_.get(GRB_IntAttr_NumVars))
      << "getSolvedVariableValues requires loadFromFastProblem to have "
         "populated loadedVarNames_ (load-order names out of sync with model)";
  const std::unique_ptr<GRBVar[]> vars(model_.getVars());
  const std::unique_ptr<double[]> varValues(
      model_.get(GRB_DoubleAttr_X, vars.get(), numVars));

  folly::F14FastMap<std::string, double> result;
  result.reserve(numVars);
  for (const auto grbVarId : folly::irange(numVars)) {
    auto [it, inserted] = result.emplace(
        std::move(loadedVarNames_[grbVarId]), varValues[grbVarId]);
    if (!inserted) {
      throw SolverError(
          fmt::format(
              "Duplicate variable name '{}' detected in Gurobi model at index {}. "
              "Previous value: {}, new value: {}. "
              "Gurobi variable names must be unique.",
              it->first,
              grbVarId,
              it->second,
              varValues[grbVarId]));
    }
  }
  loadedVarNames_.clear();
  loadedVarNames_.shrink_to_fit();
  return result;
} catch (const GRBException& e) {
  rethrowGurobiError(e);
}

} // namespace facebook::algopt::lp::detail

#endif
