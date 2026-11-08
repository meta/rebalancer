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
#include <folly/CPortability.h>
#include <folly/ExceptionWrapper.h>
#include <folly/executors/ThreadPoolExecutor.h>
#include <folly/lang/Align.h>
#include <folly/Likely.h>
#include <folly/MPMCQueue.h>
#include <folly/Optional.h>
#include <folly/synchronization/Baton.h>
#include <folly/Try.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace facebook::rebalancer {

namespace detail {

inline constexpr size_t kCacheLineSize =
    folly::hardware_destructive_interference_size;

inline constexpr size_t kQueueCapacity = 4096;
inline constexpr size_t kDefaultBatchSize = 32;

// Captures the first exception thrown across worker threads.
// Subsequent exceptions are ignored to preserve the original failure context.
struct ExceptionTracker {
  std::atomic<bool> hasException{false};
  std::exception_ptr firstException;

  [[nodiscard]] FOLLY_ALWAYS_INLINE bool hasError() const noexcept {
    return hasException.load(std::memory_order_relaxed);
  }

  inline void recordException(std::exception_ptr ex) noexcept {
    if (!hasException.exchange(true, std::memory_order_acq_rel)) {
      firstException = std::move(ex);
    }
  }

  FOLLY_ALWAYS_INLINE void rethrowIfException() const {
    if (firstException) {
      std::rethrow_exception(firstException);
    }
  }
};

template <typename Input>
using BatchPtr = std::unique_ptr<std::vector<Input>>;

// Manages batch allocation and recycling via a freelist.
// Uses unique_ptr throughout for automatic memory management - when queues
// are destroyed, all batches are automatically freed without explicit cleanup.
template <typename Input>
struct BatchPool {
  folly::MPMCQueue<BatchPtr<Input>>& freelist;

  FOLLY_ALWAYS_INLINE BatchPtr<Input> allocate() {
    BatchPtr<Input> buf;
    if (freelist.read(buf)) {
      buf->clear();
      return std::move(buf);
    }
    return std::make_unique<std::vector<Input>>();
  }

  FOLLY_ALWAYS_INLINE void recycle(BatchPtr<Input> buf) {
    // If freelist is full, write() returns false and buf is automatically
    // deleted when it goes out of scope (unique_ptr semantics)
    freelist.write(std::move(buf));
  }
};

// Consumer worker that processes batches from the queue.
template <
    typename Input,
    typename Accumulator,
    typename ProcessFn,
    typename AggregateFn>
FOLLY_ALWAYS_INLINE void consumeBatches(
    folly::MPMCQueue<BatchPtr<Input>>& queue,
    Accumulator& localAcc,
    const ProcessFn& process,
    const AggregateFn& aggregate,
    ExceptionTracker& exTracker,
    BatchPool<Input>& pool,
    folly::Baton<>& baton) noexcept {
  while (true) {
    BatchPtr<Input> batch;
    queue.blockingRead(batch);

    if (!batch) {
      break; // poison pill (null unique_ptr)
    }

    for (auto& in : *batch) {
      if (exTracker.hasError()) {
        break;
      }
      try {
        aggregate(localAcc, process(std::move(in)));
      } catch (const std::exception&) {
        exTracker.recordException(std::current_exception());
        break;
      }
    }
    pool.recycle(std::move(batch));
  }
  baton.post();
}

} // namespace detail

// Configuration for batch-based parallel execution.
struct BatchingExecutionOptions {
  // Number of items to process per batch.
  // Larger batches reduce queue contention but may cause uneven load
  // distribution. Smaller batches improve load balancing but increase queue
  // overhead.
  size_t batchSize = detail::kDefaultBatchSize;
  // Maximum number of concurrent batch consumers. If unset, uses all threads
  // in the executor.
  std::optional<size_t> maxConcurrency;
};

// executeParallelBatch runs a call with this many inputs on a sliding window
// instead of in batches, so both strategies execute it the same way.
inline bool fitsInOneBatch(
    const size_t inputs,
    const BatchingExecutionOptions& options) {
  return inputs <= options.batchSize;
}

// Sliding window parallel execution: maintains up to windowSize concurrent
// tasks, submitting new work as prior tasks complete. Best for forward-only
// iterators and streaming inputs where collection size is unknown.
template <
    class InputIterator,
    class ProcessFn,
    class InitializeFn,
    class AggregateFn>
auto executeParallelWindow(
    folly::ThreadPoolExecutor* executor,
    InputIterator first,
    InputIterator last,
    const ProcessFn& process,
    const InitializeFn& initialize,
    const AggregateFn& aggregate,
    int windowSize) -> std::invoke_result_t<InitializeFn> {
  using Input = std::remove_reference_t<decltype(*first)>;
  using Output = std::invoke_result_t<ProcessFn, Input>;

  if (windowSize == 1) {
    auto result = initialize();
    for (; first != last; ++first) {
      aggregate(result, process(*first));
    }
    return result;
  }

  folly::MPMCQueue<folly::Try<Output>> outputs(windowSize);

  folly::Optional<folly::exception_wrapper> exception;

  const auto newWorker =
      [executor, &process, &outputs, &exception](Input input) -> bool {
    auto worker = [&process, &outputs, input = std::move(input)]() {
      auto output =
          folly::makeTryWith([&] { return process(std::move(input)); });
      outputs.blockingWrite(std::move(output));
    };
    try {
      executor->add(std::move(worker));
    } catch (...) {
      if (!exception) {
        exception.assign(folly::exception_wrapper(std::current_exception()));
      }
      return false;
    }
    return true;
  };

  int pending = 0;
  for (int i = 0; i < windowSize && first != last; ++i, ++first) {
    if (!newWorker(std::move(*first))) {
      break;
    }
    ++pending;
  }

  auto result = initialize();

  while (pending != 0) {
    folly::Try<Output> resultTry;
    outputs.blockingRead(resultTry);
    --pending;

    if (exception) {
      continue;
    }

    if (resultTry.hasException()) {
      exception.assign(std::move(resultTry).exception());
    } else {
      try {
        aggregate(result, std::move(resultTry).value());
      } catch (...) {
        exception.assign(folly::exception_wrapper(std::current_exception()));
      }
    }

    if (!exception && first != last) {
      if (newWorker(std::move(*first))) {
        ++pending;
        ++first;
      }
    }
  }

  if (exception) {
    exception->throw_exception();
  }

  return result;
}

