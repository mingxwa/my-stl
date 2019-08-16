/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
#define SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_

#include <utility>
#include <stdexcept>
#include <exception>
#include <vector>
#include <tuple>
#include <memory>
#include <atomic>
#include <future>

#include "../p1648/extended.h"
#include "../common/more_utility.h"
#include "../common/more_concurrency.h"

namespace std::p0642 {

class invalid_concurrent_invocation_context : public logic_error {
 public:
  explicit invalid_concurrent_invocation_context()
      : logic_error("Invalid concurrent invocation context") {}
};

template <class CTX = void>
class concurrent_invocation_error : public runtime_error {
 public:
  explicit concurrent_invocation_error(vector<exception_ptr> nested, CTX ctx)
      : runtime_error(
            "There are nested exceptions in current concurrent invocation"),
        nested_(move(nested)), ctx_(move(ctx)) {}

  const vector<exception_ptr>& get_nested() const noexcept { return nested_; }
  const CTX& context() const noexcept { return ctx_; }

 private:
  vector<exception_ptr> nested_;
  CTX ctx_;
};

template <>
class concurrent_invocation_error<void> : public runtime_error {
 public:
  explicit concurrent_invocation_error(vector<exception_ptr> nested)
      : runtime_error(
            "There are nested exceptions in current concurrent invocation"),
        nested_(move(nested)) {}

  const vector<exception_ptr>& get_nested() const noexcept { return nested_; }

 private:
  vector<exception_ptr> nested_;
};

template <class CTX, class CB> class concurrent_token;

namespace detail {

template <class CTX, class CB>
class breakpoint {
 public:
  template <class CIU, class E_CTX, class _CB>
  explicit breakpoint(CIU&& ciu, E_CTX&& ctx, _CB&& cb)
      : ctx_(p1648::make_extended(forward<E_CTX>(ctx))), cb_(forward<_CB>(cb)) {
    size_t n = count(forward<CIU>(ciu));
    if (n == 0u) {
      invoke(move(cb_), this);
    } else {
      atomic_init(&state_, n);
      call(forward<CIU>(ciu), n);
    }
  }

  template <class CIU>
  void fork(CIU&& ciu) {
    size_t n = count(forward<CIU>(ciu));
    state_.fetch_add(n, memory_order_relaxed);
    call(forward<CIU>(ciu), n);
  }

  void join(size_t n) {
    if (state_.fetch_sub(n, memory_order_release) == n) {
      atomic_thread_fence(memory_order_acquire);
      invoke(move(cb_), this);
    }
  }

  aid::concurrent_collector<exception_ptr>& exceptions() { return exceptions_; }
  add_lvalue_reference_t<CTX> context()
      { if constexpr (!is_void_v<CTX>) { return ctx_; } }

 private:
  struct ciu_size_statistics {
    template <class CIU, class = enable_if_t<
        is_invocable_v<CIU, concurrent_token<CTX, CB>>>>
    void operator()(CIU&&) { ++value; }
    size_t value = 0u;
  };

  struct ciu_caller {
    template <class CIU, class = enable_if_t<
        is_invocable_v<CIU, concurrent_token<CTX, CB>>>>
    void operator()(CIU&& ciu) {
      --*remain;
      invoke(forward<CIU>(ciu), concurrent_token<CTX, CB>{bp_});
    }
    breakpoint* bp_;
    size_t* remain;
  };

  template <class CIU>
  static size_t count(CIU&& ciu) {
    ciu_size_statistics statistics;
    aid::for_each_in_aggregation(forward<CIU>(ciu), statistics);
    return statistics.value;
  }

  template <class CIU>
  void call(CIU&& ciu, size_t n) {
    try {
      aid::for_each_in_aggregation(forward<CIU>(ciu), ciu_caller{this, &n});
    } catch (...) {
      if (n > 0) { join(n); }
      throw;
    }
  }

  atomic_size_t state_;
  conditional_t<is_void_v<CTX>, tuple<>, CTX> ctx_;
  aid::concurrent_collector<exception_ptr> exceptions_;
  CB cb_;
};

template <class E_CTX>
decltype(auto) forward_context(E_CTX&& ctx) {
  if constexpr (is_void_v<p1648::extending_t<E_CTX>>) {
    return tuple<>{};
  } else {
    return forward<E_CTX>(ctx);
  }
}

template <class SFINAE, class CTX>
struct sfinae_is_context_reducible : false_type {};
template <class CTX>
struct sfinae_is_context_reducible<
    void_t<decltype(declval<CTX>().reduce())>, CTX> : true_type {};

template <class CTX>
inline bool constexpr is_context_reducible
    = sfinae_is_context_reducible<void, CTX>::value;

}  // namespace detail

template <class CTX, class CB>
class concurrent_token {
  friend class detail::breakpoint<CTX, CB>;

