/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"
#include "../test_utility.h"

aid::thread_executor e;

int main() {
  // The contextual data type for collaboration
  struct context {
    int value;
    std::string str;

    void print() { printf("A: %d, B: %s\n", value, str.data()); }
  };

  // CIU is short for "Concurrent Invocation Unit". A CIU may be a "Concurrent
  // Callable", or an aggregation of multiple CIUs.
  // "std::p0642::async_concurrent_callable" is a prototype for the "Concurrent
  // Callable".
  auto ciu = std::tuple{
    std::p0642::async_concurrent_callable(e, [](context& ctx) {
      test::mock_execution(1000);
      ctx.value = 123;
    }),
    std::p0642::async_concurrent_callable(e, [](context& ctx) {
      test::mock_execution(2000);
      ctx.str = "Awesome!";
    })};

  // Perform blocking concurrent invocation with the CIU. context will
  // be default-constructed for collaboration.
  auto result = std::p0642::concurrent_invoke(ciu, std::in_place_type<context>);

  result.print();
}
