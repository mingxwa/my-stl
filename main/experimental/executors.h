/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_EXPERIMENTAL_EXECUTORS_H_
#define SRC_MAIN_EXPERIMENTAL_EXECUTORS_H_

#include <utility>
#include <vector>
#include <set>
#include <queue>
#include <memory>
#include <future>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_nothrow_callable_impl.h"

namespace std::experimental {

class polymorphic_serial_task {
 public:
  template <class F>
  polymorphic_serial_task(F&& f)
      requires(!is_same_v<decay_t<F>, polymorphic_serial_task>)
      : p_(p0957::make_proxy<INothrowCallable<void()>>(forward<F>(f))) {}
  polymorphic_serial_task(polymorphic_serial_task&&) = default;
  polymorphic_serial_task& operator=(polymorphic_serial_task&&) = default;
  void operator()() noexcept { (*p_)(); }

 private:
  p0957::proxy<INothrowCallable<void()>> p_;
};

struct polymorphic_task_consumer {
  template <class F>
  decltype(auto) consume(F* f) const noexcept { return (*f)(); }
};

template <class Event = polymorphic_serial_task,
          class EventConsumer = polymorphic_task_consumer>
class static_thread_pool {
 public:
  explicit static_thread_pool(size_t thread_count)
      : static_thread_pool(thread_count, EventConsumer{}) {}

  template <class _EventConsumer>
  explicit static_thread_pool(size_t thread_count, _EventConsumer&& ec)
      : ec_(forward<_EventConsumer>(ec)), shutdown_(false) {
    for (size_t i = 0u; i < thread_count; ++i) {
      workers_.emplace_back([this] {
        unique_lock<mutex> lk{mtx_};
        for (;;) {
          if (!events_.empty()) {
            {
              Event current = move(events_.front());
              events_.pop();
              lk.unlock();
              ec_.consume(&current);
            }
            lk.lock();
          } else if (shutdown_) {
            break;
          } else {
            cond_.wait(lk);
          }
        }
      });
    }
  }

  ~static_thread_pool() {
    {
      lock_guard<mutex> lk{mtx_};
      shutdown_ = true;
    }
    cond_.notify_all();
    for (thread& worker : workers_) {
      worker.join();
    }
  }

  template <class _Event>
  void submit(_Event&& ev) {
    {
      lock_guard<mutex> lk{mtx_};
      events_.emplace(forward<_Event>(ev));
    }
    cond_.notify_one();
  }

 private:
  mutex mtx_;
  condition_variable cond_;
  EventConsumer ec_;
  queue<Event> events_;
  bool shutdown_;
  vector<thread> workers_;
};

namespace detail {

template <class T>
struct optional_traits {
  static constexpr bool applicable = false;
  using value_type = void;
};
template <class T>
struct optional_traits<optional<T>> {
  static constexpr bool applicable = true;
  using value_type = T;
};
template <class T>
struct time_point_traits {
  static constexpr bool applicable = false;
};
template <class Clock, class Duration>
struct time_point_traits<chrono::time_point<Clock, Duration>> {
  static constexpr bool applicable = true;
};
template <class T>
struct duration_traits {
  static constexpr bool applicable = false;
};
template <class Rep, class Period>
struct duration_traits<chrono::duration<Rep, Period>> {
  static constexpr bool applicable = true;
};

enum class ttp_context_type { pool, worker, detached, done };
struct ttp_context {
  explicit constexpr ttp_context(ttp_context_type type) : type_(type) {}

  const ttp_context_type type_;
};
template <ttp_context_type CT>
ttp_context* static_ttp_context() {
  static constexpr ttp_context data{CT};
  return const_cast<ttp_context*>(&data);
}

}  // namespace detail

template <class Clock>
class polymorphic_periodic_task {
  using OptTimePoint = optional<typename Clock::time_point>;

