/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <vector>
#include <list>
#include <queue>

#include "./my_to_string.h"

int main() {
  auto test_case = std::make_tuple(
      123,
      std::vector<double> {1, 2, 3.14},
      std::list<std::vector<std::string>> {{}, {"Hello"}, {"a", "b", "c"}},
      std::make_tuple(std::deque<int> {3, 2, 1}, "OK"));
  puts(test::my_to_string(test_case).c_str());
}
