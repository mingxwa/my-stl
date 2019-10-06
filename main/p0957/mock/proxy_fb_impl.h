/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_FB_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_FB_IMPL_H_

#include <utility>

#include "./proxy_fa_impl.h"

/**
 * facade FB {
 *   static void fun_b(std::value_proxy<FA>);
 * };
 */

struct FB;

namespace std::p0957 {

template <template <qualification_type, reference_type> class E>
struct proxy_meta<FB, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  explicit constexpr proxy_meta(in_place_type_t<T>) noexcept : f0_(f0<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static void f0(value_proxy<FA>&& arg_0) {
    T::fun_b(forward<value_proxy<FA>>(arg_0));
  }

  void (*f0_)(value_proxy<FA>&&);
};

template <class A>
class proxy<FB, A> : public A {
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

  void fun_b(value_proxy<FA> arg_0) {
    A::meta().f0_(forward<value_proxy<FA>>(arg_0));
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_FB_IMPL_H_
