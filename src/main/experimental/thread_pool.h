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

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p0642/concurrent_invocation.h"

namespace std::experimental {

template <class F = p0957::value_proxy<Callable<void()>>>
class static_thread_pool {
  struct context {
    mutex mtx_;
    condition_variable cond_;
    queue<F> tasks_;
    bool finished_{false};
  };

  struct continuation {
    void operator()() {}
    void error(vector<exception_ptr>&&) { terminate(); }
  };

 public:
  template <class E = aid::thread_executor>
  explicit static_thread_pool(size_t thread_count, const E& e = E{}) {
    auto single_worker = p0642::async_concurrent_callable{e, [](auto&& token) {
      context& ctx = token.context();
      unique_lock<mutex> lk(ctx.mtx_);
      for (;;) {
        if (!ctx.tasks_.empty()) {
          {
            F current = move(ctx.tasks_.front());
            ctx.tasks_.pop();
            lk.unlock();
            invoke(move(current));
          }
          lk.lock();
        } else if (ctx.finished_) {
          break;
        } else {
          ctx.cond_.wait(lk);
        }
      }
    }};

    auto ciu = tuple{[this](auto&& token) { this->token_ = move(token); },
        vector<decltype(single_worker)>{thread_count, single_worker}};

    p0642::concurrent_invoke(ciu, in_place_type<context>, continuation{});
  }

  ~static_thread_pool() {
    context& ctx = token_.context();
    {
      lock_guard<mutex> lk(ctx.mtx_);
      ctx.finished_ = true;
    }
    ctx.cond_.notify_all();
  }

  class executor_type {
   public:
    explicit executor_type(context* ctx) : ctx_(ctx) {}
    executor_type(const executor_type&) = default;

    template <class _F>
    void execute(_F&& f) const {
      {
        lock_guard<mutex> lk(ctx_->mtx_);
        ctx_->tasks_.emplace(forward<_F>(f));
      }
      ctx_->cond_.notify_one();
    }

   private:
    context* const ctx_;
  };

  executor_type executor() const { return executor_type(&token_.context()); }

 private:
  p0642::concurrent_token<context,
      p0642::async_concurrent_callback<continuation>> token_;
};

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
