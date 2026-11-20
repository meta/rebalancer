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

#include "algopt/rebalancer/algopt_common/Utils.h"
#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/entities/Map.h"
#include "algopt/rebalancer/entities/tests/UniverseBuilderTestUtils.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/ObjectDeduper.h"
#include "algopt/rebalancer/solver/utils/ObjectStore.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/MapUtil.h>
#include <folly/ExceptionWrapper.h>
#include <folly/tracing/AsyncStack.h>
#include <gtest/gtest.h>

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace facebook::rebalancer::packer::tests {

class ObjectDeduperTest : public ::testing::Test,
                          public entities::tests::UniverseBuilderTestUtils {};

TEST_F(ObjectDeduperTest, Simple) {
  setInitialAssignment(
      entities::Map<std::string, std::vector<std::string>>{
          {"container", {"101", "102", "103", "104"}}});
  const auto universe = buildUniverse();

  EquivalenceSets equivalenceSets{*universe};
  const PackerMap<entities::ObjectId, int> mapping(
      {{object("101"), 1},
       {object("102"), 2},
       {object("103"), 1},
       {object("104"), 3}});
  equivalenceSets.mappingMerge(mapping);

  const PackerSet<entities::ObjectId> objects(
      {object("101"), object("102"), object("103"), object("104")});

  auto objects_store = ObjectStore::Factory(
                           /*scores=*/nullptr,
                           /*useDynamicObjectOrdering=*/false)
                           .get();
  for (auto obj : objects) {
    objects_store.insert(obj);
  }

  const ObjectDeduper dedupedObjs(&equivalenceSets, objects_store);
  const std::set<entities::ObjectId> actual(
      dedupedObjs.begin(), dedupedObjs.end());
  const std::set<entities::ObjectId> expected(
      {object("101"), object("102"), object("104")});
  EXPECT_EQ(expected, actual);
}

} // namespace facebook::rebalancer::packer::tests
