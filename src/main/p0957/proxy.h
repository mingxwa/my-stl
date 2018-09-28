/**
 * Copyright (c) 2017-2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_PROXY_H_
#define SRC_MAIN_P0957_PROXY_H_

#include <initializer_list>

#include "./addresser.h"

namespace std {

template <class F, template <bool, bool> class> struct facade_meta_t;
template <class F, class A> class proxy;

template <class P>
struct is_proxy : false_type {};
template <class F, class A>
struct is_proxy<proxy<F, A>> : true_type {};
template <class P> inline constexpr bool is_proxy_v = is_proxy<P>::value;

struct null_proxy_t { explicit null_proxy_t() = default; };
inline constexpr null_proxy_t null_proxy {};

template <size_t S, size_t A>
struct erased_value_adapter {
  template <bool C, bool V>
  using type = erased_value<S, A, C, V>;
};

template <class F, size_t SIZE = sizeof(void*), size_t ALIGN = alignof(void*)>
using value_proxy = proxy<decay_t<F>, value_addresser<facade_meta_t<decay_t<F>,
    erased_value_adapter<SIZE, ALIGN>::template type>, SIZE, ALIGN,
    is_const_v<F>, is_volatile_v<F>>>;

template <class F>
using reference_proxy = proxy<decay_t<F>,
    reference_addresser<facade_meta_t<decay_t<F>, erased_reference>,
    is_const_v<F>, is_volatile_v<F>>>;

}  // namespace std

#endif  // SRC_MAIN_P0957_PROXY_H_
