/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_FC_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_FC_IMPL_H_

#include <utility>

#include "./proxy_fa_impl.h"
#include "./proxy_fb_impl.h"

/**
 * facade FC : FA, FB {
 *   void fun_c();
 * };
 */

struct FC;

namespace std {

template <template <qualification_type, reference_type> class E>
struct proxy_meta<FC, E> : proxy_meta<FA, E>, proxy_meta<FB, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  explicit constexpr proxy_meta(in_place_type_t<T>) noexcept
      : proxy_meta<FA, E>(in_place_type<T>),
        proxy_meta<FB, E>(in_place_type<T>), fc_op_0_(fc_op_0<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static void fc_op_0(
      E<qualification_type::none, reference_type::none> erased) {
    erased.template cast<T>().fun_c();
  }

  void (*fc_op_0_)(E<qualification_type::none, reference_type::none>);
};

template <class A>
class proxy<FC, A> : public A {
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

  void fun_a_0() {
    A::meta().fa_op_0_(A::data());
  }

  int fun_a_1(double arg_0) {
    return A::meta().fa_op_1_(A::data(), forward<double>(arg_0));
  }

  void fun_b(value_proxy<FA> arg_0) {
    A::meta().fb_op_0_(forward<value_proxy<FA>>(arg_0));
  }

  void fun_c() {
    A::meta().fc_op_0_(A::data());
  }
};

}  // namespace std

#endif  // SRC_MAIN_P0957_MOCK_PROXY_FC_IMPL_H_
