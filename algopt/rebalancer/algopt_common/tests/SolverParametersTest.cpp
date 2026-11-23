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

#include "algopt/rebalancer/algopt_common/SolverParameters.h"

#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <string>

namespace facebook::algopt {

TEST(SolverParametersTest, ToJson) {
  const std::map<std::string, double> parameters = {
      {"GRB_Threads", 16},
      {"GRB_MIPGap", 1e-4},
      {"GRB_TimeLimit", std::numeric_limits<double>::infinity()},
  };
  EXPECT_EQ(
      R"({"GRB_MIPGap":0.0001,"GRB_Threads":16,"GRB_TimeLimit":"inf"})",
      solverParametersToJson(parameters));
}

TEST(SolverParametersTest, EmptyToJson) {
  EXPECT_EQ("{}", solverParametersToJson({}));
}

} // namespace facebook::algopt
