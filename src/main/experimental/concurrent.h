/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_CONCURRENT_H_
#define SRC_MAIN_EXPERIMENTAL_CONCURRENT_H_

#include <queue>
#include <vector>
#include <set>
#include <functional>
#include <utility>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <optional>

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"

namespace std {

template <class T, class CCB>
class concurrent_context : public wang::wrapper<T> {
 public:
  template <class _CCB, class... Args>
  explicit concurrent_context(size_t count, _CCB&& callback, Args&&... args)
      : wang::wrapper<T>(forward<Args>(args)...),
        callback_(forward<_CCB>(callback)), count_(count) {}

  void fork(size_t count) const {
    count_.fetch_add(count, memory_order_relaxed);
  }

  void join() const {
    if (count_.fetch_sub(1u, memory_order_release) == 1u) {
      atomic_thread_fence(memory_order_acquire);
      concurrent_context* current = const_cast<concurrent_context*>(this);
      invoke(forward<CCB>(current->callback_), move(*current));
    }
  }

 private:
  CCB callback_;
  mutable atomic_size_t count_;
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

  sync_concurrent_callback(sync_concurrent_callback&&) = default;
  sync_concurrent_callback& operator=(sync_concurrent_callback&&) = default;

  template <class T, class CCB>
  void operator()(concurrent_context<T, CCB>&&) { semaphore_.release(); }

 private:
  BS& semaphore_;
};

template <class BS>
auto make_sync_concurrent_callback(BS& semaphore) {
  return sync_concurrent_callback<BS>(semaphore);
}

namespace wang {

enum class callback_type { context, plain, unknown };

template <class F, class T, callback_type>
struct callback_invoker;

template <class F, class T>
struct callback_invoker<F, T, callback_type::context> {
  static inline void apply(F&& f, wrapper<T>&& w)
      { invoke(forward<F>(f), w.get()); }
};

template <class F, class T>
struct callback_invoker<F, T, callback_type::plain> {
  static inline void apply(F&& f, wrapper<T>&&)
      { invoke(forward<F>(f)); }
};

template <class F, class T>
constexpr callback_type get_callback_type() {
  if (is_void_v<T>) {
    return is_invocable_v<F> ? callback_type::plain : callback_type::unknown;
  }
  int match_count = 0;
  callback_type result = callback_type::unknown;
  if (is_invocable_v<F, add_rvalue_reference_t<T>>) {
    result = callback_type::context;
    ++match_count;
  }
  if (is_invocable_v<F>) {
    result = callback_type::plain;
    ++match_count;
  }
  return match_count == 1 ? result : callback_type::unknown;
}

template <class F, class T>
void invoke_callback(F&& f, wrapper<T>&& w) {
  callback_invoker<F, T, get_callback_type<F, T>()>::apply(
      forward<F>(f), move(w));
}

}  // namespace wang

template <class MA, class CB>
class async_concurrent_callback : private wang::wrapper<MA>,
    private wang::wrapper<CB> {
 public:
  template <class _MA, class _CB>
  explicit async_concurrent_callback(_MA&& ma, _CB&& callback)
      : wang::wrapper<MA>(forward<_MA>(ma)),
        wang::wrapper<CB>(forward<_CB>(callback)) {}

  async_concurrent_callback(async_concurrent_callback&&) = default;
  async_concurrent_callback& operator=(async_concurrent_callback&&) = default;

  template <class T, class CCB>
  void operator()(concurrent_context<T, CCB>&& context) {
    wang::invoke_callback(wang::wrapper<CB>::get(),
        static_cast<wang::wrapper<T>&&>(context));
    wang::destroy(wang::wrapper<MA>::get(), &context);
  }
};

template <class MA, class CB>
auto make_async_concurrent_callback(MA&& ma, CB&& callback) {
  return async_concurrent_callback<decay_t<MA>, decay_t<CB>>(
    forward<MA>(ma), forward<CB>(callback));
}

template <class MA, class T, class CCB, class CB>
class recursive_async_concurrent_callback : private wang::wrapper<MA>,
    private wang::wrapper<CB> {
 public:
  template <class _MA, class _CB>
  explicit recursive_async_concurrent_callback(
      _MA&& ma, const concurrent_context<T, CCB>& host, _CB&& callback)
      : wang::wrapper<MA>(forward<_MA>(ma)),
        wang::wrapper<CB>(forward<_CB>(callback)), host_(host) {}

  recursive_async_concurrent_callback(recursive_async_concurrent_callback&&)
      = default;
  recursive_async_concurrent_callback& operator=(
      recursive_async_concurrent_callback&&) = default;

  template <class _T, class _CCB>
  void operator()(concurrent_context<_T, _CCB>&& context) {
    wang::invoke_callback(wang::wrapper<CB>::get(),
        static_cast<wang::wrapper<_T>&&>(context));
    const concurrent_context<T, CCB>& host = host_;
    wang::destroy(wang::wrapper<MA>::get(), &context);
    host.join();
  }

 private:
  const concurrent_context<T, CCB>& host_;
};

template <class MA, class T, class CCB, class CB>
auto make_recursive_async_concurrent_callback(MA&& ma,
    const concurrent_context<T, CCB>& host, CB&& callback) {
  return recursive_async_concurrent_callback<decay_t<MA>, T, CCB, decay_t<CB>>(
    forward<MA>(ma), host, forward<CB>(callback));
}

namespace wang {

template <class T>
class concurrent_callback_proxy {
  using context = concurrent_context<T, concurrent_callback_proxy>;

