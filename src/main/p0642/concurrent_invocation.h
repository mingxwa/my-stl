/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
#define SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_

#include <utility>
#include <tuple>
#include <memory>
#include <atomic>
#include <thread>
#include <future>

#include "../p1172/memory_allocator.h"
#include "../common/more_utility.h"

namespace std {

class bad_concurrent_invocation_context_access : public exception {
 public:
  const char* what() const noexcept override { return "Invalid context"; }
};

template <class CTX, class E_CB, class MA>
class concurrent_token;

template <class CTX, class E_CB, class MA>
class concurrent_finalizer;

namespace bp_detail {

template <class CTX, class E_CB, class MA>
class concurrent_breakpoint;

template <class CTX, class E_CB, class MA, class CIU>
size_t ciu_size(CIU&&);

template <class CTX, class E_CB, class MA, class CIU>
void concurrent_call(const concurrent_breakpoint<CTX, E_CB, MA>*, CIU&&,
    size_t*);

template <class CTX, class E_CB, class MA>
auto make_token(const concurrent_breakpoint<CTX, E_CB, MA>* bp)
    { return concurrent_token<CTX, E_CB, MA>{bp}; }

template <class CTX, class E_CB, class MA, class CIU,
    class = enable_if_t<is_invocable_v<CIU, concurrent_token<CTX, E_CB, MA>>>>
struct concurrent_callable_traits {
  static inline constexpr size_t size(CIU&&) { return 1u; }
  static inline void call(const concurrent_breakpoint<CTX, E_CB, MA>* bp,
      CIU&& ciu, size_t* remain) {
    invoke(forward<CIU>(ciu), make_token(bp));
    --*remain;
  }
};

template <class CTX, class E_CB, class MA>
struct ciu_size_statistics {
  template <class CIU>
  void operator()(CIU&& ciu)
      { value += ciu_size<CTX, E_CB, MA>(forward<CIU>(ciu)); }

  size_t value = 0u;
};

template <class CTX, class E_CB, class MA>
struct ciu_caller {
  template <class CIU>
  void operator()(CIU&& ciu)
      { concurrent_call(bp_, forward<CIU>(ciu), remain_); }

  const concurrent_breakpoint<CTX, E_CB, MA>* bp_;
  size_t* remain_;
};

template <class CTX, class E_CB, class MA, class CIU,
    class = decltype(declval<CIU>().begin()),
    class = decltype(declval<CIU>().begin() != declval<CIU>().end())>
struct container_traits {
  static inline size_t size(CIU&& ciu) {
    return aid::for_each_in_container(forward<CIU>(ciu),
        ciu_size_statistics<CTX, E_CB, MA>{}).value;
  }

  static inline void call(const concurrent_breakpoint<CTX, E_CB, MA>* bp,
      CIU&& ciu, size_t* remain) {
    aid::for_each_in_container(forward<CIU>(ciu),
        ciu_caller<CTX, E_CB, MA>{bp, remain});
  }
};

template <class CTX, class E_CB, class MA, class CIU,
    class = enable_if_t<tuple_size_v<remove_reference_t<CIU>> == 2u>,
    class = decltype(get<0u>(declval<CIU>()) != get<1u>(declval<CIU>())),
    class = decltype(++get<0u>(declval<CIU>())),
    class = decltype(*get<0u>(declval<CIU>()))>
struct ciu_iterator_pair_traits {
  static inline size_t size(CIU&& ciu) {
    return for_each(get<0u>(forward<CIU>(ciu)), get<1u>(forward<CIU>(ciu)),
        ciu_size_statistics<CTX, E_CB, MA>{}).value;
  }

  static inline void call(const concurrent_breakpoint<CTX, E_CB, MA>* bp,
      CIU&& ciu, size_t* remain) {
    for_each(get<0u>(forward<CIU>(ciu)), get<1u>(forward<CIU>(ciu)),
        ciu_caller<CTX, E_CB, MA>{bp, remain});
  }
};

template <class CTX, class E_CB, class MA, class CIU>
struct ciu_tuple_traits {
  static inline size_t size(CIU&& ciu) {
    return aid::for_each_in_tuple(forward<CIU>(ciu),
        ciu_size_statistics<CTX, E_CB, MA>{}).value;
  }

