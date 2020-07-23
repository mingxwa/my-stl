/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_

#include <utility>

template <class K, class V>
struct IImmutableMap {
  virtual const V& at(const K&) const;
};

namespace std::p0957::detail {

template <class K, class V>
struct basic_proxy_meta<IImmutableMap<K, V>> {
  template <class P>
  constexpr explicit basic_proxy_meta(in_place_type_t<P>) noexcept
      : f0([](const char& p, const K& arg_0) -> const V&
            { return (*reinterpret_cast<const P&>(p)).at(arg_0); }) {}
  basic_proxy_meta(const basic_proxy_meta&) = default;

  const V& (*f0)(const char&, const K&);
};

template <class P, class K, class V>
struct erased<IImmutableMap<K, V>, P> : erased_base<IImmutableMap<K, V>, P> {
  erased(const basic_proxy_meta<IImmutableMap<K, V>>& meta, P ptr)
      : erased_base<IImmutableMap<K, V>, P>(meta, forward<P>(ptr)) {}
  erased(const erased&) = default;

  const V& at(const K& arg_0) const requires is_convertible_v<P, const char&>
      { return this->meta_.f0(forward<P>(this->ptr_), arg_0); }
};

}  // namespace std::p0957::detail

#endif  // SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
