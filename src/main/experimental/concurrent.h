/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_CONCURRENT_H_
#define SRC_MAIN_EXPERIMENTAL_CONCURRENT_H_

#include <queue>
#include <vector>
#include <set>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <optional>

#include "./self.h"
#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p0957/mock/proxy_callable_optional_pair_t_self_impl.h"

namespace std {

namespace wang {

struct thread_hub {
  ~thread_hub() {
    if (!detach()) {
      sem_.get_future().wait();
    }
  }

  void attach() { count_.fetch_add(1, memory_order_relaxed); }

  bool detach() {
    atomic_thread_fence(memory_order_release);
    size_t c = count_.load(memory_order_relaxed);
    do {
      if (c == 0u) {
        atomic_thread_fence(memory_order_acquire);
        return true;
      }
    } while (!count_.compare_exchange_weak(c, c - 1, memory_order_relaxed));
    return false;
  }

  atomic_size_t count_;
  promise<void> sem_;
};

template <class T>
class mutex_queue {
 public:
  mutex_queue() : head_(new node()), tail_(head_) {}

  ~mutex_queue() { delete head_; }

  template <class... Args>
  void emplace(Args&&... args) {
    node* current = new node(forward<Args>(args)...), *desired;
    do {
      desired = nullptr;
    } while (!tail_.load(memory_order_relaxed)->next_
        .compare_exchange_weak(desired, current, memory_order_relaxed));
    tail_.store(current, memory_order_relaxed);
  }

  T pop() {
    node* next_head = head_->next_.load(memory_order_relaxed);
    delete head_;
    head_ = next_head;
    T result = move(*head_->data_);
    head_->data_.reset();
    return result;
  }

 private:
  struct node {
    node() : next_(nullptr) {}
    template <class... Args>
    node(Args&&... args) : data_(forward<Args>(args)...), next_(nullptr) {}

    optional<T> data_;
    atomic<node*> next_;
  };

  node* head_;
  atomic<node*> tail_;
};

template <class R>
class mediator {
 public:
  template <class F, class... Args>
  explicit mediator(F&& f, Args&&... args)
      : value_(f(forward<Args>(args)...)) {}

  template <class F>
  auto operator()(F&& f) { return f(forward<R>(value_)); }

 private:
  R value_;
};

template <>
class mediator<void> {
 public:
  template <class F, class... Args>
  explicit mediator(F&& f, Args&&... args) { f(forward<Args>(args)...); }

  template <class F>
  auto operator()(F&& f) { return f(); }
};

}

template <bool DAEMON = false>
struct thread_executor {
  template <class F>
  void operator()(F&& f) const {
    static wang::thread_hub hub;
    hub.attach();
    thread([f = forward<F>(f)] {
      f();
      if (hub.detach()) {
        hub.sem_.set_value();
      }
    }).detach();
  }
};

template <>
struct thread_executor<true> {
  template <class F>
  void operator()(F&& f) const {
    thread(forward<F>(f)).detach();
  }
};

template <class E>
class async_mutex {
  using task_type = value_proxy<Callable<void(async_mutex&)>>;

 public:
  template <class _E>
  explicit async_mutex(_E&& executor)
      : pending_(0u), executor_(forward<_E>(executor)) {}

  template <class CS, class CB>
  void attach(CS&& cs, CB&& cb) {
    queue_.emplace([cs = forward<CS>(cs), cb = forward<CB>(cb)](
        async_mutex& mtx) mutable {
      mtx.executor_([cs = forward<CS>(cs), cb = forward<CB>(cb), &mtx]()
          mutable {
        wang::mediator<decltype(cs())> med(cs);
        mtx.release();
        med(cb);
      });
    });
    if (pending_.fetch_add(1, memory_order_relaxed) == 0u) {
      atomic_thread_fence(memory_order_acquire);
      queue_.pop()(*this);
    }
  }

 private:
  void release() {
    if (pending_.fetch_sub(1, memory_order_release) != 1u) {
      queue_.pop()(*this);
    }
  }

  wang::mutex_queue<task_type> queue_;
  atomic_size_t pending_;
  E executor_;
};

template <class F = value_proxy<Callable<void()>>>
class thread_pool {
 public:
  template <class Executor = thread_executor<>>
  explicit thread_pool(size_t thread_count,
                       const Executor& executor = Executor())
      : data_(make_shared<data_type>()) {
    for (size_t i = 0; i < thread_count; ++i) {
      executor([data = data_] {
        unique_lock<mutex> lk(data->mtx_);
        for (;;) {
          if (!data->tasks_.empty()) {
            F current = move(data->tasks_.front());
            data->tasks_.pop();
            lk.unlock();
            current();
            lk.lock();
          } else if (data->is_shutdown_) {
            break;
          } else {
            data->cond_.wait(lk);
          }
        }
      });
    }
  }

  ~thread_pool() {
    {
      lock_guard<mutex> lk(data_->mtx_);
      data_->is_shutdown_ = true;
    }
    data_->cond_.notify_all();
  }

  auto executor() const {
    return [&data = *data_](auto&& f) {
      {
        lock_guard<mutex> lk(data.mtx_);
        data.tasks_.emplace(forward<decltype(f)>(f));
      }
      data.cond_.notify_one();
    };
  }

 private:
  struct data_type {
    mutex mtx_;
    condition_variable cond_;
    bool is_shutdown_ = false;
    queue<F> tasks_;
  };
  shared_ptr<data_type> data_;
};

template <class F = value_proxy<
    Callable<optional<pair<chrono::time_point<chrono::system_clock>, self>>()>>>
class timed_thread_pool {
  struct buffer_data;
  struct shared_data;
  struct task_type;

