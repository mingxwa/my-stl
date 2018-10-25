/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_MORE_TYPE_TRAITS_H_
#define SRC_MAIN_EXPERIMENTAL_MORE_TYPE_TRAITS_H_

#include <type_traits>

namespace std {

enum class qualification
    { none, const_qualified, volatile_qualified, cv_qualified };

template <class T, qualification Q>
struct add_qualification;

template <class T>
struct add_qualification<T, qualification::none> {
  using type = T;
};

template <class T>
struct add_qualification<T, qualification::const_qualified> {
  using type = add_const_t<T>;
};

template <class T>
struct add_qualification<T, qualification::volatile_qualified> {
  using type = add_volatile_t<T>;
};

template <class T>
struct add_qualification<T, qualification::cv_qualified> {
  using type = add_cv_t<T>;
};

template <class T, qualification Q>
using add_qualification_t = typename add_qualification<T, Q>::type;

template <class T>
struct qualification_of {
  static constexpr qualification value = qualification::none;
};

template <class T>
struct qualification_of<const T> {
  static constexpr qualification value = qualification::const_qualified;
};

template <class T>
struct qualification_of<volatile T> {
  static constexpr qualification value = qualification::volatile_qualified;
};

template <class T>
struct qualification_of<const volatile T> {
  static constexpr qualification value = qualification::cv_qualified;
};

template <class T>
inline constexpr qualification qualification_of_v = qualification_of<T>::value;

template <qualification Q0, qualification Q1>
struct is_qualification_convertible
    : is_convertible<add_qualification_t<void, Q0>*,
                     add_qualification_t<void, Q1>*> {};

template <qualification Q0, qualification Q1>
inline constexpr bool is_qualification_convertible_v
    = is_qualification_convertible<Q0, Q1>::value;

}  // namespace std

#endif  // SRC_MAIN_EXPERIMENTAL_MORE_TYPE_TRAITS_H_
