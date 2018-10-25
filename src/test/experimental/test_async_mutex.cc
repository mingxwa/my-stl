/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <future>
#include <mutex>

#include "../../main/experimental/concurrent.h"
#include "../common/test_utility.h"

constexpr std::size_t THREAD_COUNT = 100u;
constexpr std::size_t TASK_GROUP_COUNT = 100u;
constexpr std::size_t TASK_COUNT_PER_GROUP = 100u;
constexpr std::chrono::milliseconds TASK_EXECUTE_DURATION{10};

std::thread_pool<> pool(THREAD_COUNT);

void mock_task_execution() {
  std::this_thread::sleep_for(TASK_EXECUTE_DURATION);
}

void test_mutex() {
  struct context {
    mutable std::mutex mtx_;  // Access concurrently
  };
  test::time_recorder recorder("test_mutex");
  std::concurrent_invoker<void> outer_invoker;
  for (std::size_t i = 0; i < TASK_GROUP_COUNT; ++i) {
    outer_invoker.attach([](std::concurrent_token<void>&& token) {
      std::concurrent_invoker<context> inner_invoker;
      for (std::size_t j = 0; j < TASK_COUNT_PER_GROUP; ++j) {
        inner_invoker.attach(pool.executor(), [](const context& c) {
          c.mtx_.lock();
          mock_task_execution();
          c.mtx_.unlock();
        });
      }
      inner_invoker.recursive_async_invoke(std::move(token), [] {});
    });
  }
  outer_invoker.sync_invoke();
  recorder.record();
}

void test_async_mutex() {
  using mutex = std::async_mutex<typename std::thread_pool<>::executor_type>;
  test::time_recorder recorder("test_async_mutex");
  std::concurrent_invoker<void> outer_invoker;
  for (std::size_t i = 0; i < TASK_GROUP_COUNT; ++i) {
    outer_invoker.attach([](std::concurrent_token<void>&& token) {
      std::concurrent_invoker<mutex> inner_invoker;
      for (std::size_t j = 0; j < TASK_COUNT_PER_GROUP; ++j) {
        inner_invoker.attach([](std::concurrent_token<mutex>&& token) {
          token.context().attach([token = move(token)] {
            mock_task_execution();
          });
        });
      }
      inner_invoker.recursive_async_invoke(std::move(token), [] {},
          pool.executor());
    });
  }
  outer_invoker.sync_invoke();
  recorder.record();
}

int main() {
  test_mutex();
  test_async_mutex();
  puts("Main thread exit...");
  return 0;
}
