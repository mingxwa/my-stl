/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_UTILITY_H_
#define SRC_MAIN_COMMON_MORE_UTILITY_H_

#include <utility>
#include <type_traits>
#include <tuple>
#include <functional>

namespace aid {

struct applicable_traits { static inline constexpr bool applicable = true; };
struct inapplicable_traits { static inline constexpr bool applicable = false; };

namespace tuple_tratis_detail {

template <class SFINAE, class T>
struct sfinae_tuple_traits : inapplicable_traits {};

template <class T>
struct sfinae_tuple_traits<std::void_t<decltype(
    std::tuple_size<std::decay_t<T>>::value)>, T> : applicable_traits {};

}  // namespace tuple_tratis_detail

template <class T>
inline constexpr bool is_tuple_v
    = tuple_tratis_detail::sfinae_tuple_traits<void, T>::applicable;

template <template <class...> class... TTs>
struct equal_templates {};

namespace applicable_template_detail {

template <std::size_t I, class T,
    class = std::enable_if_t<(I < std::tuple_size_v<T>)>>
using extended_tuple_element_t = std::tuple_element_t<I, T>;

template <template <class...> class TT>
struct template_tag { template <class... Args> using type = TT<Args...>; };

template <class>
struct equal_template_to_tag_tuple;

template <template <class...> class... TTs>
struct equal_template_to_tag_tuple<equal_templates<TTs...>>
    { using type = std::tuple<template_tag<TTs>...>; };

template <class SFINAE, std::size_t From, class TTs, class... Args>
struct sfinae_applicable_equal_template_traits
    : sfinae_applicable_equal_template_traits<void, From + 1u, TTs, Args...> {};

template <std::size_t From, class TTs, class... Args>
struct sfinae_applicable_equal_template_traits<
    std::enable_if_t<From == std::tuple_size_v<TTs>>, From, TTs, Args...>
    : inapplicable_traits {};

template <std::size_t From, class TTs, class... Args>
struct sfinae_applicable_equal_template_traits<
    std::void_t<
        typename extended_tuple_element_t<From, TTs>::template type<Args...>
    >, From, TTs, Args...> : applicable_traits {
  static_assert(!sfinae_applicable_equal_template_traits<
      void, From + 1u, TTs, Args...>::applicable,
      "Ambiguous instantiation of templates with a same priority");

  using type = typename std::tuple_element_t<From, TTs>
      ::template type<Args...>;
};

template <class SFINAE, std::size_t From, class ETs, class... Args>
struct sfinae_applicable_template_selector {
  using type = typename sfinae_applicable_template_selector<
      void, From + 1u, ETs, Args...>::type;
};

template <std::size_t From, class ETs, class... Args>
struct sfinae_applicable_template_selector<
    std::enable_if_t<From == std::tuple_size_v<ETs>>, From, ETs, Args...> {
  static_assert(From != std::tuple_size_v<ETs>,
      "None of the provided templates is applicable");
};

template <std::size_t From, class ETs, class... Args>
struct sfinae_applicable_template_selector<
    std::enable_if_t<
        sfinae_applicable_equal_template_traits<
            void, 0u, extended_tuple_element_t<From, ETs>, Args...
        >::applicable
    >, From, ETs, Args...> {
  using type = typename sfinae_applicable_equal_template_traits<
      void, 0u, std::tuple_element_t<From, ETs>, Args...>::type;
};

}  // namespace applicable_template_detail

template <class... ETs>
struct applicable_template {
  template <class... Args>
  using type = typename applicable_template_detail
      ::sfinae_applicable_template_selector<
          void,
          0u,
          std::tuple<typename applicable_template_detail
              ::equal_template_to_tag_tuple<ETs>::type...>,
          Args...
      >::type;
};

template <class T>
auto make_extended_view(T&&);

namespace extended_detail {

template <class T, class = std::enable_if_t<std::is_void_v<T>>>
struct extended_void {
 public:
  constexpr explicit extended_void(std::tuple<>&&) noexcept {}

  constexpr void get() const noexcept {}
};

template <class T, class = std::enable_if_t<std::is_reference_v<T>>>
class extended_reference {
 public:
  constexpr explicit extended_reference(T value) noexcept : value_(&value) {}

  constexpr extended_reference(const extended_reference&) noexcept = default;
  constexpr extended_reference& operator=(const extended_reference&) noexcept
      = default;

