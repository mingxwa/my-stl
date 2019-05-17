/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_TEST_COMMON_INSTANT_GC_H_
#define SRC_TEST_COMMON_INSTANT_GC_H_

#include <utility>
#include <unordered_map>
#include <unordered_set>

#include "../../main/common/more_utility.h"

namespace test {

namespace detail {

template <class T, class F>
void for_each_in_deletable_aggregation(T&& value, F& f);

template <class F>
struct recursice_deletable_applier {
  template <class T>
  void operator()(T&& value) const
      { for_each_in_deletable_aggregation(std::forward<T>(value), f_); }

  F& f_;
};

template <class T, class = decltype(std::declval<T>().begin()),
    class = decltype(std::declval<T>().begin() != std::declval<T>().end())>
struct deletable_container_traits {
  template <class F>
  static void apply(T&& value, F& f) {
    aid::for_each_in_container(std::forward<T>(value),
        recursice_deletable_applier<F>{f});
  }
};

template <class T, class = std::enable_if_t<aid::is_tuple_v<T>>>
struct deletable_tuple_traits {
  template <class F>
  static void apply(T&& value, F& f) {
    aid::for_each_in_tuple(std::forward<T>(value),
        recursice_deletable_applier<F>{f});
  }
};

template <class T>
struct deletable_singleton_traits {
  template <class F>
  static void apply(T&& value, F& f) { std::invoke(f, std::forward<T>(value)); }
};

template <class T>
using deletable_traits = aid::applicable_template<
    aid::equal_templates<deletable_container_traits, deletable_tuple_traits>,
    aid::equal_templates<deletable_singleton_traits>>::type<T>;

template <class T, class F>
void for_each_in_deletable_aggregation(T&& value, F& f)
    { deletable_traits<T>::apply(std::forward<T>(value), f); }

template <class SFINAE, class T>
struct sfinae_is_relay_deletable : aid::inapplicable_traits {};

template <class T>
struct sfinae_is_relay_deletable<
    std::void_t<decltype(std::declval<T>().get_related_pointers())>, T>
    : aid::applicable_traits {};

template <class T>
inline constexpr bool is_relay_deletable_t
    = sfinae_is_relay_deletable<void, T>::applicable;

template <class T>
using deletable_t = std::remove_cv_t<std::remove_pointer_t<T>>;

template <bool DEL>
struct deletable_processor {
  template <class T>
  void operator()(T* p) {
    if (visited_.find(p) != visited_.end()) {
      return;
    }
    visited_.insert(p);
    if constexpr (is_relay_deletable_t<T>) {
      for_each_in_deletable_aggregation(p->get_related_pointers(), *this);
    }
    if constexpr (DEL) { delete p; }
  }

  std::unordered_set<void*> visited_{nullptr};
};

}  // namespace detail

template <class T, class U>
void instant_gc(T&& all, U&& useful) {
  detail::deletable_processor<false> p0;
  detail::for_each_in_deletable_aggregation(std::forward<U>(useful), p0);
  detail::deletable_processor<true> p1{std::move(p0.visited_)};
  detail::for_each_in_deletable_aggregation(std::forward<T>(all), p1);
}

template <class T>
void instant_gc(T&& all) {
  detail::deletable_processor<true> p;
  detail::for_each_in_deletable_aggregation(std::forward<T>(all), p);
}

}  // namespace test

#endif  // SRC_TEST_COMMON_INSTANT_GC_H_
