/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

aid::thread_executor e;

int main() {
  struct context {
    int value;
    std::string str;

    void print() { printf("A: %d, B: %s\n", value, str.data()); }
  };

  auto csa = std::tuple{
    std::p0642::serial_concurrent_session(e, [](context& ctx) {
      test::mock_execution(1000);
      ctx.value = 123;
    }),
    std::p0642::serial_concurrent_session(e, [](context& ctx) {
      test::mock_execution(2000);
      ctx.str = "Awesome!";
    })};

  auto continuation = std::p0642::async_concurrent_continuation{
      aid::in_place_executor{}, [](context&& data) { data.print(); }};

  std::p0642::concurrent_invoke(csa, std::in_place_type<context>, continuation);

  puts("Main exit...");
}
