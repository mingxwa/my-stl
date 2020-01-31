/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_CALLABLE_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_CALLABLE_IMPL_H_

#include <utility>

/**
 * template <class T>
 * facade Callable;  // undefined
 *
 * template <class R, class... Args>
 * facade Callable<R(Args...)> {
 *   R operator()(Args...) &&;
 * };
 */

template <class>
struct Callable;

namespace std::p0957 {

template <class R, class... Args,
    template <qualification_type, reference_type> class E>
struct proxy_meta<Callable<R(Args...)>, E> {
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
  static R f0(E<qualification_type::none, reference_type::lvalue> erased,
      Args&&... args) {
    if constexpr (is_void_v<R>) {
      erased.template cast<T>()(forward<Args>(args)...);
    } else {
      return erased.template cast<T>()(forward<Args>(args)...);
    }
  }

  R (*f0_)(E<qualification_type::none, reference_type::lvalue>, Args&&...);
};

template <class R, class... Args, class A>
class proxy<Callable<R(Args...)>, A> : public A {
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

  R operator()(Args... args) & {
    return A::meta().f0_(A::erased(), forward<Args>(args)...);
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_CALLABLE_IMPL_H_
