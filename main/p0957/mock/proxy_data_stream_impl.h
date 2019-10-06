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
      : f0_(f0<T>), f1_(f1<T>) {}

  constexpr proxy_meta() = default;
  constexpr proxy_meta(const proxy_meta&) noexcept = default;
  constexpr proxy_meta& operator=(const proxy_meta&) noexcept = default;

 private:
  template <class T>
  static V f0(E<qualification_type::none, reference_type::lvalue> erased) {
    if constexpr(is_void_v<V>) {
      erased.template cast<T>().next();
    } else {
      return erased.template cast<T>().next();
    }
  }

  template <class T>
  static bool f1(
      E<qualification_type::const_qualified, reference_type::lvalue> erased) {
    return erased.template cast<T>().has_next();
  }

  V (*f0_)(E<qualification_type::none, reference_type::lvalue>);
  bool (*f1_)(E<qualification_type::const_qualified, reference_type::lvalue>);
};

template <class V, class A>
class proxy<DataStream<V>, A> : public A {
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

  V next() & {
    return A::meta().f0_(A::erased());
  }

  bool has_next() const& {
    return A::meta().f1_(A::erased());
  }
};

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_MOCK_PROXY_DATA_STREAM_IMPL_H_
