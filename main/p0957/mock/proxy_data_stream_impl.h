/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_

#include <utility>

template <class V>
struct IDataStream {
  virtual V next();
  virtual bool has_next() const noexcept;
};

namespace std::p0957::detail {

template <class V>
struct basic_proxy_meta<IDataStream<V>> {
  template <class P>
  constexpr explicit basic_proxy_meta(in_place_type_t<P>) noexcept
      : f0([](char& p) -> V
            { return (*reinterpret_cast<P&>(p)).next(); }),
        f1([](const char& p) noexcept -> bool
            { return (*reinterpret_cast<const P&>(p)).has_next(); }) {}
  basic_proxy_meta(const basic_proxy_meta&) = default;

  V (*f0)(char&);
  bool (*f1)(const char&) noexcept;
};

template <class P, class V>
struct erased<IDataStream<V>, P> : erased_base<IDataStream<V>, P> {
  erased(const basic_proxy_meta<IDataStream<V>>& meta, P ptr)
      : erased_base<IDataStream<V>, P>(meta, forward<P>(ptr)) {}
  erased(const erased&) = default;

  V next() const requires is_convertible_v<P, char&>
      { return this->meta_.f0(forward<P>(this->ptr_)); }
  bool has_next() const noexcept requires is_convertible_v<P, const char&>
      { return this->meta_.f1(forward<P>(this->ptr_)); }
};

}  // namespace std::p0957::detail

#endif  // SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_
