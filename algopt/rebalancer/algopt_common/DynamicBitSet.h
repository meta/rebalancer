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

#include <folly/lang/SafeAssert.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace facebook::algopt {

/**
DynamicBitSet is a runtime-sized bitset, unlike std::bitset where the size has
to be known at compile time. Internally it stores 64-bit blocks
(`std::vector<std::uint64_t>`). The size is fixed at construction; bits are
zero-initialized and turned on with `set(i)`.

Complexity, with `N` total bits, `B = ceil(N / 64)` blocks, and `K` set bits:
- `set(i)`, `clear(i)`, `atomicSet(i)`, `isSet(i)`: `O(1)`.
- `any()`, `mergeFrom(other)`, `atomicOrFrom(other)`: `O(B)`.
- `forEachSetBit(fn)`: `O(B + K)`.

Thread safety: `atomicSet()` and `atomicOrFrom()` calls may run concurrently
with one another. No other operation may overlap a mutation; callers must
synchronize after the last atomic mutation before reading or using a
non-atomic mutation method.
*/
class DynamicBitSet {
 public:
  using block_type = std::uint64_t;
  static constexpr std::size_t kBitsPerBlock = 64;

  explicit DynamicBitSet(std::size_t numBits)
      : blocks_(numBits / kBitsPerBlock + (numBits % kBitsPerBlock != 0), 0),
        numBits_(numBits) {}

  bool operator==(const DynamicBitSet&) const = default;

  // Sets bit `i`. Calling on an already-set bit is a no-op. Returns true if
  // the bit was newly set, false otherwise.
  bool set(std::size_t i) noexcept {
    FOLLY_SAFE_CHECK(
        i < numBits_, "DynamicBitSet::set: bit index out of range: ", i);
    const auto blockIndex = i / kBitsPerBlock;
    const auto bitInBlock = i % kBitsPerBlock;
    const auto singleBitMask = block_type{1} << bitInBlock;
    auto& block = blocks_[blockIndex];
    if ((block & singleBitMask) != 0) {
      return false; // already set; nothing to do.
    }

    block |= singleBitMask;
    return true;
  }

  // Sets bit `i` atomically. Unlike set(), this is safe to call concurrently
  // with other atomicSet() calls on the same bitset, including on the same
  // block.
  void atomicSet(std::size_t i) noexcept {
    FOLLY_SAFE_CHECK(
        i < numBits_, "DynamicBitSet::atomicSet: bit index out of range: ", i);
    const auto blockIndex = i / kBitsPerBlock;
    const auto bitInBlock = i % kBitsPerBlock;
    const auto singleBitMask = block_type{1} << bitInBlock;
    const std::atomic_ref<block_type> block(blocks_[blockIndex]);
    // Reading first is cheaper when the bit has already been set. If multiple
    // threads see it unset, fetch_or() still sets it atomically.
    if ((block.load(std::memory_order_relaxed) & singleBitMask) != 0) {
      return;
    }
    block.fetch_or(singleBitMask, std::memory_order_relaxed);
  }

  void clear(std::size_t i) noexcept {
    FOLLY_SAFE_CHECK(
        i < numBits_, "DynamicBitSet::clear: bit index out of range: ", i);
    const auto blockIndex = i / kBitsPerBlock;
    const auto bitInBlock = i % kBitsPerBlock;
    blocks_[blockIndex] &= ~(block_type{1} << bitInBlock);
  }

  // Atomically merges `other` into this bitset. Returns true if `other`
  // contains at least one set bit.
  void atomicOrFrom(const DynamicBitSet& other) noexcept {
    FOLLY_SAFE_CHECK(
        numBits_ == other.numBits_,
        "DynamicBitSet::atomicOrFrom: size mismatch: ",
        numBits_,
        " vs ",
        other.numBits_);
    for (std::size_t b = 0; b < blocks_.size(); ++b) {
      const auto sourceBlock = other.blocks_[b];
      if (sourceBlock == 0) {
        continue;
      }
      const std::atomic_ref<block_type> block(blocks_[b]);
      if ((block.load(std::memory_order_relaxed) & sourceBlock) ==
          sourceBlock) {
        continue;
      }
      block.fetch_or(sourceBlock, std::memory_order_relaxed);
    }
  }

  // Sets every bit that is set in `other`, which must have the same size.
  // O(numBlocks) regardless of density.
  void mergeFrom(const DynamicBitSet& other) noexcept {
    FOLLY_SAFE_CHECK(
        numBits_ == other.numBits_,
        "DynamicBitSet::mergeFrom: size mismatch: ",
        numBits_,
        " vs ",
        other.numBits_);
    for (std::size_t b = 0; b < blocks_.size(); ++b) {
      blocks_[b] |= other.blocks_[b];
    }
  }

  [[nodiscard]] bool isSet(std::size_t i) const noexcept {
    FOLLY_SAFE_CHECK(
        i < numBits_, "DynamicBitSet::isSet: bit index out of range: ", i);
    const auto blockIndex = i / kBitsPerBlock;
    const auto bitInBlock = i % kBitsPerBlock;
    const auto singleBitMask = block_type{1} << bitInBlock;
    return (blocks_[blockIndex] & singleBitMask) != 0;
  }

  [[nodiscard]] std::size_t size() const noexcept {
    return numBits_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return numBits_ == 0;
  }

  [[nodiscard]] bool any() const noexcept {
    return std::any_of(blocks_.begin(), blocks_.end(), [](const auto block) {
      return block != 0;
    });
  }

  [[nodiscard]] std::size_t numBlocks() const noexcept {
    return blocks_.size();
  }

  // Raw block pointer; avoids re-loading the underlying vector's data pointer
  // on every access.
  [[nodiscard]] const block_type* dataPtr() const noexcept {
    return blocks_.data();
  }

  // Visits set bits in ascending order in O(B+K) where B is the number of
  // blocks and K is the number of set bits.
  template <typename F>
  void forEachSetBit(F&& fn) const {
    std::size_t baseIndex = 0;
    for (std::size_t b = 0; b < blocks_.size(); ++b) {
      block_type block = blocks_[b];
      while (block) {
        // find the index of the least significant set bit in the block
        const auto index = baseIndex + std::countr_zero(block);
        fn(index);

        // clear the bit we just visited
        block &= block - 1;
      }
      baseIndex += kBitsPerBlock;
    }
  }

 private:
  std::vector<block_type> blocks_;
  std::size_t numBits_;
};

} // namespace facebook::algopt
