/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P1649_APPLICABLE_TEMPLATE_H_
#define SRC_MAIN_P1649_APPLICABLE_TEMPLATE_H_

#include <tuple>

#include "../common/more_type_traits.h"

namespace std {

template <template <class...> class... TTs>
struct equal_templates {};

namespace applicable_template_detail {

template <size_t I, class T, class = enable_if_t<(I < tuple_size_v<T>)>>
using extended_tuple_element_t = tuple_element_t<I, T>;

template <template <class...> class TT>
struct template_tag { template <class... Args> using type = TT<Args...>; };

template <class>
struct equal_template_to_tag_tuple;

template <template <class...> class... TTs>
struct equal_template_to_tag_tuple<equal_templates<TTs...>>
    { using type = tuple<template_tag<TTs>...>; };

template <class SFINAE, size_t From, class TTs, class... Args>
struct sfinae_applicable_equal_template_traits
    : sfinae_applicable_equal_template_traits<void, From + 1u, TTs, Args...> {};

template <size_t From, class TTs, class... Args>
struct sfinae_applicable_equal_template_traits<
    enable_if_t<From == tuple_size_v<TTs>>, From, TTs, Args...>
    : aid::inapplicable_traits {};

template <size_t From, class TTs, class... Args>
struct sfinae_applicable_equal_template_traits<
    void_t<
        typename extended_tuple_element_t<From, TTs>::template type<Args...>
    >, From, TTs, Args...> : aid::applicable_traits {
  static_assert(!sfinae_applicable_equal_template_traits<
      void, From + 1u, TTs, Args...>::applicable,
      "Ambiguous instantiation of templates with a same priority");

  using type = typename tuple_element_t<From, TTs>::template type<Args...>;
};

template <class SFINAE, size_t From, class ETs, class... Args>
struct sfinae_applicable_template_selector {
  using type = typename sfinae_applicable_template_selector<
      void, From + 1u, ETs, Args...>::type;
};

template <size_t From, class ETs, class... Args>
struct sfinae_applicable_template_selector<
    enable_if_t<From == tuple_size_v<ETs>>, From, ETs, Args...> {
  static_assert(From != tuple_size_v<ETs>,
      "None of the provided templates is applicable");
};

template <size_t From, class ETs, class... Args>
struct sfinae_applicable_template_selector<
    enable_if_t<
        sfinae_applicable_equal_template_traits<
            void, 0u, extended_tuple_element_t<From, ETs>, Args...
        >::applicable
    >, From, ETs, Args...> {
  using type = typename sfinae_applicable_equal_template_traits<
      void, 0u, tuple_element_t<From, ETs>, Args...>::type;
};

}  // namespace applicable_template_detail

template <class... ETs>
struct applicable_template {
  template <class... Args>
  using type = typename applicable_template_detail
      ::sfinae_applicable_template_selector<
          void,
          0u,
          std::tuple<typename applicable_template_detail
              ::equal_template_to_tag_tuple<ETs>::type...>,
          Args...
      >::type;
};

}  // namespace std

#endif  // SRC_MAIN_P1649_APPLICABLE_TEMPLATE_H_
