// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "rebalancer/explorer/cpp_server/lib/TableStore.h"

#include "algopt/rebalancer/algopt_common/Timer.h"

#include <fmt/core.h>
#include <folly/CancellationToken.h>
#include <folly/coro/WithCancellation.h>
#include <folly/logging/xlog.h>

#include <exception>
#include <stdexcept>
#include <utility>

namespace facebook::rebalancer::explorer {

TableStore::TableStore(
    std::shared_ptr<folly::Executor> executor,
    BuildTaskMap tableNameToBuildTask)
    : executor_(std::move(executor)) {
  tableNameToEntry_.reserve(tableNameToBuildTask.size());
  for (auto& [tableName, buildTask] : tableNameToBuildTask) {
    tableNameToEntry_.emplace(
        tableName, std::make_unique<Entry>(std::move(buildTask)));
  }
}

folly::coro::Task<const Table&> TableStore::get(
    const std::string& tableName) const {
  const auto* table = co_await tryGet(tableName);
  if (!table) {
    throw std::runtime_error(fmt::format("Table '{}' not found", tableName));
  }
  co_return *table;
}

folly::coro::Task<const Table*> TableStore::tryGet(
    const std::string& tableName) const {
  const auto* entryPtr = folly::get_ptr(tableNameToEntry_, tableName);
  if (!entryPtr) {
    co_return nullptr;
  }
  auto& entry = **entryPtr;

  if (!entry.buildStarted.exchange(true)) {
    try {
      const algopt::Timer timer(true);
      // Keep the shared build running if the request that started it is
      // cancelled, so later requests can use the result.
      auto table = std::make_shared<const Table>(
          co_await folly::coro::co_withCancellation(
              folly::CancellationToken{},
              folly::coro::co_withExecutor(
                  executor_.get(), std::move(entry.buildTask))));
      XLOGF(
          INFO,
          "Built Explorer table '{}' in {} seconds",
          tableName,
          timer.getSeconds());
      entry.table.setValue(table);
      co_return table.get();
    } catch (...) {
      entry.table.setException(
          folly::exception_wrapper{std::current_exception()});
      throw;
    }
  }

  co_return (co_await entry.table.getFuture()).get();
}

} // namespace facebook::rebalancer::explorer
