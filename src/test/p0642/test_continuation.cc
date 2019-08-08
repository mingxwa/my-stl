/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

aid::thread_executor e;

int main() {
  struct contextual_data {
    int value;
    std::string str;

    void print() { printf("A: %d, B: %s\n", value, str.data()); }
  };

  auto ciu = std::tuple{
    std::async_concurrent_callable(e, [](contextual_data& cd) {
      test::mock_execution(1000);
      cd.value = 123;
    }),
    std::async_concurrent_callable(e, [](contextual_data& cd) {
      test::mock_execution(2000);
      cd.str = "Awesome!";
    })};

  auto continuation = std::async_concurrent_continuation{
      e, [](contextual_data&& data) { data.print(); }};

  std::concurrent_invoke(ciu, std::in_place_type<contextual_data>,
      std::context_moving_reducer{}, continuation);

  puts("Main exit...");
}
