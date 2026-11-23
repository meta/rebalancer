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

#include <fmt/format.h>
#include <folly/json/dynamic.h>
#include <folly/json/json.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>

namespace facebook::algopt {

// Serializes solver parameters (see lp::Problem::getSetParameters()) as a JSON
// object with sorted keys. Integral values are written as JSON integers and
// non-finite values, which JSON cannot represent, as strings, e.g.
// {"GRB_MIPGap":0.0001,"GRB_Threads":16,"GRB_TimeLimit":"inf"}.
inline std::string solverParametersToJson(
    const std::map<std::string, double>& parameters) {
  // Doubles represent every integer up to this one exactly.
  constexpr double kMaxExactInteger =
      static_cast<double>(int64_t{1} << std::numeric_limits<double>::digits);
  folly::dynamic json = folly::dynamic::object;
  for (const auto& [name, value] : parameters) {
    if (!std::isfinite(value)) {
      json[name] = fmt::format("{}", value);
    } else if (
        std::trunc(value) == value && std::abs(value) <= kMaxExactInteger) {
      json[name] = static_cast<int64_t>(value);
    } else {
      json[name] = value;
    }
  }
  folly::json::serialization_opts opts;
  opts.sort_keys = true;
  return folly::json::serialize(json, opts);
}

} // namespace facebook::algopt
