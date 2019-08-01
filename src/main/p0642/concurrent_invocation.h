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

namespace std {

class invalid_concurrent_invocation_context : public logic_error {
 public:
  explicit invalid_concurrent_invocation_context()
      : logic_error("Invalid concurrent invocation context") {}
};

class concurrent_invocation_error : public runtime_error {
 public:
  explicit concurrent_invocation_error(vector<exception_ptr> nested)
      : runtime_error(
            "There are nested exceptions in current concurrent invocation"),
        nested_(move(nested)) {}
  concurrent_invocation_error(const concurrent_invocation_error&) = default;
  concurrent_invocation_error& operator=(const concurrent_invocation_error&)
      = default;

  const vector<exception_ptr>& get_nested() const noexcept { return nested_; }

 private:
  vector<exception_ptr> nested_;
};

template <class CTX, class E_CB> class concurrent_token;
template <class CTX, class E_CB> class concurrent_finalizer;

namespace bp_detail {

template <class CTX, class E_CB>
class breakpoint {
 public:
  template <class CIU, class E_CTX, class _E_CB>
  explicit breakpoint(CIU&& ciu, E_CTX&& ctx, _E_CB&& cb)
      : ctx_(make_extended(forward<E_CTX>(ctx))), cb_(forward<_E_CB>(cb)) {
    size_t total = count(forward<CIU>(ciu));
    if (total == 0u) {
      join_last();
    } else {
      atomic_init(&detached_, total);
      call(forward<CIU>(ciu));
    }
  }

  template <class CIU>
  void fork(CIU&& ciu) {
    detached_.fetch_add(count(forward<CIU>(ciu)), memory_order_relaxed);
    call(forward<CIU>(ciu));
  }

  void join() {
    if (detached_.fetch_sub(1u, memory_order_release) == 1u) {
      atomic_thread_fence(memory_order_acquire);
      join_last();
    }
  }

  aid::concurrent_collector<exception_ptr>& exceptions() { return exceptions_; }
  add_lvalue_reference_t<CTX> context()
      { if constexpr (!is_void_v<CTX>) { return ctx_; } }

 private:
  struct ciu_size_statistics {
    template <class CIU, class = enable_if_t<
        is_nothrow_invocable_v<CIU, concurrent_token<CTX, E_CB>>>>
    void operator()(CIU&&) { ++value; }
    size_t value = 0u;
  };

  struct ciu_caller {
    template <class CIU, class = enable_if_t<
        is_nothrow_invocable_v<CIU, concurrent_token<CTX, E_CB>>>>
    void operator()(CIU&& ciu)
        { invoke(forward<CIU>(ciu), concurrent_token<CTX, E_CB>{bp_}); }
    breakpoint* bp_;
  };

  template <class CIU>
  static size_t count(CIU&& ciu) {
    ciu_size_statistics statistics;
    aid::for_each_in_aggregation(forward<CIU>(ciu), statistics);
    return statistics.value;
  }

  template <class CIU>
  void call(CIU&& ciu)
      { aid::for_each_in_aggregation(forward<CIU>(ciu), ciu_caller{this}); }

  void join_last() {
    extending_t<E_CB> cb = make_extended(move(cb_));
    invoke(move(cb), concurrent_finalizer<CTX, E_CB>{this});
  }

  atomic_size_t detached_;
  conditional_t<is_void_v<CTX>, tuple<>, CTX> ctx_;
  E_CB cb_;
  aid::concurrent_collector<exception_ptr> exceptions_;
};

template <class CTX, class E_CB>
void check_context(breakpoint<CTX, E_CB>* bp) {
  if (!static_cast<bool>(bp)) { throw invalid_concurrent_invocation_context{}; }
}

}  // namespace bp_detail

template <class CTX, class E_CB>
class concurrent_token {
  friend class bp_detail::breakpoint<CTX, E_CB>;

 public:
  concurrent_token() noexcept = default;
  concurrent_token(concurrent_token&&) noexcept = default;
  concurrent_token& operator=(concurrent_token&&) noexcept = default;

  bool is_valid() const noexcept { return static_cast<bool>(bp_); }

