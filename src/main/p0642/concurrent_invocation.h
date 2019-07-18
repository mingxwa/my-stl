/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
#define SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_

#include <utility>
#include <stdexcept>
#include <exception>
#include <vector>
#include <deque>
#include <tuple>
#include <memory>
#include <atomic>
#include <thread>
#include <future>
#include <mutex>

#include "../p1649/applicable_template.h"
#include "../p1648/extended.h"
#include "../common/more_utility.h"
#include "../common/more_concurrency.h"

namespace std {

class invalid_concurrent_invocation_context : public logic_error {
 public:
  explicit invalid_concurrent_invocation_context()
      : logic_error("Invalid concurrent invocation context") {}
};

template <class CTX>
class concurrent_invocation_error : public runtime_error {
 public:
  explicit concurrent_invocation_error(CTX* ctx, vector<exception_ptr> nested)
      : runtime_error(
            "There are nested exceptions in current concurrent invocation"),
        ctx_(ctx), nested_(move(nested)) {}
  concurrent_invocation_error(const concurrent_invocation_error&) = default;
  concurrent_invocation_error& operator=(const concurrent_invocation_error&)
      = default;

  const vector<exception_ptr>& get_nested() const noexcept { return nested_; }

  CTX& context() const noexcept { return *ctx_; }

 private:
  CTX* ctx_;
  const vector<exception_ptr> nested_;
};

template <class CTX, class E_CB>
class concurrent_token;

template <class CTX, class E_CB>
class concurrent_finalizer;

template <class CIU, class E_CTX, class E_CB>
void concurrent_invoke(CIU&&, E_CTX&&, E_CB&&);

namespace bp_detail {

template <class CTX, class E_CB>
struct breakpoint;

template <class CTX, class E_CB>
auto make_token(const breakpoint<CTX, E_CB>* bp)
    { return concurrent_token<CTX, E_CB>{bp}; }

template <class CTX, class E_CB>
struct ciu_size_statistics {
  template <class CIU, class = enable_if_t<
      is_invocable_v<CIU, concurrent_token<CTX, E_CB>>>>
  void operator()(CIU&&) { ++value; }

  size_t value = 0u;
};

template <class CTX, class E_CB>
struct ciu_caller {
  template <class CIU, class = enable_if_t<
      is_invocable_v<CIU, concurrent_token<CTX, E_CB>>>>
  void operator()(CIU&& ciu) {
    invoke(forward<CIU>(ciu), make_token(bp_));
    --*remain_;
  }

  const breakpoint<CTX, E_CB>* bp_;
  size_t* remain_;
};

template <class CTX, class E_CB, class CIU>
size_t ciu_size(CIU&& ciu) {
  ciu_size_statistics<CTX, E_CB> statistics;
  aid::for_each_in_aggregation(forward<CIU>(ciu), statistics);
  return statistics.value;
}

template <class CTX, class E_CB>
struct breakpoint {
  template <class E_CTX, class _E_CB>
  explicit breakpoint(E_CTX&& ctx, _E_CB&& cb)
      : ctx_(make_extended(forward<E_CTX>(ctx))), cb_(forward<_E_CB>(cb)) {}

  template <class CIU>
  void call(CIU&& ciu, size_t remain) const {
    try {
      aid::for_each_in_aggregation(forward<CIU>(ciu),
          bp_detail::ciu_caller<CTX, E_CB>{this, &remain});
    } catch (...) {
      join(remain);
      throw;
    }
  }

  void join(size_t count) const {
    if (count_.fetch_sub(count, memory_order_release) == count) {
      atomic_thread_fence(memory_order_acquire);
      const_cast<breakpoint*>(this)->join_last();
    }
  }

  void join_last() {
    invoke(make_extended(move(cb_)), concurrent_finalizer<CTX, E_CB>{this});
  }

  mutable atomic_size_t count_;
  CTX ctx_;
  E_CB cb_;
  aid::concurrent_collector<exception_ptr> exceptions_;
};

template <class CTX, class E_CB>
void check_context(const breakpoint<CTX, E_CB>* bp) {
  if (!static_cast<bool>(bp)) { throw invalid_concurrent_invocation_context{}; }
}

}  // namespace bp_detail

template <class CTX, class E_CB>
class concurrent_token {
  friend auto bp_detail::make_token<>(const bp_detail::breakpoint<CTX, E_CB>*);

 public:
  concurrent_token() = default;
  concurrent_token(concurrent_token&&) = default;
  concurrent_token& operator=(concurrent_token&&) = default;

  template <class CIU>
  void fork(CIU&& ciu) const {
    bp_detail::check_context(bp_.get());
    size_t count = bp_detail::ciu_size<CTX, E_CB>(forward<CIU>(ciu));
    bp_->count_.fetch_add(count, memory_order_relaxed);
    bp_->call(forward<CIU>(ciu), count);
  }

