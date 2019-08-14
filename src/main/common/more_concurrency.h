/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_CONCURRENCY_H_
#define SRC_MAIN_COMMON_MORE_CONCURRENCY_H_

#include <utility>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <atomic>

#include "../p1648/extended.h"

namespace aid {

class thread_executor {
 public:
  template <class F>
  void execute(F&& f) const {
    std::thread th{std::forward<F>(f)};
    store& s = get_store();
    std::lock_guard<std::mutex> lk{s.mtx_};
    s.q_.emplace_back(std::move(th));
  }

 private:
  struct store {
    ~store() {
      std::deque<std::thread> q;
      for (;;) {
        {
          std::lock_guard<std::mutex> lk{mtx_};
          std::swap(q, q_);
        }
        if (q.empty()) {
          break;
        }
        do {
          std::thread th = std::move(q.front());
          q.pop_front();
          th.join();
        } while (!q.empty());
      }
    }

    std::deque<std::thread> q_;
    std::mutex mtx_;
  };
  static inline store& get_store() {
    static store s;
    return s;
  }
};

template <class T>
class concurrent_collector {
 public:
  concurrent_collector() : last_(&sentinel_) {}
  ~concurrent_collector() {
    node* current;
    while ((current = sentinel_.next_) != nullptr) {
      sentinel_.next_ = current->next_;
      delete current;
    }
  }

  template <class U>
  void push(U&& value) {
    value_node* current = new value_node(std::forward<U>(value));
    node* last = last_.exchange(current, std::memory_order_relaxed);
    last->next_ = current;
  }

  bool empty() const { return sentinel_.next_ == nullptr; }

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
        : value_(std::p1648::make_extended(std::forward<U>(value))) {}

    T value_;
  };

  node sentinel_;
  std::atomic<node*> last_;
};

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_CONCURRENCY_H_