// Collection overload: delegates to iterator-based implementation.
template <
    class Collection,
    class ProcessFn,
    class InitializeFn,
    class AggregateFn>
auto executeParallelWindow(
    folly::ThreadPoolExecutor* executor,
    const Collection& collection,
    const ProcessFn& process,
    const InitializeFn& initialize,
    const AggregateFn& aggregate,
    int windowSize) -> std::invoke_result_t<InitializeFn> {
  return executeParallelWindow(
      executor,
      collection.begin(),
      collection.end(),
      process,
      initialize,
      aggregate,
      windowSize);
}

// Batch-based parallel execution: groups items into fixed-size batches
// and distributes them to consumer threads via a queue. Best for large
// collections where batch processing amortizes queue contention. A collection
// that fits in one batch (see fitsInOneBatch) runs one task per input instead,
// up to the same concurrency, since one consumer would process all of it
// serially.
template <
    class Collection,
    class ProcessFn,
    class InitializeFn,
    class AggregateFn>
auto executeParallelBatch(
    folly::ThreadPoolExecutor* executor,
    const Collection& collection,
    const ProcessFn& process,
    const InitializeFn& initialize,
    const AggregateFn& aggregate,
    const BatchingExecutionOptions& config = {})
    -> std::invoke_result_t<InitializeFn> {
  using Accumulator = std::invoke_result_t<InitializeFn>;
  using Input = typename Collection::value_type;

  if (collection.begin() == collection.end()) {
    return initialize();
  }

  if (config.batchSize == 0) [[unlikely]] {
    throw std::invalid_argument("batchSize must be greater than 0");
  }
  if (config.maxConcurrency && *config.maxConcurrency == 0) [[unlikely]] {
    throw std::invalid_argument("maxConcurrency must be greater than 0");
  }

  const auto executorConcurrency = std::max(size_t{1}, executor->numThreads());
  const auto numConsumers = std::min(
      executorConcurrency, config.maxConcurrency.value_or(executorConcurrency));
  const size_t batchSize = config.batchSize;

  auto it = collection.begin();
  const auto end = collection.end();
  auto firstBatch = std::make_unique<std::vector<Input>>();
  firstBatch->reserve(batchSize);
  for (; it != end && firstBatch->size() < batchSize; ++it) {
    firstBatch->push_back(*it);
  }
  // A Timeout collection can expire after the emptiness check above.
  if (firstBatch->empty()) {
    return initialize();
  }
  if (it == end) {
    return executeParallelWindow(
        executor,
        firstBatch->begin(),
        firstBatch->end(),
        process,
        initialize,
        aggregate,
        static_cast<int>(std::min(firstBatch->size(), numConsumers)));
  }

  using BatchPtr = detail::BatchPtr<Input>;
  folly::MPMCQueue<BatchPtr> queue(detail::kQueueCapacity);
  folly::MPMCQueue<BatchPtr> freelist(detail::kQueueCapacity);
  detail::BatchPool<Input> pool{freelist};

  struct alignas(detail::kCacheLineSize) AccSlot {
    Accumulator acc;
    explicit AccSlot(const InitializeFn& init) : acc(init()) {}
  };

  std::vector<AccSlot> accumulators;
  accumulators.reserve(numConsumers);
  for ([[maybe_unused]] auto i : folly::irange(numConsumers)) {
    accumulators.emplace_back(initialize);
  }
  std::vector<std::shared_ptr<folly::Baton<>>> done(numConsumers);
  detail::ExceptionTracker exTracker;

  for (auto idx : folly::irange(numConsumers)) {
    done[idx] = std::make_shared<folly::Baton<>>();

    executor->add([idx,
                   &queue,
                   &process,
                   &aggregate,
                   &accumulators,
                   &exTracker,
                   &pool,
                   baton = done[idx]]() {
      detail::consumeBatches(
          queue,
          accumulators[idx].acc,
          process,
          aggregate,
          exTracker,
          pool,
          *baton);
    });
  }

  queue.blockingWrite(std::move(firstBatch));

  while (it != end) {
    auto batch = pool.allocate();
    batch->reserve(batchSize);

    for (size_t count = 0; it != end && count < batchSize; ++it, ++count) {
      batch->push_back(*it);
    }

    if (exTracker.hasError()) {
      break;
    }

    queue.blockingWrite(std::move(batch));
  }

  for ([[maybe_unused]] auto i : folly::irange(numConsumers)) {
    queue.blockingWrite(BatchPtr{});
  }

  for (auto& b : done) {
    b->wait();
  }

  auto result = initialize();

  try {
    for (auto& slot : accumulators) {
      aggregate(result, std::move(slot.acc));
    }
  } catch (const std::exception&) {
    exTracker.recordException(std::current_exception());
  }

  // No explicit cleanup needed - unique_ptrs in queues are automatically
  // destroyed when queues go out of scope
  exTracker.rethrowIfException();

  return result;
}

} // namespace facebook::rebalancer
