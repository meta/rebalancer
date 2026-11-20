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

#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/summary/metrics/GroupRoutingLatencyMetrics.h"
#include "algopt/rebalancer/solver/summary/metrics/tests/MetricsTestBase.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/MapUtil.h>
#include <folly/coro/BlockingWait.h>
#include <folly/coro/Task.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>

#include <coroutine>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class GroupRoutingLatencyMetricsTest : public MetricsTestBase {
 protected:
  void SetUp() override {
    folly::coro::blockingWait(setUpUniverse());
  }

  folly::coro::Task<void> setUpUniverse() {
    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"host0", {"task0", "task2"}},
            {"host1", {"task1"}},
        });

    co_await addPartition(
        "tenant", {{"tenant1", {"task0"}}, {"tenant2", {"task1", "task2"}}});
    co_await addScope("region", {{"region0", {"host0", "host1"}}});
    co_await addEmptyRoutingConfig("routingConfig1", "region", "tenant");
    co_await addEmptyRoutingConfig("routingConfig2", "region", "tenant");
  }

  GroupRoutingLatencyMetrics metrics;
};

} // namespace facebook::rebalancer::packer::tests