 public:
  template <class F>
  concurrent_callback_proxy(F&& f) : f_(forward<F>(f)) {}
  concurrent_callback_proxy(concurrent_callback_proxy&&) = default;
  concurrent_callback_proxy& operator=(concurrent_callback_proxy&&) = default;
  void operator()(context&& c) { invoke(move(f_), move(c)); }

 private:
  value_proxy<Callable<void(context&&)>> f_;
};

}  // namespace wang

template <class, class, class, class>
class concurrent_invoker;

template <class T, class CCB = wang::concurrent_callback_proxy<T>>
class concurrent_token {
  template <class, class, class, class>
  friend class concurrent_invoker;

 public:
  concurrent_token(concurrent_token&& rhs) : context_(rhs.context_)
      { rhs.context_ = nullptr; }

  ~concurrent_token() {
    if (context_ != nullptr) {
      context_->join();
    }
  }

  add_lvalue_reference_t<const T> context() const { return context_->get(); }

  explicit operator bool() const noexcept { return context_; }

 private:
  explicit concurrent_token(const concurrent_context<T, CCB>& context)
      : context_(&context) {}

  const concurrent_context<T, CCB>* context_;
};

namespace wang {

enum class concurrent_callable_type { token, context, plain, unknown };

template <class F, class T, class CCB, concurrent_callable_type>
struct concurrent_callable_invoker;

template <class T, class CCB, bool Extractable>
struct concurrent_context_extractor;

template <class F, class T, class CCB>
constexpr concurrent_callable_type get_concurrent_callable_type() {
  int match_count = 0;
  concurrent_callable_type result = concurrent_callable_type::unknown;
  if (is_invocable_v<F, concurrent_token<T, CCB>>) {
    result = concurrent_callable_type::token;
    ++match_count;
  }
  if (is_invocable_v<F, add_lvalue_reference_t<const T>>) {
    result = concurrent_callable_type::context;
    ++match_count;
  }
  if (is_invocable_v<F>) {
    result = concurrent_callable_type::plain;
    ++match_count;
  }
  return match_count == 1 ? result : concurrent_callable_type::unknown;
}

template <class F, class T, class CCB>
struct concurrent_callable_invoker<F, T, CCB,
    concurrent_callable_type::token> {
  static inline void apply(F&& f, concurrent_token<T, CCB>&& t)
      { invoke(forward<F>(f), move(t)); }
};

template <class F, class T, class CCB>
struct concurrent_callable_invoker<F, T, CCB,
    concurrent_callable_type::context> {
  static inline void apply(F&& f, concurrent_token<T, CCB>&& t)
      { invoke(forward<F>(f), t.context()); }
};

template <class F, class T, class CCB>
struct concurrent_callable_invoker<F, T, CCB,
    concurrent_callable_type::plain> {
  static inline void apply(F&& f, concurrent_token<T, CCB>&&)
      { invoke(forward<F>(f)); }
};

template <class F, class T, class CCB>
void invoke_concurrent_callable(F&& f, concurrent_token<T, CCB>&& t) {
  concurrent_callable_invoker<F, T, CCB,
      get_concurrent_callable_type<F, T, CCB>()>::apply(forward<F>(f), move(t));
}

template <class T, class CCB>
struct concurrent_context_extractor<T, CCB, true> {
  static inline T apply(concurrent_context<T, CCB>&& c) { return c.get(); }
};

template <class T, class CCB>
struct concurrent_context_extractor<T, CCB, false> {
  static inline void apply(concurrent_context<T, CCB>&&) {}
};

template <class T, class CCB>
auto extract_concurrent_context(concurrent_context<T, CCB>&& c) {
  concurrent_context_extractor<T, CCB,
      is_move_constructible_v<T>>::apply(move(c));
}

}  // namespace wang

template <class T, class CCB = wang::concurrent_callback_proxy<T>,
    class Proc = value_proxy<Callable<void(concurrent_token<T, CCB>)>>,
    class Container = vector<Proc>>
class concurrent_invoker {
  using context = concurrent_context<T, CCB>;
  using token = concurrent_token<T, CCB>;

