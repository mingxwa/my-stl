/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
#define SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_

#include <utility>
#include <queue>
#include <memory>
#include <mutex>
#include <condition_variable>

#include "../p0957/proxy.h"
#include "../p0957/mock/proxy_callable_impl.h"
#include "../p0642/concurrent_invocation.h"

namespace std {

template <class F = value_proxy<Callable<void()>>>
class static_thread_pool {
  struct data_type;

 public:
  template <class E = crucial_thread_executor>
  explicit static_thread_pool(size_t thread_count, const E& executor = E())
      : data_(make_shared<data_type>()) {
    for (size_t i = 0; i < thread_count; ++i) {
      executor.execute([data = data_] {
        unique_lock<mutex> lk(data->mtx_);
        for (;;) {
          if (!data->tasks_.empty()) {
            {
              F current = move(data->tasks_.front());
              data->tasks_.pop();
              lk.unlock();
              forward<F>(current)();
            }
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

  ~static_thread_pool() {
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
    void execute(_F&& f) const {
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

}  // namespace std

#endif  // SRC_MAIN_EXPERIMENTAL_THREAD_POOL_H_
