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

#include <algopt/rebalancer/entities/Identifiers.h>
#include <algopt/rebalancer/solver/utils/ContainerSetWithHash.h>
#include <algopt/rebalancer/solver/utils/Util.h>

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <folly/hash/Hash.h>

#include <stdexcept>

namespace facebook::rebalancer {

ContainerSetWithHash::ContainerSetWithHash() = default;

void ContainerSetWithHash::set(
    std::shared_ptr<const PackerSet<entities::ContainerId>> containersPtr) {
  if (containersPtr == nullptr) {
    throw std::runtime_error("Expected to be set with non-null containersPtr");
  }
  containersPtr_ = containersPtr;

  hashValueOpt_ = folly::hash::commutative_hash_combine_range(
      containersPtr_->begin(), containersPtr_->end());
}

int64_t ContainerSetWithHash::getHashValue() const {
  if (!hashValueOpt_.has_value()) {
    throw std::runtime_error(
        "Hash value is not set. Has containerPtr been properly initialized using set()?");
  }
  if (containersPtr_->empty()) {
    // folly::hash::commutative_hash_combine_range() returns 0 as the hash
    // value if the set is empty, but we don't expect this function to be called
    // for empty sets. So, to be safe, throw
    throw std::runtime_error(
        "Unexpected attempt to get the hash value of an empty set");
  }

  return hashValueOpt_.value();
}

std::shared_ptr<const PackerSet<entities::ContainerId>>
ContainerSetWithHash::getSetPtr() const {
  return containersPtr_;
}

const PackerSet<entities::ContainerId>& ContainerSetWithHash::getNonNullSet()
    const {
  if (containersPtr_ == nullptr) {
    throw std::runtime_error("expect containerPtr to be non-null");
  }
  return *containersPtr_;
}

bool ContainerSetWithHash::exists() const {
  return (containersPtr_ != nullptr);
}

bool ContainerSetWithHash::isEmpty() const {
  return getNonNullSet().empty();
}

std::optional<size_t> ContainerSetWithHash::size() const {
  if (!containersPtr_) {
    return std::nullopt;
  }
  return containersPtr_->size();
}

} // namespace facebook::rebalancer
