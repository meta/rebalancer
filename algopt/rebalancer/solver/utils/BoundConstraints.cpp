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

#include "algopt/rebalancer/solver/utils/BoundConstraints.h"

#include "algopt/rebalancer/entities/Identifiers.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#include "multifeed/hash/QuickHashSet.h"
#endif

#include <fmt/core.h>

#include <stdexcept>

namespace facebook::rebalancer {

bool BoundConstraints::inDynamicContainerSet(
    entities::ContainerId container) const {
  if (!dynamic_.has_value()) {
    return true;
  }
  return dynamic_->count(container) != 0;
}

bool BoundConstraints::anyInDynamicContainerSet(
    const PackerSet<entities::ContainerId>& containers) const {
  if (!dynamic_.has_value()) {
    return true;
  }
  if (dynamic_->size() < containers.size()) {
    for (auto dynamic_container : dynamic_.value()) {
      if (containers.contains(dynamic_container)) {
        return true;
      }
    }
    return false;
  }
  for (auto container : containers) {
    if (dynamic_->contains(container)) {
      return true;
    }
  }
  return false;
}

bool BoundConstraints::giving(entities::ContainerId container) const {
  throwIfEmpty();

  if (!notGiving_.has_value()) {
    return inDynamicContainerSet(container);
  }
  return (!notGiving_->contains(container));
}

bool BoundConstraints::anyGiving(
    const PackerSet<entities::ContainerId>& containers) const {
  throwIfEmpty();

  if (!notGiving_.has_value()) {
    return anyInDynamicContainerSet(containers);
  }
  if (notGiving_->size() < containers.size()) {
    // there is at least one container not explicitly marked as not_giving
    return true;
  }

  for (auto container : containers) {
    if (!notGiving_->contains(container)) {
      return true;
    }
  }
  return false;
}

bool BoundConstraints::taking(entities::ContainerId container) const {
  throwIfEmpty();

  if (!notTaking_.has_value()) {
    return inDynamicContainerSet(container);
  }
  return (!notTaking_->contains(container));
}

bool BoundConstraints::anyTaking(
    const PackerSet<entities::ContainerId>& containers) const {
  throwIfEmpty();

  if (!notTaking_.has_value()) {
    return anyInDynamicContainerSet(containers);
  }
  if (notTaking_->size() < containers.size()) {
    // there is at least one container not explicitly marked as not_taking
    return true;
  }

  for (auto container : containers) {
    if (!notTaking_->contains(container)) {
      return true;
    }
  }
  return false;
}

bool BoundConstraints::isEmpty() const {
  return !(
      dynamic_.has_value() || notGiving_.has_value() || notTaking_.has_value());
}

void BoundConstraints::throwIfEmpty() const {
  if (isEmpty()) {
    throw std::runtime_error(
        "Expected to be called only when BoundConstraints is not empty.");
  }
}

void BoundConstraints::Builder::checkNotBuilt() const {
  if (built_) {
    throw std::runtime_error(
        "please create a new BoundConstraintsBuilder: build already called on the current instance");
  }
}

BoundConstraints BoundConstraints::Builder::build() {
  checkNotBuilt();

  if (dynamic_.has_value() &&
      (notGiving_.has_value() || notTaking_.has_value())) {
    throw std::runtime_error(
        fmt::format(
            "cannot specify bound constraints with both dynamic containers constraint as well as {} containers constraint",
            notGiving_.has_value() && notTaking_.has_value()
                ? "not_giving and not_taking"
                : (notGiving_.has_value() ? "not_giving" : "not_taking")));
  }

  built_ = true;
  return BoundConstraints(
      std::move(dynamic_), std::move(notGiving_), std::move(notTaking_));
}

BoundConstraints::Builder& BoundConstraints::Builder::setDynamicContainers(
    PackerSet<entities::ContainerId> dynamic_containers) {
  checkNotBuilt();
  dynamic_ = std::move(dynamic_containers);
  return *this;
}

BoundConstraints::Builder& BoundConstraints::Builder::setNotGivingContainers(
    PackerSet<entities::ContainerId> not_giving_containers) {
  checkNotBuilt();
  notGiving_ = std::move(not_giving_containers);
  return *this;
}

BoundConstraints::Builder& BoundConstraints::Builder::setNotTakingContainers(
    PackerSet<entities::ContainerId> not_taking_containers) {
  checkNotBuilt();
  notTaking_ = std::move(not_taking_containers);
  return *this;
}

} // namespace facebook::rebalancer