  constexpr T&& get() const noexcept { return std::forward<T>(*value_); }

 private:
  std::remove_reference_t<T>* value_;
};

template <class T>
class extended_value {
 public:
  template <class... E_Args>
  constexpr explicit extended_value(std::tuple<E_Args...>&& args)
      : extended_value(std::move(args), std::index_sequence_for<E_Args...>()) {}

  template <class U>
  explicit constexpr extended_value(U&& value)
      : value_(std::forward<U>(value)) {}

  constexpr extended_value(extended_value&&) = default;
  constexpr extended_value(const extended_value&) = default;
  constexpr extended_value& operator=(extended_value&&) = default;
  constexpr extended_value& operator=(const extended_value&) = default;

  constexpr T& get() & noexcept { return value_; }
  constexpr const T& get() const& noexcept { return value_; }
  constexpr T&& get() && noexcept { return std::move(value_); }
  constexpr const T&& get() const&& noexcept { return std::move(value_); }

 private:
  template <class E_ArgsTuple, size_t... I>
  constexpr extended_value(E_ArgsTuple&& args, std::index_sequence<I...>)
      noexcept : value_(
          make_extended_view(std::get<I>(std::move(args))).get()...) {}

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
  // Known bug: Bad overload resolution for std::allocator_arg_t
  template <class... _E_Args>
  constexpr explicit extending_construction(_E_Args&&... args)
      : args_(std::forward<_E_Args>(args)...) {}

  constexpr extending_construction(extending_construction&&) = default;
  constexpr extending_construction(const extending_construction&) = default;
  constexpr extending_construction& operator=(extending_construction&&)
      = default;
  constexpr extending_construction& operator=(const extending_construction&)
      = default;

  constexpr std::tuple<E_Args...> get_args() const& { return args_; }
  constexpr std::tuple<E_Args...>&& get_args() && noexcept
      { return std::move(args_); }