 public:
  template <class F, class Rep, class Period>
  explicit polymorphic_periodic_task(
      F&& f, const chrono::duration<Rep, Period>& period)
      : p_(wrap_endless(forward<F>(f), period)) {}
  template <class F, class Rep, class Period>
  explicit polymorphic_periodic_task(
      F&& f, const chrono::duration<Rep, Period>& period, size_t times)
      : p_(wrap_limited(forward<F>(f), period, times)) {}
  template <class F>
  polymorphic_periodic_task(F&& f)
      requires(!is_same_v<decay_t<F>, polymorphic_periodic_task>)
      : p_(wrap_custom(forward<F>(f))) {}
  polymorphic_periodic_task(polymorphic_periodic_task&&) = default;
  polymorphic_periodic_task& operator=(polymorphic_periodic_task&&) = default;
  OptTimePoint operator()() noexcept { return (*p_)(); }

 private:
  template <class F, class Rep, class Period>
  static auto wrap_endless(F&& f, const chrono::duration<Rep, Period>& period) {
    return p0957::make_proxy<INothrowCallable<OptTimePoint()>>(
        [f = forward<F>(f), period]() mutable noexcept -> OptTimePoint
            { f(); return Clock::now() + period; });
  }

  template <class F, class Rep, class Period>
  static auto wrap_limited(F&& f, const chrono::duration<Rep, Period>& period,
      size_t times) {
    return p0957::make_proxy<INothrowCallable<OptTimePoint()>>(
        [f = forward<F>(f), period, times]() mutable noexcept -> OptTimePoint {
          f();
          if (--times > 0u) {
            return Clock::now() + period;
          }
          return nullopt;
        });
  }

  template <class F>
  static auto wrap_custom(F&& f) {
    if constexpr (is_nothrow_invocable_r_v<OptTimePoint, decay_t<F>&>) {
      return p0957::make_proxy<INothrowCallable<OptTimePoint()>>(forward<F>(f));
    } else {
      return p0957::make_proxy<INothrowCallable<OptTimePoint()>>(
          [f = forward<F>(f)]() mutable noexcept -> OptTimePoint {
            using R = decay_t<invoke_result_t<decay_t<F>&>>;
            using RV = conditional_t<detail::optional_traits<R>::applicable,
                typename detail::optional_traits<R>::value_type, R>;
            if constexpr (detail::time_point_traits<RV>::applicable) {
              return f();
            } else if constexpr (detail::duration_traits<RV>::applicable) {
              auto r = f();
              if constexpr (detail::optional_traits<R>::applicable) {
                if (r.has_value()) {
                  return Clock::now() + r.value();
                }
                return nullopt;
              } else {
                return Clock::now() + r;
              }
            } else {
              f();
              return nullopt;
            }
          });
    }
  }

  p0957::proxy<INothrowCallable<OptTimePoint()>> p_;
};

template <class Clock, class Event = polymorphic_periodic_task<Clock>,
    class EventConsumer = polymorphic_task_consumer>
class scheduled_thread_pool {
  struct pool_context;

 public:
  explicit scheduled_thread_pool(size_t count)
      : scheduled_thread_pool(count, EventConsumer{}) {}

  template <class _EventConsumer>
  explicit scheduled_thread_pool(size_t count, _EventConsumer&& ec)
      : pool_ctx_(forward<_EventConsumer>(ec)), worker_ctxs_(count) {
    for (size_t i = 0u; i < count; ++i) {
      worker_ctxs_[i].pool_ctx_ = &pool_ctx_;
      workers_.emplace_back([worker_ctx = &worker_ctxs_[i]] {
        unique_lock<mutex> lk{worker_ctx->pool_ctx_->mtx_};
        while (worker_ctx->retrieve(&lk)) {
          if (worker_ctx->wait(&lk)) {
            worker_ctx->run(&lk);
          }
        }
      });
    }
  }

  ~scheduled_thread_pool() {
    pool_ctx_.shutdown();
    for (thread& worker : workers_) {
      worker.join();
    }
  }

