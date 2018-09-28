/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <set>
#include <future>

#include "../../main/experimental/concurrent.h"

int main() {
  constexpr int task_count = 100000;

  std::thread_pool<> pool(10);
  auto executor = pool.executor();
  std::mutex mtx;
  std::set<int> s;
  std::promise<void> p;

  for (int i = 0; i < task_count; ++i) {
    executor([&, i] {
      mtx.lock();
      s.insert(i);
      bool ok = s.size() == task_count;
      mtx.unlock();
      if (ok) {
        p.set_value();
      }
    });
  }
  p.get_future().wait();

  puts("Main thread exit...");
}
