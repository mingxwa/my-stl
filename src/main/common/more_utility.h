/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_UTILITY_H_
#define SRC_MAIN_COMMON_MORE_UTILITY_H_

#include <utility>
#include <type_traits>
#include <tuple>
#include <functional>

#include "./more_type_traits.h"

#define STATIC_ASSERT_FALSE(...) static_assert(sizeof(__VA_ARGS__) == 0)

namespace aid {

template <class T, class MA, class... Args>
T* construct(MA&& ma, Args&&... args) {
  void* result = ma.template allocate<sizeof(T), alignof(T)>();
  try {
    return new(result) T(std::forward<Args>(args)...);
  } catch (...) {
    ma.template deallocate<sizeof(T), alignof(T)>(result);
    throw;
  }
}

template <class MA, class T>
void destroy(MA&& ma, T* p) {
  try {
    p->~T();
  } catch (...) {
    ma.template deallocate<sizeof(T), alignof(T)>(p);
    throw;
  }
  ma.template deallocate<sizeof(T), alignof(T)>(p);
}

template <std::size_t I = 0u, class TP, class F>
void for_each_in_tuple(TP&& tp, F&& f) {
  if constexpr (I != std::tuple_size_v<std::remove_reference_t<TP>>) {
    if constexpr (std::is_invocable_v<
        F, decltype(std::get<I>(std::declval<TP>())),
        std::integral_constant<std::size_t, I>>) {
      std::invoke(std::forward<F>(f), std::get<I>(
          std::forward<TP>(tp)), std::integral_constant<std::size_t, I>{});
    } else if constexpr (std::is_invocable_v<
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

namespace for_each_detail {

template <class F>
struct applier {
  template <class T>
  void operator()(T&& value) const
      { for_each_in_aggregation(std::forward<T>(value), std::forward<F>(f_)); }

  F&& f_;
};

template <class SFINAE, class T, class F>
struct sfinae_for_each_traits;

template <class T, class F>
struct sfinae_for_each_traits<std::enable_if_t<is_tuple_v<T>>, T, F> {
  static inline void apply(T&& value, F&& f) {
    for_each_in_tuple(std::forward<T>(value), applier<F>{std::forward<F>(f)});
  }
};

template <class T, class F>
struct sfinae_for_each_traits<std::enable_if_t<is_container_v<T>>, T, F> {
  static inline void apply(T&& value, F&& f) {
    for_each_in_container(std::forward<T>(value),
        applier<F>{std::forward<F>(f)});
  }
};

template <class T, class F>
struct sfinae_for_each_traits<
    std::enable_if_t<std::is_invocable_v<F, T>>, T, F> {
  static inline void apply(T&& value, F&& f)
      { std::invoke(std::forward<F>(f), std::forward<T>(value)); }
};

}  // namespace for_each_detail

template <class T, class F>
void for_each_in_aggregation(T&& value, F&& f) {
  for_each_detail::sfinae_for_each_traits<void, T, F>
      ::apply(std::forward<T>(value), std::forward<F>(f));
}

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_UTILITY_H_
