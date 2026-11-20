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

#include "algopt/rebalancer/solver/iterators/MultiCollectionCartesianProduct.h"

#include <folly/container/F14Set.h>
#include <gtest/gtest.h>

#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace facebook::rebalancer::packer::tests {
TEST(
    MultiCollectionCartesianProductTest,
    BasicMultiCollectionCartesianProduct) {
  const std::vector<int> v1 = {1, 2, 3};
  const std::vector<int> v2 = {10, 20};
  const std::vector<int> v3 = {100, 200, 300};

  const std::vector<std::vector<int>> collections{v1, v2, v3};

  // explicitly disallow using temporary collections
  static_assert(!std::is_constructible_v<
                MultiCollectionCartesianProduct<std::vector<std::vector<int>>>,
                std::vector<std::vector<int>>&&>);

  auto cartesianProduct = MultiCollectionCartesianProduct(collections);

  const std::vector<std::vector<int>> output(
      cartesianProduct.begin(), cartesianProduct.end());
  const std::vector<std::vector<int>> expected = {
      {1, 10, 100},
      {1, 10, 200},
      {1, 10, 300},
      {1, 20, 100},
      {1, 20, 200},
      {1, 20, 300},
      {2, 10, 100},
      {2, 10, 200},
      {2, 10, 300},
      {2, 20, 100},
      {2, 20, 200},
      {2, 20, 300},
      {3, 10, 100},
      {3, 10, 200},
      {3, 10, 300},
      {3, 20, 100},
      {3, 20, 200},
      {3, 20, 300}};
  EXPECT_EQ(expected.size(), output.size());
  EXPECT_EQ(expected, output);
}

TEST(
    MultiCollectionCartesianProductTest,
    Basic2MultiCollectionCartesianProduct) {
  const std::vector<std::string> v1 = {"1"};
  const std::vector<std::string> v2 = {"10"};
  const std::vector<std::string> v3 = {"100"};
  const std::vector<std::string> v4 = {"1000"};
  const std::vector<std::string> v5 = {"10000"};

  const std::vector<std::vector<std::string>> collections{v1, v2, v3, v4, v5};
  auto cartesianProduct = MultiCollectionCartesianProduct(collections);

  const std::vector<std::vector<std::string>> output(
      cartesianProduct.begin(), cartesianProduct.end());
  const std::vector<std::vector<std::string>> expected = {
      {"1", "10", "100", "1000", "10000"}};
  EXPECT_EQ(expected.size(), output.size());
  EXPECT_EQ(expected, output);
}

TEST(MultiCollectionCartesianProductTest, OnlyOneInnerCollection) {
  const std::vector<std::string> v1 = {"1", "2", "3"};
  const std::vector<std::vector<std::string>> collections{v1};
  auto cartesianProduct = MultiCollectionCartesianProduct(collections);

  const std::vector<std::vector<std::string>> output(
      cartesianProduct.begin(), cartesianProduct.end());
  const std::vector<std::vector<std::string>> expected = {{"1"}, {"2"}, {"3"}};
  EXPECT_EQ(expected.size(), output.size());
  EXPECT_EQ(expected, output);
}

TEST(MultiCollectionCartesianProductTest, NoInnerCollection) {
  const std::vector<std::vector<std::string>> collections;
  auto cartesianProduct = MultiCollectionCartesianProduct(collections);

  const std::vector<std::vector<std::string>> output(
      cartesianProduct.begin(), cartesianProduct.end());
  EXPECT_EQ(0, output.size());
}

TEST(
    MultiCollectionCartesianProductTest,
    MultipleEmptyCollectionsCartesianProduct) {
  const std::vector<int> v1 = {};
  const std::vector<int> v2 = {};
  const std::vector<int> v3 = {1, 2};
  auto collections = std::vector<std::vector<int>>{v1, v2, v3};
  const MultiCollectionCartesianProduct product(collections);
  const std::vector<std::vector<int>> output(product.begin(), product.end());
  EXPECT_EQ(0, output.size());
}

TEST(
    MultiCollectionCartesianProductTest,
    SecondEmptyMultiCollectionCartesianProduct) {
  const std::vector<std::string> v1 = {"a", "b"};
  const std::vector<std::string> v2 = {};
  auto collections = std::vector<std::vector<std::string>>{v1, v2};
  const MultiCollectionCartesianProduct product(collections);
  const std::vector<std::vector<std::string>> output(
      product.begin(), product.end());
  EXPECT_EQ(0, output.size());
}

TEST(
    MultiCollectionCartesianProductTest,
    BothEmptyMultiCollectionCartesianProduct) {
  const std::vector<std::string> v1 = {};
  const std::vector<std::string> v2 = {};
  auto collections = std::vector<std::vector<std::string>>{v1, v2};
  const MultiCollectionCartesianProduct product(collections);
  const std::vector<std::vector<std::string>> output(
      product.begin(), product.end());
  EXPECT_EQ(0, output.size());
}

TEST(MultiCollectionCartesianProductTest, IteratorsTest) {
  const std::vector<std::string> v1 = {"1", "2"};
  const std::vector<std::string> v2 = {"a", "b"};
  const std::vector<std::string> v3 = {"x", "y"};

  auto collections1 = std::vector<std::vector<std::string>>{v1, v2};
  auto collections2 = std::vector<std::vector<std::string>>{v1, v3};

  const MultiCollectionCartesianProduct product1(collections1);
  const MultiCollectionCartesianProduct product2(collections1);
  const MultiCollectionCartesianProduct product3(collections2);

  EXPECT_EQ(product1.begin(), product2.begin());
  EXPECT_NE(product1.begin(), product3.begin());
}

TEST(MultiCollectionCartesianProductTest, UseSetsForInnerCollection) {
  const folly::F14FastSet<std::string> v1 = {"1", "4"};
  const folly::F14FastSet<std::string> v2 = {"10"};
  const folly::F14FastSet<std::string> v3 = {"t"};

  const std::vector<folly::F14FastSet<std::string>> collections{v1, v2, v3};
  auto cartesianProduct = MultiCollectionCartesianProduct(collections);

  std::vector<std::vector<std::string>> output(
      cartesianProduct.begin(), cartesianProduct.end());
  std::vector<std::vector<std::string>> expected = {
      {"1", "10", "t"},
      {"4", "10", "t"},
  };
  EXPECT_EQ(expected.size(), output.size());

  // note that the order of the elements in the output vector is not guaranteed
  // since the inner collections are unordered sets
  auto outputSet =
      std::set<std::vector<std::string>>(output.begin(), output.end());
  auto expectedSet =
      std::set<std::vector<std::string>>(expected.begin(), expected.end());
  EXPECT_EQ(expectedSet, outputSet);
}
} // namespace facebook::rebalancer::packer::tests