  static inline void call(const concurrent_breakpoint<CTX, E_CB, MA>* bp,
      CIU&& ciu, size_t* remain) {
    aid::for_each_in_tuple(forward<CIU>(ciu),
        ciu_caller<CTX, E_CB, MA>{bp, remain});
  }
};

template <class CTX, class E_CB, class MA, class CIU,
    class = enable_if_t<aid::is_tuple_v<CIU>>>
struct tuple_traits : aid::applicable_template<
    aid::equal_templates<ciu_iterator_pair_traits>,
    aid::equal_templates<ciu_tuple_traits>
>::type<CTX, E_CB, MA, CIU> {};

template <class CTX, class E_CB, class MA, class CIU>
struct ciu_traits : aid::applicable_template<
    aid::equal_templates<
        concurrent_callable_traits,
        tuple_traits,
        container_traits
    >
>::type<CTX, E_CB, MA, CIU> {};

template <class CTX, class E_CB, class MA, class CIU>
size_t ciu_size(CIU&& ciu)
    { return ciu_traits<CTX, E_CB, MA, CIU>::size(forward<CIU>(ciu)); }

template <class CTX, class E_CB, class MA, class CIU>
void concurrent_call(const concurrent_breakpoint<CTX, E_CB, MA>* bp, CIU&& ciu,
    size_t* remain)
    { ciu_traits<CTX, E_CB, MA, CIU>::call(bp, forward<CIU>(ciu), remain); }

template <class CTX, class E_CB, class MA>
class concurrent_breakpoint {
 public:
  template <class CIU, class EP_CTX, class _E_CB>
  explicit concurrent_breakpoint(CIU&& ciu, EP_CTX&& ctx, _E_CB&& cb,
      aid::extended<MA> ma) : ctx_(forward<EP_CTX>(ctx)),
      cb_(forward<_E_CB>(cb)), ma_(move(ma)) {
    size_t count = ciu_size<CTX, E_CB, MA>(forward<CIU>(ciu));
    if (count == 0u) {
      join_last();
    } else {
      atomic_init(&count_, count);
      call(forward<CIU>(ciu), count);
    }
  }

  template <class CIU>
  void fork(CIU&& ciu) const {
    size_t count = ciu_size<CTX, E_CB, MA>(forward<CIU>(ciu));
    count_.fetch_add(count, memory_order_relaxed);
    call(forward<CIU>(ciu), count);
  }

  void join(size_t count) const {
    if (count_.fetch_sub(count, memory_order_release) == count) {
      atomic_thread_fence(memory_order_acquire);
      const_cast<concurrent_breakpoint*>(this)->join_last();
    }
  }

  void destroy() { aid::destroy(aid::extended<MA>{move(ma_)}.get(), this); }

  decltype(auto) context() const noexcept { return ctx_.get(); }
  decltype(auto) context() noexcept { return ctx_.get(); }

 private:
  template <class CIU>
  void call(CIU&& ciu, size_t remain) const {
    try {
      concurrent_call(this, forward<CIU>(ciu), &remain);
    } catch (...) {
      join(remain);
      throw;
    }
  }

  void join_last() {
    invoke(aid::make_extended(move(cb_)).get(),
        concurrent_finalizer<CTX, E_CB, MA>{this});
  }

  mutable atomic_size_t count_;
  aid::extended<CTX> ctx_;
  E_CB cb_;
  aid::extended<MA> ma_;
};

}  // namespace bp_detail

template <class CTX, class E_CB, class MA = memory_allocator>
class concurrent_token {
  friend auto bp_detail::make_token<>(
      const bp_detail::concurrent_breakpoint<CTX, E_CB, MA>*);

 public:
  concurrent_token() = default;
  concurrent_token(concurrent_token&&) = default;

  concurrent_token& operator=(concurrent_token&&) = default;

  template <class CIU>
  void fork(CIU&& ciu) const {
    if (!static_cast<bool>(bp_)) {
      throw bad_concurrent_invocation_context_access{};
    }
    bp_->fork(forward<CIU>(ciu));
  }

  decltype(auto) context() const {
    if (!static_cast<bool>(bp_)) {
      throw bad_concurrent_invocation_context_access{};
    }
    return bp_->context();
  }

 private:
  explicit concurrent_token(
      const bp_detail::concurrent_breakpoint<CTX, E_CB, MA>* bp) : bp_(bp) {}

  struct deleter {
    void operator()(const bp_detail::concurrent_breakpoint<CTX, E_CB, MA>* bp)
        { bp->join(1u); }
  };

  unique_ptr<const bp_detail::concurrent_breakpoint<CTX, E_CB, MA>, deleter>
      bp_;
};

template <class CTX, class E_CB, class MA = memory_allocator>
class concurrent_finalizer {
  friend class bp_detail::concurrent_breakpoint<CTX, E_CB, MA>;

 public:
  concurrent_finalizer() = default;
  concurrent_finalizer(concurrent_finalizer&&) = default;

  concurrent_finalizer& operator=(concurrent_finalizer&&) = default;

  decltype(auto) context() const {
    if (!static_cast<bool>(bp_)) {
      throw bad_concurrent_invocation_context_access{};
    }
    return bp_->context();
  }

 private:
  explicit concurrent_finalizer(
      bp_detail::concurrent_breakpoint<CTX, E_CB, MA>* bp) : bp_(bp) {}

