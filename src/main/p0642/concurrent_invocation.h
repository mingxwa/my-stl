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

#include "../p1648/sinking.h"
#include "../common/more_utility.h"
#include "../common/more_concurrency.h"

namespace std::p0642 {

template <class CTX, class CB> class concurrent_token;

namespace detail {

template <class S_CTX>
decltype(auto) forward_context(S_CTX&& ctx) {
  if constexpr (is_void_v<p1648::sunk_t<S_CTX>>) {
    return tuple<>{};
  } else {
    return forward<S_CTX>(ctx);
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

template <class SFINAE, class CSA, class CTX, class CB>
struct sfinae_is_concurrent_session : false_type {};
template <class CSA, class CTX, class CB>
struct sfinae_is_concurrent_session<void_t<decltype(declval<CSA>().start(
    declval<concurrent_token<CTX, CB>>()))>, CSA, CTX, CB> : true_type {};

template <class CSA, class CTX, class CB>
inline bool constexpr is_concurrent_session
    = sfinae_is_concurrent_session<void, CSA, CTX, CB>::value;

}  // namespace detail

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

template <class CTX, class CB>
class concurrent_breakpoint {
 public:
  template <class S_CTX, class _CB>
  explicit concurrent_breakpoint(S_CTX&& ctx, _CB&& cb)
      : ctx_(p1648::sink(forward<S_CTX>(ctx))),
        cb_(forward<_CB>(cb)) {}

  template <class CSA>
  void invoke(CSA&& csa) {
    size_t n = count(forward<CSA>(csa));
    if (n == 0u) {
      std::invoke(move(cb_), this);
    } else {
      atomic_init(&state_, n);
      start(forward<CSA>(csa), n);
    }
  }

  template <class CSA>
  void fork(CSA&& csa) {
    size_t n = count(forward<CSA>(csa));
    state_.fetch_add(n, memory_order_relaxed);
    start(forward<CSA>(csa), n);
  }

  void join(size_t n) {
    if (state_.fetch_sub(n, memory_order_release) == n) {
      atomic_thread_fence(memory_order_acquire);
      std::invoke(move(cb_), this);
    }
  }

  aid::concurrent_collector<exception_ptr>& exceptions() { return exceptions_; }
  add_lvalue_reference_t<CTX> context()
      { if constexpr (!is_void_v<CTX>) { return ctx_; } }

 private:
  struct csa_count_statistics {
    template <class CSA, class = enable_if_t<
        detail::is_concurrent_session<CSA, CTX, CB>>>
    void operator()(CSA&&) { ++value; }
    size_t value = 0u;
  };

  struct csa_starter {
    template <class CSA, class = enable_if_t<
        detail::is_concurrent_session<CSA, CTX, CB>>>
    void operator()(CSA&& csa) {
      --*remain;
      forward<CSA>(csa).start(concurrent_token<CTX, CB>{bp_});
    }
    concurrent_breakpoint* bp_;
    size_t* remain;
  };

  template <class CSA>
  static size_t count(CSA&& csa) {
    csa_count_statistics statistics;
    aid::for_each_in_aggregation(forward<CSA>(csa), statistics);
    return statistics.value;
  }

  template <class CSA>
  void start(CSA&& csa, size_t n) {
    try {
      aid::for_each_in_aggregation(forward<CSA>(csa), csa_starter{this, &n});
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

template <class CTX, class CB>
class concurrent_token {
  friend class concurrent_breakpoint<CTX, CB>;
  using breakpoint = concurrent_breakpoint<CTX, CB>;

 public:
  concurrent_token() noexcept = default;
  concurrent_token(concurrent_token&&) noexcept = default;
  concurrent_token& operator=(concurrent_token&&) noexcept = default;

  bool is_valid() const noexcept { return static_cast<bool>(bp_); }
  void reset() noexcept { bp_.reset(); }
  breakpoint& get() const {
    if (!is_valid()) { throw invalid_concurrent_invocation_context{}; }
    return *bp_.get();
  }

  void set_exception(exception_ptr&& p) {
    if (!is_valid()) { throw invalid_concurrent_invocation_context{}; }
    auto bp = move(bp_);
    bp->exceptions().push(move(p));
  }

 private:
  explicit concurrent_token(breakpoint* bp) : bp_(bp) {}

  struct deleter { void operator()(breakpoint* bp) { bp->join(1u); } };
  unique_ptr<breakpoint, deleter> bp_;
};

class unexecuted_concurrent_callable : public logic_error {
 public:
  explicit unexecuted_concurrent_callable()
      : logic_error("Unexecuted concurrent callable") {}
};

template <class F, class CTX, class CB>
class concurrent_callable {
 public:
  explicit concurrent_callable(F&& f, concurrent_token<CTX, CB>&& token)
      : f_(move(f)), token_(move(token)) {}

  concurrent_callable(concurrent_callable&&) = default;
  concurrent_callable& operator=(concurrent_callable&&) = default;

  ~concurrent_callable() {
    if (token_.is_valid()) {
      token_.set_exception(
          make_exception_ptr(unexecuted_concurrent_callable{}));
    }
  }

  void operator()() noexcept {
    concurrent_token<CTX, CB> token = move(token_);
    try {
      if constexpr (is_invocable_v<F, concurrent_breakpoint<CTX, CB>&>) {
        invoke(move(f_), token.get());
      } else if constexpr (is_invocable_v<F, add_lvalue_reference_t<CTX>>) {
        invoke(move(f_), token.get().context());
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
class serial_concurrent_session {
 public:
  template <class _E, class _F>
  explicit serial_concurrent_session(_E&& e, _F&& f)
      : e_(forward<_E>(e)), f_(forward<_F>(f)) {}

  serial_concurrent_session(serial_concurrent_session&&) = default;
  serial_concurrent_session(const serial_concurrent_session&) = default;
  serial_concurrent_session& operator=(serial_concurrent_session&&) = default;
  serial_concurrent_session& operator=(const serial_concurrent_session&)
      = default;

  template <class CTX, class CB>
  void start(concurrent_token<CTX, CB>&& token) {
    move(e_).execute(concurrent_callable<F, CTX, CB>{move(f_), move(token)});
  }

 private:
  E e_; F f_;
};

template <class _E, class _F>
serial_concurrent_session(_E&&, _F&&)
    -> serial_concurrent_session<decay_t<_E>, decay_t<_F>>;

struct sync_concurrent_callback {
  template <class CTX>
  void operator()(concurrent_breakpoint<CTX, sync_concurrent_callback>*)
      { p_.set_value(); }

  promise<void> p_;
};

template <class CT>
class async_concurrent_callback {
 public:
  template <class _CT>
  explicit async_concurrent_callback(_CT&& ct) : ct_(forward<_CT>(ct)) {}

  template <class CTX>
  void operator()(concurrent_breakpoint<CTX, async_concurrent_callback>* bp) {
    auto exceptions = bp->exceptions().reduce();
    if (exceptions.empty()) {
      if constexpr (detail::is_context_reducible<CTX>) {
        if constexpr (is_void_v<decltype(declval<CTX>().reduce())>) {
          move(bp->context()).reduce();
          invoke(move(ct_));
        } else {
          invoke(move(ct_), move(bp->context()).reduce());
        }
      } else if constexpr (is_move_constructible_v<CTX>) {
        invoke(move(ct_), move(bp->context()));
      } else {
        invoke(move(ct_));
      }
    } else {
      if constexpr (detail::is_context_reducible<CTX>) {
        if constexpr (is_void_v<decltype(declval<CTX>().reduce())>) {
          move(bp->context()).reduce();
          move(ct_).error(move(exceptions));
        } else {
          move(ct_).error(move(exceptions), move(bp->context()).reduce());
        }
      } else if constexpr (is_move_constructible_v<CTX>) {
        move(ct_).error(move(exceptions), move(bp->context()));
      } else {
        move(ct_).error(move(exceptions));
      }
    }
    delete bp;
  }

 private:
  CT ct_;
};

template <class CSA, class S_CTX, class CT>
void concurrent_invoke(CSA&& csa, S_CTX&& ctx, CT&& ct) {
  using CB = async_concurrent_callback<decay_t<CT>>;
  (new concurrent_breakpoint<p1648::sunk_t<S_CTX>, CB>{
      detail::forward_context(forward<S_CTX>(ctx)), CB{forward<CT>(ct)}})
      ->invoke(forward<CSA>(csa));
}

template <class CSA, class S_CTX = in_place_type_t<void>>
auto concurrent_invoke(CSA&& csa, S_CTX&& ctx = S_CTX{}) {
  using CTX = p1648::sunk_t<S_CTX>;
  promise<void> p;
  future<void> f = p.get_future();
  concurrent_breakpoint<CTX, sync_concurrent_callback> bp{
      detail::forward_context(forward<S_CTX>(ctx)),
      sync_concurrent_callback{move(p)}};
  bp.invoke(forward<CSA>(csa));
  f.get();
  auto exceptions = bp.exceptions().reduce();
  if (exceptions.empty()) {
    if constexpr (detail::is_context_reducible<CTX>) {
      return move(bp.context()).reduce();
    } else if constexpr (is_move_constructible_v<CTX>) {
      return move(bp.context());
    }
  } else {
    if constexpr (detail::is_context_reducible<CTX>) {
      if constexpr (is_void_v<decltype(declval<CTX>().reduce())>) {
        move(bp.context()).reduce();
        throw concurrent_invocation_error<>{move(exceptions)};
      } else {
        throw concurrent_invocation_error<decltype(declval<CTX>().reduce())>{
            move(exceptions), move(bp.context()).reduce()};
      }
    } else if constexpr (is_move_constructible_v<CTX>) {
      throw concurrent_invocation_error<decay_t<CTX>>{
          move(exceptions), move(bp.context())};
    } else {
      throw concurrent_invocation_error<>{move(exceptions)};
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
