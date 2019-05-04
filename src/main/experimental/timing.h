/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_TIMING_H_
#define SRC_MAIN_EXPERIMENTAL_TIMING_H_

#include <utility>
#include <queue>
#include <vector>
#include <set>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p0642/concurrent_invocation.h"

namespace std {

namespace detail {

template <class Clock, class Duration>
class timed_task_proxy {
  using R = optional<pair<chrono::time_point<Clock, Duration>,
      timed_task_proxy>>;

 public:
  template <class F>
  timed_task_proxy(F&& f) : f_(forward<F>(f)) {}
  timed_task_proxy(timed_task_proxy&&) = default;
  timed_task_proxy& operator=(timed_task_proxy&&) = default;
  R operator()() { return invoke(move(f_)); }

 private:
  value_proxy<Callable<R()>> f_;
};

}  // namespace detail

template <class Clock = chrono::high_resolution_clock,
    class Duration = typename Clock::duration,
    class F = detail::timed_task_proxy<Clock, Duration>>
class timing_thread_pool {
  struct buffer_data;
  struct shared_data;
  struct task_type;

 public:
  template <class E = crucial_thread_executor>
  explicit timing_thread_pool(size_t count, const E& executor = E())
      : data_(make_shared<shared_data>(count)) {
    for (size_t i = 0; i < count; ++i) {
      executor.execute([data_ = data_, i] {
        shared_data& shared = *data_;
        buffer_data& buffer = shared.buffer_[i];
        for (;;) {
          unique_lock<mutex> lk(shared.mtx_);
          if (shared.shutdown_) {
            return;
          }
          if (shared.tasks_.empty()) {
            shared.idle_.push(buffer);
            buffer.task_.reset();
            do {
              buffer.cond_.wait(lk);
              if (shared.shutdown_) {
                return;
              }
            } while (!buffer.task_.has_value());
          } else {
            buffer.task_.emplace(shared.tasks_.top());
            shared.tasks_.pop();
          }
          for (;;) {
            if (Clock::now() < buffer.task_->when_) {
              shared.pending_.insert(buffer);
              while (buffer.cond_.wait_until(lk, buffer.task_->when_) ==
                  cv_status::no_timeout) {
                if (shared.shutdown_) {
                  return;
                }
              }
              shared.pending_.erase(buffer);
            }
            lk.unlock();
            auto next = buffer.task_->what_();
            if (!next.has_value()) {
              break;
            }
            lk.lock();
            if (!shared.tasks_.empty()
                && shared.tasks_.top().when_ < next->first) {
              buffer.task_.emplace(shared.tasks_.top());
              shared.tasks_.emplace(next->first, move(next->second));
            } else {
              buffer.task_.emplace(next->first, move(next->second));
            }
          }
        }
      });
    }
  }

  ~timing_thread_pool() {
    shared_data& shared = *data_;
    queue<reference_wrapper<buffer_data>> idle;
    {
      lock_guard<mutex> lk(shared.mtx_);
      shared.shutdown_ = true;
      idle = shared.idle_;
    }
    while (!idle.empty()) {
      idle.front().get().cond_.notify_one();
      idle.pop();
    }
  }

  class executor_type {
   public:
    explicit executor_type(shared_data* data) : data_(data) {}
    executor_type(const executor_type&) = default;

    template <class _F>
    void execute(const chrono::time_point<Clock, Duration>& when, _F&& f)
        const {
      buffer_data* buffer_ptr;
      {
        lock_guard<mutex> lk(data_->mtx_);
        if (data_->idle_.empty()) {
          auto it = data_->pending_.begin();
          if (it != data_->pending_.end() && when < it->get().task_->when_) {
            buffer_ptr = &it->get();
            data_->pending_.erase(it);
            data_->tasks_.emplace(*buffer_ptr->task_);
            buffer_ptr->task_.emplace(when, forward<decltype(f)>(f));
            data_->pending_.insert(*buffer_ptr);
          } else {
            data_->tasks_.emplace(when, forward<decltype(f)>(f));
            return;
          }
        } else {
          buffer_ptr = &data_->idle_.front().get();
          data_->idle_.pop();
          buffer_ptr->task_.emplace(when, forward<decltype(f)>(f));
        }
      }
      buffer_ptr->cond_.notify_one();
    }

   private:
    shared_data* const data_;
  };

  executor_type executor() const { return executor_type(data_.get()); }

 private:
  struct task_type {
    template <class _F>
    explicit task_type(const chrono::time_point<Clock, Duration>& when, _F&& f)
        : when_(when), what_(forward<_F>(f)) {}
    task_type(const task_type& rhs) : when_(rhs.when_),
        what_(move(rhs.what_)) {}
    task_type& operator=(task_type&&) = default;

    chrono::time_point<Clock, Duration> when_;
    mutable F what_;
  };

  struct buffer_data {
    condition_variable cond_;
    optional<task_type> task_;
  };

  struct task_comparator {
    bool operator()(const task_type& a, const task_type& b) const {
      return b.when_ < a.when_;
    }
  };

