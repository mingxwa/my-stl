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

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p1230/placeholders.h"
#include "../p0957/mock/proxy_callable_optional_pair_t_recursive_placeholder_0_impl.h"

namespace std {

template <class T, class CB>
class concurrent_context : public wang::wrapper<T> {
 public:
  template <class _CB, class... Args>
  explicit concurrent_context(size_t count, _CB&& callback, Args&&... args)
      : wang::wrapper<T>(false_type{}, forward<Args>(args)...),
        count_(count), callback_(forward<_CB>(callback)) {}

  void fork(size_t count) const {
    count_.fetch_add(count, memory_order_relaxed);
  }

  void join() const {
    if (count_.fetch_sub(1u, memory_order_release) == 1u) {
      atomic_thread_fence(memory_order_acquire);
      concurrent_context* current = const_cast<concurrent_context*>(this);
      invoke(forward<CB>(current->callback_), *current);
    }
  }

 private:
  mutable atomic_size_t count_;
  CB callback_;
};

class binary_semaphore {
 public:
  void acquire() { prom_.get_future().wait(); }

  void release() { prom_.set_value(); }

 private:
  std::promise<void> prom_;
};

template <class BS>
class sync_concurrent_callback {
 public:
  explicit sync_concurrent_callback(BS& semaphore) : semaphore_(semaphore) {}
  sync_concurrent_callback(const sync_concurrent_callback&) = default;

  template <class T, class CB>
  T operator()(concurrent_context<T, CB>& context) {
    semaphore_.release();
    return context.get();
  }

 private:
  BS& semaphore_;
};

template <class CB, class MA>
class async_concurrent_callback : private wang::wrapper<MA> {
 public:
  template <class _CB, class _MA>
  explicit async_concurrent_callback(_CB&& callback, _MA&& ma)
      : wang::wrapper<MA>(forward<_MA>(ma)),
        callback_(forward<_CB>(callback)) {}

  async_concurrent_callback(async_concurrent_callback&&) = default;
  async_concurrent_callback(const async_concurrent_callback&) = default;

  template <class T, class _CB>
  void operator()(concurrent_context<T, _CB>& context) {
    context.consume(forward<CB>(callback_));
    wang::destroy(this->get(), &context);
  }

 private:
  CB callback_;
};

template <class T, class CB, class MA, class... Args>
auto* make_async_concurrent_context(size_t count, CB&& callback,
    true_type, MA&& ma, Args&&... args) {
  using ACB = async_concurrent_callback<decay_t<CB>, decay_t<MA>>;
  return wang::construct<concurrent_context<ACB, T>>(
      forward<MA>(ma), count, ACB(ma, forward<CB>(callback)),
      forward<Args>(args)...);
}

template <class T, class CB, class... Args>
auto* make_async_concurrent_context(size_t count, CB&& callback,
    false_type, Args&&... args) {
  return make_async_concurrent_context(count, forward<CB>(callback),
      true_type{}, memory_allocator{}, forward<Args>(args)...);
}

template <class T, class CB, class... Args>
auto* make_async_concurrent_context(size_t count, CB&& callback,
    Args&&... args) {
  return make_async_concurrent_context(count, forward<CB>(callback),
      false_type{}, forward<Args>(args)...);
}

namespace wang {

template <class T>
class concurrent_callback_proxy {
  using context = concurrent_context<T, concurrent_callback_proxy>;

 public:
  template <class CB>
  concurrent_callback_proxy(CB&& callback) : callback_(forward<CB>(callback)) {}

  void operator()(concurrent_context<T, concurrent_callback_proxy>& c)
      { invoke(move(callback_), c); }

 private:
  value_proxy<Callable<void(context&)>> callback_;
};

}  // namespace wang

template <class, class, class, class>
class concurrent_invoker;

template <class T, class CB = wang::concurrent_callback_proxy<T>>
class concurrent_breakpoint {
  template <class, class, class, class>
  friend class concurrent_invoker;

