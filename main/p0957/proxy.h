/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0957_PROXY_H_
#define SRC_MAIN_P0957_PROXY_H_

#include <new>
#include <initializer_list>
#include <type_traits>
#include <utility>
#include <typeinfo>
#include <tuple>
#include <concepts>

namespace std {

enum class constraint_level { none, nontrivial, nothrow, trivial };

template <class T, auto F> struct facade_expr;
template <class R, class... Args, auto F>
struct facade_expr<R(Args...), F> {
  using return_type = R;
  using argument_types = tuple<Args...>;
  static constexpr auto invoker = F;
  facade_expr() = delete;
};

template <class... Es>
struct facade {
  using expressions = tuple<Es...>;
  static constexpr size_t maximum_size = sizeof(void*) * 2u;
  static constexpr size_t maximum_alignment = alignof(void*);
  static constexpr constraint_level minimum_copyability =
      constraint_level::none;
  static constexpr constraint_level minimum_relocatability =
      constraint_level::nothrow;
  static constexpr constraint_level minimum_destructibility =
      constraint_level::nothrow;
  facade() = delete;
};

namespace detail {

struct applicable_traits { static constexpr bool applicable = true; };
struct inapplicable_traits { static constexpr bool applicable = false; };

template <class T>
constexpr bool has_copyability(constraint_level level) {
  switch (level) {
    case constraint_level::trivial: return is_trivially_copy_constructible_v<T>;
    case constraint_level::nothrow: return is_nothrow_copy_constructible_v<T>;
    case constraint_level::nontrivial: return is_copy_constructible_v<T>;
    case constraint_level::none: return true;
    default: return false;
  }
}
template <class T>
constexpr bool has_relocatability(constraint_level level) {
  switch (level) {
    case constraint_level::trivial:
      return is_trivially_move_constructible_v<T> &&
          is_trivially_destructible_v<T>;
    case constraint_level::nothrow:
      return is_nothrow_move_constructible_v<T> && is_nothrow_destructible_v<T>;
    case constraint_level::nontrivial:
      return is_move_constructible_v<T> && is_destructible_v<T>;
    case constraint_level::none: return true;
    default: return false;
  }
}
template <class T>
constexpr bool has_destructibility(constraint_level level) {
  switch (level) {
    case constraint_level::trivial: return is_trivially_destructible_v<T>;
    case constraint_level::nothrow: return is_nothrow_destructible_v<T>;
    case constraint_level::nontrivial: return is_destructible_v<T>;
    case constraint_level::none: return true;
    default: return false;
  }
}

template <class P> struct pointer_traits : inapplicable_traits {};
template <class P> requires(requires(P ptr) { { *ptr }; })
struct pointer_traits<P> : applicable_traits
    { using value_type = decltype(*declval<P&>()); };

template <class T, class... Us>
struct index_traits : integral_constant<size_t, 0u> {};
template <class T, class U, class... Us> requires(!is_same_v<T, U>)
struct index_traits<T, U, Us...>
    : integral_constant<size_t, index_traits<T, Us...>::value + 1u> {};

template <class E, class Args>
struct facade_expr_traits_impl : inapplicable_traits {};
template <class E, class... Args>
struct facade_expr_traits_impl<E, tuple<Args...>> : applicable_traits {
  using function_type = typename E::return_type (*)(char*, Args...);

  template <class T>
  static constexpr bool applicable_operand = requires(T operand, Args... args)
      { { E::invoker(forward<T>(operand), forward<Args>(args)...) }; };
  template <class P>
  static typename E::return_type invoke(char* p, Args... args)
      { return E::invoker(**reinterpret_cast<P*>(p), forward<Args>(args)...); }
};

template <class E>
struct facade_expr_traits : inapplicable_traits {};
template <class E> requires(requires {
      typename E::return_type;
      typename E::argument_types;
      { E::invoker };
    })
struct facade_expr_traits<E>
    : facade_expr_traits_impl<E, typename E::argument_types> {};

struct type_info_meta {
  template <class P>
  constexpr explicit type_info_meta(in_place_type_t<P>) : type(typeid(P)) {}

  const type_info& type;
};
template <class... Es>
struct facade_expr_meta {
  template <class P>
  constexpr explicit facade_expr_meta(in_place_type_t<P>)
      : expr_functions(facade_expr_traits<Es>::template invoke<P>...) {}

  tuple<typename facade_expr_traits<Es>::function_type...> expr_functions;
};
struct copy_meta {
  template <class P>
  constexpr explicit copy_meta(in_place_type_t<P>)
      : clone([](char* self, const char* rhs)
            { new(self) P(*reinterpret_cast<const P*>(rhs)); }) {}