 public:
  concurrent_token() noexcept = default;
  concurrent_token(concurrent_token&&) noexcept = default;
  concurrent_token& operator=(concurrent_token&&) noexcept = default;

  bool is_valid() const noexcept { return static_cast<bool>(bp_); }

  template <class CIU>
  void fork(CIU&& ciu) const
      { check_preconditions(); bp_->fork(forward<CIU>(ciu)); }

  add_lvalue_reference_t<CTX> context() const
      { check_preconditions(); return bp_->context(); }

  void set_exception(exception_ptr&& p) {
    check_preconditions();
    auto bp = move(bp_);
    bp->exceptions().push(move(p));
  }

 private:
  void check_preconditions() const
      { if (!is_valid()) { throw invalid_concurrent_invocation_context{}; } }

  explicit concurrent_token(detail::breakpoint<CTX, CB>* bp) : bp_(bp) {}

  struct deleter
      { void operator()(detail::breakpoint<CTX, CB>* bp) { bp->join(1u); } };

  unique_ptr<detail::breakpoint<CTX, CB>, deleter> bp_;
};

class unexecuted_concurrent_callable : public logic_error {
 public:
  explicit unexecuted_concurrent_callable()
      : logic_error("Unexecuted concurrent callable") {}
};

template <class F, class CTX, class CB>
class contextual_async_concurrent_callable {
 public:
  explicit contextual_async_concurrent_callable(F&& f,
      concurrent_token<CTX, CB>&& token) : f_(move(f)), token_(move(token)) {}

  contextual_async_concurrent_callable(contextual_async_concurrent_callable&&)
      = default;
  contextual_async_concurrent_callable& operator=(
      contextual_async_concurrent_callable&&) = default;

  ~contextual_async_concurrent_callable() {
    if (token_.is_valid()) {
      token_.set_exception(
          make_exception_ptr(unexecuted_concurrent_callable{}));
    }
  }

  void operator()() noexcept {
    concurrent_token<CTX, CB> token = move(token_);
    try {
      if constexpr (is_invocable_v<F, const concurrent_token<CTX, CB>&>) {
        invoke(move(f_), token);
      } else if constexpr (is_invocable_v<F, add_lvalue_reference_t<CTX>>) {
        invoke(move(f_), token.context());
      } else if constexpr (is_invocable_v<F>) {
        invoke(move(f_));
      } else {
        STATIC_ASSERT_FALSE(F);
      }
    } catch (...) {
      token.set_exception(current_exception());
    }
  }

 private:
  F f_; concurrent_token<CTX, CB> token_;
};

template <class E, class F>
class async_concurrent_callable {
 public:
  template <class _E, class _F>
  explicit async_concurrent_callable(_E&& e, _F&& f)
      : e_(forward<_E>(e)), f_(forward<_F>(f)) {}

  async_concurrent_callable(async_concurrent_callable&&) = default;
  async_concurrent_callable(const async_concurrent_callable&) = default;
  async_concurrent_callable& operator=(async_concurrent_callable&&) = default;
  async_concurrent_callable& operator=(const async_concurrent_callable&)
      = default;

  template <class CTX, class CB>
  void operator()(concurrent_token<CTX, CB>&& token) {
    move(e_).execute(contextual_async_concurrent_callable<F, CTX, CB>{
        move(f_), move(token)});
  }

 private:
  E e_; F f_;
};

template <class _E, class _F>
async_concurrent_callable(_E&&, _F&&)
    -> async_concurrent_callable<decay_t<_E>, decay_t<_F>>;

struct sync_concurrent_callback {
  template <class CTX>
  void operator()(detail::breakpoint<CTX, sync_concurrent_callback>*)
      { p_.set_value(); }

  promise<void> p_;
};

template <class CT>
class async_concurrent_callback {
 public:
  template <class _CT>
  explicit async_concurrent_callback(_CT&& ct) : ct_(forward<_CT>(ct)) {}

