/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <iostream>

#include "../../main/experimental/thread_pool.h"
#include "../test_utility.h"

int main() {
  std::experimental::static_thread_pool<> pool(10);
  auto ex = pool.executor();
  for (int i = 0; i < 20; ++i) {
    ex.execute([]() {
      test::mock_random_execution(1000, 3000);
      puts("Hello World!");
    });
  }
  puts("Main thread exit...");
}
