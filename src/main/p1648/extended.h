/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P1648_EXTENDED_H_
#define SRC_MAIN_P1648_EXTENDED_H_

#include <utility>
#include <tuple>
#include <initializer_list>

namespace std::p1648 {

template <class T>
decltype(auto) make_extended(T&&);

template <class T, class... E_Args>
class extending_construction {
 public:
  // Known bug: Bad overload resolution for allocator_arg_t
  template <class... _E_Args>
  constexpr explicit extending_construction(_E_Args&&... args)
      : args_(forward<_E_Args>(args)...) {}

  constexpr extending_construction(extending_construction&&) = default;
  constexpr extending_construction(const extending_construction&) = default;
  constexpr extending_construction& operator=(extending_construction&&)
      = default;
  constexpr extending_construction& operator=(const extending_construction&)
      = default;

  constexpr tuple<E_Args...> get_args() const& { return args_; }
  constexpr tuple<E_Args...>&& get_args() && noexcept { return move(args_); }

 private:
  tuple<E_Args...> args_;
};

template <class T, class... E_Args>
auto make_extending_construction(E_Args&&... args) {
  return extending_construction<T, decay_t<E_Args>...>(
      forward<E_Args>(args)...);
}

template <class T, class U, class... E_Args>
auto make_extending_construction(initializer_list<U> il,  E_Args&&... args) {
  return extending_construction<T, initializer_list<U>, decay_t<E_Args>...>(
      il, forward<E_Args>(args)...);
}

namespace detail {

template <class T>
struct in_place_type_traits : false_type {};

template <class T>
struct in_place_type_traits<in_place_type_t<T>> : true_type
    { using constructed = T; };

template <class T>
struct extending_construction_traits : false_type {};

template <class T, class... Args>
struct extending_construction_traits<extending_construction<T, Args...>>
    : true_type {
  using constructed = T;
  static inline constexpr size_t ARGS_COUNT = sizeof...(Args);
};

template <class SFINAE, class T>
struct sfinae_extending_traits {
  using constructed = decay_t<T>;
  static inline T&& extend(T&& value) { return forward<T>(value); }
};

template <class T>
struct sfinae_extending_traits<
    enable_if_t<in_place_type_traits<decay_t<T>>::value>, T> {
  using constructed = typename in_place_type_traits<decay_t<T>>::constructed;
  static inline constructed extend(T&&) { return constructed{}; }
};

template <class T, class E_ArgsTuple, size_t... I>
T make_from_extending_tuple(E_ArgsTuple&& args, index_sequence<I...>)
    { return T{make_extended(get<I>(move(args)))...}; }

template <class T>
struct sfinae_extending_traits<enable_if_t<
    extending_construction_traits<decay_t<T>>::value>, T> {
  using constructed = typename extending_construction_traits<decay_t<T>>
      ::constructed;
  static inline constructed extend(T&& value) {
    return make_from_extending_tuple<constructed>(forward<T>(value).get_args(),
        make_index_sequence<extending_construction_traits<decay_t<T>>
            ::ARGS_COUNT>{});
  }
};

}  // namespace detail

template <class T>
using extending_t = typename detail::sfinae_extending_traits<void, T>
    ::constructed;

template <class T>
decltype(auto) make_extended(T&& value) {
  return detail::sfinae_extending_traits<void, T>::extend(forward<T>(value));
}

}  // namespace std::p1648

#endif  // SRC_MAIN_P1648_EXTENDED_H_
