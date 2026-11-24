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
#include "algopt/rebalancer/algopt_common/Concepts.h"

#include <folly/container/irange.h>
#include <folly/CPortability.h>

#include <numeric>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace facebook::algopt::utils {

template <typename ThriftStructOrUnion, typename FieldType, size_t Index>
constexpr bool assignFieldAtThisIndex(
    ThriftStructOrUnion& structOrUnion,
    FieldType&& field) {
  using Ordinal = apache::thrift::type::detail::pos_to_ordinal<Index>;
  using ThisField =
      apache::thrift::op::get_native_type<ThriftStructOrUnion, Ordinal>;
  if constexpr (std::is_assignable_v<ThisField, FieldType>) {
    apache::thrift::op::get<Ordinal>(structOrUnion) =
        std::forward<FieldType>(field);
    return true;
  }
  return false;
}

template <typename ThriftStructOrUnion, typename FieldType, size_t... Index>
constexpr bool assignThriftField(
    ThriftStructOrUnion& structOrUnion,
    FieldType&& field,
    std::index_sequence<Index...>) {
  return (
      ... ||
      assignFieldAtThisIndex<ThriftStructOrUnion, FieldType, Index>(
          structOrUnion, std::forward<FieldType>(field)));
}

template <typename ThriftUnion>
void throwIfUnionIsEmpty(const ThriftUnion& u) {
  if (u.getType() == ThriftUnion::Type::__EMPTY__) {
    throw std::runtime_error(
        fmt::format(
            "Thrift union of type '{}' is empty. Please initialize it with a valid type.",
            apache::thrift::op::get_class_name<ThriftUnion>()));
  }
}

template <class ThriftStruct, class Field>
  requires FieldTypeAssignableToThriftStructOrUnion<ThriftStruct, Field>
void assignThriftStruct(ThriftStruct& thriftStruct, Field&& field) {
  assignThriftField<ThriftStruct, Field>(
      thriftStruct,
      std::forward<Field>(field),
      std::make_integer_sequence<
          size_t,
          apache::thrift::op::num_fields<ThriftStruct>>{});
}

template <class ThriftUnion, class Field>
  requires FieldTypeAssignableToThriftStructOrUnion<ThriftUnion, Field>
void assignThriftUnion(ThriftUnion& thriftUnion, Field&& field) {
  assignThriftField<ThriftUnion, Field>(
      thriftUnion,
      std::forward<Field>(field),
      std::make_integer_sequence<
          size_t,
          apache::thrift::op::num_fields<ThriftUnion>>{});
  throwIfUnionIsEmpty(thriftUnion);
}

template <class ThriftUnion, class Field>
ThriftUnion createThriftUnionByField(Field&& field) {
  ThriftUnion union_;
  assignThriftUnion(union_, std::forward<Field>(field));
  return union_;
}

// can be linear in the number of fields in the struct
template <typename ThriftStruct>
void setStringFieldToValueIfEmptyOrUnset(
    ThriftStruct& thriftStruct,
    const std::string& fieldName,
    std::string&& value) {
  apache::thrift::op::for_each_field_id<ThriftStruct>([&]<class Id>(Id) {
    if (apache::thrift::op::get_field_name<ThriftStruct, Id>() == fieldName) {
      auto ref = apache::thrift::op::get<Id>(thriftStruct);
      if constexpr (std::is_assignable_v<decltype(ref), std::string>) {
        constexpr bool is_optional = folly::is_instantiation_of_v<
            apache::thrift::optional_field_ref,
            decltype(ref)>;
        bool has_value = true;
        if constexpr (is_optional) {
          has_value = ref.has_value();
        }
        if (!has_value || ref->empty()) {
          ref = std::move(value);
        }
      }
    }
  });
}

