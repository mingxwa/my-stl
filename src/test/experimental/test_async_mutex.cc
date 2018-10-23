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
  std::concurrent_invoker<void> invoker;
  for (std::size_t i = 0; i < TASK_GROUP_COUNT; ++i) {
    invoker.attach([](std::concurrent_token<void>&& token) {
      std::concurrent_invoker<context> invoker;
      for (std::size_t i = 0; i < TASK_COUNT_PER_GROUP; ++i) {
        invoker.attach(pool.executor(), [](const context& c) {
          c.mtx_.lock();
          mock_task_execution();
          c.mtx_.unlock();
        });
      }
      invoker.recursive_async_invoke(std::move(token), [] {});
    });
  }
  invoker.sync_invoke();
  recorder.record();
}

void test_async_mutex() {
  using mutex = std::async_mutex<decltype(pool.executor())>;
  test::time_recorder recorder("test_async_mutex");
  std::concurrent_invoker<void> invoker;
  for (std::size_t i = 0; i < TASK_GROUP_COUNT; ++i) {
    invoker.attach([](std::concurrent_token<void>&& token) {
      std::concurrent_invoker<mutex> invoker;
      for (std::size_t i = 0; i < TASK_COUNT_PER_GROUP; ++i) {
        invoker.attach([](std::concurrent_token<mutex>&& token) {
          token.context().attach([token = move(token)] {
            mock_task_execution();
          });
        });
      }
      invoker.recursive_async_invoke(std::move(token), [] {}, pool.executor());
    });
  }
  invoker.sync_invoke();
  recorder.record();
}

int main() {
  test_mutex();
  test_async_mutex();
  puts("Main thread exit...");
  return 0;
}
