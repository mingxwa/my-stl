/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_PROXY_H_
#define SRC_MAIN_P0957_PROXY_H_

#include "./addresser.h"

namespace std {

template <class F, class A> class proxy;

namespace proxy_detail {

template <class P>
struct proxy_traits : aid::inapplicable_traits {};

template <class F, class A>
struct proxy_traits<proxy<F, A>> : aid::applicable_traits {};

template <class P>
inline constexpr bool is_proxy_v = proxy_traits<P>::applicable;

template <class SFINAE, class... Args>
struct sfinae_proxy_delegated_construction_traits : aid::applicable_traits {};

template <class T>
struct sfinae_proxy_delegated_construction_traits<
    enable_if_t<is_proxy_v<decay_t<T>>>, T>
    : aid::applicable_traits {};

template <class... Args>
inline constexpr bool is_proxy_delegated_construction_v
    = sfinae_proxy_delegated_construction_traits<void, Args...>::applicable;

}  // namespace proxy_detail

template <class F, template <qualification_type, reference_type> class E>
    struct proxy_meta;

template <size_t SIZE, size_t ALIGN>
struct erased_value_adapter {
  template <qualification_type Q, reference_type R>
  using type = erased_value<Q, R, SIZE, ALIGN>;
};

template <
    class F,
    size_t SIZE = sizeof(void*),
    size_t ALIGN = alignof(void*)
>
using value_proxy = proxy<
    decay_t<F>,
    value_addresser<
        proxy_meta<
            decay_t<F>,
            erased_value_adapter<SIZE, ALIGN>::template type
        >,
        SIZE,
        ALIGN
    >
>;

template <class F>
using reference_proxy = proxy<
    decay_t<F>,
    reference_addresser<
        proxy_meta<decay_t<F>, erased_reference>,
        qualification_of_v<F>
    >
>;

}  // namespace std

#endif  // SRC_MAIN_P0957_PROXY_H_
