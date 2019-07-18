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

template <class E_E, class E_F>
auto make_concurrent_callable(E_E&& e, E_F&& f) {
  using F = extending_t<E_F>;
  return [e = forward<E_E>(e), f = forward<E_F>(f)](auto&& token) mutable {
    make_extended(move(e)).execute([f = move(f), token = move(token)]()
        mutable {
      auto tk = move(token);
      try {
        if constexpr (is_invocable_v<F, decltype(tk)&>) {
          invoke(make_extended(move(f)), tk);
        } else if constexpr (is_invocable_v<F, decltype(tk.context())>) {
          invoke(make_extended(move(f)), tk.context());
        } else if constexpr (is_invocable_v<F>) {
          invoke(make_extended(move(f)));
        } else {
          STATIC_ASSERT_FALSE(E_E);
        }
      } catch (...) {
        tk.set_exception(current_exception());
      }
    });
  };
}

template <class E_E, class E_CT, class E_EH>
auto make_concurrent_callback(E_E&& e, E_CT&& ct, E_EH&& eh) {
  using CT = extending_t<E_CT>;
  return [e = forward<E_E>(e), ct = forward<E_CT>(ct), eh = forward<E_EH>(eh)](
      auto&& finalizer) mutable {
    make_extended(move(e)).execute([ct = move(ct), eh = move(eh),
        finalizer = move(finalizer)]() mutable {
      auto fl = move(finalizer);
      try {
        fl.context();
      } catch (const concurrent_invocation_error<
          decay_t<decltype(fl.context())>>& ex) {
        invoke(make_extended(move(eh)), ex);
        return;
      }
      if constexpr (is_invocable_v<CT, decltype(fl)>) {
        invoke(make_extended(move(ct)), fl);
      } else if constexpr (is_invocable_v<CT, decltype(move(fl.context()))>) {
        invoke(make_extended(move(ct)), move(fl.context()));
      } else if constexpr (is_invocable_v<CT>) {
        invoke(make_extended(move(ct)));
      } else {
        STATIC_ASSERT_FALSE(E_E);
      }
    });
  };
}

template <class E_E, class E_CT>
auto make_concurrent_callback(E_E&& e, E_CT&& ct) {
  return make_concurrent_callback(forward<E_E>(e), forward<E_CT>(ct),
      [](auto&&) { throw; });
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
