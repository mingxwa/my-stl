/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_P0957_PROXY_H_
#define SRC_MAIN_P0957_PROXY_H_

#include <type_traits>
#include <utility>
#include <typeinfo>

namespace std::p0957 {

enum class type_requirements_level { none, nontrivial, nothrow, trivial };

struct default_proxy_config {
  static constexpr size_t max_size = sizeof(void*) * 3u;
  static constexpr size_t max_alignment = alignof(void*);
  static constexpr type_requirements_level copyability
      = type_requirements_level::none;
  static constexpr type_requirements_level relocatability
      = type_requirements_level::nothrow;
  static constexpr type_requirements_level destructibility
      = type_requirements_level::nothrow;
};
template <class I> struct global_proxy_config : default_proxy_config {};

namespace detail {

template <class I> struct basic_proxy_meta;
template <class I, class P> class erased;

template <type_requirements_level> struct copyability_meta
    { template <class _> constexpr explicit copyability_meta(_) {} };
template <>
struct copyability_meta<type_requirements_level::nothrow> {
  template <class P>
  constexpr explicit copyability_meta(in_place_type_t<P>)
      : clone([](char* self, const char& rhs) noexcept
          { new(self) P(reinterpret_cast<const P&>(rhs)); }) {}

  void (*clone)(char*, const char&) noexcept;
};
template <>
struct copyability_meta<type_requirements_level::nontrivial> {
  template <class P>
  constexpr explicit copyability_meta(in_place_type_t<P>)
      : clone([](char* self, const char& rhs)
          { new(self) P(reinterpret_cast<const P&>(rhs)); }) {}

  void (*clone)(char*, const char&);
};

template <type_requirements_level> struct relocatability_meta
    { template <class _> constexpr explicit relocatability_meta(_) {} };
template <>
struct relocatability_meta<type_requirements_level::nothrow> {
  template <class P>
  constexpr explicit relocatability_meta(in_place_type_t<P>)
      : relocate([](char* self, char&& rhs) noexcept {
        new(self) P(reinterpret_cast<P&&>(rhs));
        reinterpret_cast<P*>(&rhs)->~P();
      }) {}

  void (*relocate)(char*, char&&) noexcept;
};
template <>
struct relocatability_meta<type_requirements_level::nontrivial> {
  template <class P>
  constexpr explicit relocatability_meta(in_place_type_t<P>)
      : relocate([](char* self, char&& rhs) {
        new(self) P(reinterpret_cast<P&&>(rhs));
        reinterpret_cast<P*>(&rhs)->~P();
      }) {}

  void (*relocate)(char*, char&&);
};

template <type_requirements_level> struct destructibility_meta
    { template <class _> constexpr explicit destructibility_meta(_) {} };
template <>
struct destructibility_meta<type_requirements_level::nothrow> {
  template <class P>
  constexpr explicit destructibility_meta(in_place_type_t<P>)
      : destroy([](char* self) noexcept
          { reinterpret_cast<P*>(self)->~P(); }) {}

  void (*destroy)(char*) noexcept;
};
template <>
struct destructibility_meta<type_requirements_level::nontrivial> {
  template <class P>
  constexpr explicit destructibility_meta(in_place_type_t<P>)
      : destroy([](char* self) { reinterpret_cast<P*>(self)->~P(); }) {}

