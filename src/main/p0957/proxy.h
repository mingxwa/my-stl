/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_PROXY_H_
#define SRC_MAIN_P0957_PROXY_H_

#include <type_traits>
#include <utility>
#include <typeinfo>
#include <initializer_list>
#include <stdexcept>

#include "../p1144/trivially_relocatable.h"
#include "../p1172/memory_allocator.h"
#include "../p1648/extended.h"
#include "../common/more_utility.h"

namespace std {

enum class qualification_type
    { none, const_qualified, volatile_qualified, cv_qualified };

enum class reference_type { lvalue, rvalue };

namespace qualification_detail {

template <class T, qualification_type Q> struct add_qualification_helper;
template <class T>
struct add_qualification_helper<T, qualification_type::none>
    { using type = T; };
template <class T>
struct add_qualification_helper<T, qualification_type::const_qualified>
    { using type = add_const_t<T>; };
template <class T>
struct add_qualification_helper<T, qualification_type::volatile_qualified>
    { using type = add_volatile_t<T>; };
template <class T>
struct add_qualification_helper<T, qualification_type::cv_qualified>
    { using type = add_cv_t<T>; };

template <class T>
struct qualification_of_helper
    { static constexpr qualification_type value = qualification_type::none; };
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

template <class T, reference_type R>
using add_reference_t = conditional_t<R == reference_type::lvalue, T&, T&&>;

struct delegated_tag_t { explicit delegated_tag_t() = default; };
inline constexpr delegated_tag_t delegated_tag{};

class null_value_addresser_error : public logic_error {
 public:
  explicit null_value_addresser_error()
      : logic_error("The value addresser is not representing a value") {}
};

template <class T, class MA>
struct allocated_value {
  template <class _T>
  explicit allocated_value(_T&& value, MA&& ma)
      : value_(make_extended(forward<_T>(value))), ma_(move(ma)) {}

  T value_;
  MA ma_;
};

namespace erased_detail {

template <class T, size_t SIZE, size_t ALIGN>
inline constexpr bool VALUE_USES_SBO = sizeof(T) <= SIZE
    && alignof(T) <= ALIGN && is_trivially_relocatable_v<T>;

template <size_t SIZE, size_t ALIGN>
union value_storage {
  alignas(ALIGN) char value_[SIZE];
  void* ptr_;
};

template <qualification_type Q, reference_type R, class = enable_if_t<
    R == reference_type::lvalue>>
class erased_reference_impl {
 public:
  erased_reference_impl(add_qualification_t<void, Q>* p) : p_(p) {}

  template <class T>
  add_qualification_t<T, Q>& cast() const
      { return *static_cast<add_qualification_t<T, Q>*>(p_); }

 private:
  add_qualification_t<void, Q>* p_;
};

template <qualification_type Q, reference_type R, size_t SIZE, size_t ALIGN>
class erased_value_impl {
  using storage_ref = add_reference_t<add_qualification_t<
      erased_detail::value_storage<SIZE, ALIGN>, Q>, R>;

 public:
  erased_value_impl(storage_ref storage)
      : storage_(forward<storage_ref>(storage)) {}

  template <class T>
  add_reference_t<add_qualification_t<T, Q>, R> cast() const {
    using U = add_qualification_t<T, Q>;
    add_qualification_t<void, Q>* p;
    if constexpr (erased_detail::VALUE_USES_SBO<T, SIZE, ALIGN>) {
      p = &storage_.value_;
    } else {
      p = storage_.ptr_;
    }
    return forward<add_reference_t<U, R>>(*static_cast<U*>(p));
  }