  template <class CTX>
  void operator()(detail::breakpoint<CTX, async_concurrent_callback>* bp) {
    auto& exceptions = bp->exceptions();
    if (exceptions.empty()) {
      if constexpr (detail::is_context_reducible<CTX>) {
        invoke(move(ct_), move(bp->context()).reduce());
      } else if constexpr (is_move_constructible_v<CTX>) {
        invoke(move(ct_), move(bp->context()));
      } else {
        invoke(move(ct_));
      }
    } else {
      if constexpr (detail::is_context_reducible<CTX>) {
        move(ct_).error(exceptions.reduce(), move(bp->context()).reduce());
      } else if constexpr (is_move_constructible_v<CTX>) {
        move(ct_).error(exceptions.reduce(), move(bp->context()));
      } else {
        move(ct_).error(exceptions.reduce());
      }
    }
    delete bp;
  }

 private:
  CT ct_;
};

template <class CIU, class E_CTX, class CT>
void concurrent_invoke(CIU&& ciu, E_CTX&& ctx, CT&& ct) {
  using CB = async_concurrent_callback<decay_t<CT>>;
  new detail::breakpoint<p1648::extending_t<E_CTX>, CB>{
      forward<CIU>(ciu), detail::forward_context(forward<E_CTX>(ctx)),
      CB{forward<CT>(ct)}};
}

template <class CIU, class E_CTX = in_place_type_t<void>>
auto concurrent_invoke(CIU&& ciu, E_CTX&& ctx = E_CTX{}) {
  using CTX = p1648::extending_t<E_CTX>;
  promise<void> p;
  future<void> f = p.get_future();
  detail::breakpoint<CTX, sync_concurrent_callback> bp{
      forward<CIU>(ciu), detail::forward_context(forward<E_CTX>(ctx)),
      sync_concurrent_callback{move(p)}};
  f.get();
  auto& exceptions = bp.exceptions();
  if (exceptions.empty()) {
    if constexpr (detail::is_context_reducible<CTX>) {
      return move(bp.context()).reduce();
    } else if constexpr (is_move_constructible_v<CTX>) {
      return move(bp.context());
    }
  } else {
    if constexpr (detail::is_context_reducible<CTX>) {
      throw concurrent_invocation_error<decay_t<
          decltype(move(bp.context()).reduce())>>{exceptions.reduce(),
              move(bp.context()).reduce()};
    } else if constexpr (is_move_constructible_v<CTX>) {
      throw concurrent_invocation_error<decay_t<CTX>>{
          exceptions.reduce(), move(bp.context())};
    } else {
      throw concurrent_invocation_error<>{exceptions.reduce()};
    }
  }
}

template <class E, class F, class EH>
class async_concurrent_continuation {
 public:
  template <class _E, class _F, class _EH = EH>
  explicit async_concurrent_continuation(_E&& e, _F&& f, _EH&& eh = EH{})
      : e_(forward<_E>(e)), f_(forward<_F>(f)), eh_(forward<_EH>(eh)) {}

  template <class CTX>
  void operator()(CTX&& ctx) { invoke(move(f_), forward<CTX>(ctx)); }
  void operator()() { invoke(move(f_)); }

  template <class CTX>
  void error(vector<exception_ptr>&& ex, CTX&& ctx)
      { invoke(move(eh_), move(ex), forward<CTX>(ctx)); }
  void error(vector<exception_ptr>&& ex) { invoke(move(eh_), move(ex)); }

 private:
  E e_; F f_; EH eh_;
};

struct throwing_concurrent_error_handler {
  template <class CTX>
  void operator()(vector<exception_ptr>&& ex, CTX&& ctx) const {
    throw concurrent_invocation_error<decay_t<CTX>>{
        move(ex), forward<CTX>(ctx)};
  }
  void operator()(vector<exception_ptr>&& ex) const
      { throw concurrent_invocation_error<>{move(ex)}; }
};

template <class _E, class _F, class _EH>
async_concurrent_continuation(_E&&, _F&&, _EH&&)
    -> async_concurrent_continuation<decay_t<_E>, decay_t<_F>, decay_t<_EH>>;
template <class _E, class _F>
async_concurrent_continuation(_E&&, _F&&)
    -> async_concurrent_continuation<decay_t<_E>, decay_t<_F>,
        throwing_concurrent_error_handler>;

}  // namespace std::p0642

#endif  // SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
