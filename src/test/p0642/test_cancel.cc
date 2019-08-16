/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

struct context {
  std::atomic_int who_wins{0};

  int reduce() { return who_wins.load(std::memory_order_relaxed); }
};

struct competing_task {
  template <class Token>
  void operator()(const Token& token) const {
    int loop_times = test::random_int(10, 100);
    std::atomic_int& who_wins = token.context().who_wins;
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
  std::vector<std::p0642::async_concurrent_callable<
      aid::thread_executor, competing_task>> ciu;

  constexpr int THREADS = 100;
  for (int number = 1; number <= THREADS; ++number) {
    ciu.emplace_back(e, competing_task{number});
  }

  printf("Start executing with %d threads...\n", THREADS);
  int who_wins = std::p0642::concurrent_invoke(ciu,
      std::in_place_type<context>);
  printf("All set! Task %d wins.\n", who_wins);
}