 public:
  concurrent_breakpoint(concurrent_breakpoint&& rhs)
      : context_(rhs.context_) { rhs.context_ = nullptr; }

  ~concurrent_breakpoint() {
    if (context_ != nullptr) {
      context_->join();
    }
  }

  auto context() { return context_->get(); }

  explicit operator bool() const noexcept { return context_; }

 private:
  explicit concurrent_breakpoint(const concurrent_context<T, CB>* context)
      : context_(context) {}

  const concurrent_context<T, CB>* context_;
};

template <class T, class CB = wang::concurrent_callback_proxy<T>,
    class Proc = value_proxy<Callable<void(concurrent_breakpoint<T, CB>)>>,
    class Container = vector<Proc>>
class concurrent_invoker {
  using context = concurrent_context<T, CB>;
  using breakpoint = concurrent_breakpoint<T, CB>;

 public:
  template <class E, class F>
  void add(E&& executor, F&& f) {
    add([executor = forward<E>(executor),
        f = decorator<decay_t<F>>::wrap(forward<F>(f))](
        breakpoint&& bp) mutable {
      executor([f = move(f), bp = move(bp)]() mutable {
        invoke(move(f), move(bp));
      });
    });
  }

  template <class _Proc>
  void add(_Proc&& proc) {
    container_.emplace_back(forward<_Proc>(proc));
  }

  template <class BS, class... Args>
  T sync_invoke(BS& semaphore, Args&&... args) {
    context c(container_.size(), sync_concurrent_callback(semaphore),
        forward<Args>(args)...);
    do_invoke(&c);
    semaphore.acquire();
    return c.get();
  }

  template <class... Args>
  T sync_invoke(Args&&... args) {
    binary_semaphore sem;
    return sync_invoke(sem, forward<Args>(args)...);
  }

  template <class _CB, class MA, class... Args>
  void async_invoke(_CB&& callback, true_type, MA&& ma, Args&&... args) {
    do_invoke(wang::construct<context>(forward<MA>(ma), container_.size(),
        async_concurrent_callback<decay_t<_CB>, decay_t<MA>>(
        forward<_CB>(callback), ma), forward<Args>(args)...));
  }

  template <class _CB, class... Args>
  void async_invoke(_CB&& callback, false_type, Args&&... args) {
    async_invoke(forward<_CB>(callback), true_type{}, memory_allocator{},
        forward<Args>(args)...);
  }

  template <class _CB, class... Args>
  void async_invoke(_CB&& callback, Args&&... args) {
    async_invoke(forward<_CB>(callback), false_type{}, forward<Args>(args)...);
  }

  void fork(breakpoint& bp) {
    bp.context_->fork(container_.size());
    do_invoke(bp.context_);
  }

 private:
  void do_invoke(const context* c) {
    for (Proc& proc : container_) {
      invoke(forward<Proc>(proc), breakpoint(c));
    }
  }

  template <class F, bool BREAKPOINT_CONSUMER = is_invocable_v<F, breakpoint>,
      bool CONTEXT_CONSUMER = is_invocable_v<F, const add_lvalue_reference<T>>>
  struct decorator;

  template <class F>
  struct decorator<F, true, false> {
    static inline F&& wrap(F&& f) { return forward<F>(f); }
  };

  template <class F>
  struct decorator<F, false, true> {
    static inline auto wrap(F&& f) {
      return [f = forward<F>(f)](breakpoint&& bp) mutable {
        invoke(forward<F>(f), bp.context());
      };
    }
  };

  template <class F>
  struct decorator<F, false, false> {
    static inline auto wrap(F&& f) {
      return [f = forward<F>(f)](breakpoint&&) mutable {
        invoke(forward<F>(f));
      };
    }
  };

  Container container_;
};

namespace wang {

struct thread_hub {
  thread_hub() : context_(1u, sem_) {}

  ~thread_hub() {
    context_.join();
    sem_.acquire();
  }

  void attach() { context_.fork(1u); }
  void detach() { context_.join(); }

