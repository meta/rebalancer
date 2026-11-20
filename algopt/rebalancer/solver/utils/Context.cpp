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

#include "algopt/rebalancer/solver/utils/Context.h"

#include "algopt/rebalancer/solver/expressions/ObjectVector.h"
#include "algopt/rebalancer/solver/utils/equivalence_sets/EquivalenceSets.h"
#include "algopt/rebalancer/solver/utils/Util.h"

#ifndef REBALANCER_OSS_BUILD
#include "multifeed/hash/HeterogeneousAccess.h"
#endif

#include <boost/iterator/iterator_facade.hpp>

namespace facebook::rebalancer {

const PackerMap<entities::EquivalenceSetId, double>& LpContext::getEquivSetMap(
    ObjectVector* objVec,
    const EquivalenceSets& equivalenceSets) {
  return objVectorExprIdToEquivSetToValue_.getSavedOrCompute(
      objVec->getId(),
      [&]() { return objVec->computeEquivSetMap(equivalenceSets); });
}

} // namespace facebook::rebalancer
