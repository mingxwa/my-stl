/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>

#include "../../main/p0642/concurrent_invocation.h"

struct ResultTypeA { int value; };
struct ResultTypeB { std::string str; };

ResultTypeA call_library_a() {
  std::this_thread::sleep_for(std::chrono::seconds(1));
  return {123};
}

ResultTypeB call_library_b() {
  std::this_thread::sleep_for(std::chrono::seconds(2));
  return {"Awesome!"};
}

struct contextual_data {
  mutable ResultTypeA result_of_library_a;
  mutable ResultTypeB result_of_library_b;

  void print() {
    printf("A: %d, B: %s\n", result_of_library_a.value,
        result_of_library_b.str.data());
  }
};

int main() {
  std::crucial_thread_executor e;

  auto ciu = std::make_tuple(
    std::make_concurrent_callable(e, [](const contextual_data& cd) noexcept {
      cd.result_of_library_a = call_library_a();
    }),
    std::make_concurrent_callable(e, [](const contextual_data& cd) noexcept {
      cd.result_of_library_b = call_library_b();
    }));

  auto cb = std::make_concurrent_callback(
      std::in_place_executor{}, [](contextual_data&& data) { data.print(); });

  std::concurrent_invoke(std::move(ciu), std::in_place_type<contextual_data>,
      std::move(cb));

  puts("Main exit...");
}
