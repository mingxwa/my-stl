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

template <class T> struct InvokeExpr;
template <class R, class... Args>
struct InvokeExpr<R(Args...)> : std::facade_expr<
    R(Args&&...), [](auto& self, Args&&... args)
        { return self(std::forward<Args>(args)...); }> {};
template <class T>
struct FunctionFacade : std::facade<InvokeExpr<T>> {};

template <class T>
struct ForEachExpr : std::facade_expr<
    void(std::proxy<FunctionFacade<void(T&)>>),
    [](auto& self, std::proxy<FunctionFacade<void(T&)>>&& func)
        { std::ranges::for_each(self, [&func](T& value)
            { func.invoke(value); }); }> {};
template <class T>
struct IterableFacade : std::facade<ForEachExpr<T>> {};

void MyPrintLibrary(std::proxy<IterableFacade<int>> p) {
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
