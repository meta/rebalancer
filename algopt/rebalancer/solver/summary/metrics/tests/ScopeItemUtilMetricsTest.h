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

#include "algopt/rebalancer/entities/builders/AsyncUniverseBuilder.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/materializer/utils/Descriptor.h"
#include "algopt/rebalancer/solver/expressions/Expression.h"
#include "algopt/rebalancer/solver/expressions/ObjectPartitionLookup.h"
#include "algopt/rebalancer/solver/expressions/tests/ExpressionTestsBase.h"
#include "algopt/rebalancer/solver/summary/metrics/ScopeItemUtilMetrics.h"
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
#include <gtest/gtest.h>

#include <coroutine>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class ScopeItemUtilMetricsTest : public MetricsTestBase {
 protected:
  using UtilMetric = facebook::rebalancer::materializer::UtilMetric;

 protected:
  void SetUp() override {
    universeBuilder_.setObjectTypeName("task");
    universeBuilder_.setContainerTypeName("host");
  }

  folly::coro::Task<void> setUpUniverse() {
    setInitialAssignment(
        entities::Map<std::string, std::vector<std::string>>{
            {"host0", {"task0", "task2"}},
            {"host1", {"task1"}},
        });

    // create two object dimensions
    co_await addObjectDimension("load", {{"task0", 10}, {"task1", 20}}, 0.0);
    co_await addObjectDimension("memory", {{"task0", 50}, {"task1", 5}}, 0.0);

    // create new scope "region" which contains both host0 and host1
    co_await addScope("region", {{"region0", {"host0", "host1"}}});

    // Set up partitions for tests that need them
    co_await addPartition(
        "partition1", {{"group0", {"task0"}}, {"group1", {"task1", "task2"}}});
    co_await addPartition("partition2", {{"group0", {"task0", "task1"}}});
  }

  void addToScopeItemUtilCollection(
      ExprPtr expr,
      UtilMetric utilMetric,
      entities::DimensionId dimensionId,
      entities::ScopeId scopeId,
      entities::ScopeItemId scopeItemId,
      std::optional<entities::PartitionId> partitionId = std::nullopt,
      std::optional<entities::GroupId> groupId = std::nullopt) {
    metrics.add(
        expr,
        utilMetric,
        materializer::Descriptor{
            .dimensionId = dimensionId,
            .scopeId = scopeId,
            .scopeItemId = scopeItemId,
            .partitionId = partitionId,
            .groupId = groupId});
  }

  void addToScopeItemUtilCollection(
      const std::shared_ptr<ObjectPartitionLookupDefault> lookup,
      UtilMetric utilMetric) {
    metrics.add(lookup, utilMetric);
  }

  ScopeItemUtilMetrics metrics;
};

} // namespace facebook::rebalancer::packer::tests
