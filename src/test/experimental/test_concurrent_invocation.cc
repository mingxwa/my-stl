/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/experimental/concurrent.h"

int main() {
  std::crucial_thread_executor e;

  std::concurrent_invocation<void>()
      .attach(e, [] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        puts("Call library 1 -- from invocation 1");
      })
      .attach(e, [] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        puts("Call library 2 -- from invocation 1");
      })
      .sync_invoke();
  puts("Done 1");

  std::concurrent_invocation<void> invocation2;
  for (int i = 0; i < 10; ++i) {
    invocation2.attach(e, [] {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      puts("Hello World! -- from invocation 2");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    });
  }
  invocation2.async_invoke([] { puts("Done 2"); });
  puts("Main thread exit...");
}
