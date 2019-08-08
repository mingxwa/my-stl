/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

struct contextual_data {
  std::atomic_int who_wins{0};
};

struct competing_task {
  template <class Token>
  void operator()(Token& token) const {
    int loop_times = test::random_int(10, 100);
    for (int i = 0; i < loop_times; ++i) {
      if (token.is_canceled()) {
        printf("Task %d exits because of cancellation.\n", number);
        return;
      }
      test::mock_execution(200);
    }
    int expected = 0;
    if (token.context().who_wins
        .compare_exchange_strong(expected, number, std::memory_order_relaxed)) {
      printf("Task %d wins!\n", number);
      token.cancel();  // You may delete this line to see how it effects collaboration
    } else {
      printf("Task %d has finished but lost.\n", number);
    }
  }

  int number;
};

aid::thread_executor e;

int main() {
  std::vector<std::async_concurrent_callable<
      aid::thread_executor, competing_task>> ciu;

  constexpr int THREADS = 100;
  for (int number = 1; number <= THREADS; ++number) {
    ciu.emplace_back(e, competing_task{number});
  }

  auto context_reducer = [](contextual_data&& data)
      { return data.who_wins.load(std::memory_order_relaxed); };

  printf("Start executing with %d threads...\n", THREADS);
  int who_wins = std::concurrent_invoke(ciu,
      std::in_place_type<contextual_data>, context_reducer);
  printf("All set! Task %d wins.\n", who_wins);
}
