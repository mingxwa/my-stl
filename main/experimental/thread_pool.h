/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
#define SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_

#include <utility>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "../p1648/sinking.h"
#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p0642/concurrent_invocation.h"

namespace std::experimental {

namespace detail {

template <class Token>
struct initiating_session {
  initiating_session(Token* token) : token_(token) {}
  void start(Token&& token) const { *token_ = move(token); }
  Token* const token_;
};

struct worker_thread_executor {
  template <class F>
  void execute(F&& f) const { std::thread{std::forward<F>(f)}.detach(); }
};

struct global_concurrency_decrement_continuation {
  void operator()() { aid::decrease_global_concurrency(); }
  void error(vector<exception_ptr>&&) { terminate(); }
};

}  // namespace detail

struct callable_event_consumer {
  template <class F>
  void operator()(F&& f) const noexcept { invoke(move(f)); }
};

template <class Event = p0957::value_proxy<Callable<void()>>,
          class EventConsumer = callable_event_consumer>
class static_thread_pool {
  struct context {
    template <class _EventConsumer>
    explicit context(_EventConsumer&& ec)
        : ec_(p1648::sink(forward<_EventConsumer>(ec))) {}
    context(context&&) = delete;

    mutex mtx_;
    condition_variable cond_;
    EventConsumer ec_;
    queue<Event> events_;
    bool finished_{false};
  };

 public:
  explicit static_thread_pool(size_t thread_count)
      : static_thread_pool(thread_count, EventConsumer{}) {}

  template <class _EventConsumer>
  explicit static_thread_pool(size_t thread_count, _EventConsumer&& ec) {
    auto single_worker = p0642::serial_concurrent_session{
        detail::worker_thread_executor{}, [](auto&& bp) {
      context& ctx = bp.context();
      unique_lock<mutex> lk(ctx.mtx_);
      for (;;) {
        if (!ctx.events_.empty()) {
          {
            Event current = move(ctx.events_.front());
            ctx.events_.pop();
            lk.unlock();
            ctx.ec_(move(current));
          }
          lk.lock();
        } else if (ctx.finished_) {
          break;
        } else {
          ctx.cond_.wait(lk);
        }
      }
    }};

    auto csa = tuple{detail::initiating_session{&token_},
        vector<decltype(single_worker)>{thread_count, single_worker}};
    auto ctx = p1648::make_sinking_construction<context>(
        forward<_EventConsumer>(ec));
    auto ct = detail::global_concurrency_decrement_continuation{};

    aid::increase_global_concurrency(1u);
    p0642::concurrent_invoke(csa, ctx, ct);
  }

  ~static_thread_pool() {
    if (token_.is_valid()) {
      context& ctx = token_.get().context();
      {
        lock_guard<mutex> lk(ctx.mtx_);
        ctx.finished_ = true;
      }
      ctx.cond_.notify_all();
    }
  }

  class executor_type {
   public:
    explicit executor_type(context* ctx) : ctx_(ctx) {}
    executor_type(const executor_type&) = default;
    executor_type& operator=(const executor_type&) = default;

    template <class _Event>
    void execute(_Event&& ev) const {
      {
        lock_guard<mutex> lk(ctx_->mtx_);
        ctx_->events_.emplace(forward<_Event>(ev));
      }
      ctx_->cond_.notify_one();
    }

    template <class _EventConsumer>
    void reset(_EventConsumer&& ec) const {
      lock_guard<mutex> lk(ctx_->mtx_);
      ctx_->ec_ = p1648::sink(forward<_EventConsumer>(ec));
    }

   private:
    context* ctx_;
  };

  executor_type executor() const
      { return executor_type(&token_.get().context()); }

 private:
  p0642::concurrent_token<context, p0642::async_concurrent_callback<
      detail::global_concurrency_decrement_continuation>> token_;
};

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