  struct deleter {
    void operator()(bp_detail::concurrent_breakpoint<CTX, E_CB, MA>* bp)
        { bp->destroy(); }
  };

  unique_ptr<bp_detail::concurrent_breakpoint<CTX, E_CB, MA>, deleter> bp_;
};

namespace ci_detail {

template <class CT, class CTX, class E_CB, class MA, class = enable_if_t<
    is_invocable_v<CT, concurrent_finalizer<CTX, E_CB, MA>>>>
struct invoke_continuation_with_finalizer_processor {
  static inline void apply(
      CT&& ct, concurrent_finalizer<CTX, E_CB, MA>&& finalizer)
      { invoke(forward<CT>(ct), move(finalizer)); }
};

template <class CT, class CTX, class E_CB, class MA,
    class = enable_if_t<is_invocable_v<CT, CTX>>>
struct invoke_continuation_with_context_processor {
  static inline void apply(
      CT&& ct, concurrent_finalizer<CTX, E_CB, MA> finalizer)
      { invoke(forward<CT>(ct), forward<CTX>(finalizer.context())); }
};

template <class CT, class CTX, class E_CB, class MA,
    class = enable_if_t<is_invocable_v<CT>>>
struct invoke_continuation_without_finalizer_processor {
  static inline void apply(CT&& ct,
      concurrent_finalizer<CTX, E_CB, MA>&& finalizer)
      { { auto f = move(finalizer); } invoke(forward<CT>(ct)); }
};

template <class CT, class CTX, class E_CB, class MA>
void invoke_continuation(CT&& ct,
    concurrent_finalizer<CTX, E_CB, MA>&& finalizer) {
  aid::applicable_template<
      aid::equal_templates<invoke_continuation_with_finalizer_processor>,
      aid::equal_templates<invoke_continuation_with_context_processor>,
      aid::equal_templates<invoke_continuation_without_finalizer_processor>
  >::type<CT, CTX, E_CB, MA>::apply(forward<CT>(ct), move(finalizer));
}

template <class F, class CTX, class E_CB, class MA,
    class = enable_if_t<is_invocable_v<F, concurrent_token<CTX, E_CB, MA>>>>
struct invoke_callable_with_token_processor {
  static inline void apply(F&& f, concurrent_token<CTX, E_CB, MA>&& token)
      { invoke(forward<F>(f), move(token)); }
};

template <class F, class CTX, class E_CB, class MA,
    class = enable_if_t<is_invocable_v<F, add_lvalue_reference_t<const CTX>>>>
struct invoke_callable_with_context_processor {
  static inline void apply(F&& f, concurrent_token<CTX, E_CB, MA> token)
      { invoke(forward<F>(f), token.context()); }
};

template <class F, class CTX, class E_CB, class MA,
    class = enable_if_t<is_invocable_v<F>>>
struct invoke_callable_without_token_processor {
  static inline void apply(F&& f, concurrent_token<CTX, E_CB, MA>)
      { invoke(forward<F>(f)); }
};

template <class F, class CTX, class E_CB, class MA>
void invoke_callable(F&& f, concurrent_token<CTX, E_CB, MA>&& token) {
  aid::applicable_template<
      aid::equal_templates<invoke_callable_with_token_processor>,
      aid::equal_templates<invoke_callable_with_context_processor>,
      aid::equal_templates<invoke_callable_without_token_processor>
  >::type<F, CTX, E_CB, MA>::apply(forward<F>(f), move(token));
}

}  // namespace ci_detail

template <class E_F, class CTX, class E_CB, class MA>
class contextual_concurrent_callable {
 public:
  explicit contextual_concurrent_callable(E_F&& f,
      concurrent_token<CTX, E_CB, MA>&& token)
      : f_(move(f)), token_(move(token)) {}

  contextual_concurrent_callable(contextual_concurrent_callable&&) = default;

  void operator()() && {
    ci_detail::invoke_callable(
        aid::make_extended_view(move(f_)).get(), move(token_));
  }

 private:
  E_F f_;
  concurrent_token<CTX, E_CB, MA> token_;
};

template <class E_E, class E_F>
class concurrent_callable {
 public:
  template <class _E_E, class _E_CC>
  explicit concurrent_callable(_E_E&& e, _E_CC&& f)
      : e_(forward<_E_E>(e)), f_(forward<_E_CC>(f)) {}

  concurrent_callable(const concurrent_callable&) = default;
  concurrent_callable(concurrent_callable&&) = default;

  template <class CTX, class E_CB, class MA>
  void operator()(concurrent_token<CTX, E_CB, MA>&& token) && {
    aid::make_extended_view(move(e_)).get()
        .execute(contextual_concurrent_callable<E_F, CTX, E_CB, MA>{
            move(f_), move(token)});
  }