  void (*destroy)(char*);
};

template <class I, class C>
struct proxy_meta : copyability_meta<C::copyability>,
    relocatability_meta<C::relocatability>,
    destructibility_meta<C::destructibility> {
  template <class P>
  constexpr explicit proxy_meta(in_place_type_t<P>)
      : copyability_meta<C::copyability>(in_place_type<P>),
        relocatability_meta<C::relocatability>(in_place_type<P>),
        destructibility_meta<C::destructibility>(in_place_type<P>),
        get_type([]() noexcept -> const type_info& { return typeid(P); }),
        basic_meta(in_place_type<P>) {}

  const type_info&(*get_type)() noexcept;
  basic_proxy_meta<I> basic_meta;
};
template <class I, class C, class P>
constexpr proxy_meta<I, C> META{in_place_type<P>};

template <class P>
constexpr type_requirements_level get_copyability() {
  if (is_trivially_copy_constructible_v<P>) {
    return type_requirements_level::trivial;
  }
  if (is_nothrow_copy_constructible_v<P>) {
    return type_requirements_level::nothrow;
  }
  if (is_copy_constructible_v<P>) {
    return type_requirements_level::nontrivial;
  }
  return type_requirements_level::none;
}
template <class P>
constexpr type_requirements_level get_relocatability() {
  if (is_trivially_move_constructible_v<P> && is_trivially_destructible_v<P>) {
    return type_requirements_level::trivial;
  }
  if (is_nothrow_move_constructible_v<P> && is_nothrow_destructible_v<P>) {
    return type_requirements_level::nothrow;
  }
  if (is_move_constructible_v<P> && is_destructible_v<P>) {
    return type_requirements_level::nontrivial;
  }
  return type_requirements_level::none;
}
template <class P>
constexpr type_requirements_level get_destructibility() {
  if (is_trivially_destructible_v<P>) {
    return type_requirements_level::trivial;
  }
  if (is_nothrow_destructible_v<P>) {
    return type_requirements_level::nothrow;
  }
  if (is_destructible_v<P>) {
    return type_requirements_level::nontrivial;
  }
  return type_requirements_level::none;
}

}  // namespace detail

template <class P, class C>
concept proxiable = sizeof(P) <= C::max_size && alignof(P) <= C::max_alignment
    && detail::get_copyability<P>() >= C::copyability
    && detail::get_relocatability<P>() >= C::relocatability
    && detail::get_destructibility<P>() >= C::destructibility
    && C::destructibility >= type_requirements_level::nontrivial;
template <class P, class I>
concept globally_proxiable = proxiable<P, global_proxy_config<I>>;

template <class I, class C = global_proxy_config<I>>
class proxy {
 public:
  proxy() noexcept : meta_(nullptr) {}
  proxy(const proxy& rhs)
      noexcept(C::copyability >= type_requirements_level::nothrow)
      requires(C::copyability >= type_requirements_level::nontrivial) {
    if (rhs.meta_ != nullptr) {
      if constexpr (C::copyability == type_requirements_level::trivial) {
        memcpy(ptr_, rhs.ptr_, C::max_size);
      } else {
        rhs.meta_->clone(ptr_, *rhs.ptr_);
      }
      meta_ = rhs.meta_;
    } else {
      meta_ = nullptr;
    }
  }
  proxy(proxy&& rhs)
      noexcept(C::relocatability >= type_requirements_level::nothrow)
      requires(C::relocatability >= type_requirements_level::nontrivial) {
    if (rhs.meta_ != nullptr) {
      if constexpr (C::relocatability == type_requirements_level::trivial) {
        memcpy(ptr_, rhs.ptr_, C::max_size);
      } else {
        rhs.meta_->relocate(ptr_, move(*rhs.ptr_));
      }
      meta_ = rhs.meta_;
      rhs.meta_ = nullptr;
    } else {
      meta_ = nullptr;
    }
  }
  template <class P>
  proxy(P&& ptr) noexcept(is_nothrow_constructible_v<decay_t<P>, P>)
      requires(!is_same_v<decay_t<P>, proxy> && proxiable<decay_t<P>, C>)
      : proxy(in_place_type<decay_t<P>>, forward<P>(ptr)) {}
  template <class P, class... Args>
  explicit proxy(in_place_type_t<P>, Args&&... args)
      noexcept(is_nothrow_constructible_v<P, Args...>)
      requires(proxiable<P, C>) {
    new(ptr_) P(forward<Args>(args)...);
    meta_ = &detail::META<I, C, P>;
  }
  proxy& operator=(const proxy& rhs)
      noexcept(C::copyability >= type_requirements_level::nothrow
          && C::destructibility >= type_requirements_level::nothrow)
      requires(C::copyability >= type_requirements_level::nontrivial
          && C::destructibility >= type_requirements_level::nontrivial) {
    this->~proxy();
    new(this) proxy(rhs);
    return *this;
  }
  proxy& operator=(proxy&& rhs)
      noexcept(C::relocatability >= type_requirements_level::nothrow
          && C::destructibility >= type_requirements_level::nothrow)
      requires(C::relocatability >= type_requirements_level::nontrivial
          && C::destructibility >= type_requirements_level::nontrivial) {
    this->~proxy();
    new(this) proxy(move(rhs));
    return *this;
  }
  template <class P>
  proxy& operator=(P&& ptr)
      noexcept(is_nothrow_constructible_v<decay_t<P>, P>
          && C::relocatability >= type_requirements_level::nothrow
          && C::destructibility >= type_requirements_level::nothrow)
      requires(!is_same_v<decay_t<P>, proxy>
          && proxiable<decay_t<P>, C>
          && C::destructibility >= type_requirements_level::nontrivial) {
    proxy temp{forward<P>(ptr)};
    swap(temp);
    return *this;
  }
  ~proxy() noexcept(C::destructibility >= type_requirements_level::nothrow)
      requires(C::destructibility >= type_requirements_level::nontrivial) {
    if constexpr (C::destructibility != type_requirements_level::trivial) {
      if (meta_ != nullptr) {
        meta_->destroy(ptr_);
      }
    }
  }

  auto value() & { return detail::erased<I, char&>{meta_->basic_meta, *ptr_}; }
  auto value() const& { return detail::erased<I, const char&>{
      meta_->basic_meta, *ptr_}; }
  auto value() && { return detail::erased<I, char&&>{
      meta_->basic_meta, move(*ptr_)}; }
  auto value() const&& { return detail::erased<I, const char&&>{
      meta_->basic_meta, move(*ptr_)}; }
  auto operator*() & { return value(); }
  auto operator*() const& { return value(); }
  auto operator*() && { return move(*this).value(); }
  auto operator*() const&& { return move(*this).value(); }

