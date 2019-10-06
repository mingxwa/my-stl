/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

aid::thread_executor e;

int main() {
  auto csa = std::tuple{
    std::p0642::serial_concurrent_session(e, []() {
      test::mock_execution(2000);
      puts("OK for the first path");
    }),
    std::p0642::serial_concurrent_session(e, []() {
      test::mock_execution(1000);
      puts("Let's do something evil for the second path ^_^");

      // Any unhandled exception within a concurrent invocation will be attached
      // to its host invocation, and will be handled after the concurrent
      // invocation completes.
      throw std::runtime_error("Pretending there is an error...");
    })};

  auto continuation = std::p0642::async_concurrent_continuation{
      aid::in_place_executor{},
      []() { puts("Normal control flow..."); }, [](auto&& exceptions) {
    puts("Error control flow");
    for (auto& ep : exceptions) {
      try {
        std::rethrow_exception(ep);
      } catch (const std::runtime_error& re) {
        printf("Nested runtime_error: %s\n", re.what());
      }
    }
  }};

  std::p0642::concurrent_invoke(csa, std::in_place_type<void>, continuation);

  puts("Main exit...");
}
