/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
#define SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_

#include <utility>
#include <stdexcept>
#include <tuple>
#include <memory>
#include <atomic>
#include <thread>
#include <future>

#include "../p1172/memory_allocator.h"
#include "../p1649/applicable_template.h"
#include "../p1648/extended.h"
#include "../common/more_utility.h"

namespace std {

class invalid_concurrent_breakpoint : public logic_error {
 public:
  explicit invalid_concurrent_breakpoint()
      : logic_error("Invalid concurrent breakpoint") {}
};

template <class CTX, class E_CB, class MA>
class concurrent_breakpoint;

template <class CTX, class E_CB, class MA = global_memory_allocator>
class concurrent_token;

template <class CTX, class E_CB, class MA = global_memory_allocator>
class concurrent_finalizer;

template <class CIU, class E_CTX, class E_CB,
    class E_MA = global_memory_allocator>
void concurrent_invoke(CIU&&, E_CTX&&, E_CB&&, E_MA&& = E_MA{});

namespace bp_detail {

template <class CTX, class E_CB, class MA>
auto make_token(const concurrent_breakpoint<CTX, E_CB, MA>* bp)
    { return concurrent_token<CTX, E_CB, MA>{bp}; }

template <class CTX, class E_CB, class MA>
struct ciu_size_statistics {
  template <class CIU, class = enable_if_t<
      is_invocable_v<CIU, concurrent_token<CTX, E_CB, MA>>>>
  void operator()(CIU&&) { ++value; }

  size_t value = 0u;
};

template <class CTX, class E_CB, class MA>
struct ciu_caller {
  template <class CIU, class = enable_if_t<
      is_invocable_v<CIU, concurrent_token<CTX, E_CB, MA>>>>
  void operator()(CIU&& ciu) {
    invoke(forward<CIU>(ciu), make_token(bp_));
    --*remain_;
  }

  const concurrent_breakpoint<CTX, E_CB, MA>* bp_;
  size_t* remain_;
};

template <class CTX, class E_CB, class MA, class CIU>
size_t ciu_size(CIU&& ciu) {
  ciu_size_statistics<CTX, E_CB, MA> statistics;
  aid::for_each_in_aggregation(forward<CIU>(ciu), statistics);
  return statistics.value;
}

}  // namespace bp_detail

template <class CTX, class E_CB, class MA>
class concurrent_breakpoint {
  friend class concurrent_token<CTX, E_CB, MA>;

  friend class concurrent_finalizer<CTX, E_CB, MA>;

  template <class CIU, class E_CTX, class _E_CB, class E_MA>
  friend void concurrent_invoke(CIU&&, E_CTX&&, _E_CB&&, E_MA&&);

 public:
  template <class EP_CTX, class _E_CB>
  explicit concurrent_breakpoint(EP_CTX&& ctx, _E_CB&& cb, extended<MA> ma)
      : ctx_(forward<EP_CTX>(ctx)), cb_(forward<_E_CB>(cb)), ma_(move(ma)) {}

 private:
  template <class CIU>
  void call(CIU&& ciu, size_t remain) const {
    try {
      aid::for_each_in_aggregation(forward<CIU>(ciu),
          bp_detail::ciu_caller<CTX, E_CB, MA>{this, &remain});
    } catch (...) {
      join(remain);
      throw;
    }
  }

  void join(size_t count) const {
    if (count_.fetch_sub(count, memory_order_release) == count) {
      atomic_thread_fence(memory_order_acquire);
      const_cast<concurrent_breakpoint*>(this)->join_last();
    }
  }

  void join_last() {
    invoke(make_extended(move(cb_)).get(),
        concurrent_finalizer<CTX, E_CB, MA>{this});
  }

  mutable atomic_size_t count_;
  extended<CTX> ctx_;
  E_CB cb_;
  extended<MA> ma_;
};

template <class CTX, class E_CB, class MA>
class concurrent_token {
  friend auto bp_detail::make_token<>(
      const concurrent_breakpoint<CTX, E_CB, MA>*);

 public:
  concurrent_token() = default;
  concurrent_token(concurrent_token&&) = default;

  concurrent_token& operator=(concurrent_token&&) = default;

  template <class CIU>
  void fork(CIU&& ciu) const {
    if (!static_cast<bool>(bp_)) { throw invalid_concurrent_breakpoint{}; }
    size_t count = bp_detail::ciu_size<CTX, E_CB, MA>(forward<CIU>(ciu));
    bp_->count_.fetch_add(count, memory_order_relaxed);
    bp_->call(forward<CIU>(ciu), count);
  }

  decltype(auto) context() const {
    if (!static_cast<bool>(bp_)) { throw invalid_concurrent_breakpoint{}; }
    return bp_->ctx_.get();
  }

 private:
  explicit concurrent_token(const concurrent_breakpoint<CTX, E_CB, MA>* bp)
      : bp_(bp) {}

