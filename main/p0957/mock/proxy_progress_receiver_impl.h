/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_PROGRESS_RECEIVER_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_PROGRESS_RECEIVER_IMPL_H_

#include <utility>
#include <exception>

struct IProgressReceiver {
  virtual void Initialize(std::size_t total);
  virtual void UpdateProgress(std::size_t progress);
  virtual bool IsCanceled() noexcept;
  virtual void OnException(std::exception_ptr ptr) noexcept;
};

namespace std::p0957::detail {

template <>
struct basic_proxy_meta<IProgressReceiver> {
  template <class P>
  constexpr explicit basic_proxy_meta(in_place_type_t<P>) noexcept
      : f0([](char& p, std::size_t arg_0) -> void
            { (*reinterpret_cast<P&>(p)).Initialize(arg_0); }),
        f1([](char& p, std::size_t arg_0) -> void
            { (*reinterpret_cast<const P&>(p)).UpdateProgress(arg_0); }),
        f2([](char& p) noexcept -> bool
            { return (*reinterpret_cast<const P&>(p)).IsCanceled(); }),
        f3([](char& p, std::exception_ptr&& arg_0) noexcept -> void
            { (*reinterpret_cast<const P&>(p)).OnException(move(arg_0)); }) {}
  basic_proxy_meta(const basic_proxy_meta&) = default;

  void (*f0)(char&, std::size_t);
  void (*f1)(char&, std::size_t);
  bool (*f2)(char&) noexcept;
  void (*f3)(char&, std::exception_ptr&&);
};

template <class P>
struct erased<IProgressReceiver, P> : erased_base<IProgressReceiver, P> {
  erased(const basic_proxy_meta<IProgressReceiver>& meta, P ptr)
      : erased_base<IProgressReceiver, P>(meta, forward<P>(ptr)) {}
  erased(const erased&) = default;

  void Initialize(std::size_t arg_0) const requires is_convertible_v<P, char&>
      { this->meta_.f0(forward<P>(this->ptr_), arg_0); }
  void UpdateProgress(std::size_t arg_0) const
      requires is_convertible_v<P, char&>
      { this->meta_.f1(forward<P>(this->ptr_), arg_0); }
  bool IsCanceled() const noexcept requires is_convertible_v<P, char&>
      { return this->meta_.f2(forward<P>(this->ptr_)); }
  void OnException(std::exception_ptr arg_0) const noexcept
      requires is_convertible_v<P, char&>
      { return this->meta_.f3(forward<P>(this->ptr_), move(arg_0)); }
};

}  // namespace std::p0957::detail

#endif  // SRC_MAIN_P0957_MOCK_PROXY_PROGRESS_RECEIVER_IMPL_H_
