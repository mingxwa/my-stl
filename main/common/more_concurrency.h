/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_CONCURRENCY_H_
#define SRC_MAIN_COMMON_MORE_CONCURRENCY_H_

#include <utility>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

#include "../p1648/sinking.h"

namespace aid {

namespace detail {

class global_concurrency_manager {
 public:
  static inline global_concurrency_manager* instance() {
    static global_concurrency_manager s;
    return &s;
  }

  void increase(std::size_t count)
      { concurrency_.fetch_add(count, std::memory_order_relaxed); }

  void decrease() {
    std::atomic_thread_fence(std::memory_order_release);
    std::size_t concurrency = concurrency_.load(std::memory_order_relaxed);
    do {
      if (concurrency == 1u) {
        {
          std::lock_guard<std::mutex> lk{mtx_};
          finished_ = true;
        }
        cond_.notify_one();
        concurrency_.store(0u, std::memory_order_release);
        break;
      }
    } while (!concurrency_.compare_exchange_weak(
        concurrency, concurrency - 1u, std::memory_order_relaxed));
  }

 private:
  global_concurrency_manager() : concurrency_(1u), finished_(false) {}

  ~global_concurrency_manager() {
    if (concurrency_.fetch_sub(1u, std::memory_order_relaxed) != 1u) {
      {
        std::unique_lock<std::mutex> lk{mtx_};
        cond_.wait(lk, [=] { return finished_; });
      }
      while (concurrency_.load(std::memory_order_relaxed) != 0u)
          { std::this_thread::yield(); }
    }
    std::atomic_thread_fence(std::memory_order_acquire);
  }

  std::atomic_size_t concurrency_;
  bool finished_;
  std::mutex mtx_;
  std::condition_variable cond_;
};

}  // namespace detail

inline void increase_global_concurrency(std::size_t count) {
  detail::global_concurrency_manager::instance()->increase(count);
}

inline void decrease_global_concurrency() {
  detail::global_concurrency_manager::instance()->decrease();
}

struct thread_executor {
  template <class F>
  void execute(F&& f) const {
    increase_global_concurrency(1u);
    std::thread{[f = std::forward<F>(f)]() mutable {
      std::invoke(std::move(f));
      decrease_global_concurrency();
    }}.detach();
  }
};

struct in_place_executor {
  template <class F>
  void execute(F&& f) const { std::invoke(std::forward<F>(f)); }
};

template <class T>
class concurrent_collector {
 public:
  concurrent_collector() : last_(&sentinel_) {}
  ~concurrent_collector()
      { if (sentinel_.next_ != nullptr) { std::terminate(); } }

  template <class U>
  void push(U&& value) {
    value_node* current = new value_node(std::forward<U>(value));
    node* last = last_.exchange(current, std::memory_order_relaxed);
    last->next_ = current;
  }

  template <class C = std::vector<T>>
  C reduce(C container = C{}) {
    node* current;
    while ((current = sentinel_.next_) != nullptr) {
      container.emplace_back(std::move(
          static_cast<value_node*>(current)->value_));
      sentinel_.next_ = current->next_;
      delete current;
    }
    std::atomic_init(&last_, &sentinel_);
    return container;
  }

 private:
  struct value_node;
  struct node { value_node* next_ = nullptr; };

  struct value_node : node {
    template <class U>
    explicit value_node(U&& value)
        : value_(std::p1648::sink(std::forward<U>(value))) {}

    T value_;
  };

  node sentinel_;
  std::atomic<node*> last_;
};

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_CONCURRENCY_H_