  struct deleter {
    void operator()(const concurrent_breakpoint<CTX, E_CB, MA>* bp)
        { bp->join(1u); }
  };

  unique_ptr<const concurrent_breakpoint<CTX, E_CB, MA>, deleter> bp_;
};

template <class CTX, class E_CB, class MA>
class concurrent_finalizer {
  friend class concurrent_breakpoint<CTX, E_CB, MA>;

 public:
  concurrent_finalizer() = default;
  concurrent_finalizer(concurrent_finalizer&&) = default;

  concurrent_finalizer& operator=(concurrent_finalizer&&) = default;

  decltype(auto) context() const {
    if (!static_cast<bool>(bp_)) { throw invalid_concurrent_breakpoint{}; }
    return bp_->ctx_.get();
  }

 private:
  explicit concurrent_finalizer(concurrent_breakpoint<CTX, E_CB, MA>* bp)
      : bp_(bp) {}

  struct deleter {
    void operator()(concurrent_breakpoint<CTX, E_CB, MA>* bp)
        { aid::destroy(extended<MA>{move(bp->ma_)}.get(), bp); }
  };

  unique_ptr<concurrent_breakpoint<CTX, E_CB, MA>, deleter> bp_;
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
  static inline void apply(
      CT&& ct, concurrent_finalizer<CTX, E_CB, MA>&& finalizer)
      { { auto f = move(finalizer); } invoke(forward<CT>(ct)); }
};

template <class CT, class CTX, class E_CB, class MA>
void invoke_continuation(CT&& ct,
    concurrent_finalizer<CTX, E_CB, MA>&& finalizer) {
  applicable_template<
      equal_templates<invoke_continuation_with_finalizer_processor>,
      equal_templates<invoke_continuation_with_context_processor>,
      equal_templates<invoke_continuation_without_finalizer_processor>
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
  applicable_template<
      equal_templates<invoke_callable_with_token_processor>,
      equal_templates<invoke_callable_with_context_processor>,
      equal_templates<invoke_callable_without_token_processor>
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
        make_extended_view(move(f_)).get(), move(token_));
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
    make_extended_view(move(e_)).get().execute(contextual_concurrent_callable<
        E_F, CTX, E_CB, MA>{move(f_), move(token)});
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

  contextual_concurrent_callback(contextual_concurrent_callback&&) = default;

  void operator()() && {
    ci_detail::invoke_continuation(make_extended_view(move(ct_)).get(),
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
    make_extended_view(move(e_)).get().execute(contextual_concurrent_callback<
        E_CT, CTX, E_CB, MA>{move(ct_), move(finalizer)});
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

template <class CIU, class E_CTX, class E_CB, class E_MA>
void concurrent_invoke(CIU&& ciu, E_CTX&& ctx, E_CB&& cb, E_MA&& ma) {
  auto extended_ma = make_extended(ma);
  auto* bp = aid::construct<concurrent_breakpoint<
      extending_t<E_CTX>, decay_t<E_CB>, extending_t<E_MA>>>(extended_ma.get(),
      extending_arg(forward<E_CTX>(ctx)), forward<E_CB>(cb), move(extended_ma));
  size_t count = bp_detail::ciu_size<
      extending_t<E_CTX>, decay_t<E_CB>, extending_t<E_MA>>(forward<CIU>(ciu));
  if (count == 0u) {
    bp->join_last();
  } else {
    atomic_init(&bp->count_, count);
    bp->call(forward<CIU>(ciu), count);
  }
}

struct in_place_executor {
  template <class F>
  void execute(F&& f) const { invoke(make_extended_view(forward<F>(f)).get()); }
};

template <class CIU, class E_CTX = in_place_type_t<void>>
decltype(auto) concurrent_invoke(CIU&& ciu, E_CTX&& ctx = E_CTX{}) {
  using R = conditional_t<
      is_move_constructible_v<extending_t<E_CTX>>, extending_t<E_CTX>, void>;
  promise<R> p;
  future<R> result = p.get_future();
  concurrent_invoke(forward<CIU>(ciu), forward<E_CTX>(ctx),
      make_concurrent_callback(in_place_executor{},
          promise_continuation<R>{move(p)}));
  return result;
}

class thread_executor {
  using token_t = concurrent_token<void, concurrent_callback<
        in_place_executor, promise_continuation<void>>>;

 public:
  template <class E_F>
  void execute(E_F&& f) const {
    token_t token;
    get_token().fork([&](token_t&& t) { token = move(t); });
    thread([f = forward<E_F>(f), token = move(token)]() mutable
        { invoke(make_extended_view(move(f)).get()); }).detach();
  }

 private:
  static inline const token_t& get_token() {
    struct store {
      store() noexcept {
        f_ = concurrent_invoke([=](token_t&& token) { token_ = move(token); });
      }
      ~store() { { auto token = move(token_); } f_.get(); }

      future<void> f_;
      token_t token_;
    } static const s;
    return s.token_;
  }
};

}  // namespace std

#endif  // SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