 public:
  template <class... Args>
  explicit concurrent_invoker(Args&&... args)
      : container_(forward<Args>(args)...) {}

  template <class U, class... Args>
  explicit concurrent_invoker(initializer_list<U> il, Args&&... args)
      : container_(il, forward<Args>(args)...) {}

  template <class E, class F>
  void attach(E&& executor, F&& f) {
    attach([executor = forward<E>(executor), f = forward<F>(f)](token&& t)
        mutable {
      invoke(forward<E>(executor), [f = forward<F>(f), t = move(t)]() mutable
          { wang::invoke_concurrent_callable(forward<F>(f), move(t)); });
    });
  }

  template <class _Proc>
  void attach(_Proc&& proc) { container_.emplace_back(forward<_Proc>(proc)); }

  template <class... Args>
  auto sync_invoke(Args&&... args) {
    binary_semaphore sem;
    return sync_invoke_explicit(sem, forward<Args>(args)...);
  }

  template <class BS, class... Args>
  auto sync_invoke_explicit(BS& semaphore, Args&&... args) {
    context c(container_.size(), make_sync_concurrent_callback(semaphore),
        forward<Args>(args)...);
    call(c);
    semaphore.acquire();
    return wang::extract_concurrent_context(move(c));
  }

  template <class CB, class... Args>
  void async_invoke(CB&& callback, Args&&... args) {
    async_invoke_explicit(memory_allocator{}, forward<CB>(callback),
        forward<Args>(args)...);
  }

  template <class MA, class CB, class... Args>
  void async_invoke_explicit(MA&& ma, CB&& callback, Args&&... args) {
    call(*wang::construct<context>(forward<MA>(ma), container_.size(),
        make_async_concurrent_callback(ma, forward<CB>(callback)),
        forward<Args>(args)...));
  }

  template <class _T, class _CCB, class CB, class... Args>
  void recursive_async_invoke(concurrent_token<_T, _CCB>&& t, CB&& callback,
      Args&&... args) {
    recursive_async_invoke_explicit(memory_allocator{}, move(t),
        forward<CB>(callback), forward<Args>(args)...);
  }

  template <class MA, class _T, class _CCB, class CB, class... Args>
  void recursive_async_invoke_explicit(MA&& ma, concurrent_token<_T, _CCB>&& t,
      CB&& callback, Args&&... args) {
    call(*wang::construct<context>(forward<MA>(ma), container_.size(),
        make_recursive_async_concurrent_callback(ma, *t.context_,
        forward<CB>(callback)), forward<Args>(args)...));
    t.context_ = nullptr;
  }

  void fork(token& t) {
    t.context_->fork(container_.size());
    call(t.context_);
  }

 private:
  void call(const context& c) {
    for (Proc& proc : container_) {
      invoke(forward<Proc>(proc), token(c));
    }
  }

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

template <class T, class MA>
class mutex_queue : private wang::wrapper<MA> {
 public:
  template <class _MA>
  mutex_queue(_MA&& ma) : wang::wrapper<MA>(forward<_MA>(ma)),
      head_(wang::construct<node>(this->get())), tail_(head_) {}

  ~mutex_queue() { wang::destroy(this->get(), head_); }

  template <class... Args>
  void emplace(Args&&... args) {
    node* current = wang::construct<node>(this->get(), forward<Args>(args)...);
    node* desired;
    do {
      desired = nullptr;
    } while (!tail_.load(memory_order_relaxed)->next_
        .compare_exchange_weak(desired, current, memory_order_relaxed));
    tail_.store(current, memory_order_relaxed);
  }

  T pop() {
    node* next_head = head_->next_.load(memory_order_relaxed);
    wang::destroy(this->get(), head_);
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
  void operator()(F&& f) const { thread(forward<F>(f)).detach(); }
};

template <class E, class F, class MA>
class async_mutex;

namespace wang {

template <class E, class MA>
class async_mutex_procedure_proxy {
  using mutex = async_mutex<E, MA, async_mutex_procedure_proxy>;

