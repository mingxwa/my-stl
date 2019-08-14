/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_

#include <utility>

/**
 * template <class V>
 * facade DataStream {
 *   V next();
 *   bool has_next() const;
 * };
 */

template <class>
struct DataStream;

namespace std::p0957 {

template <class V, template <qualification_type, reference_type> class E>
struct proxy_meta<DataStream<V>, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  constexpr explicit proxy_meta(in_place_type_t<T>) noexcept
      : data_stream_op_0_(data_stream_op_0<T>),
        data_stream_op_1_(data_stream_op_1<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static V data_stream_op_0(
      E<qualification_type::none, reference_type::lvalue> erased) {
    if constexpr(is_void_v<V>) {
      erased.template cast<T>().next();
    } else {
      return erased.template cast<T>().next();
    }
  }

  template <class T>
  static bool data_stream_op_1(
      E<qualification_type::const_qualified, reference_type::lvalue> erased) {
    return erased.template cast<T>().has_next();
  }

  V (*data_stream_op_0_)(E<qualification_type::none, reference_type::lvalue>);
  bool (*data_stream_op_1_)(
      E<qualification_type::const_qualified, reference_type::lvalue>);
};

template <class V, class A>
class proxy<DataStream<V>, A> : public A {
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

  V next() & {
    return A::meta().data_stream_op_0_(A::erased());
  }

  bool has_next() const& {
    return A::meta().data_stream_op_1_(A::erased());
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_
