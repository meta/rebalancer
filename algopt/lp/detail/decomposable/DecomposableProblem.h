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

#include "algopt/lp/detail/generic/impl/GenericExpressionImpl.h"
#include "algopt/lp/detail/generic/impl/GenericProblemImpl.h"
#include "algopt/lp/detail/generic/impl/GenericRelationImpl.h"
#include "algopt/lp/detail/generic/impl/GenericVariableImpl.h"

#include <folly/container/F14Set.h>

#include <optional>

namespace facebook::algopt::lp::detail {

class DecomposableExpression;

class DecomposableProblem : public GenericProblemImpl {
 public:
  explicit DecomposableProblem(const std::function<Problem()>& factory);

  explicit DecomposableProblem(
      const std::function<Problem()>& factory,
      const folly::F14FastSet<PartitionId>& allowedPartitions);

  explicit DecomposableProblem(
      const DecomposableProblem& masterProblem,
      const folly::F14FastSet<PartitionId>& allowedPartitions);

  virtual void solve(
      bool useSolverSpecificMultiObjectiveSolveIfAvailable = true) override;

  // setObjective() always *sets* an objective---meaning all the previously
  // added objectives are cleared
  void setObjective(std::shared_ptr<const ExpressionImpl> expression);

  /** Returns variable -> value mapping in case solution is needed but
     not desirable to save all results.
    - if output files are provided, we save the MIP model and the resulting
    logs **/
  folly::F14FastMap<ConstGenericVarPtr, double> solve(
      bool saveResults,
      bool returnInitialValuesOnFailure = false,
      std::optional<std::string> solverOutputFile = std::nullopt,
      std::optional<std::string> modelOutputFile = std::nullopt,
      bool useSolverSpecificMultiObjectiveSolveIfAvailable = true);

  /** creates an image of @param expr with respect to allowedPartitions */
  Expression image(
      std::shared_ptr<const GenericExpressionImpl> expr,
      RealizationRule realizationRule = RealizationRule()) const;

  /** creates an image of @param rel with respect to allowedPartitions */
  Relation image(
      const GenericRelationImpl& rel,
      RealizationRule realizationRule = RealizationRule()) const;

  /* returns the partitions allowed for this problem, throws if
   * allowedPartitions_ was unset */
  const folly::F14FastSet<PartitionId>& getAllowedPartitions() const;

  // What the problem the last solve() realized and solved received.
  std::map<std::string, double> getSetParameters() const override;

 private:
  std::optional<folly::F14FastSet<PartitionId>> allowedPartitions_;
  // Parameters explicitly set on the problem the last solve() solved.
  std::optional<std::map<std::string, double>> solvedParamNameToValue_;
};

} // namespace facebook::algopt::lp::detail
