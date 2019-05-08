/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_ADDRESSER_H_
#define SRC_MAIN_P0957_ADDRESSER_H_

#include <typeinfo>
#include <utility>
#include <initializer_list>

#include "../p1144/trivially_relocatable.h"
#include "../p1172/memory_allocator.h"
#include "../common/more_utility.h"
#include "./more_type_traits.h"

namespace std {

struct delegated_tag_t { explicit delegated_tag_t() = default; };
inline constexpr delegated_tag_t delegated_tag{};

namespace detail {

template <class T, size_t SIZE, size_t ALIGN>
inline constexpr bool VALUE_USES_SBO = sizeof(aid::extended<T>) <= SIZE
    && alignof(aid::extended<T>) <= ALIGN && is_trivially_relocatable_v<T>;

template <class T, class MA>
struct managed_storage : aid::extended<T> {
 public:
  template <class _T>
  explicit managed_storage(_T&& value, aid::extended<MA>&& ma)
      : aid::extended<T>(forward<_T>(value)), ma_(move(ma)) {}

  aid::extended<MA> ma_;
};

template <size_t SIZE, size_t ALIGN>
union value_storage {
  aligned_storage_t<SIZE, ALIGN> value_;
  void* ptr_;
};

template <qualification_type Q, reference_type R, class = enable_if_t<
    R != reference_type::rvalue>>
struct erased_reference_impl {
 public:
  erased_reference_impl(add_qualification_t<void, Q>* p) : p_(p) {}

  template <class T>
  add_qualification_t<T, Q>& cast() const
      { return *static_cast<add_qualification_t<T, Q>*>(p_); }

 private:
  add_qualification_t<void, Q>* p_;
};

}  // namespace detail

template <qualification_type Q, reference_type R>
using erased_reference = detail::erased_reference_impl<Q, R>;

template <qualification_type Q, reference_type R, size_t SIZE, size_t ALIGN>
struct erased_value {
  template <class T, class = enable_if_t<R == reference_type::none
      || (is_lvalue_reference_v<T> == (R == reference_type::lvalue))>>
  erased_value(T&& storage) : storage_(&storage) {}

  template <class T>
  decltype(auto) cast() const {
    using U = add_qualification_t<aid::extended<T>, Q>;
    add_qualification_t<void, Q>* p;
    if constexpr (detail::VALUE_USES_SBO<T, SIZE, ALIGN>) {
      p = &storage_->value_;
    } else {
      p = storage_->ptr_;
    }
    return forward<conditional_t<R == reference_type::rvalue, U&&, U&>>(
        *static_cast<U*>(p)).get();
  }

  add_qualification_t<detail::value_storage<SIZE, ALIGN>, Q>* storage_;
};

namespace detail {

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
void destroy_small_value(void* erased) {
  using U = aid::extended<T>;
  static_cast<U*>(erased)->~U();
}

template <class T, class MA>
void destroy_large_value(void* erased) {
  detail::managed_storage<T, MA>* p =
      *static_cast<detail::managed_storage<T, MA>**>(erased);
  aid::extended<MA> ma = move(p->ma_);
  aid::destroy(move(ma).get(), p);
}

template <class T>
const type_info& get_type() { return typeid(T); }

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

}  // namespace detail

template <class M, size_t SIZE, size_t ALIGN>
class value_addresser {
 public:
  value_addresser(value_addresser&& rhs) noexcept {
    meta_ = rhs.meta_;
    storage_ = rhs.storage_;
    rhs.meta_ = nullptr;
  }

  explicit value_addresser(delegated_tag_t) noexcept : meta_(nullptr) {}

  template <class E_T>
  explicit value_addresser(delegated_tag_t, E_T&& value)
      : value_addresser(delegated_tag) {
    if constexpr (detail::VALUE_USES_SBO<aid::extending_t<E_T>, SIZE, ALIGN>) {
      init_small(forward<E_T>(value));
    } else {
      init_large(forward<E_T>(value), memory_allocator{});
    }
  }

  template <class E_T, class E_MA>
  explicit value_addresser(delegated_tag_t, E_T&& value, E_MA&& ma)
      : value_addresser(delegated_tag) {
    if constexpr (detail::VALUE_USES_SBO<aid::extending_t<E_T>, SIZE, ALIGN>) {
      init_small(forward<E_T>(value));
    } else {
      init_large(forward<E_T>(value), forward<E_MA>(ma));
    }
  }

  ~value_addresser() {
    if (meta_ != nullptr) {
      meta_->destroy_(&storage_);
    }
  }

  value_addresser& operator=(value_addresser&& rhs) noexcept
      { swap(rhs); return *this; }

  const M& meta() const { return meta_->core_; }
  decltype(auto) data() & { return storage_; }
  decltype(auto) data() && { return move(storage_); }
  decltype(auto) data() const& { return storage_; }
  decltype(auto) data() const&& { return move(storage_); }

  bool has_value() const noexcept { return meta_ != nullptr; }
  const type_info& type() const noexcept { return meta_ == nullptr ?
      typeid(void) : meta_->type_(); }

  void reset() noexcept { value_addresser(delegated_tag).swap(*this); }

  template <class T>
  void assign(T&& val) {
    value_addresser(delegated_tag, forward<T>(val)).swap(*this);
  }

  template <class T, class MA>
  void assign(T&& val, MA&& ma) {
    value_addresser(delegated_tag, forward<T>(val), forward<MA>(ma))
        .swap(*this);
  }

  void swap(value_addresser& rhs) noexcept
      { std::swap(meta_, rhs.meta_); std::swap(storage_, rhs.storage_); }

 private:
  template <class E_T>
  void init_small(E_T&& value) {
    using T = aid::extending_t<E_T>;
    new(&storage_.value_)
        aid::extended<T>(aid::extending_arg(forward<E_T>(value)));
    meta_ = &detail::META_STORAGE<detail::value_meta<M>, T>;
  }

  template <class E_T, class E_MA>
  void init_large(E_T&& value, E_MA&& ma) {
    using T = aid::extending_t<E_T>;
    using MA = aid::extending_t<E_MA>;
    aid::extended<MA> extended_ma = aid::make_extended(forward<E_MA>(ma));
    storage_.ptr_ = aid::construct<detail::managed_storage<T, MA>>(
        extended_ma.get(), aid::extending_arg(forward<E_T>(value)),
        move(extended_ma));
    meta_ = &detail::META_STORAGE<detail::value_meta<M>, T, MA>;
  }

  const detail::value_meta<M>* meta_;
  detail::value_storage<SIZE, ALIGN> storage_;
};

template <class M, qualification_type Q>
class reference_addresser {
  template <class, qualification_type>
  friend class reference_addresser;

 public:
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

  const M& meta() const { return meta_; }
  auto data() const { return ptr_; }

  template <class T>
  void assign(T& value) noexcept {
    meta_ = typename detail::reference_meta<M>::type{in_place_type<decay_t<T>>};
    ptr_ = &value;
  }

 private:
  typename detail::reference_meta<M>::type meta_;
  add_qualification_t<void, Q>* ptr_;
};

}  // namespace std

#endif  // SRC_MAIN_P0957_ADDRESSER_H_
