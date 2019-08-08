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
  ResultTypeA result_of_library_a;
  ResultTypeB result_of_library_b;

  void print() {
    printf("A: %d, B: %s\n", result_of_library_a.value,
        result_of_library_b.str.data());
  }
};

int main() {
  aid::thread_executor e;

  auto ciu = std::tuple{
    std::async_concurrent_callable(e, [](contextual_data& cd) {
      cd.result_of_library_a = call_library_a();
    }),
    std::async_concurrent_callable(e, [](contextual_data& cd) {
      cd.result_of_library_b = call_library_b();
    })};

  std::concurrent_invoke(std::move(ciu), std::in_place_type<contextual_data>)
      .print();

  puts("Main exit...");
}