 private:
  storage_ref storage_;
};

}  // namespace erased_detail

template <qualification_type Q, reference_type R>
using erased_reference = erased_detail::erased_reference_impl<Q, R>;

template <size_t SIZE, size_t ALIGN>
struct erased_value_selector {
  template <qualification_type Q, reference_type R>
  using type = erased_detail::erased_value_impl<Q, R, SIZE, ALIGN>;
};

namespace meta_detail {

template <class M, class... T>
inline constexpr M META_STORAGE{in_place_type<T>...};

template <class M, bool SBO = sizeof(M) <= sizeof(void*)>
struct reference_meta { using type = M; };

template <class M>
struct reference_meta<M, false> {
  struct type {
    type() = default;
    type(const type&) = default;
    explicit type(const M& rhs) { ptr_ = &rhs; }
    template <class T>
    explicit type(in_place_type_t<T>) { ptr_ = &META_STORAGE<M, T>; }
    type& operator=(const type&) = default;

    operator const M&() const { return *ptr_; }

    const M* ptr_;
  };
};

template <class T>
void destroy_small_value(void* erased)
    { static_cast<T*>(erased)->~T(); }

template <class T, class MA>
void destroy_large_value(void* erased) {
  allocated_value<T, MA>* p = *static_cast<allocated_value<T, MA>**>(erased);
  MA ma = move(p->ma_);
  aid::destroy(move(ma), p);
}

template <class T> const type_info& get_type() { return typeid(T); }

template <class M>
struct value_meta {
  template <class T>
  constexpr explicit value_meta(in_place_type_t<T>)
      : core_(in_place_type<T>), destroy_(destroy_small_value<T>),
        type_(get_type<T>) {}

  template <class T, class MA>
  constexpr explicit value_meta(in_place_type_t<T>, in_place_type_t<MA>)
      : core_(in_place_type<T>), destroy_(destroy_large_value<T, MA>),
        type_(get_type<T>) {}

  M core_;
  void (*destroy_)(void*);
  const type_info&(*type_)();
};

}  // namespace meta_detail

template <class M, size_t SIZE, size_t ALIGN>
class value_addresser {
  using storage_t = erased_detail::value_storage<SIZE, ALIGN>;

 public:
  bool has_value() const noexcept { return meta_ != nullptr; }
  const type_info& type() const noexcept
      { return meta_ == nullptr ? typeid(void) : meta_->type_(); }

  void reset() noexcept { value_addresser(delegated_tag).swap(*this); }

  template <class T>
  void assign(T&& val)
      { value_addresser(delegated_tag, forward<T>(val)).swap(*this); }

  template <class T, class MA>
  void assign(T&& val, MA&& ma) {
    value_addresser(delegated_tag, forward<T>(val), forward<MA>(ma))
        .swap(*this);
  }

  void swap(value_addresser& rhs) noexcept
      { std::swap(meta_, rhs.meta_); std::swap(storage_, rhs.storage_); }

 protected:
  value_addresser(value_addresser&& rhs) noexcept {
    meta_ = rhs.meta_;
    storage_ = rhs.storage_;
    rhs.meta_ = nullptr;
  }

  explicit value_addresser(delegated_tag_t) noexcept : meta_(nullptr) {}

  template <class E_T>
  explicit value_addresser(delegated_tag_t, E_T&& value)
      : value_addresser(delegated_tag, forward<E_T>(value),
          global_memory_allocator{}) {}

  template <class E_T, class E_MA>
  explicit value_addresser(delegated_tag_t, E_T&& value, E_MA&& ma)
      : value_addresser(delegated_tag) {
    if constexpr (erased_detail
        ::VALUE_USES_SBO<extending_t<E_T>, SIZE, ALIGN>) {
      init_small(forward<E_T>(value));
    } else {
      init_large(forward<E_T>(value), forward<E_MA>(ma));
    }
  }

  ~value_addresser() { if (meta_ != nullptr) { meta_->destroy_(&storage_); } }

  value_addresser& operator=(value_addresser&& rhs) noexcept
      { swap(rhs); return *this; }