 private:
  E_E e_;
  E_F f_;
};

template <class E_CT, class CTX, class E_CB, class MA>
class contextual_concurrent_callback {
 public:
  explicit contextual_concurrent_callback(E_CT&& ct,
      concurrent_finalizer<CTX, E_CB, MA>&& finalizer)
      : ct_(move(ct)), finalizer_(move(finalizer)) {}

  contextual_concurrent_callback(contextual_concurrent_callback&&)
      = default;

  void operator()() && {
    ci_detail::invoke_continuation(aid::make_extended_view(move(ct_)).get(),
        move(finalizer_));
  }

 private:
  E_CT ct_;
  concurrent_finalizer<CTX, E_CB, MA> finalizer_;
};

template <class E_E, class E_CT>
class concurrent_callback {
 public:
  template <class _E_E, class _E_CT>
  explicit concurrent_callback(_E_E&& e, _E_CT&& ct)
      : e_(forward<_E_E>(e)), ct_(forward<_E_CT>(ct)) {}

  concurrent_callback(const concurrent_callback&) = default;
  concurrent_callback(concurrent_callback&&) = default;

  template <class CTX, class E_CB, class MA>
  void operator()(concurrent_finalizer<CTX, E_CB, MA>&& finalizer) && {
    aid::make_extended_view(move(e_)).get()
        .execute(contextual_concurrent_callback<E_CT, CTX, E_CB, MA>{
            move(ct_), move(finalizer)});
  }

 private:
  E_E e_;
  E_CT ct_;
};

template <class E_E, class E_F>
auto make_concurrent_callable(E_E&& e, E_F&& f) {
  return concurrent_callable<decay_t<E_E>, decay_t<E_F>>(
      forward<E_E>(e), forward<E_F>(f));
}

template <class E_E, class E_CT>
auto make_concurrent_callback(E_E&& e, E_CT&& ct) {
  return concurrent_callback<decay_t<E_E>, decay_t<E_CT>>(
      forward<E_E>(e), forward<E_CT>(ct));
}

template <class CTX>
class promise_continuation {
 public:
  explicit promise_continuation(promise<CTX>&& p) noexcept : p_(move(p)) {}

  void operator()(CTX&& ctx) && { p_.set_value(move(ctx)); }

 private:
  promise<CTX> p_;
};

template <>
class promise_continuation<void> {
 public:
  explicit promise_continuation(promise<void>&& p) noexcept : p_(move(p)) {}

  void operator()() && { p_.set_value(); }

 private:
  promise<void> p_;
};

template <class CIU, class E_CTX, class E_CB, class E_MA = memory_allocator>
void concurrent_invoke(CIU&& ciu, E_CTX&& ctx, E_CB&& cb, E_MA&& ma = E_MA{}) {
  using BP = bp_detail::concurrent_breakpoint<
      aid::extending_t<E_CTX>, decay_t<E_CB>, aid::extending_t<E_MA>>;
  auto extended_ma = aid::make_extended(ma);
  aid::construct<BP>(extended_ma.get(), forward<CIU>(ciu),
      aid::extending_arg(forward<E_CTX>(ctx)), forward<E_CB>(cb),
      move(extended_ma));
}

struct in_place_executor {
  template <class F>
  void execute(F&& f) const { invoke(forward<F>(f)); }
};

template <class CIU, class E_CTX = in_place_type_t<void>>
decltype(auto) concurrent_invoke(CIU&& ciu, E_CTX&& ctx = E_CTX{}) {
  using R = conditional_t<
      is_move_constructible_v<aid::extending_t<E_CTX>>,
      aid::extending_t<E_CTX>, void>;
  promise<R> p;
  future<R> result = p.get_future();
  concurrent_invoke(forward<CIU>(ciu), forward<E_CTX>(ctx),
      make_concurrent_callback(in_place_executor{},
          promise_continuation<R>{move(p)}));
  return result;
}

struct daemon_thread_executor {
  template <class E_F>
  void execute(E_F&& f) const {
    thread([f = forward<E_F>(f)]() mutable
        { invoke(aid::make_extended_view(move(f)).get()); }).detach();
  }
};

class crucial_thread_executor {
 public:
  template <class E_F>
  void execute(E_F&& f) const {
    get_store().token_.fork(
        make_concurrent_callable(daemon_thread_executor{}, forward<E_F>(f)));
  }

 private:
  struct store {
    store() noexcept
        { f_ = concurrent_invoke([=](auto&& token) { token_ = move(token); }); }
    ~store() { { auto token = move(token_); } f_.get(); }

    future<void> f_;
    concurrent_token<void, concurrent_callback<
        in_place_executor, promise_continuation<void>>> token_;
  };

  static inline const store& get_store() {
    static const store s;
    return s;
  }
};

}  // namespace std

#endif  // SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
