/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <iostream>

#include "../../main/experimental/thread_pool.h"
#include "../test_utility.h"

int main() {
  std::experimental::static_thread_pool<> pool(5);
  auto ex = pool.executor();
  ex.execute([] { test::mock_execution(2000); puts("lalala"); });
}