 private:
  binary_semaphore sem_;
  concurrent_context<void, sync_concurrent_callback<binary_semaphore>> context_;
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

}  // namespace wang

template <bool DAEMON = false>
struct thread_executor {
  template <class F>
  void operator()(F&& f) const {
    static wang::thread_hub hub;
    hub.attach();
    thread([f = forward<F>(f)]() mutable {
      invoke(forward<F>(f));
      hub.detach();
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
        wang::wrapper<decltype(invoke(forward<CS>(cs)))>
            res(true_type{}, forward<CS>(cs));
        mtx.release();
        res.consume(forward<CB>(cb));
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
  template <class E = thread_executor<>>
  explicit thread_pool(size_t thread_count, const E& executor = E())
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

template <class Clock = chrono::high_resolution_clock,
    class Duration = typename Clock::duration,
    class F = value_proxy<Callable<optional<
        pair<chrono::time_point<Clock, Duration>,
        p1230::recursive_placeholder_0>>()>>>
class timed_thread_pool {
  struct buffer_data;
  struct shared_data;
  struct task_type;

 public:
  template <class E = thread_executor<>>
  explicit timed_thread_pool(size_t count,
                             const E& executor = E())
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
    return [&data = *data_](const chrono::time_point<Clock, Duration>& when,
                            auto&& f) {
      buffer_data* buffer_ptr;
      {
        lock_guard<mutex> lk(data.mtx_);
        if (data.idle_.empty()) {
          auto it = data.pending_.begin();
          if (it != data.pending_.end() && when < it->get().task_->when_) {
            buffer_ptr = &it->get();
            data.pending_.erase(it);
            data.tasks_.emplace(*buffer_ptr->task_);
            buffer_ptr->task_.emplace(when, forward<decltype(f)>(f));
            data.pending_.insert(*buffer_ptr);
          } else {
            data.tasks_.emplace(when, forward<decltype(f)>(f));
            return;
          }
        } else {
          buffer_ptr = &data.idle_.front().get();
          data.idle_.pop();
          buffer_ptr->task_.emplace(when, forward<decltype(f)>(f));
        }
      }
      buffer_ptr->cond_.notify_one();
    };
  }

 private:
  struct task_type {
    template <class _F>
    explicit task_type(const chrono::time_point<Clock, Duration>& when, _F&& f)
        : when_(when), what_(forward<_F>(f)) {}

    task_type(const task_type& rhs)
        : when_(rhs.when_), what_(move(rhs.what_)) {}

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

namespace wang {

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

}  // namespace wang

template <class Clock, class Duration, class E, class F>
class circulation_trigger {
 public:
  template <class _E, class _F>
  explicit circulation_trigger(_E&& executor, _F&& functor)
      : executor_(forward<_E>(executor)), data_ptr_(
      make_shared<wang::circulation_data<F>>(forward<_F>(functor))) {}

  circulation_trigger(circulation_trigger&&) = default;
  circulation_trigger(const circulation_trigger&) = delete;

  template <class Rep, class Period>
  void fire(const chrono::duration<Rep, Period>& delay) const {
    wang::circulation_data<F>& data = *data_ptr_;
    executor_(Clock::now() + delay,
        wang::timed_circulation<Clock, Duration, F>(
        data_ptr_, data.advance_version()));
  }

  void fire() const { fire(chrono::duration<int>::zero()); }
  void suspend() const { data_ptr_->advance_version(); }

 private:
  const E executor_;
  shared_ptr<wang::circulation_data<F>> data_ptr_;
};

template <class Clock = chrono::high_resolution_clock,
    class Duration = typename Clock::duration, class E, class F>
auto make_timed_circulation(E&& executor, F&& functor) {
  return circulation_trigger<Clock, Duration, decay_t<E>, decay_t<F>>(
      forward<E>(executor), forward<F>(functor));
}

}  // namespace std

#endif  // SRC_MAIN_EXPERIMENTAL_CONCURRENT_H_
