/**
 * Copyright (c) 2017-2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
#define SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_

/**
 * template <class K, class V>
 * facade ImmutableMap {
 *   const V& at(const K&) const;
 * };
 */

template <class, class>
struct ImmutableMap;

namespace std {

template <class K, class V, template <qualification> class E>
struct facade_meta_t<ImmutableMap<K, V>, E> {
  template <class, class>
  friend class proxy;

 public:
  template <class T>
  constexpr explicit facade_meta_t(in_place_type_t<T>)
      : immutable_map_op_0_(immutable_map_op_0<T>) {}

  facade_meta_t() = default;
  constexpr facade_meta_t(const facade_meta_t&) = default;

 private:
  template <class T>
  static const V& immutable_map_op_0(E<qualification::const_qualified> erased, const K& arg_0) {
    return erased.cast(in_place_type<const T>).at(arg_0);
  }

  const V& (*immutable_map_op_0_)(E<qualification::const_qualified>, const K&);
};

template <class K, class V, class A>
class proxy<ImmutableMap<K, V>, A> : public A {
 public:
  proxy() : A() {}

  proxy(null_proxy_t) : A() {}

  proxy(const proxy&) = default;

  template <class _F, class _A>
  proxy(const proxy<_F, _A>& rhs) : A(static_cast<const _A&>(rhs)) {}

  proxy(proxy&&) = default;

  template <class _F, class _A>
  proxy(proxy<_F, _A>&& rhs) : A(static_cast<_A&&>(rhs)) {}

  template <class T, class = enable_if_t<!is_proxy_v<decay_t<T>>>>
  proxy(T&& value) : proxy(in_place_type<decay_t<T>>, forward<T>(value)) {}

  template <class T, class U, class... _Args,
            class = enable_if_t<is_same_v<T, decay_t<T>>>>
  explicit proxy(in_place_type_t<T>, initializer_list<U> il, _Args&&... args)
      : A(in_place_type<T>, il, forward<_Args>(args)...) {}

  template <class T, class... _Args,
            class = enable_if_t<is_same_v<T, decay_t<T>>>>
  explicit proxy(in_place_type_t<T>, _Args&&... args)
      : A(in_place_type<T>, forward<_Args>(args)...) {}

  proxy& operator=(null_proxy_t) {
    A::reset();
    return *this;
  }

  template <class T, class = enable_if_t<!is_proxy_v<decay_t<T>>>>
  proxy& operator=(T&& value) {
    A::reset(forward<T>(value));
    return *this;
  }

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

  const V& at(const K& arg_0) const {
    const A& a = static_cast<const A&>(*this);
    return a.meta().immutable_map_op_0_(a.data(), arg_0);
  }
};

}  // namespace std

#endif  // SRC_MAIN_P0957_MOCK_PROXY_IMMUTABLE_MAP_IMPL_H_