  void (*clone)(char*, const char*);
};
struct relocation_meta {
  template <class P>
  constexpr explicit relocation_meta(in_place_type_t<P>)
      : relocate([](char* self, char* rhs) {
              new(self) P(move(*reinterpret_cast<P*>(rhs)));
              reinterpret_cast<P*>(rhs)->~P();
            }) {}

  void (*relocate)(char*, char*);
};
struct destruction_meta {
  template <class P>
  constexpr explicit destruction_meta(in_place_type_t<P>)
      : destroy([](char* self) { reinterpret_cast<P*>(self)->~P(); }) {}

  void (*destroy)(char*);
};

template <class... Ms>
struct facade_meta : Ms... {
  template <class P>
  constexpr explicit facade_meta(in_place_type_t<P>)
      : Ms(in_place_type<P>)... {}
};

template <constraint_level C, class M> struct conditional_meta_tag {};
template <class M, class Ms> struct facade_meta_traits_impl;
template <class M, class... Ms>
struct facade_meta_traits_impl<M, facade_meta<Ms...>>
    { using type = facade_meta<M, Ms...>; };
template <constraint_level C, class M, class... Ms>
    requires(C > constraint_level::none && C < constraint_level::trivial)
struct facade_meta_traits_impl<conditional_meta_tag<C, M>, facade_meta<Ms...>>
    { using type = facade_meta<M, Ms...>; };
template <constraint_level C, class M, class... Ms>
    requires(C < constraint_level::nontrivial || C > constraint_level::nothrow)
struct facade_meta_traits_impl<conditional_meta_tag<C, M>, facade_meta<Ms...>>
    { using type = facade_meta<Ms...>; };
template <class... Ms> struct facade_meta_traits;
template <class M, class... Ms>
struct facade_meta_traits<M, Ms...> : facade_meta_traits_impl<
    M, typename facade_meta_traits<Ms...>::type> {};
template <> struct facade_meta_traits<> { using type = facade_meta<>; };

template <class T, class U> struct flattening_traits_impl;
template <class T>
struct flattening_traits_impl<tuple<>, T> { using type = T; };
template <class T, class... Ts, class U>
struct flattening_traits_impl<tuple<T, Ts...>, U>
    : flattening_traits_impl<tuple<Ts...>, U> {};
template <class T, class... Ts, class... Us>
    requires(index_traits<T, Us...>::value == sizeof...(Us))
struct flattening_traits_impl<tuple<T, Ts...>, tuple<Us...>>
    : flattening_traits_impl<tuple<Ts...>, tuple<Us..., T>> {};
template <class T> struct flattening_traits { using type = tuple<T>; };
template <> struct flattening_traits<tuple<>> { using type = tuple<>; };
template <class T, class... Ts>
struct flattening_traits<tuple<T, Ts...>> : flattening_traits_impl<
    typename flattening_traits<T>::type,
    typename flattening_traits<tuple<Ts...>>::type> {};

template <class F, class Es> struct basic_facade_traits_impl;
template <class F, class... Es>
struct basic_facade_traits_impl<F, tuple<Es...>> : applicable_traits {
  using meta_type = typename facade_meta_traits<
      conditional_meta_tag<F::minimum_copyability, copy_meta>,
      conditional_meta_tag<F::minimum_relocatability, relocation_meta>,
      conditional_meta_tag<F::minimum_destructibility, destruction_meta>,
      type_info_meta>::type;
  using default_expr = conditional_t<
      sizeof...(Es) == 1u, tuple_element_t<0u, tuple<Es...>>, void>;

  template <class E> static constexpr size_t expr_index =
      index_traits<E, Es...>::value;
  template <class E> static constexpr bool has_expr =
      expr_index<E> < sizeof...(Es);
};
template <class F> struct basic_facade_traits : inapplicable_traits {};
template <class F> requires(requires {
      typename F::expressions;
      typename integral_constant<size_t, F::maximum_size>;
      typename integral_constant<size_t, F::maximum_alignment>;
      typename integral_constant<constraint_level, F::minimum_copyability>;
      typename integral_constant<constraint_level, F::minimum_relocatability>;
      typename integral_constant<constraint_level, F::minimum_destructibility>;
    })
struct basic_facade_traits<F> : basic_facade_traits_impl<
    F, typename flattening_traits<typename F::expressions>::type> {};

template <class F, class Es>
struct facade_traits_impl : inapplicable_traits {};
template <class F, class... Es>
    requires(facade_expr_traits<Es>::applicable && ...)
struct facade_traits_impl<F, tuple<Es...>> : applicable_traits {
  using meta_type = facade_meta<
      typename basic_facade_traits<F>::meta_type, facade_expr_meta<Es...>>;

