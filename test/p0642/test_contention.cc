/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

struct context {
  std::atomic_int who_wins{0};

  int reduce() { return who_wins.load(std::memory_order_relaxed); }
};

struct competing_task {
  void operator()(context& ctx) const {
    int loop_times = test::random_int(10, 100);
    std::atomic_int& who_wins = ctx.who_wins;
    for (int i = 0; i < loop_times; ++i) {
      if (who_wins.load(std::memory_order_relaxed) != 0) {
        printf("Task %d exits because there is a winner.\n", number);
        return;
      }
      test::mock_execution(200);
    }
    int expected = 0;
    if (who_wins.compare_exchange_strong(
        expected, number, std::memory_order_relaxed)) {
      printf("Task %d wins!\n", number);
    } else {
      printf("Task %d has finished but lost.\n", number);
    }
  }

  int number;
};

aid::thread_executor e;

int main() {
  std::vector<std::p0642::serial_concurrent_session<
      aid::thread_executor, competing_task>> csa;

  constexpr int THREADS = 100;
  for (int number = 1; number <= THREADS; ++number) {
    csa.emplace_back(e, competing_task{number});
  }

  printf("Start executing with %d threads...\n", THREADS);
  int who_wins = std::p0642::concurrent_invoke(csa,
      std::p0642::prepare_concurrent_context<context>());
  printf("All set! Task %d wins.\n", who_wins);
}
