/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <future>

#include "../../main/experimental/concurrent.h"

int main() {
  using time_unit = std::chrono::milliseconds;
  constexpr int task_count = 1000000;
  srand(time(0));

  std::thread_pool<> pool(10);
  std::timed_thread_pool<> timed_pool(10);
  auto executor = pool.executor();
  auto timed_executor = timed_pool.executor();
  std::async_mutex<decltype(executor)> mtx(executor);
  std::set<int> s;
  std::promise<void> p;
  auto now = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < task_count; ++i) {
    timed_executor(now + time_unit(rand() % 1000 + 500), [&, i] {
      mtx.attach([&, i] {
        s.insert(i);
        return s.size() == task_count;
      }, [&](bool ok) {
        if (ok) {
          p.set_value();
        }
      });
      return std::nullopt;
    });
  }
  p.get_future().wait();

  puts("Main thread exit...");
}
