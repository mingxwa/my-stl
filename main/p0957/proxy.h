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
#include <memory>

#include "../p1144/trivially_relocatable.h"
#include "../p1648/sinking.h"

namespace std::p0957 {

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

class null_value_addresser_error : public logic_error {
 public:
  explicit null_value_addresser_error()
      : logic_error("The value addresser is not representing a value") {}
};

template <class T, class A>
struct allocated_value {
  using Alloc = typename allocator_traits<A>
      ::template rebind_alloc<allocated_value>;

  template <class _T>
  explicit allocated_value(_T&& value, const A& alloc)
      : value_(p1648::sink(forward<_T>(value))), alloc_(alloc) {}

  T value_;
  Alloc alloc_;
};

namespace erased_detail {

template <class T, size_t SIZE, size_t ALIGN>
inline constexpr bool VALUE_USES_SBO = sizeof(T) <= SIZE
    && alignof(T) <= ALIGN && p1144::is_trivially_relocatable_v<T>;

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
void destroy_small_value(void* erased) { static_cast<T*>(erased)->~T(); }

template <class T, class A>
void destroy_large_value(void* erased) {
  using V = allocated_value<T, A>;
  using Alloc = typename V::Alloc;
  V* p = *static_cast<V**>(erased);
  Alloc alloc = move(p->alloc_);
  p->~V();

  // Requires: V* shall be convertible to Alloc::typename pointer
  allocator_traits<Alloc>::deallocate(alloc, p, 1u);
}

template <class T> const type_info& get_type() { return typeid(T); }

template <class M>
struct value_meta {
  template <class T>
  constexpr explicit value_meta(in_place_type_t<T>)
      : core_(in_place_type<T>), destroy_(destroy_small_value<T>),
        type_(get_type<T>) {}

  template <class T, class A>
  constexpr explicit value_meta(in_place_type_t<T>, in_place_type_t<A>)
      : core_(in_place_type<T>), destroy_(destroy_large_value<T, A>),
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

  void reset() noexcept { value_addresser().swap(*this); }
  template <class T, class A>
  void assign(T&& val, A&& alloc)
      { value_addresser(forward<T>(val), forward<A>(alloc)).swap(*this); }
  void swap(value_addresser& rhs) noexcept
      { std::swap(meta_, rhs.meta_); std::swap(storage_, rhs.storage_); }

 protected:
  value_addresser() noexcept : meta_(nullptr) {}
  value_addresser(value_addresser&& rhs) noexcept {
    meta_ = rhs.meta_;
    storage_ = rhs.storage_;
    rhs.meta_ = nullptr;
  }

  template <class S_T>
  explicit value_addresser(S_T&& value)
      : value_addresser(forward<S_T>(value), allocator<char>{}) {}

  template <class S_T, class A>
  explicit value_addresser(S_T&& value, const A& alloc) : value_addresser() {
    using T = p1648::sunk_t<S_T>;
    if constexpr (erased_detail::VALUE_USES_SBO<T, SIZE, ALIGN>) {
      new(storage_.value_) T(p1648::sink(forward<S_T>(value)));
      meta_ = &meta_detail::META_STORAGE<meta_detail::value_meta<M>, T>;
    } else {
      using V = allocated_value<T, A>;
      using Alloc = typename V::Alloc;
      Alloc real_alloc(alloc);
      V* ptr = allocator_traits<Alloc>::allocate(real_alloc, 1u);
      try {
        new(ptr) V(forward<S_T>(value), move(real_alloc));
      } catch (...) {
        allocator_traits<Alloc>::deallocate(real_alloc, ptr, 1u);
        throw;
      }
      storage_.ptr_ = ptr;
      meta_ = &meta_detail::META_STORAGE<meta_detail::value_meta<M>, T, A>;
    }
  }

  value_addresser& operator=(value_addresser&& rhs) noexcept
      { swap(rhs); return *this; }
  template <class S_T>
  value_addresser& operator=(S_T&& value)
      { return *this = value_addresser{value}; }

  ~value_addresser() { if (meta_ != nullptr) { meta_->destroy_(&storage_); } }

  const M& meta() const {
    if (meta_ == nullptr) { throw null_value_addresser_error{}; }
    return meta_->core_;
  }
  storage_t& erased() & { return storage_; }
  storage_t&& erased() && { return move(storage_); }
  const storage_t& erased() const& { return storage_; }
  const storage_t&& erased() const&& { return move(storage_); }

 private:
  const meta_detail::value_meta<M>* meta_;
  storage_t storage_;
};

template <class M, qualification_type Q>
class reference_addresser {
 protected:
  reference_addresser(const reference_addresser&) noexcept = default;
  template <class T>
  explicit reference_addresser(T& value) noexcept
      : meta_(in_place_type<decay_t<T>>), ptr_(&value) {}

  reference_addresser& operator=(const reference_addresser& rhs)
      noexcept = default;
  template <class T>
  reference_addresser& operator=(T& value) noexcept
      { return *this = reference_addresser{value}; }

  const M& meta() const noexcept { return meta_; }
  auto erased() const noexcept { return ptr_; }

 private:
  typename meta_detail::reference_meta<M>::type meta_;
  add_qualification_t<void, Q>* ptr_;
};

template <class F, template <qualification_type, reference_type> class E>
    struct proxy_meta;  // Mock implementation

template <class F, class A> class proxy;  // Mock implementation

template <class F, size_t SIZE = sizeof(void*), size_t ALIGN = alignof(void*)>
using value_proxy = proxy<F, value_addresser<proxy_meta<
    F, erased_value_selector<SIZE, ALIGN>::template type>, SIZE, ALIGN>>;

template <class F>
using reference_proxy = proxy<decay_t<F>, reference_addresser<
    proxy_meta<decay_t<F>, erased_reference>, qualification_of_v<F>>>;

}  // namespace std::p0957

#endif  // SRC_MAIN_P0957_PROXY_H_