  template <class CIU>
  void fork(CIU&& ciu) const {
    bp_detail::check_context(bp_.get());
    bp_->fork(forward<CIU>(ciu));
  }

  add_lvalue_reference_t<CTX> context() const {
    bp_detail::check_context(bp_.get());
    return bp_->context();
  }

  void set_exception(exception_ptr&& p) {
    bp_detail::check_context(bp_.get());
    bp_->exceptions().push(move(p));
    bp_.reset();
  }

 private:
  explicit concurrent_token(bp_detail::breakpoint<CTX, E_CB>* bp) : bp_(bp) {}

  struct deleter
      { void operator()(bp_detail::breakpoint<CTX, E_CB>* bp) { bp->join(); } };

  unique_ptr<bp_detail::breakpoint<CTX, E_CB>, deleter> bp_;
};

template <class CTX, class E_CB>
class concurrent_finalizer {
  friend class bp_detail::breakpoint<CTX, E_CB>;

 public:
  concurrent_finalizer() = default;
  concurrent_finalizer(concurrent_finalizer&&) = default;
  concurrent_finalizer& operator=(concurrent_finalizer&&) = default;

  bool is_valid() const noexcept { return static_cast<bool>(bp_); }

  add_lvalue_reference_t<CTX> context() const {
    bp_detail::check_context(bp_.get());
    auto& exceptions = bp_->exceptions();
    if (!exceptions.empty()) {
      vector<exception_ptr> ev;
      exceptions.collect(ev);
      throw concurrent_invocation_error{move(ev)};
    }
    return bp_->context();
  }

 private:
  explicit concurrent_finalizer(bp_detail::breakpoint<CTX, E_CB>* bp)
      : bp_(bp) {}

  unique_ptr<bp_detail::breakpoint<CTX, E_CB>> bp_;
};

class unexecuted_concurrent_callable : public logic_error {
 public:
  explicit unexecuted_concurrent_callable()
      : logic_error("Unexecuted concurrent callable") {}
};

template <class E_E, class E_F>
class async_concurrent_callable;

template <class E_F, class CTX, class E_CB>
class contextual_async_concurrent_callable {
  template <class, class>
  friend class async_concurrent_callable;

 public:
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
    using F = extending_t<E_F>;
    auto token = move(token_);
    try {
      if constexpr (is_invocable_v<F, concurrent_token<CTX, E_CB>&>) {
        invoke(make_extended(move(f_)), token);
      } else if constexpr (is_invocable_v<F, add_lvalue_reference_t<CTX>>) {
        invoke(make_extended(move(f_)), token.context());
      } else if constexpr (is_invocable_v<F>) {
        invoke(make_extended(move(f_)));
      } else {
        STATIC_ASSERT_FALSE(F);
      }
    } catch (...) {
      token.set_exception(current_exception());
    }
  }

 private:
  explicit contextual_async_concurrent_callable(E_F&& f,
      concurrent_token<CTX, E_CB>&& token) : f_(move(f)), token_(move(token)) {}

  E_F f_;
  concurrent_token<CTX, E_CB> token_;
};

template <class E_E, class E_F>
class async_concurrent_callable {
 public:
  template <class _E_E, class _E_F>
  explicit async_concurrent_callable(_E_E&& e, _E_F&& f)
      : e_(forward<E_E>(e)), f_(forward<E_F>(f)) {}

  async_concurrent_callable(async_concurrent_callable&&) = default;
  async_concurrent_callable(const async_concurrent_callable&) = default;
  async_concurrent_callable& operator=(async_concurrent_callable&&) = default;
  async_concurrent_callable& operator=(const async_concurrent_callable&)
      = default;

  template <class CTX, class E_CB>
  void operator()(concurrent_token<CTX, E_CB>&& token) noexcept {
    contextual_async_concurrent_callable<E_F, CTX, E_CB>
        callable{move(f_), move(token)};
    try {
      make_extended(move(e_)).execute(move(callable));
    } catch (...) {
      callable.token_.set_exception(current_exception());
    }
  }

 private:
  E_E e_;
  E_F f_;
};

template <class _E_E, class _E_F>
async_concurrent_callable(_E_E&&, _E_F&&)
    -> async_concurrent_callable<decay_t<_E_E>, decay_t<_E_F>>;

