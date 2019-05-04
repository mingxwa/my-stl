/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_MORE_TYPE_TRAITS_H_
#define SRC_MAIN_P0957_MORE_TYPE_TRAITS_H_

#include <type_traits>

namespace std {

enum class qualification_type
    { none, const_qualified, volatile_qualified, cv_qualified };

enum class reference_type { none, lvalue, rvalue };

namespace qualification_detail {

template <class T, qualification_type Q>
struct add_qualification_helper;

template <class T>
struct add_qualification_helper<T, qualification_type::none> {
  using type = T;
};

template <class T>
struct add_qualification_helper<T, qualification_type::const_qualified> {
  using type = add_const_t<T>;
};

template <class T>
struct add_qualification_helper<T, qualification_type::volatile_qualified> {
  using type = add_volatile_t<T>;
};

template <class T>
struct add_qualification_helper<T, qualification_type::cv_qualified> {
  using type = add_cv_t<T>;
};

template <class T>
struct qualification_of_helper {
  static constexpr qualification_type value = qualification_type::none;
};

template <class T>
struct qualification_of_helper<const T> {
  static constexpr qualification_type value
      = qualification_type::const_qualified;
};

template <class T>
struct qualification_of_helper<volatile T> {
  static constexpr qualification_type value
      = qualification_type::volatile_qualified;
};

template <class T>
struct qualification_of_helper<const volatile T> {
  static constexpr qualification_type value = qualification_type::cv_qualified;
};

}  // namespace qualification_detail

template <class T, qualification_type Q>
using add_qualification_t
    = typename qualification_detail::add_qualification_helper<T, Q>::type;

template <class T>
inline constexpr qualification_type qualification_of_v
    = qualification_detail::qualification_of_helper<T>::value;

}  // namespace std

#endif  // SRC_MAIN_P0957_MORE_TYPE_TRAITS_H_