 public:
  template <class Executor = thread_executor<>>
  explicit timed_thread_pool(size_t count,
                             const Executor& executor = Executor())
      : data_(make_shared<shared_data>(count)) {
    for (size_t i = 0; i < count; ++i) {
      executor([data_ = data_, i] {
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
            shared.pending_.insert(buffer);
            while (buffer.cond_.wait_until(lk, buffer.task_->when_) ==
                cv_status::no_timeout) {
              if (shared.shutdown_) {
                return;
              }
            }
            shared.pending_.erase(buffer);
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

  ~timed_thread_pool() {
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

  auto executor() const {
    return [&shared = *data_](const auto& when, auto&&... args) {
      buffer_data* buffer_ptr;
      {
        lock_guard<mutex> lk(shared.mtx_);
        if (shared.idle_.empty()) {
          auto it = shared.pending_.begin();
          if (it != shared.pending_.end() && when < it->get().task_->when_) {
            buffer_ptr = &it->get();
            shared.pending_.erase(it);
            shared.tasks_.emplace(move(*buffer_ptr->task_));
          } else {
            shared.tasks_.emplace(when, forward<decltype(args)>(args)...);
            return;
          }
        } else {
          buffer_ptr = &shared.idle_.front().get();
          shared.idle_.pop();
        }
        buffer_ptr->task_.emplace(when, forward<decltype(args)>(args)...);
        shared.pending_.insert(*buffer_ptr);
      }
      buffer_ptr->cond_.notify_one();
    };
  }

 private:
  struct task_type {
    template <class Clock, class Duration, class... Args>
    explicit task_type(const chrono::time_point<Clock, Duration>& when,
        Args&&... args) : when_(when), what_(forward<Args>(args)...) {}

    task_type(task_type&& rhs) = default;
    task_type(const task_type& rhs)
        : when_(rhs.when_), what_(move(rhs.what_)) {}

    task_type& operator=(task_type&&) = default;

    chrono::time_point<chrono::system_clock> when_;
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

template <class E, class F>
class timed_circulation {
  struct shared_data;
  struct task_type;

 public:
  template <class _E, class _F>
  explicit timed_circulation(_E&& executor, _F&& functor)
      : executor_(forward<_E>(executor)),
        data_ptr_(make_shared<shared_data>(forward<_F>(functor))) {}

  timed_circulation(timed_circulation&&) = default;
  timed_circulation(const timed_circulation&) = delete;

  template <class Clock, class Duration>
  void trigger(const chrono::time_point<Clock, Duration>& when) const {
    shared_data& data = *data_ptr_;
    data.executor_(when, task_type(data_ptr_, data.advance_version()));
  }

  void trigger() const { trigger(chrono::system_clock::now()); }
  void suspend() const { data_ptr_->advance_version(); }

 private:
  struct shared_data {
    template <class _E, class _F>
    explicit shared_data(_F&& functor)
        : state_(0u), functor_(forward<_F>(functor)) {}

    uint64_t advance_version() {
      uint64_t s = state_.load(memory_order_relaxed), v;
      do {
        v = (s + 1) & IDLE_MASK;
      } while (!state_.compare_exchange_weak(
          s, (s & RUNNING_MASK) | v, memory_order_relaxed));
      return v;
    }

    atomic_uint64_t state_;
    F functor_;
  };

  struct task_type {
    explicit task_type(const shared_ptr<shared_data>& data_ptr,
        uint64_t version) : data_ptr_(data_ptr), version_(version) {}

    task_type(task_type&&) = default;
    task_type(const task_type&) = default;

    optional<pair<chrono::time_point<chrono::system_clock>, task_type>>
        operator()() {
      shared_data& data = *data_ptr_;
      uint64_t s = data.state_.load(memory_order_relaxed);
      for (;;) {
        if ((s & IDLE_MASK) != version_) {
          return nullopt;
        }
        if (s & RUNNING_MASK) {
          if (data.state_.compare_exchange_weak(
              s, (s & NO_RESERVATION_MASK) | (s << RESERVATION_OFFSET),
              memory_order_relaxed)) {
            return nullopt;
          }
        } else {
          if (data.state_.compare_exchange_weak(
              s, s | RUNNING_MASK, memory_order_relaxed)) {
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
          if ((s & NO_RESERVATION_MASK) == (s >> RESERVATION_OFFSET)) {
            if (data.state_.compare_exchange_weak(
                s, s & NO_RESERVATION_MASK, memory_order_relaxed)) {
              version_ = s & IDLE_MASK;
              break;
            }
          } else {
            if (data.state_.compare_exchange_weak(
                s, s & IDLE_MASK, memory_order_relaxed)) {
              if (version_ == (s & IDLE_MASK)) {
                if (gap.has_value()) {
                  return make_pair(chrono::system_clock::now() + gap.value(),
                                   move(*this));
                }
              }
              return nullopt;
            }
          }
        }
      }
    }

    shared_ptr<shared_data> data_ptr_;
    uint64_t version_;
  };

  const E executor_;
  shared_ptr<shared_data> data_ptr_;

  static constexpr int RESERVATION_OFFSET = 32;
  static constexpr uint64_t IDLE_MASK = 0x000000007FFFFFFFULL;
  static constexpr uint64_t RUNNING_MASK = 0x0000000080000000ULL;
  static constexpr uint64_t NO_RESERVATION_MASK = IDLE_MASK | RUNNING_MASK;
};

template <class E, class F>
auto make_timed_circulation(E&& executor, F&& functor) {
  return timed_circulation<decay_t<E>, decay_t<F>>(
      forward<E>(executor), forward<F>(functor));
}

}  // namespace std

#endif  // SRC_MAIN_EXPERIMENTAL_CONCURRENT_H_