  template <class Duration, class _Event>
  auto submit(const chrono::time_point<Clock, Duration>& when, _Event&& ev) {
    shared_ptr<job_context> job_ctx
        = make_shared<job_context>(when, forward<_Event>(ev));
    auto cancelator = [job_ctx_weak = weak_ptr<job_context>{job_ctx}]() {
      shared_ptr<job_context> job_ctx = job_ctx_weak.lock();
      if (!static_cast<bool>(job_ctx)) {
        return;
      }
      for (;;) {
        detail::ttp_context* ttp_ctx
            = job_ctx->associated_.load(memory_order_relaxed);
        switch (ttp_ctx->type_) {
          case detail::ttp_context_type::pool:
            if (static_cast<pool_context*>(ttp_ctx)->cancel(job_ctx)) {
              return;
            }
            break;
          case detail::ttp_context_type::worker:
            if (static_cast<worker_context*>(ttp_ctx)
                ->pool_ctx_->cancel(job_ctx)) {
              return;
            }
            break;
          case detail::ttp_context_type::detached:
            if (job_ctx->associated_.compare_exchange_weak(
                ttp_ctx, detail::static_ttp_context<
                    detail::ttp_context_type::done>(),
                memory_order_relaxed)) {
              return;
            }
            break;
          case detail::ttp_context_type::done:
            return;
          default:
            terminate();
        }
      }
    };
    pool_ctx_.submit(move(job_ctx));
    return cancelator;
  }

 private:
  struct job_context {
    template <class Duration, class _Event>
    explicit job_context(const chrono::time_point<Clock, Duration>& when,
        _Event&& ev) : when_(when), ev_(forward<_Event>(ev)) {}

    typename Clock::time_point when_;
    Event ev_;
    atomic<detail::ttp_context*> associated_;
  };

  struct worker_context : detail::ttp_context {
    worker_context() : ttp_context(detail::ttp_context_type::worker) {}

    bool retrieve(unique_lock<mutex>* lk) {
      if (!pool_ctx_->queueing_.empty()) {
        job_ctx_ = move(pool_ctx_->queueing_.extract(
            pool_ctx_->queueing_.begin()).value());
        job_ctx_->associated_.store(this, memory_order_relaxed);
        return true;
      }
      if (pool_ctx_->shutdown_) {
        return false;
      }
      pool_ctx_->idle_.push(this);
      for (;;) {
        cond_.wait(*lk);
        if (static_cast<bool>(job_ctx_)) {
          return true;
        }
        if (pool_ctx_->shutdown_) {
          return false;
        }
      }
    }

    bool wait(unique_lock<mutex>* lk) {
      pool_ctx_->scheduling_.insert(this);
      cv_status status;
      do {
        status = cond_.wait_until(*lk, job_ctx_->when_);
        if (!static_cast<bool>(job_ctx_)) {
          return false;
        }
      } while (status == cv_status::no_timeout);
      pool_ctx_->scheduling_.erase(this);
      return true;
    }

    void run(unique_lock<mutex>* lk) {
      shared_ptr<job_context> job_ctx = move(job_ctx_);
      detail::ttp_context* ttp_ctx
          = detail::static_ttp_context<detail::ttp_context_type::detached>();
      job_ctx->associated_.store(ttp_ctx, memory_order_relaxed);
      lk->unlock();
      optional<typename Clock::time_point> next_time
          = pool_ctx_->ec_.consume(&job_ctx->ev_);
      if (static_cast<bool>(next_time)) {
        job_ctx->when_ = *next_time;
        lk->lock();
        if (job_ctx->associated_.compare_exchange_strong(
            ttp_ctx, pool_ctx_, memory_order_relaxed)) {
          pool_ctx_->queueing_.insert(move(job_ctx));
        }
      } else {
        job_ctx->associated_.store(detail::static_ttp_context<
            detail::ttp_context_type::done>(), memory_order_relaxed);
        lk->lock();
      }
    }

