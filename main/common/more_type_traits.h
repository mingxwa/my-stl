/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_TYPE_TRAITS_H_
#define SRC_MAIN_COMMON_MORE_TYPE_TRAITS_H_

#include <type_traits>
#include <tuple>

namespace aid {

namespace detail {

template <class SFINAE, class T>
struct sfinae_tuple_traits : std::false_type {};

template <class T>
struct sfinae_tuple_traits<std::void_t<decltype(
    std::tuple_size<std::decay_t<T>>::value)>, T> : std::true_type {};

template <class SFINAE, class T>
struct sfinae_container_traits : std::false_type {};

template <class T>
struct sfinae_container_traits<std::void_t<
    decltype(std::declval<T>().begin() != std::declval<T>().end())>, T>
    : std::true_type {};

template <class T, class... U> struct is_qualified_same : std::false_type {};
template <class T, class U> struct is_qualified_same<T, U>
    : std::is_same<T, std::decay_t<U>> {};

}  // namespace detail

template <class T>
inline constexpr bool is_tuple_v = detail::sfinae_tuple_traits<void, T>::value;

template <class T>
inline constexpr bool is_container_v
    = detail::sfinae_container_traits<void, T>::value;

template <class T, class... U>
inline constexpr bool is_qualified_same_v
    = detail::is_qualified_same<T, U...>::value;

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_TYPE_TRAITS_H_
