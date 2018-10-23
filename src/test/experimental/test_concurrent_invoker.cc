/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/experimental/concurrent.h"

int main() {
  std::concurrent_invoker<void> invoker1;
  for (int i = 0; i < 10; ++i) {
    invoker1.attach(std::thread_executor<>(), [](std::concurrent_token<void>) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      puts("Hello World! -- from invoker 1");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    });
  }
  invoker1.sync_invoke();
  puts("Done 1");

  std::concurrent_invoker<void> invoker2;
  for (int i = 0; i < 10; ++i) {
    invoker2.attach(std::thread_executor<>(), [] {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      puts("Hello World! -- from invoker 2");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    });
  }
  invoker2.async_invoke([] { puts("Done 2"); });
  puts("Main thread exit...");
}