  const CTX& context() const {
    bp_detail::check_context(bp_.get());
    return bp_->ctx_;
  }

  void set_exception(exception_ptr&& p) {
    bp_detail::check_context(bp_.get());
    bp_->exceptions_.push(move(p));
    bp_.reset();
  }

 private:
  explicit concurrent_token(const bp_detail::breakpoint<CTX, E_CB>* bp)
      : bp_(bp) {}

  struct deleter {
    void operator()(const bp_detail::breakpoint<CTX, E_CB>* bp)
        { bp->join(1u); }
  };

  unique_ptr<const bp_detail::breakpoint<CTX, E_CB>, deleter> bp_;
};

template <class CTX, class E_CB>
class concurrent_finalizer {
  friend struct bp_detail::breakpoint<CTX, E_CB>;

 public:
  concurrent_finalizer() = default;
  concurrent_finalizer(concurrent_finalizer&&) = default;
  concurrent_finalizer& operator=(concurrent_finalizer&&) = default;

  CTX& context() const {
    bp_detail::check_context(bp_.get());
    auto& exceptions = bp_->exceptions_;
    if (!exceptions.empty()) {
      vector<exception_ptr> ev;
      exceptions.collect(ev);
      throw concurrent_invocation_error{&bp_->ctx_, move(ev)};
    }
    return bp_->ctx_;
  }

 private:
  explicit concurrent_finalizer(bp_detail::breakpoint<CTX, E_CB>* bp)
      : bp_(bp) {}

  unique_ptr<bp_detail::breakpoint<CTX, E_CB>> bp_;
};

namespace ci_detail {

template <class CT, class CTX, class E_CB, class = enable_if_t<
    is_invocable_v<CT, concurrent_finalizer<CTX, E_CB>>>>
struct invoke_continuation_with_finalizer_traits {
  static inline void apply(
      CT&& ct, concurrent_finalizer<CTX, E_CB>&& finalizer)
      { invoke(forward<CT>(ct), move(finalizer)); }
};

template <class CT, class CTX, class E_CB,
    class = enable_if_t<is_invocable_v<CT, CTX>>>
struct invoke_continuation_with_context_traits {
  static inline void apply(
      CT&& ct, concurrent_finalizer<CTX, E_CB> finalizer)
      { invoke(forward<CT>(ct), forward<CTX>(finalizer.context())); }
};

template <class CT, class CTX, class E_CB,
    class = enable_if_t<is_invocable_v<CT>>>
struct invoke_continuation_without_finalizer_traits {
  static inline void apply(
      CT&& ct, concurrent_finalizer<CTX, E_CB>&& finalizer)
      { { auto f = move(finalizer); } invoke(forward<CT>(ct)); }
};

template <class F, class CTX, class E_CB,
    class = enable_if_t<is_invocable_v<F, concurrent_token<CTX, E_CB>&>>>
struct invoke_callable_with_token_traits {
  static inline void apply(F&& f, concurrent_token<CTX, E_CB>* token)
      { invoke(forward<F>(f), *token); }
};

template <class F, class CTX, class E_CB,
    class = enable_if_t<is_invocable_v<F, const CTX&>>>
struct invoke_callable_with_context_traits {
  static inline void apply(F&& f, concurrent_token<CTX, E_CB>* token)
      { invoke(forward<F>(f), token->context()); }
};

template <class F, class CTX, class E_CB,
    class = enable_if_t<is_invocable_v<F>>>
struct invoke_callable_without_token_traits {
  static inline void apply(F&& f, concurrent_token<CTX, E_CB>*)
      { invoke(forward<F>(f)); }
};

}  // namespace ci_detail

template <class E_F, class CTX, class E_CB>
class contextual_concurrent_callable {
 public:
  explicit contextual_concurrent_callable(E_F&& f,
      concurrent_token<CTX, E_CB>&& token)
      : f_(move(f)), token_(move(token)) {}

  contextual_concurrent_callable(contextual_concurrent_callable&&) = default;
  contextual_concurrent_callable& operator=(contextual_concurrent_callable&&)
      = default;

  void operator()() && {
    concurrent_token<CTX, E_CB> token = move(token_);
    try {
      applicable_template<
          equal_templates<ci_detail::invoke_callable_with_token_traits>,
          equal_templates<ci_detail::invoke_callable_with_context_traits>,
          equal_templates<ci_detail::invoke_callable_without_token_traits>
      >::type<extending_t<E_F>, CTX, E_CB>::apply(
          make_extended(move(f_)), &token);
    } catch (...) {
      token.set_exception(current_exception());
    }
  }

 private:
  E_F f_;
  concurrent_token<CTX, E_CB> token_;
};

template <class E_E, class E_F>
class concurrent_callable {
 public:
  template <class _E_E, class _E_F>
  explicit concurrent_callable(_E_E&& e, _E_F&& f)
      : e_(forward<_E_E>(e)), f_(forward<_E_F>(f)) {}

