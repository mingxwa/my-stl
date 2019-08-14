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
  constexpr explicit proxy_meta(in_place_type_t<T>) noexcept
      : immutable_map_op_0_(immutable_map_op_0<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static const V& immutable_map_op_0(
      E<qualification_type::const_qualified, reference_type::lvalue> erased,
      const K& arg_0) {
    return erased.template cast<T>().at(arg_0);
  }

  const V& (*immutable_map_op_0_)(
      E<qualification_type::const_qualified, reference_type::lvalue>, const K&);
};

template <class K, class V, class A>
class proxy<ImmutableMap<K, V>, A> : public A {
 public:
  proxy(const proxy&) = default;

  template <class _F, class _A>
  proxy(const proxy<_F, _A>& rhs) : A(static_cast<const _A&>(rhs)) {}

  proxy(proxy&&) = default;

  template <class _F, class _A>
  proxy(proxy<_F, _A>&& rhs) : A(static_cast<_A&&>(rhs)) {}

  template <class... _Args, class = enable_if_t<
      proxy_detail::is_proxy_delegated_construction_v<_Args...>>>
  proxy(_Args&&... args) : A(delegated_tag, forward<_Args>(args)...) {}

  template <class... _Args>
  proxy(delegated_tag_t, _Args&&... args)
      : A(delegated_tag, forward<_Args>(args)...) {}

  proxy& operator=(const proxy& rhs) = default;

  template <class _F, class _A>
  proxy& operator=(const proxy<_F, _A>& rhs) {
    static_cast<A&>(*this) = static_cast<const _A&>(rhs);
    return *this;
  }

  proxy& operator=(proxy&& rhs) = default;

  template <class _F, class _A>
  proxy& operator=(proxy<_F, _A>&& rhs) {
    static_cast<A&>(*this) = static_cast<_A&&>(rhs);
    return *this;
  }

  template <class T, class = enable_if_t<!proxy_detail::is_proxy_v<decay_t<T>>>>
  proxy& operator=(T&& value) {
    A::assign(forward<T>(value));
    return *this;
  }

  const V& at(const K& arg_0) const& {
    return A::meta().immutable_map_op_0_(A::erased(), arg_0);
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