  template <class P>
  static constexpr bool applicable_pointer =
      sizeof(P) <= F::maximum_size && alignof(P) <= F::maximum_alignment &&
      has_copyability<P>(F::minimum_copyability) &&
      has_relocatability<P>(F::minimum_relocatability) &&
      has_destructibility<P>(F::minimum_destructibility) &&
      (facade_expr_traits<Es>::template applicable_operand<
          typename pointer_traits<P>::value_type> && ...);
  template <class P> static constexpr meta_type meta{in_place_type<P>};
};
template <class F> struct facade_traits : facade_traits_impl<
    F, typename flattening_traits<typename F::expressions>::type> {};

template <class T, class U> struct dependent_traits { using type = T; };
template <class T, class U>
using dependent_t = typename dependent_traits<T, U>::type;

}  // namespace detail

class bad_proxy_cast : public bad_cast {
 public:
  const char* what() const noexcept override { return "Bad proxy cast"; }
};

template <class P, class F>
concept proxiable = detail::pointer_traits<P>::applicable &&
    detail::basic_facade_traits<F>::applicable &&
    detail::facade_traits<F>::applicable &&
    detail::facade_traits<F>::template applicable_pointer<P>;

template <class F> requires(detail::basic_facade_traits<F>::applicable)
class proxy {
  using BasicTraits = detail::basic_facade_traits<F>;
  using Traits = detail::facade_traits<F>;

  template <class P, class... Args>
  static constexpr bool HasNothrowPolyConstructor = conditional_t<
      proxiable<P, F>, is_nothrow_constructible<P, Args...>, false_type>::value;
  template <class P, class... Args>
  static constexpr bool HasPolyConstructor = conditional_t<
      proxiable<P, F>, is_constructible<P, Args...>, false_type>::value;
  static constexpr bool HasTrivialCopyConstructor =
      F::minimum_copyability == constraint_level::trivial;
  static constexpr bool HasNothrowCopyConstructor =
      F::minimum_copyability >= constraint_level::nothrow;
  static constexpr bool HasCopyConstructor =
      F::minimum_copyability >= constraint_level::nontrivial;
  static constexpr bool HasNothrowMoveConstructor =
      F::minimum_relocatability >= constraint_level::nothrow;
  static constexpr bool HasMoveConstructor =
      F::minimum_relocatability >= constraint_level::nontrivial;
  static constexpr bool HasTrivialDestructor =
      F::minimum_destructibility == constraint_level::trivial;
  static constexpr bool HasNothrowDestructor =
      F::minimum_destructibility >= constraint_level::nothrow;
  static constexpr bool HasDestructor =
      F::minimum_destructibility >= constraint_level::nontrivial;
  template <class P, class... Args>
  static constexpr bool HasNothrowPolyAssignment =
      HasNothrowPolyConstructor<P, Args...> && HasNothrowDestructor;
  template <class P, class... Args>
  static constexpr bool HasPolyAssignment = HasPolyConstructor<P, Args...> &&
      HasDestructor;
  static constexpr bool HasTrivialCopyAssignment = HasTrivialCopyConstructor &&
      HasTrivialDestructor;
  static constexpr bool HasNothrowCopyAssignment = HasNothrowCopyConstructor &&
      HasNothrowDestructor;
  static constexpr bool HasCopyAssignment = HasNothrowCopyAssignment ||
      (HasCopyConstructor && HasMoveConstructor && HasDestructor);
  static constexpr bool HasNothrowMoveAssignment = HasNothrowMoveConstructor &&
      HasNothrowDestructor;
  static constexpr bool HasMoveAssignment = HasMoveConstructor && HasDestructor;

