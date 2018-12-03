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
  std::concurrent_invocation<void> outer_invocation;
  for (std::size_t i = 0; i < TASK_GROUP_COUNT; ++i) {
    outer_invocation.attach([](std::concurrent_breakpoint<void>&& breakpoint) {
      std::concurrent_invocation<context> inner_invocation;
      for (std::size_t j = 0; j < TASK_COUNT_PER_GROUP; ++j) {
        inner_invocation.attach(pool.executor(), [](const context& c) {
          c.mtx_.lock();
          mock_task_execution();
          c.mtx_.unlock();
        });
      }
      inner_invocation.recursive_async_invoke(std::move(breakpoint), [] {});
    });
  }
  outer_invocation.sync_invoke();
  recorder.record();
}

void test_async_mutex() {
  using mutex = std::async_mutex<typename std::thread_pool<>::executor_type>;
  test::time_recorder recorder("test_async_mutex");
  std::concurrent_invocation<void> outer_invocation;
  for (std::size_t i = 0; i < TASK_GROUP_COUNT; ++i) {
    outer_invocation.attach([](std::concurrent_breakpoint<void>&& breakpoint) {
      std::concurrent_invocation<mutex> inner_invocation;
      for (std::size_t j = 0; j < TASK_COUNT_PER_GROUP; ++j) {
        inner_invocation.attach([](std::concurrent_breakpoint<mutex>&&
            breakpoint) {
          breakpoint.get().attach([breakpoint = move(breakpoint)] {
            mock_task_execution();
          });
        });
      }
      inner_invocation.recursive_async_invoke(std::move(breakpoint), [] {},
          pool.executor());
    });
  }
  outer_invocation.sync_invoke();
  recorder.record();
}

int main() {
  test_mutex();
  test_async_mutex();
  puts("Main thread exit...");
  return 0;
}
