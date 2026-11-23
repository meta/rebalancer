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

#include "algopt/lp/environment/Environment.h" // NOLINT

#ifdef REBALANCER_USE_XPRESS

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Resolves an Xpress control name ("XPRS_TIMELIMIT") to its integer code, so
// callers can set controls by name. The tables live in XpressParamCodes.cpp;
// they run to several hundred entries and are regenerated wholesale by
// third-party/xpress/upgrade-xpress.py on each vendor upgrade.

namespace facebook::algopt::lp::detail::xpress_params {

std::optional<int> getIntParamCode(const std::string& name);
std::optional<int> getDoubleParamCode(const std::string& name);

// Names of every numeric control of the linked Xpress version.
std::vector<std::string_view> getParamNames();

// Version string of the linked Xpress optimizer library, e.g. "48.01.01" for
// Xpress 10.0; empty if Xpress does not report one.
std::string getXpressVersion();

} // namespace facebook::algopt::lp::detail::xpress_params

#endif // REBALANCER_USE_XPRESS
