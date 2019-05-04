/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_ASYNC_MUTEX_H_
#define SRC_MAIN_EXPERIMENTAL_ASYNC_MUTEX_H_

#include <utility>
#include <atomic>
#include <optional>

#include "../common/more_utility.h"
#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"

namespace std {

namespace detail {

template <class T, class MA>
class mutex_queue : private aid::wrapper<MA> {
 public:
  template <class _MA>
  explicit mutex_queue(_MA&& ma) : aid::wrapper<MA>(forward<_MA>(ma)),
      head_(aid::construct<node>(this->get())), tail_(head_) {}

  ~mutex_queue() { aid::destroy(this->get(), head_); }

  template <class... Args>
  void emplace(Args&&... args) {
    node* current = aid::construct<node>(this->get(), forward<Args>(args)...);
    node* desired;
    do {
      desired = nullptr;
    } while (!tail_.load(memory_order_relaxed)->next_
        .compare_exchange_weak(desired, current, memory_order_relaxed));
    tail_.store(current, memory_order_relaxed);
  }

  T pop() {
    node* next_head = head_->next_.load(memory_order_relaxed);
    aid::destroy(this->get(), head_);
    head_ = next_head;
    T result = move(*head_->data_);
    head_->data_.reset();
    return result;
  }

 private:
  struct node {
    node() : next_(nullptr) {}
    template <class... Args>
    explicit node(Args&&... args) : data_(forward<Args>(args)...),
        next_(nullptr) {}

    optional<T> data_;
    atomic<node*> next_;
  };

  node* head_;
  atomic<node*> tail_;
};

}  // namespace detail

template <class E, class F, class MA>
class async_mutex;

namespace detail {

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

}  // namespace detail

template <class E, class MA = memory_allocator,
    class F = detail::async_mutex_procedure_proxy<E, MA>>
class async_mutex {
 public:
  template <class _E, class _MA>
  explicit async_mutex(_E&& executor, _MA&& ma) : pending_(0u),
      executor_(forward<_E>(executor)), queue_(forward<_MA>(ma)) {}

  template <class _E>
  explicit async_mutex(_E&& executor)
      : async_mutex(forward<_E>(executor), MA{}) {}

  template <class CS>
  void attach(CS&& cs) const { attach(forward<CS>(cs), [] {}); }

  template <class CS, class CB>
  void attach(CS&& cs, CB&& cb) const {
    queue_.emplace([cs = forward<CS>(cs), cb = forward<CB>(cb)](
        const async_mutex& mtx) mutable {
      mtx.executor_.execute([cs = forward<CS>(cs), cb = forward<CB>(cb), &mtx]()
          mutable {
        auto res = aid::make_wrapper_from_callable(forward<CS>(cs));
        mtx.release();
        aid::invoke_contextual(forward<CB>(cb), move(res));
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
  mutable detail::mutex_queue<F, MA> queue_;
};

}  // namespace std

#endif  // SRC_MAIN_EXPERIMENTAL_ASYNC_MUTEX_H_
