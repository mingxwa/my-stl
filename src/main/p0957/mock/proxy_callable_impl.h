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
  constexpr explicit proxy_meta(in_place_type_t<T>) noexcept
      : callable_op_0_(callable_op_0<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static R callable_op_0(
      E<qualification_type::none, reference_type::rvalue> erased,
      Args&&... args) {
    if constexpr (is_void_v<R>) {
      erased.template cast<T>()(forward<Args>(args)...);
    } else {
      return erased.template cast<T>()(forward<Args>(args)...);
    }
  }

  R (*callable_op_0_)(E<qualification_type::none, reference_type::rvalue>,
      Args&&...);
};

template <class R, class... Args, class A>
class proxy<Callable<R(Args...)>, A> : public A {
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

  R operator()(Args... args) && {
    return A::meta().callable_op_0_(move(*this).A::erased(),
        forward<Args>(args)...);
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_CALLABLE_IMPL_H_
