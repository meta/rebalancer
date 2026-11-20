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

#include <folly/container/irange.h>
#include <folly/small_vector.h>

#include <cstddef>
#include <functional>
#include <iterator>
#include <utility>
#include <vector>

namespace facebook::rebalancer {
/**
 Compute the cartesian product of several inner collections that are part of the
 outer collection. For example, OuterCollection = { {1, 2}, {3, 4}, {5} } will
 result in the following cartesian product: { {1, 3, 5}, {1, 4, 5}, {2, 3, 5},
 {2, 4, 5} }

 nInnerCollections is used to size the folly::small_vector
 */
template <typename Collections, int nInnerCollections = 4>
class MultiCollectionCartesianProduct {
  using InnerCollection = typename Collections::value_type;
  using InnerCollectionIterator = typename InnerCollection::const_iterator;
  using InnerCollectionIters =
      folly::small_vector<InnerCollectionIterator, nInnerCollections>;
  using InnerCollectionValueType = typename InnerCollection::value_type;

 public:
  class Iterator;
  using const_iterator = Iterator;

  explicit MultiCollectionCartesianProduct(const Collections& collections) {
    startIters_.reserve(collections.size());
    endIters_.reserve(collections.size());
    for (auto& collection : collections) {
      startIters_.emplace_back(collection.begin());
      endIters_.emplace_back(collection.end());
    }
  }

  Iterator begin() const {
    return Iterator(std::cref(startIters_), std::cref(endIters_));
  }

  Iterator end() const {
    return Iterator(std::cref(endIters_), std::cref(endIters_));
  }

 private:
  // to prevent the caller from using temporary (r-value) collections which is
  // not safe
  explicit MultiCollectionCartesianProduct(Collections&& collections) = delete;

 private:
  InnerCollectionIters startIters_;
  InnerCollectionIters endIters_;
};

template <typename Collections, int nInnerCollections>
class MultiCollectionCartesianProduct<Collections, nInnerCollections>::
    Iterator {
 public:
  // Iterator traits to replace deprecated std::iterator base class
  using iterator_category = std::forward_iterator_tag;
  using value_type = std::vector<InnerCollectionValueType>;
  using difference_type = std::ptrdiff_t;
  using pointer = value_type*;
  using reference = value_type&;

  Iterator(
      std::reference_wrapper<const InnerCollectionIters> startItersWrapper,
      std::reference_wrapper<const InnerCollectionIters> endItersWrapper)
      : startIters_(std::move(startItersWrapper)),
        endIters_(std::move(endItersWrapper)) {
    auto& startIters = startIters_.get();
    auto& endIters = endIters_.get();
    atLeastOneEmptyCollection_ = startIters.empty();
    currIters_.reserve(startIters.size());
    for (const auto i : folly::irange(startIters.size())) {
      auto startIter = startIters.at(i);
      auto endIter = endIters.at(i);
      if (startIter == endIter) {
        // if any of the inner collections is empty, then the cartesian product
        // is empty, so store that information and stop populating currIters_
        atLeastOneEmptyCollection_ = true;
        break;
      }

      currIters_.emplace_back(startIter);
    }
  }

  Iterator& operator++() {
    // increment would only be called if there is at least one inner collection
    size_t j = (currIters_.size() - 1);
    ++currIters_.at(j);

    while (j > 0 && currIters_.at(j) == endIters_.get().at(j)) {
      // for everything except the first collection, we always reset to the
      // start when we reach the end
      currIters_[j] = startIters_.get().at(j);
      --j;
      ++currIters_.at(j);
    }

    return *this;
  }

  std::vector<InnerCollectionValueType> operator*() const {
    std::vector<InnerCollectionValueType> result;
    result.reserve(currIters_.size());
    for (auto& currIter : currIters_) {
      result.emplace_back(*currIter);
    }
    return result;
  }

  bool operator==(const Iterator& other) const {
    if (doneIterating()) {
      return other.doneIterating();
    }
    return currIters_ == other.currIters_;
  }

  bool operator!=(const Iterator& other) const {
    return !operator==(other);
  }

 private:
  bool doneIterating() const {
    // We are done iterating if there were was at least one empty collection to
    // begin with, no innerCollections at all (=>atLeastOneEmptyCollection), or
    // the first collection reaches its end. Note that all collections other
    // than first are never at end(), unless they were initially at end() (=>
    // they were empty).
    return (
        atLeastOneEmptyCollection_ ||
        currIters_.at(0) == endIters_.get().at(0));
  }

 private:
  const std::reference_wrapper<const InnerCollectionIters> startIters_;
  const std::reference_wrapper<const InnerCollectionIters> endIters_;
  InnerCollectionIters currIters_;
  bool atLeastOneEmptyCollection_;
};

} // namespace facebook::rebalancer