    pool_context* pool_ctx_;
    condition_variable cond_;
    shared_ptr<job_context> job_ctx_;
  };

  struct job_comparator {
    bool operator()(const shared_ptr<job_context>& a,
        const shared_ptr<job_context>& b) const {
      if (a->when_ != b->when_) {
        return a->when_ < b->when_;
      }
      return a < b;
    }
  };

  struct worker_comparator {
    bool operator()(const worker_context* a, const worker_context* b) const {
      if (a->job_ctx_->when_ != b->job_ctx_->when_) {
        return b->job_ctx_->when_ < a->job_ctx_->when_;
      }
      return a < b;
    }
  };

  struct pool_context : detail::ttp_context {
    template <class _EventConsumer>
    explicit pool_context(_EventConsumer&& ec)
        : ttp_context(detail::ttp_context_type::pool),
          ec_(forward<_EventConsumer>(ec)), shutdown_(false) {}

    void submit(shared_ptr<job_context>&& job_ctx) {
      worker_context* worker;
      {
        lock_guard<mutex> lk{mtx_};
        if (idle_.empty()) {
          auto it = scheduling_.begin();
          if (it == scheduling_.end()
              || job_ctx->when_ >= (*it)->job_ctx_->when_) {
            new(&job_ctx->associated_) atomic<detail::ttp_context*>(this);
            queueing_.insert(move(job_ctx));
            return;
          }
          worker = *it;
          scheduling_.erase(it);
          new(&job_ctx->associated_) atomic<detail::ttp_context*>(worker);
          worker->job_ctx_->associated_.store(this, memory_order_relaxed);
          queueing_.insert(move(worker->job_ctx_));
          worker->job_ctx_ = move(job_ctx);
          scheduling_.insert(worker);
        } else {
          worker = idle_.front();
          idle_.pop();
          new(&job_ctx->associated_) atomic<detail::ttp_context*>(worker);
          worker->job_ctx_ = move(job_ctx);
        }
      }
      worker->cond_.notify_one();
    }

    bool cancel(const shared_ptr<job_context>& job_ctx) {
      worker_context* worker;
      {
        lock_guard<mutex> lk{mtx_};
        detail::ttp_context* ttp_ctx
            = job_ctx->associated_.load(memory_order_relaxed);
        if (ttp_ctx->type_ == detail::ttp_context_type::pool) {
          queueing_.erase(job_ctx);
          job_ctx->associated_.store(detail::static_ttp_context<
              detail::ttp_context_type::done>(), memory_order_relaxed);
          return true;
        } else if (ttp_ctx->type_ == detail::ttp_context_type::worker) {
          worker = static_cast<worker_context*>(ttp_ctx);
          scheduling_.erase(worker);
          worker->job_ctx_.reset();
          job_ctx->associated_.store(detail::static_ttp_context<
              detail::ttp_context_type::done>(), memory_order_relaxed);
        } else {
          return false;
        }
      }
      worker->cond_.notify_one();
      return true;
    }

    void shutdown() {
      vector<worker_context*> worker_ctxs;
      {
        lock_guard<mutex> lk{mtx_};
        shutdown_ = true;
        queueing_.clear();
        for (worker_context* worker_ctx : scheduling_) {
          worker_ctx->job_ctx_.reset();
          worker_ctxs.push_back(worker_ctx);
        }
        scheduling_.clear();
        while (!idle_.empty()) {
          worker_ctxs.push_back(idle_.front());
          idle_.pop();
        }
      }
      for (worker_context* worker_ctx : worker_ctxs) {
        worker_ctx->cond_.notify_one();
      }
    }

    mutex mtx_;
    EventConsumer ec_;
    set<shared_ptr<job_context>, job_comparator> queueing_;
    set<worker_context*, worker_comparator> scheduling_;
    queue<worker_context*> idle_;
    bool shutdown_;
  };

  pool_context pool_ctx_;
  vector<worker_context> worker_ctxs_;
  vector<thread> workers_;
};

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_EXECUTORS_H_
