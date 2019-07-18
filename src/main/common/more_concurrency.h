/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_COMMON_MORE_CONCURRENCY_H_
#define SRC_MAIN_COMMON_MORE_CONCURRENCY_H_

#include <utility>
#include <atomic>

#include "../p1648/extended.h"

namespace aid {

template <class T>
class concurrent_collector {
 public:
  concurrent_collector() : last_(&sentinel_) {}
  ~concurrent_collector() { destroy(sentinel_.next_); }

  template <class U>
  void push(U&& value) const {
    value_node* current = new value_node(std::forward<U>(value));
    node* last = last_.exchange(current, std::memory_order_relaxed);
    last->next_ = current;
  }

  bool empty() const { return sentinel_.next_ == nullptr; }

  template <class C>
  void collect(C& container) const {
    node* current = sentinel_.next_;
    while (current != nullptr) {
      container.push_back(static_cast<value_node*>(current)->value_);
      current = current->next_;
    }
  }

 private:
  struct value_node;
  struct node { value_node* next_ = nullptr; };

  struct value_node : node {
    template <class U>
    explicit value_node(U&& value)
        : value_(std::make_extended(std::forward<U>(value))) {}

    T value_;
  };

  static inline void destroy(value_node* p) {
    if (p != nullptr) {
      destroy(p->next_);
      delete p;
    }
  }

  node sentinel_;
  mutable std::atomic<node*> last_;
};

}  // namespace aid

#endif  // SRC_MAIN_COMMON_MORE_CONCURRENCY_H_
