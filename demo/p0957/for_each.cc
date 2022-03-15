/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <cstdio>
#include <algorithm>
#include <forward_list>
#include <deque>
#include <ranges>

#include "../../main/p0957/proxy.h"

template <class T> struct Call;
template <class R, class... Args>
struct Call<R(Args...)> : std::dispatch<R(Args&&...)> {
  template <class T>
  auto operator()(T& self, Args&&... args)
      { return self(std::forward<Args>(args)...); }
};
template <class T>
struct FCallable : std::facade<Call<T>> {};

template <class T>
struct ForEach : std::dispatch<void(std::proxy<FCallable<void(T&)>>)> {
  template <class U>
  void operator()(U& self, std::proxy<FCallable<void(T&)>>&& func) {
    std::ranges::for_each(self, [&func](T& value) { func.invoke(value); });
  }
};
template <class T>
struct FIterable : std::facade<ForEach<T>> {};

void MyPrintLibrary(std::proxy<FIterable<int>> p) {
  auto f = [](double value) { printf("%f\n", value); };
  p.invoke(&f);
  puts("");
}

int main() {
  std::forward_list<int> a{1, 2, 3, 4, 5};
  std::deque<int> b{6, 7, 8, 9, 10};
  MyPrintLibrary(&a);
  MyPrintLibrary(&b);
}
