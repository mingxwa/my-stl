/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

aid::thread_executor e;

int main() {
  auto ciu = std::tuple{
    std::p0642::async_concurrent_callable{e, [](auto& token) {
      auto single_task = std::p0642::async_concurrent_callable{e, []() {
        test::mock_random_execution(2000, 3000);
        puts("Single Fork Task Done");
      }};

      // Users may "fork" other CIUs on the host concurrent invocation with the
      // token.
      token.fork(std::vector{10, single_task});
      puts("First Task Done");
    }},
    std::p0642::async_concurrent_callable{e, []() {
      test::mock_execution(1000);
      puts("Second Task Done");
    }}};

  std::p0642::concurrent_invoke(ciu);
}
