/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

aid::thread_executor e;

int main() {
  auto csa = std::tuple{
    std::p0642::serial_concurrent_session{e, [](auto& bp) {
      auto single_task = std::p0642::serial_concurrent_session{e, []() {
        test::mock_random_execution(2000, 3000);
        puts("Single Fork Task Done");
      }};

      // Users may "spawn" other CSAs on the host concurrent invocation with the
      // token.
      bp.spawn(std::vector{10, single_task});
      puts("First Task Done");
    }},
    std::p0642::serial_concurrent_session{e, []() {
      test::mock_execution(1000);
      puts("Second Task Done");
    }}};

  std::p0642::concurrent_invoke(csa);
}
