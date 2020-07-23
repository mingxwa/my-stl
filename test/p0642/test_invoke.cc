/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
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

  // CSA is short for "Concurrent Session Aggregation". A CSA may be a
  // "Concurrent Session", or an aggregation of multiple CSAs.
  // "std::p0642::serial_concurrent_session" is a prototype for the "Concurrent
  // Aggregation".
  auto csa = std::tuple{
    std::p0642::serial_concurrent_session(e, [](context& ctx) {
      test::mock_execution(1000);
      ctx.value = 123;
    }),
    std::p0642::serial_concurrent_session(e, [](context& ctx) {
      test::mock_execution(2000);
      ctx.str = "Awesome!";
    })};

  auto ctx = std::p0642::prepare_concurrent_context<context>();

  // Perform blocking concurrent invocation with the CSA. context will
  // be default-constructed for collaboration.
  auto result = std::p0642::concurrent_invoke(csa, ctx);

  result.print();
}
