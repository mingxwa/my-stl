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

namespace std::p0957 {

template <template <qualification_type, reference_type> class E>
struct proxy_meta<FC, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  explicit constexpr proxy_meta(in_place_type_t<T>) noexcept
      : f0_(f0<T>), f1_(f1<T>), f2_(f2<T>), f3_(f3<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static void f0(E<qualification_type::none, reference_type::lvalue> erased) {
    erased.template cast<T>().fun_a_0();
  }

  template <class T>
  static int f1(E<qualification_type::none, reference_type::lvalue> erased,
      double&& arg_0) {
    return erased.template cast<T>().fun_a_1(forward<double>(arg_0));
  }

  template <class T>
  static void f2(value_proxy<FA>&& arg_0) {
    T::fun_b(forward<value_proxy<FA>>(arg_0));
  }

  template <class T>
  static void f3(E<qualification_type::none, reference_type::lvalue> erased) {
    erased.template cast<T>().fun_c();
  }

  void (*f0_)(E<qualification_type::none, reference_type::lvalue>);
  int (*f1_)(E<qualification_type::none, reference_type::lvalue>, double&&);
  void (*f2_)(value_proxy<FA>&&);
  void (*f3_)(E<qualification_type::none, reference_type::lvalue>);
};

template <class A>
class proxy<FC, A> : public A {
 public:
  proxy(const proxy&) = default;
  proxy(proxy&&) = default;
  template <class... _Args, class = enable_if_t<
      !aid::is_qualified_same_v<proxy, _Args...>>>
  proxy(_Args&&... args) : A(forward<_Args>(args)...) {}
  proxy& operator=(const proxy& rhs) = default;
  proxy& operator=(proxy&& rhs) = default;
  template <class T, class = enable_if_t<!aid::is_qualified_same_v<proxy, T>>>
  proxy& operator=(T&& value) {
    A::operator=(forward<T>(value));
    return *this;
  }

  void fun_a_0() & {
    A::meta().f0_(A::erased());
  }

  int fun_a_1(double arg_0) & {
    return A::meta().f1_(A::erased(), forward<double>(arg_0));
  }

  void fun_b(value_proxy<FA> arg_0) {
    A::meta().f2_(forward<value_proxy<FA>>(arg_0));
  }

  void fun_c() & {
    A::meta().f3_(A::erased());
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_FC_IMPL_H_