// takes the data as a collection of cells, builds a table from it and
// returns the formatted table as a string
template <typename T>
std::string formatAsPrettyTable(
    const std::vector<std::vector<T>>& rows,
    const std::vector<T>& header = {},
    int cellWidth = 25,
    std::optional<int> sortByCol = std::nullopt,
    bool sortDescending = true) {
  if (header.empty() && rows.empty()) {
    return "";
  }
  int numCols = header.size();
  if (header.empty()) {
    numCols = rows.begin()->size();
  }
  const int rowWidth = cellWidth * numCols + (numCols - 1);
  auto head = fmt::format("┌{0:─^{1}}┐\n", "", rowWidth);
  auto tail = fmt::format("└{0:─^{1}}┘\n", "", rowWidth);
  auto hline = fmt::format("│{0:=^{1}}│\n", "", rowWidth);
  auto line = fmt::format("│{0:─^{1}}│\n", "", rowWidth);

  auto formatRow = [&](const std::vector<T>& row) {
    std::string rowStr;
    for (const auto& col : row) {
      rowStr += fmt::format("│{0:^{1}}", col, cellWidth);
    }
    return rowStr + "│\n";
  };

  std::stringstream ss;
  ss << "\n" << head;

  // build header (if provided)
  if (!header.empty()) {
    ss << formatRow(header) << hline;
  }

  std::vector<int> rowIdxInOrder(rows.size());
  std::iota(rowIdxInOrder.begin(), rowIdxInOrder.end(), 0);

  if (sortByCol.has_value()) {
    const auto col = sortByCol.value();
    auto rowOrderFunc = [&](const auto& i, const auto& j) {
      // try to convert the value to a double before comparing, otherwise just
      // compare as the provided type
      const auto& iVal = rows[i][col];
      const auto& jVal = rows[j][col];
      try {
        double iNum = std::stod(iVal);
        double jNum = std::stod(jVal);
        return sortDescending ? (iNum > jNum) : (iNum < jNum);
      } catch (const std::exception& /*unused*/) {
        return sortDescending ? (iVal > jVal) : (iVal < jVal);
      }
    };
    std::sort(rowIdxInOrder.begin(), rowIdxInOrder.end(), rowOrderFunc);
  };

  if (!rowIdxInOrder.empty()) {
    for (const auto i : folly::irange(rowIdxInOrder.size() - 1)) {
      ss << formatRow(rows[rowIdxInOrder[i]]) << line;
    }
    // last row is formatted differently
    ss << formatRow(rows[rowIdxInOrder.back()]) << tail;
  }
  return ss.str();
}

template <class M>
auto getMapKeys(const M& map) {
  std::vector<typename M::key_type> result;
  result.reserve(map.size());
  for (const auto& k : map | std::views::keys) {
    result.push_back(k);
  }
  return result;
}

template <template <typename, typename...> class Container>
struct to_fn {
  template <std::ranges::input_range Range>
  auto operator()(Range&& range) const {
    using ValueType = std::ranges::range_value_t<Range>;
    Container<ValueType> result;
    if constexpr (std::ranges::sized_range<Range> && requires {
                    result.reserve(size_t{});
                  }) {
      result.reserve(std::ranges::size(range));
    }
    for (auto&& element : range) {
      if constexpr (requires {
                      result.push_back(
                          std::forward<decltype(element)>(element));
                    }) {
        result.push_back(std::forward<decltype(element)>(element));
      } else if constexpr (requires {
                             result.insert(
                                 std::forward<decltype(element)>(element));
                           }) {
        result.insert(std::forward<decltype(element)>(element));
      } else {
        static_assert(
            !std::is_same_v<ValueType, ValueType>,
            "Unsupported container type");
      }
    }
    return result;
  }
  // Overload for pipe operator
  template <std::ranges::input_range Range>
  friend auto operator|(Range&& range, const to_fn& fn) {
    return fn(std::forward<Range>(range));
  }
};

template <template <typename, typename...> class Container>
inline constexpr to_fn<Container> to;

template <typename Container>
constexpr void reserveIfPossible(Container& container, size_t size) {
  if constexpr (requires { container.reserve(size); }) {
    container.reserve(size);
  }
}

// Use back_inserter if supported, otherwise use inserter
template <typename Container>
constexpr auto getBackInserterElseInserter(Container& container) {
  if constexpr (requires {
                  container.push_back(typename Container::value_type{});
                }) {
    return std::back_inserter(container);
  } else {
    return std::inserter(container, container.end());
  }
}

// Iterates `container`, invoking `fn(key, value)` for each entry. Uses the
// container's `forEachNonDefault` member if it has one, otherwise range-for.
template <typename Map, typename Fn>
void forEachNonDefault(const Map& keyToValue, Fn&& fn) {
  if constexpr (requires { keyToValue.forEachNonDefault(fn); }) {
    keyToValue.forEachNonDefault(std::forward<Fn>(fn));
  } else {
    for (const auto& [k, v] : keyToValue) {
      fn(k, v);
    }
  }
}

} // namespace facebook::algopt::utils