 public:
  proxy() noexcept { meta_ = nullptr; }
  proxy(nullptr_t) noexcept : proxy() {}
  proxy(const proxy& rhs) noexcept(HasNothrowCopyConstructor)
      requires(!HasTrivialCopyConstructor && HasCopyConstructor) {
    if (rhs.meta_ != nullptr) {
      rhs.meta_->clone(ptr_, rhs.ptr_);
      meta_ = rhs.meta_;
    } else {
      meta_ = nullptr;
    }
  }
  proxy(const proxy&) noexcept requires(HasTrivialCopyConstructor) = default;
  proxy(const proxy&) requires(!HasCopyConstructor) = delete;
  proxy(proxy&& rhs) noexcept(HasNothrowMoveConstructor)
      requires(HasMoveConstructor) {
    if (rhs.meta_ != nullptr) {
      if constexpr (F::minimum_relocatability == constraint_level::trivial) {
        memcpy(ptr_, rhs.ptr_, F::maximum_size);
      } else {
        rhs.meta_->relocate(ptr_, rhs.ptr_);
      }
      meta_ = rhs.meta_;
      rhs.meta_ = nullptr;
    } else {
      meta_ = nullptr;
    }
  }
  proxy(proxy&&) requires(!HasMoveConstructor) = delete;
  template <class P>
  proxy(P&& ptr) noexcept(HasNothrowPolyConstructor<decay_t<P>, P>)
      requires(HasPolyConstructor<decay_t<P>, P>)
      : proxy(in_place_type<decay_t<P>>, forward<P>(ptr)) {}
  template <class P, class... Args>
  explicit proxy(in_place_type_t<P>, Args&&... args)
      noexcept(HasNothrowPolyConstructor<P, Args...>)
      requires(HasPolyConstructor<P, Args...>) {
    new(ptr_) P(forward<Args>(args)...);
    meta_ = &Traits::template meta<P>;
  }
  template <class P, class U, class... Args>
  explicit proxy(in_place_type_t<P>, initializer_list<U> il, Args&&... args)
      noexcept(HasNothrowPolyConstructor<P, initializer_list<U>&, Args...>)
      requires(HasPolyConstructor<P, initializer_list<U>&, Args...>)
      : proxy(in_place_type<P>, il, forward<Args>(args)...) {}
  proxy& operator=(nullptr_t) noexcept(HasNothrowDestructor)
      requires(HasDestructor) {
    this->~proxy();
    new(this) proxy();
    return *this;
  }
  proxy& operator=(const proxy& rhs) noexcept
      requires(!HasTrivialCopyAssignment && HasNothrowCopyAssignment) {
    if (this != &rhs) {
      this->~proxy();
      new(this) proxy(rhs);
    }
    return *this;
  }
  proxy& operator=(const proxy& rhs)
      requires(!HasNothrowCopyAssignment && HasCopyAssignment) {
    proxy temp{rhs};
    swap(temp);
    return *this;
  }
  proxy& operator=(const proxy&) noexcept requires(HasTrivialCopyAssignment) =
      default;
  proxy& operator=(const proxy&) requires(!HasCopyAssignment) = delete;
  proxy& operator=(proxy&& rhs) noexcept requires(HasNothrowMoveAssignment) {
    if (this != &rhs) {
      this->~proxy();
      new(this) proxy(move(rhs));
    }
    return *this;
  }
  proxy& operator=(proxy&& rhs)
      requires(!HasNothrowMoveAssignment && HasMoveAssignment) {
    proxy temp{move(rhs)};
    swap(temp);
    return *this;
  }
  proxy& operator=(proxy&&) requires(!HasMoveAssignment) = delete;
  template <class P>
  proxy& operator=(P&& ptr) noexcept
      requires(HasNothrowPolyAssignment<decay_t<P>, P>) {
    this->~proxy();
    new(this) proxy(forward<P>(ptr));
    return *this;
  }
  template <class P>
  proxy& operator=(P&& ptr) requires(!HasNothrowPolyAssignment<decay_t<P>, P> &&
      HasPolyAssignment<decay_t<P>, P>) {
    proxy temp{forward<P>(ptr)};
    swap(temp);
    return *this;
  }
  ~proxy() noexcept(HasNothrowDestructor)
      requires(!HasTrivialDestructor && HasDestructor) {
    if (meta_ != nullptr) {
      meta_->destroy(ptr_);
    }
  }
  ~proxy() requires(HasTrivialDestructor) = default;
  ~proxy() requires(!HasDestructor) = delete;

