/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_FA_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_FA_IMPL_H_

#include <utility>

/**
 * facade FA {
 *   void fun_a_0();
 *   int fun_a_1(double some_arg);
 * };
 */

struct FA;

namespace std {

template <template <qualification_type, reference_type> class E>
struct proxy_meta<FA, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  constexpr explicit proxy_meta(in_place_type_t<T>) noexcept
      : fa_op_0_(fa_op_0<T>), fa_op_1_(fa_op_1<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static void fa_op_0(
      E<qualification_type::none, reference_type::none> erased) {
    erased.template cast<T>().fun_a_0();
  }

  template <class T>
  static int fa_op_1(E<qualification_type::none, reference_type::none> erased,
      double&& arg_0) {
    return erased.template cast<T>().fun_a_1(forward<double>(arg_0));
  }

  void (*fa_op_0_)(E<qualification_type::none, reference_type::none>);
  int (*fa_op_1_)(E<qualification_type::none, reference_type::none>, double&&);
};

template <class A>
class proxy<FA, A> : public A {
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
};

}  // namespace std

#endif  // SRC_MAIN_P0957_MOCK_PROXY_FA_IMPL_H_