template <class E_E, class E_CT, class E_EH>
class async_concurrent_callback {
 public:
  template <class _E_E, class _E_CT, class _E_EH>
  explicit async_concurrent_callback(_E_E&& e, _E_CT&& ct, _E_EH&& eh)
      : e_(forward<_E_E>(e)), ct_(forward<_E_CT>(ct_)),
        eh_(forward<_E_EH>(eh)) {}

  template <class _E_E, class _E_CT>
  explicit async_concurrent_callback(_E_E&& e, _E_CT&& ct)
      : async_concurrent_callback(
          forward<_E_E>(e), forward<_E_CT>(ct), E_EH{}) {}

  async_concurrent_callback(async_concurrent_callback&&) = default;
  async_concurrent_callback(const async_concurrent_callback&) = default;
  async_concurrent_callback& operator=(async_concurrent_callback&&) = default;
  async_concurrent_callback& operator=(const async_concurrent_callback&)
      = default;

  template <class CTX, class E_CB>
  void operator()(concurrent_finalizer<CTX, E_CB>&& finalizer) {
    using CT = extending_t<E_CT>;
    make_extended(move(e_)).execute([ct = move(ct_), eh = move(eh_),
        finalizer = move(finalizer)]() mutable {
      auto fl = move(finalizer);
      try {
        fl.context();
      } catch (const concurrent_invocation_error& ex) {
        invoke(make_extended(move(eh)), ex);
        return;
      }
      if constexpr (is_invocable_v<CT, concurrent_finalizer<CTX, E_CB>>) {
        invoke(make_extended(move(ct)), fl);
      } else if constexpr (is_invocable_v<CT, CTX>) {
        invoke(make_extended(move(ct)), move(fl.context()));
      } else if constexpr (is_invocable_v<CT>) {
        invoke(make_extended(move(ct)));
      } else {
        STATIC_ASSERT_FALSE(E_E);
      }
    });
  }

 private:
  E_E e_;
  E_CT ct_;
  E_EH eh_;
};

template <class _E_E, class _E_CT, class _E_EH>
async_concurrent_callback(_E_E&&, _E_CT&&, _E_EH&&)
    -> async_concurrent_callback<decay_t<_E_E>, decay_t<_E_CT>, decay_t<_E_EH>>;

struct throwing_concurrent_exception_handler
    { void operator()(const concurrent_invocation_error&) { throw; } };

template <class _E_E, class _E_CT>
async_concurrent_callback(_E_E&&, _E_CT&&)
    -> async_concurrent_callback<
        decay_t<_E_E>, decay_t<_E_CT>, throwing_concurrent_exception_handler>;

template <class CIU, class E_CTX, class E_CB>
void concurrent_invoke(CIU&& ciu, E_CTX&& ctx, E_CB&& cb) {
  using BP = bp_detail::breakpoint<extending_t<E_CTX>, decay_t<E_CB>>;
  if constexpr (is_void_v<extending_t<E_CTX>>) {
    new BP(forward<CIU>(ciu), tuple<>{}, forward<E_CB>(cb));
  } else {
    new BP(forward<CIU>(ciu), forward<E_CTX>(ctx), forward<E_CB>(cb));
  }
}

template <class CIU, class E_CTX = in_place_type_t<void>>
decltype(auto) concurrent_invoke(CIU&& ciu, E_CTX&& ctx = E_CTX{}) {
  using R = conditional_t<is_move_constructible_v<extending_t<E_CTX>>,
      extending_t<E_CTX>, void>;
  promise<R> p;
  future<R> f = p.get_future();
  concurrent_invoke(forward<CIU>(ciu), forward<E_CTX>(ctx),
      [p = move(p)](auto&& finalizer) mutable {
    try {
      finalizer.context();
    } catch (const concurrent_invocation_error&) {
      p.set_exception(current_exception());
      return;
    }
    if constexpr (is_same_v<R, void>) {
      p.set_value();
    } else {
      p.set_value(move(finalizer.context()));
    }
  });
  return f;
}

}  // namespace std

#endif  // SRC_MAIN_P0642_CONCURRENT_INVOCATION_H_