  struct buffer_comparator {
    bool operator()(const buffer_data& a, const buffer_data& b) const {
      if (a.task_->when_ != b.task_->when_) {
        return b.task_->when_ < a.task_->when_;
      }
      return &a < &b;
    }
  };

  struct shared_data {
    explicit shared_data(size_t count) : buffer_(count), shutdown_(false) {}

    mutex mtx_;
    priority_queue<task_type, vector<task_type>, task_comparator> tasks_;
    vector<buffer_data> buffer_;
    queue<reference_wrapper<buffer_data>> idle_;
    set<reference_wrapper<buffer_data>, buffer_comparator> pending_;
    bool shutdown_;
  };

  shared_ptr<shared_data> data_;
};

namespace detail {

inline constexpr uint32_t CIRCULATION_VERSION_MASK = 0x3FFFFFFF;
inline constexpr uint32_t CIRCULATION_RUNNING_MASK = 0x40000000;
inline constexpr uint32_t CIRCULATION_RESERVATION_MASK = 0x80000000;

template <class F>
struct circulation_data {
  template <class _F>
  explicit circulation_data(_F&& functor) : state_(0u),
      functor_(forward<_F>(functor)) {}

  uint32_t advance_version() {
    uint32_t s = state_.load(memory_order_relaxed), v;
    do {
      v = (s + 1) & CIRCULATION_VERSION_MASK;
    } while (!state_.compare_exchange_weak(
        s, (s & CIRCULATION_RUNNING_MASK) | v, memory_order_relaxed));
    return v;
  }

  atomic_uint32_t state_;
  F functor_;
};

template <class Clock, class Duration, class F>
class timed_circulation {
 public:
  explicit timed_circulation(const shared_ptr<circulation_data<F>>& data_ptr,
      uint32_t version) : data_ptr_(data_ptr), version_(version) {}

  timed_circulation(timed_circulation&&) = default;
  timed_circulation(const timed_circulation&) = default;

  optional<pair<chrono::time_point<Clock, Duration>, timed_circulation>>
      operator()() {
    circulation_data<F>& data = *data_ptr_;
    uint32_t s = data.state_.load(memory_order_relaxed);
    for (;;) {
      if ((s & CIRCULATION_VERSION_MASK) != version_) {
        return nullopt;
      }
      if (s & CIRCULATION_RUNNING_MASK) {
        if (data.state_.compare_exchange_weak(
            s, s | CIRCULATION_RESERVATION_MASK, memory_order_relaxed)) {
          return nullopt;
        }
      } else {
        if (data.state_.compare_exchange_weak(
            s, s | CIRCULATION_RUNNING_MASK, memory_order_relaxed)) {
          break;
        }
      }
    }
    atomic_thread_fence(memory_order_acquire);
    for (;;) {
      auto gap = data.functor_();
      atomic_thread_fence(memory_order_release);
      s = data.state_.load(memory_order_relaxed);
      for (;;) {
        if (s & CIRCULATION_RESERVATION_MASK) {
          if (data.state_.compare_exchange_weak(
              s, s & ~CIRCULATION_RESERVATION_MASK, memory_order_relaxed)) {
            version_ = s & CIRCULATION_VERSION_MASK;
            break;
          }
        } else {
          if (data.state_.compare_exchange_weak(
              s, s & ~CIRCULATION_RUNNING_MASK, memory_order_relaxed)) {
            if (version_ == (s & CIRCULATION_VERSION_MASK) && gap.has_value()) {
              return make_pair(Clock::now() + gap.value(), move(*this));
            }
            return nullopt;
          }
        }
      }
    }
  }

  shared_ptr<circulation_data<F>> data_ptr_;
  uint32_t version_;
};

}  // namespace detail

template <class Clock, class Duration, class E, class F>
class circulation_trigger {
 public:
  template <class _E, class _F>
  explicit circulation_trigger(_E&& executor, _F&& functor)
      : executor_(forward<_E>(executor)), data_ptr_(
      make_shared<detail::circulation_data<F>>(forward<_F>(functor))) {}

  circulation_trigger(circulation_trigger&&) = default;
  circulation_trigger(const circulation_trigger&) = delete;

  template <class Rep, class Period>
  void fire(const chrono::duration<Rep, Period>& delay) const {
    detail::circulation_data<F>& data = *data_ptr_;
    executor_.execute(Clock::now() + delay,
        detail::timed_circulation<Clock, Duration, F>(
        data_ptr_, data.advance_version()));
  }

  void fire() const { fire(chrono::duration<int>::zero()); }
  void suspend() const { data_ptr_->advance_version(); }

 private:
  const E executor_;
  shared_ptr<detail::circulation_data<F>> data_ptr_;
};

template <class Clock = chrono::high_resolution_clock,
    class Duration = typename Clock::duration, class E, class F>
auto make_timed_circulation(E&& executor, F&& functor) {
  return circulation_trigger<Clock, Duration, decay_t<E>, decay_t<F>>(
      forward<E>(executor), forward<F>(functor));
}

}  // namespace std

#endif  // SRC_MAIN_EXPERIMENTAL_TIMING_H_
