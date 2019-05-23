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
#include "../p1649/applicable_template.h"

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

namespace for_each_in_tuple_detail {

template <std::size_t I, class Tuple, class F>
void for_each_in_tuple_helper(Tuple&& tp, F&& f);

template <class Index, class Tuple, class F, class = std::enable_if_t<
    Index::value == std::tuple_size_v<std::remove_reference_t<Tuple>>>>
struct for_each_in_tuple_boundary_processor
    { static inline void apply(Tuple&&, F&&) noexcept {} };

template <class Index, class Tuple, class F, class = std::enable_if_t<
    std::is_invocable_v<
        F, decltype(std::get<Index::value>(std::declval<Tuple>())), Index>>>
struct for_each_in_tuple_with_index_processor {
  static inline void apply(Tuple&& tp, F&& f) {
    std::invoke(std::forward<F>(f), std::get<Index::value>(
        std::forward<Tuple>(tp)), Index{});
    for_each_in_tuple_helper<Index::value + 1u>(
        std::forward<Tuple>(tp), std::forward<F>(f));
  }
};

template <class Index, class Tuple, class F, class = std::enable_if_t<
    std::is_invocable_v<
        F, decltype(std::get<Index::value>(std::declval<Tuple>()))>>>
struct for_each_in_tuple_without_index_processor {
  static inline void apply(Tuple&& tp, F&& f) {
    std::invoke(std::forward<F>(f),
        std::get<Index::value>(std::forward<Tuple>(tp)));
    for_each_in_tuple_helper<Index::value + 1u>(
        std::forward<Tuple>(tp), std::forward<F>(f));
  }
};

template <std::size_t I, class Tuple, class F>
void for_each_in_tuple_helper(Tuple&& tp, F&& f) {
  std::applicable_template<
      std::equal_templates<for_each_in_tuple_boundary_processor>,
      std::equal_templates<for_each_in_tuple_with_index_processor>,
      std::equal_templates<for_each_in_tuple_without_index_processor>
  >::type<std::integral_constant<std::size_t, I>, Tuple, F>
      ::apply(std::forward<Tuple>(tp), std::forward<F>(f));
}

}  // namespace for_each_in_tuple_detail

template <class Tuple, class F>
void for_each_in_tuple(Tuple&& tp, F&& f) {
  for_each_in_tuple_detail::for_each_in_tuple_helper<0u>(
      std::forward<Tuple>(tp), std::forward<F>(f));
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

namespace for_each_in_aggregation_detail {

template <class T, class F>
void for_each_impl(T&& value, F&& f);

template <class F>
struct applier {
  template <class T>
  void operator()(T&& value) const
      { for_each_impl(std::forward<T>(value), std::forward<F>(f_)); }

  F&& f_;
};

template <class T, class F, class = std::enable_if_t<is_tuple_v<T>>>
struct tuple_traits {
  static inline void apply(T&& value, F&& f) {
    for_each_in_tuple(std::forward<T>(value), applier<F>{std::forward<F>(f)});
  }
};

template <class T, class F, class = std::enable_if_t<is_container_v<T>>>
struct container_traits {
  static inline void apply(T&& value, F&& f) {
    for_each_in_container(std::forward<T>(value),
        applier<F>{std::forward<F>(f)});
  }
};

template <class T, class F, class = std::enable_if_t<std::is_invocable_v<F, T>>>
struct singleton_traits {
  static inline void apply(T&& value, F&& f)
      { std::invoke(std::forward<F>(f), std::forward<T>(value)); }
};

template <class T, class F>
void for_each_impl(T&& value, F&& f) {
  std::applicable_template<
      std::equal_templates<tuple_traits, container_traits, singleton_traits>>
      ::type<T, F>::apply(std::forward<T>(value), std::forward<F>(f));
}

}  // namespace for_each_in_aggregation_detail

template <class T, class F>
void for_each_in_aggregation(T&& value, F&& f) {
  for_each_in_aggregation_detail::for_each_impl(
      std::forward<T>(value), std::forward<F>(f));
}

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_UTILITY_H_
