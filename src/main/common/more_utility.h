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

enum class contextual_callable_type { accepts_data, plain, unknown };

template <class F, class T>
constexpr contextual_callable_type get_contextual_callable_type() {
  contextual_callable_type result = contextual_callable_type::unknown;
  if (is_invocable_v<F, T>) {
    result = contextual_callable_type::accepts_data;
  }
  if (is_invocable_v<F>) {
    if (result != contextual_callable_type::unknown) {
      return contextual_callable_type::unknown;  // Ambiguous callable
    }
    result = contextual_callable_type::plain;
  }
  return result;
}

template <class F, class T, contextual_callable_type>
struct invoke_contextual_helper;

template <class F, class T>
struct invoke_contextual_helper<F, T, contextual_callable_type::accepts_data> {
  static inline void apply(F&& f, wrapper<T>& w)
      { invoke(forward<F>(f), w.get()); }

  static inline void apply(F&& f, const wrapper<T>& w)
      { invoke(forward<F>(f), w.get()); }
};

template <class F, class T>
struct invoke_contextual_helper<F, T, contextual_callable_type::plain> {
  static inline void apply(F&& f, const wrapper<T>&)
      { invoke(forward<F>(f)); }
};

template <class F, class T>
void invoke_contextual(F&& f, wrapper<T>& w) {
  invoke_contextual_helper<F, T, get_contextual_callable_type<F, T>()>::apply(
      forward<F>(f), w);
}

template <class F, class T>
void invoke_contextual(F&& f, const wrapper<T>& w) {
  invoke_contextual_helper<F, T, get_contextual_callable_type<F,
      add_lvalue_reference_t<const T>>()>::apply(forward<F>(f), w);
}

template <class F, bool V>
struct make_wrapper_from_callable_helper;

template <class F>
struct make_wrapper_from_callable_helper<F, true> {
  static inline wrapper<decltype(invoke(declval<F>()))> apply(F&& f)
      { return invoke(forward<F>(f)); }
};

template <class F>
struct make_wrapper_from_callable_helper<F, false> {
  static inline wrapper<void> apply(F&& f)
      { invoke(forward<F>(f)); return {}; }
};

template <class F>
auto make_wrapper_from_callable(F&& f) {
  return make_wrapper_from_callable_helper<F, is_move_constructible_v<
      decltype(invoke(forward<F>(f)))>>::apply(forward<F>(f));
}

template <class T, bool V>
struct extract_data_from_wrapper_helper;

template <class T>
struct extract_data_from_wrapper_helper<T, true> {
  static inline T apply(wrapper<T>& w) { return w.get(); }
};

template <class T>
struct extract_data_from_wrapper_helper<T, false> {
  static inline void apply(wrapper<T>&) {}
};

template <class T>
auto extract_data_from_wrapper(wrapper<T>& w) {
  extract_data_from_wrapper_helper<T, is_move_constructible_v<T>>::apply(w);
}

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
