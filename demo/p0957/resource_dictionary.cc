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
#include <typeinfo>

#include "../../main/p0957/proxy.h"

struct at : std::dispatch<std::string(int)> {
  template <class T>
  auto operator()(T& self, int key) { return self.at(key); }
};

class MyReflectionInfo {
 public:
  template <class P>
  constexpr explicit MyReflectionInfo(std::in_place_type_t<P>)
      : type_(typeid(P)) {}

  const char* GetName() const noexcept { return type_.name(); }

 private:
  const std::type_info& type_;
};

struct FResourceDictionary : std::facade<at> {
  using reflection_type = MyReflectionInfo;
  static constexpr std::size_t maximum_size =
      sizeof(std::optional<std::vector<std::string>>);
  static constexpr std::constraint_level minimum_copyability =
      std::constraint_level::nontrivial;
};

void DoSomethingWithResourceDictionary(std::proxy<FResourceDictionary> p) {
  auto p2 = p; // Copyable because of the facade configuration
  try {
    std::cout << "Pointer type name: " << p2.reflect().GetName() << std::endl;
    std::cout << p2.invoke(1) << std::endl;
  } catch (const std::out_of_range& e) {
    std::cout << "No such element: " << e.what() << std::endl;
  }
  puts("");
}

int main() {
  std::map<int, std::string> var1{{1, "Hello"}};
  std::unordered_map<int, std::string> var2{{2, "CPP"}};
  std::vector<std::string> var3{"I", "love", "Proxy", "!"};
  DoSomethingWithResourceDictionary(&var1);
  DoSomethingWithResourceDictionary(&var2);
  DoSomethingWithResourceDictionary(std::optional<std::vector<std::string>>(var3));
  DoSomethingWithResourceDictionary(std::make_shared<std::map<int, std::string>>());
}