  concurrent_callable(const concurrent_callable&) = default;
  concurrent_callable(concurrent_callable&&) = default;
  concurrent_callable& operator=(const concurrent_callable&) = default;
  concurrent_callable& operator=(concurrent_callable&&) = default;

  template <class CTX, class E_CB>
  void operator()(concurrent_token<CTX, E_CB>&& token) && {
    make_extended(move(e_)).execute(contextual_concurrent_callable<
        E_F, CTX, E_CB>{move(f_), move(token)});
  }

 private:
  E_E e_;
  E_F f_;
};

template <class E_CT, class CTX, class E_CB>
class contextual_concurrent_callback {
 public:
  explicit contextual_concurrent_callback(E_CT&& ct,
      concurrent_finalizer<CTX, E_CB>&& finalizer)
      : ct_(move(ct)), finalizer_(move(finalizer)) {}

  contextual_concurrent_callback(contextual_concurrent_callback&&) = default;
  contextual_concurrent_callback& operator=(contextual_concurrent_callback&&)
      = default;

  void operator()() && {
    applicable_template<
        equal_templates<ci_detail::invoke_continuation_with_finalizer_traits>,
        equal_templates<ci_detail::invoke_continuation_with_context_traits>,
        equal_templates<ci_detail::invoke_continuation_without_finalizer_traits>
    >::type<extending_t<E_CT>, CTX, E_CB>::apply(
        make_extended(move(ct_)), move(finalizer_));
  }

 private:
  E_CT ct_;
  concurrent_finalizer<CTX, E_CB> finalizer_;
};

template <class E_E, class E_CT>
class concurrent_callback {
 public:
  template <class _E_E, class _E_CT>
  explicit concurrent_callback(_E_E&& e, _E_CT&& ct)
      : e_(forward<_E_E>(e)), ct_(forward<_E_CT>(ct)) {}

  concurrent_callback(const concurrent_callback&) = default;
  concurrent_callback(concurrent_callback&&) = default;
  concurrent_callback& operator=(const concurrent_callback&) = default;
  concurrent_callback& operator=(concurrent_callback&&) = default;

  template <class CTX, class E_CB>
  void operator()(concurrent_finalizer<CTX, E_CB>&& finalizer) && {
    make_extended(move(e_)).execute(contextual_concurrent_callback<
        E_CT, CTX, E_CB>{move(ct_), move(finalizer)});
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
class promise_callback {
 public:
  explicit promise_callback(promise<CTX>&& p) noexcept : p_(move(p)) {}

  template <class E_CB>
  void operator()(concurrent_finalizer<CTX, E_CB>&& finalizer) &&
      { p_.set_value(move(finalizer.context())); }

 private:
  promise<CTX> p_;
};

template <>
class promise_callback<void> {
 public:
  explicit promise_callback(promise<void>&& p) noexcept : p_(move(p)) {}

  template <class CTX, class E_CB>
  void operator()(concurrent_finalizer<CTX, E_CB>&&) && { p_.set_value(); }

 private:
  promise<void> p_;
};

template <class CIU, class E_CTX, class E_CB>
void concurrent_invoke(CIU&& ciu, E_CTX&& ctx, E_CB&& cb) {
  auto* bp = new bp_detail::breakpoint<extending_t<E_CTX>, decay_t<E_CB>>(
      forward<E_CTX>(ctx), forward<E_CB>(cb));
  size_t count = bp_detail::ciu_size<extending_t<E_CTX>, decay_t<E_CB>>(
      forward<CIU>(ciu));
  if (count == 0u) {
    bp->join_last();
  } else {
    atomic_init(&bp->count_, count);
    bp->call(forward<CIU>(ciu), count);
  }
}

template <class CIU, class E_CTX>
decltype(auto) concurrent_invoke(CIU&& ciu, E_CTX&& ctx) {
  using R = conditional_t<
      is_move_constructible_v<extending_t<E_CTX>>, extending_t<E_CTX>, void>;
  promise<R> p;
  future<R> result = p.get_future();
  concurrent_invoke(forward<CIU>(ciu), forward<E_CTX>(ctx),
      promise_callback<R>{move(p)});
  return result;
}

class thread_executor {
 public:
  template <class F>
  void execute(F&& f) const {
    thread th{forward<F>(f)};
    store& s = get_store();
    lock_guard<mutex> lk{s.mtx_};
    s.q_.emplace_back(move(th));
  }

 private:
  struct store {
    ~store() {
      deque<thread> q;
      for (;;) {
        {
          lock_guard<mutex> lk{mtx_};
          swap(q, q_);
        }
        if (q.empty()) {
          break;
        }
        do {
          thread th = move(q.front());
          q.pop_front();
          th.join();
        } while (!q.empty());
      }
    };

    deque<thread> q_;
    mutex mtx_;
  };
  static inline store& get_store() {
    static store s;
    return s;
  }
};

}  // namespace std

#endif  // SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
