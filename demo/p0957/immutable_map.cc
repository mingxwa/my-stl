/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <map>
#include <unordered_map>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <memory>

#include "../../main/p0957/proxy.h"
#include "../../main/p0957/mock/proxy_immutable_map_impl.h"

namespace std::p0957 {

template <>
struct global_proxy_config<IImmutableMap<int, std::string>>
    : default_proxy_config {
  static constexpr type_requirements_level copyability
      = type_requirements_level::nontrivial;
  static constexpr std::size_t max_size = sizeof(std::optional<std::vector<std::string>>);
};

}  // namespace std::p0957

void do_something_with_map(std::p0957::proxy<IImmutableMap<int, std::string>> m) {
  auto p = m;
  try {
    std::cout << (*m).at(1) << std::endl;
  } catch (const std::out_of_range& e) {
    std::cout << "No such element: " << e.what() << std::endl;
  }
}

int main() {
  std::map<int, std::string> var1{{1, "Hello"}};
  std::unordered_map<int, std::string> var2{{2, "CPP"}};
  std::vector<std::string> var3{"I", "love", "Proxy", "!"};
  do_something_with_map(&var1);
  do_something_with_map(&var2);
  do_something_with_map(std::optional<std::vector<std::string>>(var3));
  do_something_with_map(std::make_shared<std::map<int, std::string>>());
}