 private:
  std::tuple<E_Args...> args_;
};

namespace extended_detail {

template <class T>
struct reference_wrapper_traits : inapplicable_traits {};

template <class T>
struct reference_wrapper_traits<std::reference_wrapper<T>>
    : applicable_traits { using constructed = T&; };

template <class T>
struct in_place_type_traits : inapplicable_traits {};

template <class T>
struct in_place_type_traits<std::in_place_type_t<T>>
    : applicable_traits { using constructed = T; };

template <class T>
struct extending_construction_traits : inapplicable_traits {};

template <class T, class... Args>
struct extending_construction_traits<extending_construction<T, Args...>>
    : applicable_traits { using constructed = T; };

template <class T>
struct tuple_traits : inapplicable_traits {};

template <class... Args>
struct tuple_traits<std::tuple<Args...>> : applicable_traits {};

template <class T, class = std::enable_if_t<
    reference_wrapper_traits<std::decay_t<T>>::applicable>>
struct extended_reference_wrapper_traits {
  using constructed = typename reference_wrapper_traits<std::decay_t<T>>
      ::constructed;
  static inline constructed get_arg(T&& value) { return value.get(); }
};

template <class T, class = std::enable_if_t<
    in_place_type_traits<std::decay_t<T>>::applicable>>
struct extended_in_place_type_traits {
  using constructed = typename in_place_type_traits<std::decay_t<T>>
      ::constructed;
  static inline std::tuple<> get_arg(T&&) { return {}; }
};

template <class T, class = std::enable_if_t<
    extending_construction_traits<std::decay_t<T>>::applicable>>
struct extended_extending_construction_traits {
  using constructed = typename extending_construction_traits<std::decay_t<T>>
      ::constructed;
  static inline decltype(auto) get_arg(T&& value)
      { return std::forward<T>(value).get_args(); }
};

template <class T, class = std::enable_if_t<tuple_traits<T>::applicable>>
struct extended_tuple_traits {
  using constructed = T;
  static inline decltype(auto) get_arg(T&& value)
      { return std::forward_as_tuple(std::forward<T>(value)); }
};

template <class T>
struct extended_default_traits {
  using constructed = std::decay_t<T>;
  static inline T&& get_arg(T&& value) { return std::forward<T>(value); }
};

template <class T>
struct extended_traits : applicable_template<
    equal_templates<
        extended_reference_wrapper_traits,
        extended_in_place_type_traits,
        extended_extending_construction_traits,
        extended_tuple_traits
    >,
    equal_templates<extended_default_traits>
>::type<T> {};

}  // namespace extended_detail

template <class T, class... E_Args>
auto make_extending_construction(E_Args&&... args) {
  return extending_construction<T, std::decay_t<E_Args>...>(
      std::forward<E_Args>(args)...);
}

template <class T, class U, class... E_Args>
auto make_extending_construction(std::initializer_list<U> il,
    E_Args&&... args) {
  return extending_construction<T, std::initializer_list<U>,
      std::decay_t<E_Args>...>(il, std::forward<E_Args>(args)...);
}

template <class T>
using extending_t = typename extended_detail::extended_traits<T>::constructed;

template <class T>
decltype(auto) extending_arg(T&& value) {
  return extended_detail::extended_traits<T>::get_arg(std::forward<T>(value));
}

template <class T>
auto make_extended(T&& value)
    { return extended<extending_t<T>>{extending_arg(std::forward<T>(value))}; }

template <class T>
auto make_extended_view(T&& value) {
  if constexpr (std::is_same_v<T, extending_t<T>> && !std::is_reference_v<T>) {
    return extended<T&&>(std::move(value));
  } else {
    return make_extended(std::forward<T>(value));
  }
}

template <class T, class MA, class... Args>
T* construct(MA&& ma, Args&&... args) {
  return new(ma.template allocate<sizeof(T), alignof(T)>())
      T(std::forward<Args>(args)...);
}

template <class MA, class T>
void destroy(MA&& ma, T* p) {
  p->~T();
  ma.template deallocate<sizeof(T), alignof(T)>(p);
}

namespace for_each_in_tuple_detail {

template <std::size_t I, class Tuple, class F>
void for_each_in_tuple_helper(Tuple&& tp, F* fp);

template <class Index, class Tuple, class F, class = std::enable_if_t<
    Index::value == std::tuple_size_v<std::remove_reference_t<Tuple>>>>
struct for_each_in_tuple_boundary_processor {
  static inline void apply(Tuple&&, F*) noexcept {}
};

template <class Index, class Tuple, class F, class = std::enable_if_t<
    std::is_invocable_v<
        F&, decltype(std::get<Index::value>(std::declval<Tuple>())), Index
    >>>
struct for_each_in_tuple_with_index_processor {
  static inline void apply(Tuple&& tp, F* fp) {
    std::invoke(*fp, std::get<Index::value>(std::forward<Tuple>(tp)), Index{});
    for_each_in_tuple_helper<Index::value + 1u>(std::forward<Tuple>(tp), fp);
  }
};

template <class Index, class Tuple, class F, class = std::enable_if_t<
    std::is_invocable_v<
        F&, decltype(std::get<Index::value>(std::declval<Tuple>()))>>>
struct for_each_in_tuple_without_index_processor {
  static inline void apply(Tuple&& tp, F* fp) {
    std::invoke(*fp, std::get<Index::value>(std::forward<Tuple>(tp)));
    for_each_in_tuple_helper<Index::value + 1u>(std::forward<Tuple>(tp), fp);
  }
};

template <std::size_t I, class Tuple, class F>
void for_each_in_tuple_helper(Tuple&& tp, F* fp) {
  applicable_template<
      equal_templates<for_each_in_tuple_boundary_processor>,
      equal_templates<for_each_in_tuple_with_index_processor>,
      equal_templates<for_each_in_tuple_without_index_processor>
  >::type<std::integral_constant<std::size_t, I>, Tuple, F>
      ::apply(std::forward<Tuple>(tp), fp);
}

}  // namespace for_each_in_tuple_detail

template <class Tuple, class F>
F for_each_in_tuple(Tuple&& tp, F f) {
  for_each_in_tuple_detail::for_each_in_tuple_helper<0u>(
      std::forward<Tuple>(tp), &f);
  return std::move(f);
}

template <class Container, class F>
F for_each_in_container(Container&& c, F f) {
  auto begin = std::forward<Container>(c).begin();
  auto end = std::forward<Container>(c).end();
  using Value = std::conditional_t<!std::is_lvalue_reference_v<Container>
      && std::is_lvalue_reference_v<decltype(*begin)>,
      std::remove_reference_t<decltype(*begin)>&&, decltype(*begin)>;
  for (; begin != end; ++begin) {
    std::invoke(f, static_cast<Value>(*begin));
  }
  return std::move(f);
}

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_UTILITY_H_
