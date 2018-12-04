/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/experimental/concurrent.h"

std::string do_something() {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  return "Test";
}

std::string do_something_else() {
  std::this_thread::sleep_for(std::chrono::seconds(2));
  return "Awesome!";
}

int main() {
  struct merged_result {
    mutable std::string part_0;
    mutable std::string part_1;

    void print() {
      printf("part_0=%s, part_1=%s\n", part_0.data(), part_1.data());
    }
  };

  std::crucial_thread_executor e;

  std::concurrent_invocation<merged_result>()
      .attach(e, [](const merged_result& r) {
        r.part_0 = do_something();
      })
      .attach(e, [](const merged_result& r) {
        r.part_1 = do_something_else();
      })
      .async_invoke([](merged_result&& result) {
        result.print();
      });

  puts("Main thread exit...");
}
