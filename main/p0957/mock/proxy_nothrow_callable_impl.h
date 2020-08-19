/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_NOTHROW_CALLABLE_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_NOTHROW_CALLABLE_IMPL_H_

#include <utility>

template <class> struct INothrowCallable;
template <class R, class... Args>
struct INothrowCallable<R(Args...)> {
  virtual R operator()(Args...) noexcept;
};

namespace std::p0957::detail {

template <class R, class... Args>
struct basic_proxy_meta<INothrowCallable<R(Args...)>> {
  template <class P>
  constexpr explicit basic_proxy_meta(in_place_type_t<P>) noexcept
      : f0([](char& p, Args&&... args) noexcept -> R
            { return (*reinterpret_cast<P&>(p))(forward<Args>(args)...); }) {}
  basic_proxy_meta(const basic_proxy_meta&) = default;

  R (*f0)(char&, Args&&...) noexcept;
};

template <class P, class R, class... Args>
class erased<INothrowCallable<R(Args...)>, P> {
 public:
  explicit erased(const basic_proxy_meta<INothrowCallable<R(Args...)>>& meta,
      P ptr) : meta_(meta), ptr_(forward<P>(ptr)) {}
  erased(const erased&) = default;

  R operator()(Args... args) const noexcept requires is_convertible_v<P, char&>
      { return meta_.f0(forward<P>(ptr_), forward<Args>(args)...); }

 private:
  const basic_proxy_meta<INothrowCallable<R(Args...)>>& meta_;
  P ptr_;
};

}  // namespace std::p0957::detail

#endif  // SRC_MAIN_P0957_MOCK_PROXY_NOTHROW_CALLABLE_IMPL_H_