  bool has_value() const noexcept { return meta_ != nullptr; }
  const type_info& type() const noexcept
      { return meta_ == nullptr ? typeid(void) : meta_->get_type(); }
  void reset() noexcept(C::destructibility >= type_requirements_level::nothrow)
      requires(C::destructibility >= type_requirements_level::nontrivial)
      { this->~proxy(); meta_ = nullptr; }
  void swap(proxy& rhs)
      noexcept(C::relocatability >= type_requirements_level::nothrow)
      requires(C::relocatability >= type_requirements_level::nontrivial) {
    if (meta_ != nullptr) {
      if (rhs.meta_ != nullptr) {
        alignas(C::max_alignment) char temp[C::max_size];
        if constexpr (C::relocatability == type_requirements_level::trivial) {
          memcpy(temp, ptr_, C::max_size);
          memcpy(ptr_, rhs.ptr_, C::max_size);
          memcpy(rhs.ptr_, temp, C::max_size);
        } else {
          meta_->relocate(temp, move(*ptr_));
          rhs.meta_->relocate(ptr_, move(*rhs.ptr_));
          meta_->relocate(rhs.ptr_, move(*temp));
        }
      } else {
        if constexpr (C::relocatability == type_requirements_level::trivial) {
          memcpy(rhs.ptr_, ptr_, C::max_size);
        } else {
          meta_->relocate(rhs.ptr_, move(*ptr_));
        }
      }
    } else if (rhs.meta_ != nullptr) {
      if constexpr (C::relocatability == type_requirements_level::trivial) {
        memcpy(ptr_, rhs.ptr_, C::max_size);
      } else {
        rhs.meta_->relocate(ptr_, move(*rhs.ptr_));
      }
    } else {
      return;
    }
    std::swap(meta_, rhs.meta_);
  }
  template <class P, class... Args>
  P& emplace(Args&&... args)
      noexcept(is_nothrow_constructible_v<P, Args...>
          && C::relocatability >= type_requirements_level::nothrow
          && C::destructibility >= type_requirements_level::nothrow)
      requires(proxiable<P, C>
          && C::destructibility >= type_requirements_level::nontrivial) {
    reset();
    proxy temp{in_place_type<P>, forward<Args>(args)...};
    swap(temp);
    return reinterpret_cast<P&>(ptr_);
  }

 private:
  const detail::proxy_meta<I, C>* meta_;
  alignas(C::max_alignment) char ptr_[C::max_size];
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

  T& operator*() & noexcept { return value_; }
  const T& operator*() const& noexcept { return value_; }
  T&& operator*() && noexcept { return move(value_); }
  const T&& operator*() const&& noexcept { return move(value_); }

 private:
  T value_;
};

template <class T>
class deep_ptr {
 public:
  template <class... Args>
  deep_ptr(Args&&... args) : ptr_(new T(forward<Args>(args)...)) {}
  deep_ptr(const deep_ptr& rhs) noexcept(is_nothrow_copy_constructible_v<T>)
      requires(is_copy_constructible_v<T>) : ptr_(new T(*rhs)) {}
  deep_ptr(deep_ptr&& rhs) noexcept requires(is_move_constructible_v<T>)
      : ptr_(new T(*move(rhs))) { rhs.ptr_ = nullptr; }
  ~deep_ptr() noexcept { delete ptr_; }

  T& operator*() & noexcept { return *ptr_; }
  const T& operator*() const& noexcept { return *ptr_; }
  T&& operator*() && noexcept { return move(*ptr_); }
  const T&& operator*() const&& noexcept { return move(*ptr_); }

 private:
  T* ptr_;
};

template <class I, class T, class C, class... Args>
proxy<I, C> make_proxy_impl(Args&&... args) {
  using P = conditional_t<proxiable<sbo_ptr<T>, C>, sbo_ptr<T>, deep_ptr<T>>;
  return proxy<I, C>{in_place_type<P>, forward<Args>(args)...};
}

}  // namespace detail

template <class I, class T, class C = global_proxy_config<I>, class... Args>
proxy<I, C> make_proxy(Args&&... args) {
  return detail::make_proxy_impl<I, T, C>(forward<Args>(args)...);
}

template <class I, class T, class C = global_proxy_config<I>, class U,
    class... Args>
proxy<I, C> make_proxy(initializer_list<U> il, Args&&... args) {
  return make_proxy<I, T, C>(il, forward<Args>(args)...);
}

template <class I, class T = void, class C = global_proxy_config<I>, class U>
proxy<I, C> make_proxy(U&& value) {
  return detail::make_proxy_impl<
      I, conditional_t<is_void_v<T>, decay_t<U>, T>, C>(forward<U>(value));
}

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_PROXY_H_
