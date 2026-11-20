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
#include "algopt/rebalancer/solver/utils/EntityAttributes.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>
#include <fmt/core.h>
#include <folly/container/MapUtil.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace facebook::rebalancer {

class EntityAttributesStore {
 public:
  explicit EntityAttributesStore() = default;

  void registerAttributes(
      const std::string& key,
      std::shared_ptr<EntityAttributes> attributes);

  template <typename AttributeType>
  const AttributeType& getAttributes(const std::string& key) const {
    if (auto ptr = folly::get_ptr(nameToAttributes_, key)) {
      if (auto attr = std::dynamic_pointer_cast<AttributeType>(*ptr)) {
        return *attr;
      }
    }
    throw std::runtime_error(
        fmt::format("Attribute with key {} was not found", key));
  }

  // update attributes when assignment changes
  void updateOnApply(const ChangeSet& changes);

 private:
  entities::Map<std::string, std::shared_ptr<EntityAttributes>>
      nameToAttributes_;
};

} // namespace facebook::rebalancer
