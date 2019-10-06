/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_

#include <utility>

/**
 * template <class K, class V>
 * facade ImmutableMap {
 *   const V& at(const K&) const;
 * };
 */

template <class, class>
struct ImmutableMap;

namespace std::p0957 {

template <class K, class V,
    template <qualification_type, reference_type> class E>
struct proxy_meta<ImmutableMap<K, V>, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  constexpr explicit proxy_meta(in_place_type_t<T>) noexcept : f0_(f0<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static const V& f0(
      E<qualification_type::const_qualified, reference_type::lvalue> erased,
      const K& arg_0) {
    return erased.template cast<T>().at(arg_0);
  }

  const V& (*f0_)(
      E<qualification_type::const_qualified, reference_type::lvalue>, const K&);
};

template <class K, class V, class A>
class proxy<ImmutableMap<K, V>, A> : public A {
 public:
  proxy(const proxy&) = default;
  proxy(proxy&&) = default;
  template <class... _Args>
  proxy(_Args&&... args) : A(forward<_Args>(args)...) {}
  proxy& operator=(const proxy& rhs) = default;
  proxy& operator=(proxy&& rhs) = default;
  template <class T>
  proxy& operator=(T&& value) {
    A::operator=(forward<T>(value));
    return *this;
  }

  const V& at(const K& arg_0) const& {
    return A::meta().f0_(A::erased(), arg_0);
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
