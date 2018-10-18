/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <iostream>

#include "../../main/experimental/concurrent.h"

std::timed_thread_pool<> pool(10);
std::atomic_int task_count = 3;

int main() {
  using time_unit = std::chrono::milliseconds;

  auto trigger = std::make_timed_circulation(pool.executor(),
      [&]() -> std::optional<time_unit> {
    int current = task_count.load();
    do {
      if (current == 0) {
        return std::nullopt;
      }
    } while (!task_count.compare_exchange_weak(current, current - 1));
    printf("start... %d\n", current);
    std::this_thread::sleep_for(time_unit(1000));
    printf("end...\n");
    if (current == 1) {
      return std::nullopt;
    }
    return time_unit(2000);
  });

  trigger.fire(time_unit(3000));

  int add;
  while (std::cin >> add && add >= 0) {
    task_count += add;
    trigger.fire();
  }
  puts("Main thread exit...");
}
