/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P1648_EXTENDED_H_
#define SRC_MAIN_P1648_EXTENDED_H_

#include <utility>
#include <tuple>
#include <initializer_list>

#include "../common/more_type_traits.h"
#include "../p1649/applicable_template.h"

namespace std {

template <class T>
auto make_extended_view(T&&);

namespace extended_detail {

template <class T, class = enable_if_t<is_void_v<T>>>
struct extended_void {
 public:
  constexpr explicit extended_void(tuple<>&&) noexcept {}

  constexpr void get() const noexcept {}
};

template <class T, class = enable_if_t<is_reference_v<T>>>
class extended_reference {
 public:
  constexpr explicit extended_reference(T value) noexcept : value_(&value) {}

  constexpr extended_reference(const extended_reference&) noexcept = default;
  constexpr extended_reference& operator=(const extended_reference&) noexcept
      = default;

  constexpr T&& get() const noexcept { return forward<T>(*value_); }

 private:
  remove_reference_t<T>* value_;
};

template <class T>
class extended_value {
 public:
  template <class... E_Args>
  constexpr explicit extended_value(tuple<E_Args...>&& args)
      : extended_value(move(args), index_sequence_for<E_Args...>()) {}

  template <class U>
  explicit constexpr extended_value(U&& value) : value_(forward<U>(value)) {}

  constexpr extended_value(extended_value&&) = default;
  constexpr extended_value(const extended_value&) = default;
  constexpr extended_value& operator=(extended_value&&) = default;
  constexpr extended_value& operator=(const extended_value&) = default;

  constexpr T& get() & noexcept { return value_; }
  constexpr const T& get() const& noexcept { return value_; }
  constexpr T&& get() && noexcept { return move(value_); }
  constexpr const T&& get() const&& noexcept { return move(value_); }

 private:
  template <class E_ArgsTuple, size_t... I>
  constexpr extended_value(E_ArgsTuple&& args, index_sequence<I...>) noexcept
      : value_(make_extended_view(std::get<I>(move(args))).get()...) {}

  T value_;
};

}  // namespace extended_detail

template <class T>
using extended = applicable_template<
    equal_templates<
        extended_detail::extended_void,
        extended_detail::extended_reference
    >,
    equal_templates<extended_detail::extended_value>
>::type<T>;

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

namespace extended_detail {

template <class T>
struct reference_wrapper_traits : aid::inapplicable_traits {};

template <class T>
struct reference_wrapper_traits<reference_wrapper<T>> : aid::applicable_traits
    { using constructed = T&; };

template <class T>
struct in_place_type_traits : aid::inapplicable_traits {};

template <class T>
struct in_place_type_traits<in_place_type_t<T>> : aid::applicable_traits
    { using constructed = T; };

template <class T>
struct extending_construction_traits : aid::inapplicable_traits {};

template <class T, class... Args>
struct extending_construction_traits<extending_construction<T, Args...>>
    : aid::applicable_traits { using constructed = T; };

template <class T>
struct tuple_traits : aid::inapplicable_traits {};

template <class... Args>
struct tuple_traits<tuple<Args...>> : aid::applicable_traits {};

template <class T, class = enable_if_t<
    reference_wrapper_traits<decay_t<T>>::applicable>>
struct extended_reference_wrapper_traits {
  using constructed = typename reference_wrapper_traits<decay_t<T>>
      ::constructed;
  static inline constructed get_arg(T&& value) { return value.get(); }
};

template <class T, class = enable_if_t<
    in_place_type_traits<decay_t<T>>::applicable>>
struct extended_in_place_type_traits {
  using constructed = typename in_place_type_traits<decay_t<T>>::constructed;
  static inline tuple<> get_arg(T&&) { return {}; }
};

template <class T, class = enable_if_t<
    extending_construction_traits<decay_t<T>>::applicable>>
struct extended_extending_construction_traits {
  using constructed = typename extending_construction_traits<decay_t<T>>
      ::constructed;
  static inline decltype(auto) get_arg(T&& value)
      { return forward<T>(value).get_args(); }
};

template <class T, class = enable_if_t<tuple_traits<T>::applicable>>
struct extended_tuple_traits {
  using constructed = T;
  static inline decltype(auto) get_arg(T&& value)
      { return forward_as_tuple(forward<T>(value)); }
};

template <class T>
struct extended_default_traits {
  using constructed = decay_t<T>;
  static inline T&& get_arg(T&& value) { return forward<T>(value); }
};

template <class T>
using extended_traits = applicable_template<
    equal_templates<
        extended_reference_wrapper_traits,
        extended_in_place_type_traits,
        extended_extending_construction_traits,
        extended_tuple_traits
    >,
    equal_templates<extended_default_traits>
>::type<T>;

}  // namespace extended_detail

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

template <class T>
using extending_t = typename extended_detail::extended_traits<T>::constructed;

template <class T>
decltype(auto) extended_arg(T&& value)
    { return extended_detail::extended_traits<T>::get_arg(forward<T>(value)); }

template <class T>
auto make_extended(T&& value)
    { return extended<extending_t<T>>{extended_arg(forward<T>(value))}; }

template <class T>
auto make_extended_view(T&& value) {
  if constexpr (is_same_v<T, extending_t<T>> && !is_reference_v<T>) {
    return extended<T&&>(move(value));
  } else {
    return make_extended(forward<T>(value));
  }
}

}  // namespace std

#endif  // SRC_MAIN_P1648_EXTENDED_H_