  const M& meta() const {
    if (meta_ == nullptr) { throw null_value_addresser_error{}; }
    return meta_->core_;
  }
  storage_t& erased() & { return storage_; }
  storage_t&& erased() && { return move(storage_); }
  const storage_t& erased() const& { return storage_; }
  const storage_t&& erased() const&& { return move(storage_); }

 private:
  template <class E_T>
  void init_small(E_T&& value) {
    using T = extending_t<E_T>;
    new(&storage_.value_) T(make_extended(forward<E_T>(value)));
    meta_ = &meta_detail::META_STORAGE<meta_detail::value_meta<M>, T>;
  }

  template <class E_T, class E_MA>
  void init_large(E_T&& value, E_MA&& ma) {
    using T = extending_t<E_T>;
    using MA = extending_t<E_MA>;
    decltype(auto) ema = make_extended(forward<E_MA>(ma));
    storage_.ptr_ = aid::construct<allocated_value<T, MA>>(
        ema, forward<E_T>(value), forward<decltype(ema)>(ema));
    meta_ = &meta_detail::META_STORAGE<meta_detail::value_meta<M>, T, MA>;
  }

  const meta_detail::value_meta<M>* meta_;
  storage_t storage_;
};

template <class M, qualification_type Q>
class reference_addresser {
  template <class, qualification_type>
  friend class reference_addresser;

 public:
  template <class T>
  void assign(T& value) noexcept {
    meta_ = typename meta_detail::reference_meta<M>
        ::type{in_place_type<decay_t<T>>};
    ptr_ = &value;
  }

 protected:
  reference_addresser(const reference_addresser&) noexcept = default;

  template <class _M, qualification_type _Q>
  reference_addresser(const reference_addresser<_M, _Q>& rhs) noexcept
      : meta_(rhs.meta_), ptr_(rhs.ptr_) {}

  template <class T>
  explicit reference_addresser(delegated_tag_t, T& value) noexcept
      : meta_(in_place_type<decay_t<T>>), ptr_(&value) {}

  reference_addresser& operator=(const reference_addresser& rhs)
      noexcept = default;

  template <class _M, qualification_type _Q>
  reference_addresser& operator=(const reference_addresser<_M, _Q>& rhs)
      noexcept { meta_ = rhs.meta(); ptr_ = rhs.ptr_; return *this; }

  const M& meta() const noexcept { return meta_; }
  auto erased() const noexcept { return ptr_; }

 private:
  typename meta_detail::reference_meta<M>::type meta_;
  add_qualification_t<void, Q>* ptr_;
};

template <class F, template <qualification_type, reference_type> class E>
    struct proxy_meta;  // Mock implementation

template <class F, class A> class proxy;  // Mock implementation

namespace proxy_detail {

template <class P> struct proxy_traits : aid::inapplicable_traits {};
template <class F, class A> struct proxy_traits<proxy<F, A>>
    : aid::applicable_traits {};
template <class P> inline constexpr bool is_proxy_v
    = proxy_traits<P>::applicable;

template <class SFINAE, class... Args>
struct sfinae_proxy_delegated_construction_traits : aid::applicable_traits {};
template <class T>
struct sfinae_proxy_delegated_construction_traits<
    enable_if_t<is_proxy_v<decay_t<T>>>, T> : aid::applicable_traits {};

template <class... Args>
inline constexpr bool is_proxy_delegated_construction_v
    = sfinae_proxy_delegated_construction_traits<void, Args...>::applicable;

}  // namespace proxy_detail

template <class F, size_t SIZE = sizeof(void*), size_t ALIGN = alignof(void*)>
using value_proxy = proxy<F, value_addresser<proxy_meta<
    F, erased_value_selector<SIZE, ALIGN>::template type>, SIZE, ALIGN>>;

template <class F>
using reference_proxy = proxy<decay_t<F>, reference_addresser<
    proxy_meta<decay_t<F>, erased_reference>, qualification_of_v<F>>>;

}  // namespace std

#endif  // SRC_MAIN_P0957_PROXY_H_
