/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_TEST_COMMON_MY_TO_STRING_H_
#define SRC_TEST_COMMON_MY_TO_STRING_H_

#include <utility>
#include <string>

#include "../../main/common/more_utility.h"

namespace test {

namespace detail {

template <class T>
void append_to_string(std::string* s, T&& value);

struct string_appender {
  void operator()(std::string* s, std::string&& value) const {
    *s += std::move(value);
  }

  void operator()(std::string* s, const std::string& value) const {
    *s += value;
  }
};

struct primitive_appender {
  template <class T, class = decltype(std::to_string(std::declval<T>()))>
  void operator()(std::string* s, T&& value) const {
    *s += std::to_string(std::forward<T>(value));
  }
};

struct container_appender {
  template <class T,
      class = decltype(std::declval<T>().begin()),
      class = decltype(std::declval<T>().end())>
  void operator()(std::string* s, T&& value) const {
    auto begin = value.begin();
    auto end = value.end();
    if (begin == end) {
      *s += "[]";
      return;
    }
    *s += "[";
    for (;;) {
      append_to_string(s, *begin++);
      if (begin == end) {
        break;
      }
      *s += ", ";
    }
    *s += "]";
  }
};

struct tuple_appender {
  template <class T, class = std::enable_if_t<aid::is_tuple_v<T>>>
  void operator()(std::string* s, T&& value) const {
    if constexpr (aid::extended_tuple_size_v<T> == 0u) {
      *s += "[]";
    } else {
      *s += "[";
      aid::for_each_in_tuple(std::forward<T>(value), element_appender{s});
      *s += "]";
    }
  }

  struct element_appender {
    template <class T>
    void operator()(T&& value, std::integral_constant<std::size_t, 0u>) const
        { append_to_string(s, std::forward<T>(value)); }

    template <class T>
    void operator()(T&& value) const {
      *s += ", ";
      append_to_string(s, std::forward<T>(value));
    }

    std::string* s;
  };
};

template <class T>
void append_to_string(std::string* s, T&& value) {
  constexpr auto APPEND_PROCESS_SEQUENCE = std::make_tuple(
      std::make_tuple(
          string_appender{},
          primitive_appender{}),
      container_appender{},
      tuple_appender{});
  aid::invoke_applicable(APPEND_PROCESS_SEQUENCE, s, std::forward<T>(value));
}

}  // namespace detail

template <class T>
std::string my_to_string(T&& value) {
  std::string result{};
  detail::append_to_string(&result, std::forward<T>(value));
  return result;
}

}  // namespace test

#endif  // SRC_TEST_COMMON_MY_TO_STRING_H_
