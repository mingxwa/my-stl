/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_UTILITY_H_
#define SRC_MAIN_COMMON_MORE_UTILITY_H_

#include <type_traits>
#include <functional>

namespace std::wang {

template <class T, bool EBO>
class wrapper_impl;

template <class T>
class wrapper_impl<T, true> : private T {
 public:
  template <class... Args>
  explicit wrapper_impl(Args&&... args) : T(forward<Args>(args)...) {}

  wrapper_impl(wrapper_impl&&) = default;
  wrapper_impl(const wrapper_impl&) = default;

  const T& get() const { return *this; }
  T&& get() { return forward<T>(*this); }
};

template <class T>
class wrapper_impl<T, false> {
 public:
  template <class... Args>
  explicit wrapper_impl(Args&&... args) : data_(forward<Args>(args)...) {}

  wrapper_impl(wrapper_impl&&) = default;
  wrapper_impl(const wrapper_impl&) = default;

  const T& get() const { return data_; }
  T&& get() { return forward<T>(data_); }

 private:
  T data_;
};

template <>
class wrapper_impl<void, false> { void get() const {} };

template <class T>
using wrapper = wrapper_impl<T, !is_final_v<T> && is_empty_v<T>>;

template <class T, class MA, class... Args>
T* construct(MA&& ma, Args&&... args) {
  T* value = static_cast<T*>(ma.allocate(integral_constant<size_t, sizeof(T)>{},
      integral_constant<size_t, alignof(T)>{}));
  return new (value) T(forward<Args>(args)...);
}

template <class MA, class T>
void destroy(MA&& ma, T* value) {
  value->~T();
  ma.deallocate(value, integral_constant<size_t, sizeof(T)>{},
                integral_constant<size_t, alignof(T)>{});
}

template <class T, class MA>
struct managed_storage : private wrapper<T>, private wrapper<MA> {
 public:
  template <class _MA, class... Args>
  explicit managed_storage(_MA&& ma, Args&&... args)
      : wrapper<T>(forward<Args>(args)...), wrapper<MA>(forward<_MA>(ma)) {}

  void destroy() {
    MA ma = wrapper<MA>::get();
    std::wang::destroy(forward<MA>(ma), this);
  }
};

template <class T, class MA, class... Args>
managed_storage<T, decay_t<MA>>* make_managed_storage(MA&& ma, Args&&... args) {
  using storage = managed_storage<T, decay_t<MA>>;
  storage* value = static_cast<storage*>(
      ma.allocate(integral_constant<size_t, sizeof(storage)>{},
                  integral_constant<size_t, alignof(storage)>{}));
  return new (value) storage(forward<MA>(ma), forward<Args>(args)...);
}

}  // namespace std::wang

#endif  // SRC_MAIN_COMMON_MORE_UTILITY_H_