  bool has_value() const noexcept { return meta_ != nullptr; }
  const type_info& type() const noexcept
      { return meta_ == nullptr ? typeid(void) : meta_->type; }
  void reset() noexcept(HasNothrowDestructor) requires(HasDestructor)
      { this->~proxy(); meta_ = nullptr; }
  void swap(proxy& rhs) noexcept(HasNothrowMoveConstructor)
      requires(HasMoveConstructor) {
    if constexpr (F::minimum_relocatability == constraint_level::trivial) {
      std::swap(meta_, rhs.meta_);
      std::swap(ptr_, rhs.ptr);
    } else {
      if (meta_ != nullptr) {
        if (rhs.meta_ != nullptr) {
          proxy temp = move(*this);
          new(this) proxy(move(rhs));
          new(&rhs) proxy(move(temp));
        } else {
          new(&rhs) proxy(move(*this));
        }
      } else if (rhs.meta_ != nullptr) {
        new(this) proxy(move(rhs));
      }
    }
  }
  template <class P, class... Args>
  P& emplace(Args&&... args) noexcept(HasNothrowPolyAssignment<P, Args...>)
      requires(HasPolyAssignment<P, Args...>) {
    reset();
    new(this) proxy(in_place_type<P>, forward<Args>(args)...);
    return *reinterpret_cast<P*>(ptr_);
  }
  template <class P, class U, class... Args>
  P& emplace(initializer_list<U> il, Args&&... args)
      noexcept(HasNothrowPolyAssignment<P, initializer_list<U>&, Args...>)
      requires(HasPolyAssignment<P, initializer_list<U>&, Args...>)
      { return emplace<P>(il, forward<Args>(args)...); }
  template <class P>
  P& cast() requires(proxiable<P, F>) {
    if (type() != typeid(P)) {
      throw bad_proxy_cast{};
    }
    return *reinterpret_cast<P*>(ptr_);
  }
  template <class P>
  const P& cast() const requires(proxiable<P, F>) {
    if (type() != typeid(P)) {
      throw bad_proxy_cast{};
    }
    return *reinterpret_cast<const P*>(ptr_);
  }
  template <class E = typename BasicTraits::default_expr, class... Args>
  decltype(auto) invoke(Args&&... args)
      requires(detail::dependent_t<Traits, E>::applicable &&
          BasicTraits::template has_expr<E> &&
          is_convertible_v<tuple<Args...>, typename E::argument_types>) {
    return get<BasicTraits::template expr_index<E>>(
        static_cast<const typename Traits::meta_type*>(meta_)->expr_functions)(
        ptr_, forward<Args>(args)...);
  }

 private:
  const typename BasicTraits::meta_type* meta_;
  alignas(F::maximum_alignment) char ptr_[F::maximum_size];
};

namespace detail {

template <class T>
class sbo_ptr {
 public:
  template <class... Args>
  sbo_ptr(Args&&... args) : value_(forward<Args>(args)...) {}
  sbo_ptr(const sbo_ptr&) noexcept(is_nothrow_copy_constructible_v<T>)
      = default;
  sbo_ptr(sbo_ptr&&) noexcept(is_nothrow_move_constructible_v<T>) = default;

  T& operator*() { return value_; }

 private:
  T value_;
};

template <class T>
class deep_ptr {
 public:
  template <class... Args>
  deep_ptr(Args&&... args) : ptr_(new T(forward<Args>(args)...)) {}
  deep_ptr(const deep_ptr& rhs) noexcept(is_nothrow_copy_constructible_v<T>)
      requires(is_copy_constructible_v<T>)
      : ptr_(rhs.ptr_ == nullptr ? nullptr : new T(*rhs)) {}
  deep_ptr(deep_ptr&& rhs) noexcept : ptr_(rhs.ptr_) { rhs.ptr_ = nullptr; }
  ~deep_ptr() noexcept { delete ptr_; }

  T& operator*() const { return *ptr_; }

 private:
  T* ptr_;
};

}  // namespace detail

template <class F, class T, class... Args>
proxy<F> make_proxy(Args&&... args) {
  if constexpr (proxiable<detail::sbo_ptr<T>, F>) {
    return proxy<F>{in_place_type<detail::sbo_ptr<T>>, forward<Args>(args)...};
  } else {
    return proxy<F>{in_place_type<detail::deep_ptr<T>>, forward<Args>(args)...};
  }
}
template <class F, class T, class U, class... Args>
proxy<F> make_proxy(initializer_list<U> il, Args&&... args)
    { return make_proxy<F, T>(il, forward<Args>(args)...); }
template <class F, class T>
proxy<F> make_proxy(T&& value)
    { return make_proxy<F, decay_t<T>>(forward<T>(value)); }

template <class F>
void swap(std::proxy<F>& a, std::proxy<F>& b)
    noexcept(F::minimum_relocatability >= constraint_level::nothrow)
    requires(F::minimum_relocatability >= constraint_level::nontrivial)
    { a.swap(b); }

}  // namespace std

#endif  // SRC_MAIN_P0957_PROXY_H_
