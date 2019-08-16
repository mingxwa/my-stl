/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
#define SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_

#include <utility>
#include <stdexcept>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "../p1648/extended.h"
#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p0642/concurrent_invocation.h"

namespace std::experimental {

namespace detail {

enum class thread_pool_status { STABLE, SHRINKING, SHURDOWN };

}  // namespace detail

template <class F = p0957::value_proxy<Callable<void()>>>
class thread_pool_rejection : runtime_error {
 public:
  explicit thread_pool_rejection(F&& f)
      : runtime_error("The submission has been rejected"), f_(move(f)) {}

  F get_submission() && { return move(f_); }

 private:
  F f_;
};

template <class F = p0957::value_proxy<Callable<void()>>>
class thread_pool {
  struct queue_node {
    F f_;
    queue_node* next_ = nullptr;
  };

  struct context {
    explicit context(size_t core_capacity, size_t max_queue_size,
        size_t max_capacity, const chrono::steady_clock::duration& timeout)
        : core_capacity_(core_capacity), max_queue_size_(max_queue_size),
          max_capacity_(max_capacity), timeout_(timeout) {}

    size_t core_capacity_;
    size_t max_queue_size_;
    size_t max_capacity_;
    chrono::steady_clock::duration timeout_;

    mutex mtx_;
    condition_variable cond_;
    queue_node* task_queue_head_ = nullptr;
    queue_node* task_queue_tail_;
    size_t reserved_task_count_ = 0u;
    size_t unreserved_task_count_ = 0u;
    size_t idle_thread_count_ = core_capacity_;
    size_t total_thread_count_ = core_capacity_;
    detail::thread_pool_status status_ = detail::thread_pool_status::STABLE;
  };

  struct continuation {
    void operator()() {}
    void error(vector<exception_ptr>&&) { terminate(); }
  };

  class worker {
   public:
    void operator()(context& ctx) {
      unique_lock<mutex> lk(ctx.mtx_);
      for (;;) {
        if (ctx.reserved_task_count_ != 0u) {
          --ctx.reserved_task_count_;
          execute_one(&ctx);
          ++ctx.idle_thread_count_;
        } else if (ctx.status_ != detail::thread_pool_status::STABLE) {
          if (ctx.status_ == detail::thread_pool_status::SHRINKING) {
            --ctx.idle_thread_count_;
            if (--ctx.total_thread_count_ == ctx.max_capacity_) {
              ctx.status_ = detail::thread_pool_status::STABLE;
            }
          } else if (ctx.total_thread_count_ > ctx.max_capacity_) {
            --ctx.total_thread_count_;
          } else {
            while (ctx.task_queue_head_ != nullptr) { execute_one(&ctx); }
          }
          break;
        } else if (ctx.unreserved_task_count_ != 0u) {
          --ctx.idle_thread_count_;
          --ctx.unreserved_task_count_;
          execute_one(&ctx);
          ++ctx.idle_thread_count_;
        } else if (ctx.timeout_ == chrono::steady_clock::duration::max()
            || ctx.total_thread_count_ == ctx.core_capacity_) {
          ctx.cond_.wait(lk);
        } else if (ctx.cond_.wait_for(lk, ctx.timeout_) == cv_status::timeout
            && ctx.total_thread_count_ > ctx.core_capacity_
            && ctx.reserved_task_count_ == 0u) {
          --ctx.idle_thread_count_;
          --ctx.total_thread_count_;
          break;
        }
      }
    }

   private:
    inline static void execute_one(context* ctx) {
      queue_node* current = ctx->task_queue_head_;
      ctx->task_queue_head_ = current->next_;
      ctx->mtx_.unlock();
      invoke(move(current->f_));  // TODO(Mingxin Wang): Handle exception
      delete current;
      ctx->mtx_.lock();
    }
  };

  using token_t = p0642::concurrent_token<context,
      p0642::async_concurrent_callback<continuation>>;

