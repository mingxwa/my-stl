/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P1648_SINKING_H_
#define SRC_MAIN_P1648_SINKING_H_

#include <utility>
#include <tuple>
#include <initializer_list>

namespace std::p1648 {

template <class T, class... Args>
class sinking_construction {
 public:
  template <class... _Args>
  constexpr explicit sinking_construction(_Args&&... args)
      : args_(forward<_Args>(args)...) {}

  constexpr sinking_construction(sinking_construction&&) = default;
  constexpr sinking_construction(const sinking_construction&) = default;
  constexpr sinking_construction& operator=(sinking_construction&&) = default;
  constexpr sinking_construction& operator=(const sinking_construction&)
      = default;

  constexpr tuple<Args...> get_args() const& { return args_; }
  constexpr tuple<Args...>&& get_args() && noexcept { return move(args_); }

 private:
  tuple<Args...> args_;
};

template <class T, class... Args>
auto make_sinking_construction(Args&&... args) {
  return sinking_construction<T, decay_t<Args>...>(forward<Args>(args)...);
}

template <class T, class U, class... Args>
auto make_sinking_construction(initializer_list<U> il,  Args&&... args) {
  return sinking_construction<T, initializer_list<U>, decay_t<Args>...>(
      il, forward<Args>(args)...);
}

namespace detail {

template <class T>
struct sinking_traits {
  using sunk = T;
  template <class U>
  static conditional_t<is_same_v<T, U>, T&&, T> sink(U&& value)
      { return forward<T>(value); }
};

template <class T>
struct sinking_traits<in_place_type_t<T>> {
  using sunk = T;
  static inline T sink(in_place_type_t<T>) { return T{}; }
};

template <class T, class... Args>
struct sinking_traits<sinking_construction<T, Args...>> {
  using sunk = T;
  template <class U>
  static T sink(U&& value)
      { return make_from_tuple<T>(forward<U>(value).get_args()); }
};

}  // namespace detail

template <class T>
using sunk_t = typename detail::sinking_traits<decay_t<T>>::sunk;

template <class T>
decltype(auto) sink(T&& value)
    { return detail::sinking_traits<decay_t<T>>::sink(forward<T>(value)); }

}  // namespace std::p1648

#endif  // SRC_MAIN_P1648_SINKING_H_
