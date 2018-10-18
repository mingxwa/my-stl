/**
 * Copyright (c) 2017-2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0957_ADDRESSER_H_
#define SRC_MAIN_P0957_ADDRESSER_H_

#include <typeinfo>
#include <utility>

#include "../p1144/trivially_relocatable.h"
#include "../p1172/memory_allocator.h"
#include "../common/more_utility.h"

namespace std {

template <class T, bool C, bool V> struct qualified { using type = T; };
template <class T>
struct qualified<T, true, false> { using type = add_const_t<T>; };
template <class T>
struct qualified<T, false, true> { using type = add_volatile_t<T>; };
template <class T>
struct qualified<T, true, true> { using type = add_cv_t<T>; };
template <class T, bool C, bool V>
using qualified_t = typename qualified<T, C, V>::type;

namespace wang {

template <class T, size_t SIZE, size_t ALIGN>
static inline constexpr bool VALUE_USES_SBO = sizeof(T) <= SIZE
    && alignof(T) <= ALIGN && is_trivially_relocatable_v<T>;

template <size_t SIZE, size_t ALIAS>
union value_storage {
  alignas(ALIAS) char value_[SIZE];
  void* ptr_;
};

}  // namespace wang

template <bool C, bool V>
class erased_reference {
  template <bool, bool>
  friend class erased_reference;

 public:
  erased_reference(const erased_reference&) = default;
  template <bool _C, bool _V>
  erased_reference(const erased_reference<_C, _V>& rhs) : ptr_(rhs.ptr_) {}
  explicit erased_reference(qualified_t<void, C, V>* ptr) : ptr_(ptr) {}

  template <class T>
  T& cast(in_place_type_t<T>) { return *static_cast<T*>(ptr_); }

 private:
  qualified_t<void, C, V>* ptr_;
};

template <size_t S, size_t A, bool C, bool V>
class erased_value {
  template <size_t, size_t, bool, bool>
  friend class erased_value;

 public:
  erased_value(const erased_value&) = default;
  template <bool _C, bool _V>
  erased_value(const erased_value<S, A, _C, _V>& rhs)
      : storage_(rhs.storage_) {}
  explicit erased_value(qualified_t<wang::value_storage<S, A>, C, V>& storage)
      : storage_(storage) {}

  template <class T>
  T& cast(in_place_type_t<T>) {
    qualified_t<void, C, V>* p;
    if constexpr (wang::VALUE_USES_SBO<T, S, A>) {
      p = storage_.value_;
    } else {
      p = storage_.ptr_;
    }
    return *static_cast<T*>(p);
  }

 private:
  qualified_t<wang::value_storage<S, A>, C, V>& storage_;
};

namespace wang {

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

    operator const M&() const { return *ptr_; }

    const M* ptr_;
  };
};

template <size_t S, size_t A>
struct value_meta_ext_t {
 public:
  template <class T>
  constexpr explicit value_meta_ext_t(in_place_type_t<T>)
      : destroy_(destroy_small<T>), copy_(copy_small<T>), type_(type<T>) {}

  template <class T, class MA>
  constexpr explicit value_meta_ext_t(in_place_type_t<T>, in_place_type_t<MA>)
      : destroy_(destroy_large<T, MA>), copy_(copy_large<T, MA>),
        type_(type<T>) {}

  void (*destroy_)(value_storage<S, A>*);
  void (*copy_)(value_storage<S, A>*, value_storage<S, A>*);
  const type_info&(*type_)();

 private:
  template <class T>
  static void destroy_small(value_storage<S, A>* erased) {
    reinterpret_cast<T*>(erased->value_)->~T();
  }

  template <class T, class MA>
  static void destroy_large(value_storage<S, A>* erased) {
    static_cast<managed_storage<T, MA>*>(erased->ptr_)->destroy();
  }

  template <class T>
  static void copy_small(value_storage<S, A>* src, value_storage<S, A>* dest) {
    new (reinterpret_cast<T*>(dest->value_))
        T(*reinterpret_cast<const T*>(src->value_));
  }

  template <class T, class MA>
  static void copy_large(value_storage<S, A>* src, value_storage<S, A>* dest) {
    dest->ptr_ = static_cast<const managed_storage<T, MA>*>(src->ptr_)->clone();
  }

  template <class T>
  static const type_info& type() { return typeid(T); }
};

template <class M, size_t S, size_t A>
struct value_meta_t {
  template <class T, class... O>
  constexpr explicit value_meta_t(in_place_type_t<T>, in_place_type_t<O>...)
      : core_(in_place_type<T>), ext_(in_place_type<T>, in_place_type<O>...) {}

  M core_;
  value_meta_ext_t<S, A> ext_;
};

}  // namespace wang

template <class M, size_t S, size_t A, bool C, bool V>
class value_addresser {
  template <class, size_t, size_t, bool, bool>
  friend class value_addresser;

 public:
  void reset() noexcept { value_addresser().swap(*this); }

  template <class T>
  void reset(T&& val) {
    value_addresser(in_place_type<decay_t<T>>, forward<T>(val)).swap(*this);
  }

  template <class T, class U, class... Args>
  qualified_t<T, C, V>& emplace(initializer_list<U> il, Args&&... args) {
    return emplace<T>(il, forward<Args>(args)...);
  }

  template <class T, class... Args>
  qualified_t<T, C, V>& emplace(Args&&... args) {
    reset();
    value_addresser(in_place_type<T>, forward<Args>(args)...).swap(*this);
    return data().cast(in_place_type<qualified_t<T, C, V>>);
  }

  const M& meta() const { return meta_->core_; }
  auto data() const { return erased_value<S, A, C, V>{storage_}; }

  bool has_value() const noexcept { return meta_ != nullptr; }
  const type_info& type() const noexcept { return meta_ == nullptr ?
      typeid(void) : meta_->ext_.type_(); }

  void swap(value_addresser& rhs) noexcept
      { std::swap(meta_, rhs.meta_); std::swap(storage_, rhs.storage_); }

 protected:
  constexpr value_addresser() noexcept : meta_(nullptr) {}

  value_addresser(value_addresser&& rhs) noexcept { move_init(rhs); }

  template <bool _C, bool _V>
  value_addresser(value_addresser<M, S, A, _C, _V>&& rhs) noexcept
      { move_init(rhs); }

  value_addresser(const value_addresser& rhs) { copy_init(rhs); }

  template <bool _C, bool _V>
  value_addresser(const value_addresser<M, S, A, _C, _V>& rhs)
      { copy_init(rhs); }

  template <class T, class... Args>
  explicit value_addresser(in_place_type_t<T>, Args&&... args)
      : value_addresser(in_place_type<T>, false_type{},
                        forward<Args>(args)...) {}

  template <class T, bool MEMORY_ALLOCATOR_ENABLED, class... Args,
            class = enable_if_t<is_same_v<T, decay_t<T>>>>
  explicit value_addresser(in_place_type_t<T>,
      bool_constant<MEMORY_ALLOCATOR_ENABLED>, Args&&... args) {
    if constexpr (!is_nothrow_constructible_v<T>) {
      meta_ = nullptr;  // For exception safety
    }
    if constexpr (wang::VALUE_USES_SBO<T, S, A>) {
      if constexpr (MEMORY_ALLOCATOR_ENABLED) {
        init_small_ignore_memory_allocator<T>(forward<Args>(args)...);
      } else {
        init_small<T>(forward<Args>(args)...);
      }
    } else {
      if constexpr (MEMORY_ALLOCATOR_ENABLED) {
        init_large<T>(forward<Args>(args)...);
      } else {
        init_large<T>(memory_allocator{}, forward<Args>(args)...);
      }
    }
  }

  ~value_addresser() {
    if (meta_ != nullptr) {
      meta_->ext_.destroy_(&storage_);
    }
  }

  value_addresser& operator=(value_addresser&& rhs) noexcept
      { swap(rhs); return *this; }

  template <bool _C, bool _V>
  value_addresser& operator=(value_addresser<M, S, A, _C, _V>&& rhs) {
    value_addresser(forward<value_addresser<M, S, A, _C, _V>>(rhs)).swap(*this);
    return *this;
  }

  value_addresser& operator=(const value_addresser& rhs) {
    value_addresser(rhs).swap(*this);
    return *this;
  }

  template <bool _C, bool _V>
  value_addresser& operator=(const value_addresser<M, S, A, _C, _V>& rhs) {
    value_addresser(rhs).swap(*this);
    return *this;
  }

 private:
  template <class T, class... Args>
  void init_small(Args&&... args) {
    new (reinterpret_cast<T*>(storage_.value_)) T(forward<Args>(args)...);
    meta_ = &wang::META_STORAGE<wang::value_meta_t<M, S, A>, T>;
  }

  template <class T, class MA, class... Args>
  void init_large(MA&& ma, Args&&... args) {
    storage_.ptr_ = wang::make_managed_storage<T>(
        forward<MA>(ma), std::forward<Args>(args)...);
    meta_ = &wang::META_STORAGE<wang::value_meta_t<M, S, A>, T, decay_t<MA>>;
  }

  template <class T, class MA, class... Args>
  void init_small_ignore_memory_allocator(MA&&, Args&&... args) {
    init_small<T>(forward<Args>(args)...);
  }

  template <bool _C, bool _V, class = enable_if_t<!_V>>
  void copy_init(const value_addresser<M, S, A, _C, _V>& rhs) {
    if (rhs.meta_ != nullptr) {
      meta_ = nullptr;  // For exception safety
      rhs.meta_->ext_.copy_(&rhs.storage_, &storage_);
      meta_ = rhs.meta_;
    }
  }

  template <bool _C, bool _V, class = enable_if_t<(C || !_C) && (V || !_V)>>
  void move_init(value_addresser<M, S, A, _C, _V>& rhs) {
    meta_ = rhs.meta_;
    storage_ = rhs.storage_;
    rhs.meta_ = nullptr;
  }

  const wang::value_meta_t<M, S, A>* meta_;
  mutable wang::value_storage<S, A> storage_;
};

template <class M, bool C, bool V>
class reference_addresser {
  template <class _M, bool _C, bool _V>
  friend class reference_addresser;

 public:
  const M& meta() const { return meta_; }
  auto data() const { return erased_reference<C, V>{ptr_}; }

 protected:
  reference_addresser(const reference_addresser&) noexcept = default;

  template <class _M, bool _C, bool _V>
  reference_addresser(const reference_addresser<_M, _C, _V>& rhs) noexcept
      : meta_(rhs.meta_), ptr_(rhs.ptr_) {}

  template <class T, class U, class = enable_if_t<is_same_v<T, decay_t<T>>>>
  explicit reference_addresser(in_place_type_t<T>, U& value) noexcept
      : meta_(in_place_type<T>), ptr_(&value) {}

  reference_addresser& operator=(const reference_addresser& rhs)
      noexcept = default;

  template <class _M, bool _C, bool _V>
  reference_addresser& operator=(const reference_addresser<_M, _C, _V>& rhs)
      noexcept { meta_ = rhs.meta_; ptr_ = rhs.ptr_; }

 private:
  typename wang::reference_meta<M>::type meta_;
  qualified_t<void, C, V>* ptr_;
};

}  // namespace std

#endif  // SRC_MAIN_P0957_ADDRESSER_H_