 public:
  template <class Rep = chrono::steady_clock::rep,
      class Period = chrono::steady_clock::period>
  explicit thread_pool(size_t core_capacity, size_t max_queue_size = SIZE_MAX,
      size_t overflow_capacity = 0u, const chrono::duration<Rep, Period>&
          timeout = chrono::steady_clock::duration::max()) {
    auto ciu = tuple{[this](auto&& token) { this->token_ = move(token); },
        make_ciu(core_capacity)};

    auto ctx = p1648::make_extending_construction<context>(core_capacity,
        max_queue_size, get_max_capacity(core_capacity, overflow_capacity),
        timeout);

    p0642::concurrent_invoke(ciu, ctx, continuation{});
  }
  thread_pool(thread_pool&&) noexcept {}

  ~thread_pool() {
    context& ctx = token_.context();
    {
      lock_guard<mutex> lk(ctx.mtx_);
      ctx.reserved_task_count_ = 0u;
      ctx.status_ = detail::thread_pool_status::SHURDOWN;
    }
    ctx.cond_.notify_all();
  }

  class executor_type {
   public:
    explicit executor_type(const token_t* token) : token_(token) {}
    executor_type(const executor_type&) = default;

    template <class _F>
    void execute(_F&& f) const {
      context& ctx = token_->context();
      queue_node* node = new queue_node{forward<_F>(f)};
      {
        lock_guard<mutex> lk(ctx.mtx_);
        if (ctx.idle_thread_count_ != 0u) {
          --ctx.idle_thread_count_;
          ++ctx.reserved_task_count_;
          push(&ctx, node);
          goto THREAD_POOL_ACTION_NOTIFY_IDEL_THREAD;
        }
        if (ctx.unreserved_task_count_ < ctx.max_queue_size_) {
          ++ctx.unreserved_task_count_;
          push(&ctx, node);
          return;
        }
        if (ctx.total_thread_count_ < ctx.max_capacity_) {
          ++ctx.reserved_task_count_;
          ++ctx.total_thread_count_;
          push(&ctx, node);
          goto THREAD_POOL_ACTION_CREATE_NEW_THREAD;
        }
      }
      {
        F submission = move(node->f_);
        delete node;
        throw thread_pool_rejection<F>{move(submission)};
      }
      THREAD_POOL_ACTION_NOTIFY_IDEL_THREAD:
        ctx.cond_.notify_one();
        return;
      THREAD_POOL_ACTION_CREATE_NEW_THREAD:
        token_->fork(make_ciu(1u));
    }

   private:
    inline static void push(context* ctx, queue_node* node) {
      if (ctx->task_queue_head_ == nullptr) {
        ctx->task_queue_head_ = ctx->task_queue_tail_ = node;
      } else {
        ctx->task_queue_tail_ = ctx->task_queue_tail_->next_ = node;
      }
    }

    const token_t* token_;
  };

  executor_type executor() const { return executor_type(&token_); }

  template <class Rep = chrono::steady_clock::rep,
      class Period = chrono::steady_clock::period>
  void reset(size_t core_capacity, size_t max_queue_size = SIZE_MAX,
      size_t overflow_capacity = 0u, const chrono::duration<Rep, Period>&
          timeout = chrono::steady_clock::duration::max()) {
    context& ctx = token_.context();
    size_t new_thread_count = 0u;
    {
      lock_guard<mutex> lk(ctx.mtx_);
      ctx.core_capacity_ = core_capacity;
      ctx.max_queue_size_ = max_queue_size;
      ctx.max_capacity_ = get_max_capacity(core_capacity, overflow_capacity);
      ctx.timeout_ = timeout;
      if (ctx.total_thread_count_ < core_capacity) {
        ctx.status_ = detail::thread_pool_status::STABLE;
        new_thread_count = core_capacity - ctx.total_thread_count_;
      } else if (ctx.total_thread_count_ > ctx.max_capacity_) {
        ctx.status_ = detail::thread_pool_status::SHRINKING;
      }
    }
    ctx.cond_.notify_all();
    token_.fork(make_ciu(new_thread_count));
  }

 private:
  static inline size_t get_max_capacity(size_t core_cap, size_t overflow_cap) {
    return core_cap <= SIZE_MAX - overflow_cap
        ? core_cap + overflow_cap : SIZE_MAX;
  }

  static inline auto make_ciu(size_t count) {
    auto single_worker = p0642::async_concurrent_callable{
        aid::thread_executor{}, worker{}};
    return vector<decltype(single_worker)>{count, single_worker};
  }

  token_t token_;
};

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