 public:
  template <class F>
  async_mutex_procedure_proxy(F&& f) : f_(forward<F>(f)) {}
  async_mutex_procedure_proxy(async_mutex_procedure_proxy&&) = default;
  async_mutex_procedure_proxy& operator=(async_mutex_procedure_proxy&&)
      = default;
  void operator()(const mutex& mtx) { invoke(move(f_), mtx); }

 private:
  value_proxy<Callable<void(const mutex&)>> f_;
};

template <class CS, bool ResultVoid>
struct critical_section_invoker;

template <class CS>
struct critical_section_invoker<CS, true> {
  static inline wrapper<void> apply(CS&& cs)
      { invoke(forward<CS>(cs)); return {}; }
};

template <class CS>
struct critical_section_invoker<CS, false> {
  static inline wrapper<decltype(invoke(declval<CS>()))> apply(CS&& cs)
      { return invoke(forward<CS>(cs)); }
};

template <class CS>
auto invoke_critical_section(CS&& cs) {
  return critical_section_invoker<CS, is_void_v<
      decltype(invoke(forward<CS>(cs)))>>::apply(forward<CS>(cs));
}

}  // namespace wang

template <class E, class MA = memory_allocator,
    class F = wang::async_mutex_procedure_proxy<E, MA>>
class async_mutex {
 public:
  template <class _E, class _MA>
  explicit async_mutex(_E&& executor, _MA&& ma) : pending_(0u),
      executor_(forward<_E>(executor)), queue_(forward<_MA>(ma)) {}

  template <class _E>
  explicit async_mutex(_E&& executor)
      : async_mutex(forward<_E>(executor), MA{}) {}

  template <class CS>
  void attach(CS&& cs) const { attach(forward<CS>(cs), [](auto&&...) {}); }

  template <class CS, class CB>
  void attach(CS&& cs, CB&& cb) const {
    queue_.emplace([cs = forward<CS>(cs), cb = forward<CB>(cb)](
        const async_mutex& mtx) mutable {
      mtx.executor_([cs = forward<CS>(cs), cb = forward<CB>(cb), &mtx]()
          mutable {
        auto res = wang::invoke_critical_section(forward<CS>(cs));
        mtx.release();
        wang::invoke_callback(forward<CB>(cb), move(res));
      });
    });
    if (pending_.fetch_add(1, memory_order_relaxed) == 0u) {
      atomic_thread_fence(memory_order_acquire);
      queue_.pop()(*this);
    }
  }

 private:
  void release() const {
    if (pending_.fetch_sub(1, memory_order_release) != 1u) {
      queue_.pop()(*this);
    }
  }

  mutable atomic_size_t pending_;
  E executor_;
  mutable wang::mutex_queue<F, MA> queue_;
};

template <class F = value_proxy<Callable<void()>>>
class thread_pool {
  struct data_type;

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

  class executor_type {
   public:
    explicit executor_type(data_type* data) : data_(data) {}
    executor_type(const executor_type&) = default;

    template <class _F>
    void operator()(_F&& f) const {
      {
        lock_guard<mutex> lk(data_->mtx_);
        data_->tasks_.emplace(forward<_F>(f));
      }
      data_->cond_.notify_one();
    }

   private:
    data_type* const data_;
  };

  executor_type executor() const { return executor_type(data_.get()); }

 private:
  struct data_type {
    mutex mtx_;
    condition_variable cond_;
    bool is_shutdown_ = false;
    queue<F> tasks_;
  };

  shared_ptr<data_type> data_;
};

namespace wang {

template <class Clock, class Duration>
class timed_task_proxy {
  using return_type = optional<pair<chrono::time_point<Clock, Duration>,
      timed_task_proxy>>;

 public:
  template <class F>
  timed_task_proxy(F&& f) : f_(forward<F>(f)) {}
  timed_task_proxy(timed_task_proxy&&) = default;
  timed_task_proxy& operator=(timed_task_proxy&&) = default;
  return_type operator()() { return invoke(move(f_)); }

 private:
  value_proxy<Callable<return_type()>> f_;
};

}  // namespace wang

template <class Clock = chrono::high_resolution_clock,
    class Duration = typename Clock::duration,
    class F = wang::timed_task_proxy<Clock, Duration>>
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

  class executor_type {
   public:
    explicit executor_type(shared_data* data) : data_(data) {}
    executor_type(const executor_type&) = default;

    template <class _F>
    void operator()(const chrono::time_point<Clock, Duration>& when, _F&& f)
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
