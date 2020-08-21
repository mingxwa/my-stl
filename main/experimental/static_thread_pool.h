/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_EXPERIMENTAL_STATIC_THREAD_POOL_H_
#define SRC_MAIN_EXPERIMENTAL_STATIC_THREAD_POOL_H_

#include <utility>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_nothrow_callable_impl.h"
#include "./nullary_callable_invoking_consumer.h"

namespace std::experimental {

class polymorphic_serial_callable_event {
 public:
  template <class F>
  polymorphic_serial_callable_event(F&& f)
      requires(!is_same_v<decay_t<F>, polymorphic_serial_callable_event>)
      : p_(p0957::make_proxy<INothrowCallable<void()>>(forward<F>(f))) {}
  polymorphic_serial_callable_event(polymorphic_serial_callable_event&&)
      = default;
  polymorphic_serial_callable_event& operator=(
      polymorphic_serial_callable_event&&) = default;
  void operator()() noexcept { (*p_)(); }

 private:
  p0957::proxy<INothrowCallable<void()>> p_;
};

template <class Event = polymorphic_serial_callable_event,
          class EventConsumer = nullary_callable_invoking_consumer>
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

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_STATIC_THREAD_POOL_H_
