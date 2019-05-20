/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_TEST_P1649_INSTANT_GC_H_
#define SRC_TEST_P1649_INSTANT_GC_H_

#include <utility>
#include <unordered_set>

#include "../../main/p1649/applicable_template.h"
#include "../../main/common/more_utility.h"

namespace test {

namespace detail {

template <class SFINAE, class T>
struct sfinae_is_relay_deletable_traits : aid::inapplicable_traits {};

template <class T>
struct sfinae_is_relay_deletable_traits<
    std::void_t<decltype(std::declval<T>().get_related_pointers())>, T>
    : aid::applicable_traits {};

template <bool DEL>
struct deletable_processor {
  template <class T>
  void operator()(T* p) {
    if (visited_.find(p) != visited_.end()) {
      return;
    }
    visited_.insert(p);
    if constexpr (sfinae_is_relay_deletable_traits<void, T>::applicable) {
      aid::for_each_in_aggregation(p->get_related_pointers(), *this);
    }
    if constexpr (DEL) { delete p; }
  }

  std::unordered_set<void*> visited_{nullptr};
};

}  // namespace detail

template <class T, class U>
void instant_gc(T&& all, U&& useful) {
  detail::deletable_processor<false> p0;
  aid::for_each_in_aggregation(std::forward<U>(useful), p0);
  detail::deletable_processor<true> p1{std::move(p0.visited_)};
  aid::for_each_in_aggregation(std::forward<T>(all), p1);
}

template <class T>
void instant_gc(T&& all) {
  detail::deletable_processor<true> p;
  aid::for_each_in_aggregation(std::forward<T>(all), p);
}

}  // namespace test

#endif  // SRC_TEST_P1649_INSTANT_GC_H_
