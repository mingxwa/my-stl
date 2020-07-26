/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_EXPERIMENTAL_EXECUTORS_H_
#define SRC_MAIN_EXPERIMENTAL_EXECUTORS_H_

#include <utility>
#include <queue>
#include <functional>
#include <memory>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_nothrow_callable_impl.h"

namespace std::experimental {

namespace detail {

using concurrent_token = shared_ptr<promise<void>>;
inline concurrent_token make_concurrent_token() {
  struct hub {
    hub() : token(&p, [](promise<void>* p) { p->set_value(); }) {}
    ~hub() { token.reset(); p.get_future().wait(); }

    concurrent_token token;
    promise<void> p;
  } static h;
  return h.token;
}

}  // namespace detail

struct thread_executor {
  template <class F>
  void execute(F&& f) const {
    thread([f = forward<F>(f), t = detail::make_concurrent_token()]()
        mutable { invoke(move(f)); }).detach();
  }
};

class nothrow_task_wrapper {
 public:
  template <class F>
  nothrow_task_wrapper(F&& f) {
    if constexpr (p0957::globally_proxiable<optional<decay_t<F>>,
        NothrowCallable<void()>>) {
      p_.emplace<optional<decay_t<F>>>(forward<F>(f));
    } else {
      p_ = make_unique<decay_t<F>>(forward<F>(f));
    }
  }
  nothrow_task_wrapper(nothrow_task_wrapper&&) = default;
  nothrow_task_wrapper& operator=(nothrow_task_wrapper&&) = default;
  void operator()() noexcept { (*p_)(); }

 private:
  p0957::proxy<NothrowCallable<void()>> p_;
};

struct nothrow_task_consumer {
  template <class F> void operator()(F&& f) const noexcept { invoke(f); }
};

template <class Event = nothrow_task_wrapper,
          class EventConsumer = nothrow_task_consumer>
class static_thread_pool {
  struct context {
    template <class _EventConsumer>
    explicit context(_EventConsumer&& ec) : ec_(forward<_EventConsumer>(ec)),
        finished_(false), token_(detail::make_concurrent_token()) {}

    mutex mtx_;
    condition_variable cond_;
    EventConsumer ec_;
    queue<Event> events_;
    bool finished_;
    detail::concurrent_token token_;
  };

 public:
  explicit static_thread_pool(size_t thread_count)
      : static_thread_pool(thread_count, EventConsumer{}) {}

  template <class _EventConsumer>
  explicit static_thread_pool(size_t thread_count, _EventConsumer&& ec)
      : ctx_(make_shared<context>(forward<_EventConsumer>(ec))) {
    for (size_t i = 0u; i < thread_count; ++i) {
      thread([ctx = ctx_] {
        unique_lock<mutex> lk(ctx->mtx_);
        for (;;) {
          if (!ctx->events_.empty()) {
            {
              Event current = move(ctx->events_.front());
              ctx->events_.pop();
              lk.unlock();
              ctx->ec_(move(current));
            }
            lk.lock();
          } else if (ctx->finished_) {
            break;
          } else {
            ctx->cond_.wait(lk);
          }
        }
      }).detach();
    }
  }

  ~static_thread_pool() {
    if (static_cast<bool>(ctx_)) {
      {
        lock_guard<mutex> lk(ctx_->mtx_);
        ctx_->finished_ = true;
      }
      ctx_->cond_.notify_all();
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
      ctx_->ec_ = forward<_EventConsumer>(ec);
    }

   private:
    context* ctx_;
  };

  executor_type executor() const { return executor_type(ctx_.get()); }

 private:
  shared_ptr<context> ctx_;
};

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_EXECUTORS_H_
