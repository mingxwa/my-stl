/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <map>
#include <unordered_map>
#include <iostream>
#include <string>
#include <vector>

#include "../../main/p0957/proxy.h"
#include "../../main/p0957/mock/proxy_immutable_map_impl.h"

void do_something_with_map(std::p0957::reference_proxy<
    const ImmutableMap<int, std::string>> m) {
  try {
    std::cout << m.at(1) << std::endl;
  } catch (const std::out_of_range& e) {
    std::cout << "Out of range: " << e.what() << std::endl;
  }
}

int main() {
  std::map<int, std::string> var1{{1, "Hello"}};
  std::unordered_map<int, std::string> var2{{2, "CPP"}};
  std::vector<std::string> var3{"I", "love", "PFA", "!"};
  std::map<int, std::string> var4{};
  do_something_with_map(var1);
  do_something_with_map(var2);
  do_something_with_map(var3);
  do_something_with_map(var4);
}
