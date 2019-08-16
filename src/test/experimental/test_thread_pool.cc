/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <iostream>

#include "../../main/experimental/thread_pool.h"
#include "../test_utility.h"

int main() {
  std::experimental::thread_pool<> pool(100);
  auto ex = pool.executor();
  auto single_task = std::p0642::async_concurrent_callable{
      ex, [] { test::mock_execution(1); }};
  auto ciu = std::vector<decltype(single_task)>{100000, single_task};

  test::time_recorder rec("basic thread pool");
  std::p0642::concurrent_invoke(ciu);
  rec.record();

  puts("Main thread exit...");
}
