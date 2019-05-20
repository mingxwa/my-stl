/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_TEST_P1649_MY_TO_STRING_H_
#define SRC_TEST_P1649_MY_TO_STRING_H_

#include <utility>
#include <string>

#include "../../main/p1649/applicable_template.h"
#include "../../main/common/more_utility.h"

namespace test {

template <class T>
std::string my_to_string(const T& value);

namespace detail {

template <class T, class = decltype(std::to_string(std::declval<const T&>()))>
struct primitive_stringification_traits {
  static inline std::string apply(const T& value)
      { return std::to_string(value); }
};

template <class T, class = std::enable_if_t<
    std::is_convertible_v<const T&, std::string>>>
struct string_stringification_traits {
  static inline std::string apply(const T& value)
      { return static_cast<std::string>(value); }
};

struct aggregation_appender {
  template <class T>
  void operator()(const T& value) {
    if (is_first_) {
      is_first_ = false;
    } else {
      *result_ += ", ";
    }
    *result_ += my_to_string(value);
  }

  std::string* const result_;
  bool is_first_ = true;
};

template <class T, class = std::enable_if_t<aid::is_container_v<T>>>
struct container_stringification_traits {
  static inline std::string apply(const T& value) {
    std::string result = "[";
    aid::for_each_in_container(value, aggregation_appender{&result});
    result += "]";
    return result;
  }
};

template <class T, class = std::enable_if_t<aid::is_tuple_v<T>>>
struct tuple_stringification_traits {
  static inline std::string apply(const T& value) {
    std::string result = "[";
    aid::for_each_in_tuple(value, aggregation_appender{&result});
    result += "]";
    return result;
  }
};

}  // namespace detail

template <class T>
std::string my_to_string(const T& value) {
  return std::applicable_template<
      std::equal_templates<detail::primitive_stringification_traits>,
      std::equal_templates<detail::string_stringification_traits>,
      std::equal_templates<
          detail::container_stringification_traits,
          detail::tuple_stringification_traits>
  >::type<T>::apply(value);
}

}  // namespace test

#endif  // SRC_TEST_P1649_MY_TO_STRING_H_
