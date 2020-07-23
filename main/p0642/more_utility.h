/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0642_MORE_UTILITY_H_
#define SRC_MAIN_P0642_MORE_UTILITY_H_

#include <utility>
#include <type_traits>
#include <tuple>
#include <functional>

#define STATIC_ASSERT_FALSE(...) static_assert(sizeof(__VA_ARGS__) < 0)

namespace aid {

template <std::size_t I = 0u, class TP, class F>
void for_each_in_tuple(TP&& tp, F&& f) {
  if constexpr (I != std::tuple_size_v<std::remove_reference_t<TP>>) {
    if constexpr (std::is_invocable_v<
        F, decltype(std::get<I>(std::declval<TP>()))>) {
      std::invoke(std::forward<F>(f), std::get<I>(std::forward<TP>(tp)));
    } else {
      STATIC_ASSERT_FALSE(TP);
    }
    for_each_in_tuple<I + 1u>(std::forward<TP>(tp), std::forward<F>(f));
  }
}

template <class Container, class F>
void for_each_in_container(Container&& c, F&& f) {
  auto begin = std::forward<Container>(c).begin();
  auto end = std::forward<Container>(c).end();
  using Value = std::conditional_t<!std::is_lvalue_reference_v<Container>
      && std::is_lvalue_reference_v<decltype(*begin)>,
      std::remove_reference_t<decltype(*begin)>&&, decltype(*begin)>;
  for (; begin != end; ++begin) {
    std::invoke(std::forward<F>(f), static_cast<Value>(*begin));
  }
}

template <class T, class F>
void for_each_in_aggregation(T&& value, F&& f);

namespace detail {

template <class SFINAE, class T>
struct sfinae_tuple_traits : std::false_type {};

template <class T>
struct sfinae_tuple_traits<std::void_t<decltype(
    std::tuple_size<std::decay_t<T>>::value)>, T> : std::true_type {};

template <class SFINAE, class T>
struct sfinae_container_traits : std::false_type {};

template <class T>
struct sfinae_container_traits<std::void_t<
    decltype(std::declval<T>().begin() != std::declval<T>().end())>, T>
    : std::true_type {};

template <class F>
struct for_each_applier {
  template <class T>
  void operator()(T&& value) const
      { for_each_in_aggregation(std::forward<T>(value), std::forward<F>(f_)); }

  F&& f_;
};

template <class SFINAE, class T, class F>
struct sfinae_for_each_traits;

template <class T, class F>
struct sfinae_for_each_traits<std::enable_if_t<
    sfinae_tuple_traits<void, T>::value>, T, F> {
  static inline void apply(T&& value, F&& f) {
    for_each_in_tuple(std::forward<T>(value),
        for_each_applier<F>{std::forward<F>(f)});
  }
};

template <class T, class F>
struct sfinae_for_each_traits<std::enable_if_t<
    sfinae_container_traits<void, T>::value>, T, F> {
  static inline void apply(T&& value, F&& f) {
    for_each_in_container(std::forward<T>(value),
        for_each_applier<F>{std::forward<F>(f)});
  }
};

template <class T, class F>
struct sfinae_for_each_traits<
    std::enable_if_t<std::is_invocable_v<F, T>>, T, F> {
  static inline void apply(T&& value, F&& f)
      { std::invoke(std::forward<F>(f), std::forward<T>(value)); }
};

}  // namespace detail

template <class T, class F>
void for_each_in_aggregation(T&& value, F&& f) {
  detail::sfinae_for_each_traits<void, T, F>
      ::apply(std::forward<T>(value), std::forward<F>(f));
}

}  // namespace aid

#endif  // SRC_MAIN_P0642_MORE_UTILITY_H_
