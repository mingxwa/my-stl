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

  try {
    std::p0642::concurrent_invoke(csa);
  } catch (const std::p0642::concurrent_invocation_error<>& ex) {
    puts("Caught concurrent_invocation_error<>");
    for (auto& ep : ex.get_nested()) {
      try {
        std::rethrow_exception(ep);
      } catch (const std::runtime_error& re) {
        printf("Nested runtime_error: %s\n", re.what());
      }
    }
  }
}
