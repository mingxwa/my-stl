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
class erased<IImmutableMap<K, V>, P> {
 public:
  explicit erased(const basic_proxy_meta<IImmutableMap<K, V>>& meta, P ptr)
      : meta_(meta), ptr_(forward<P>(ptr)) {}
  erased(const erased&) = default;

  const V& at(const K& arg_0) const requires is_convertible_v<P, const char&>
      { return meta_.f0(forward<P>(ptr_), arg_0); }

 private:
  basic_proxy_meta<IImmutableMap<K, V>> meta_;
  P ptr_;
};

}  // namespace std::p0957::detail

#endif  // SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
