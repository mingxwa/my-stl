/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_TYPE_TRAITS_H_
#define SRC_MAIN_COMMON_MORE_TYPE_TRAITS_H_

#include <type_traits>
#include <tuple>

namespace aid {

struct applicable_traits { static inline constexpr bool applicable = true; };
struct inapplicable_traits { static inline constexpr bool applicable = false; };

namespace aggregation_tratis_detail {

template <class SFINAE, class T>
struct sfinae_tuple_traits : inapplicable_traits {};

template <class T>
struct sfinae_tuple_traits<std::void_t<decltype(
    std::tuple_size<std::decay_t<T>>::value)>, T> : applicable_traits {};

template <class SFINAE, class T>
struct sfinae_container_traits : inapplicable_traits {};

template <class T>
struct sfinae_container_traits<std::void_t<
    decltype(std::declval<T>().begin() != std::declval<T>().end())>, T>
    : applicable_traits {};

}  // namespace aggregation_tratis_detail

template <class T>
inline constexpr bool is_tuple_v
    = aggregation_tratis_detail::sfinae_tuple_traits<void, T>::applicable;

template <class T>
inline constexpr bool is_container_v
    = aggregation_tratis_detail::sfinae_container_traits<void, T>::applicable;

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_TYPE_TRAITS_H_
