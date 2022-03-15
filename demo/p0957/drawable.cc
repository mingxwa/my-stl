/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#include <cstdio>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <numbers>
#include <memory_resource>

#include "../../main/p0957/proxy.h"

struct Draw : std::dispatch<void()> {
  template <class T>
  void operator()(const T& self) { self.Draw(); }
};
struct Area : std::dispatch<double()> {
  template <class T>
  double operator()(const T& self) { return self.Area(); }
};

struct FDrawable : std::facade<Draw, Area> {};

class Rectangle {
 public:
  void Draw() const
      { printf("{Rectangle: width = %f, height = %f}", width_, height_); }
  void SetWidth(double width) { width_ = width; }
  void SetHeight(double height) { height_ = height; }
  void SetTransparency(double);
  double Area() const { return width_ * height_; }

 private:
  double width_;
  double height_;
};

class Circle {
 public:
  void Draw() const { printf("{Circle: radius = %f}", radius_); }
  void SetRadius(double radius) { radius_ = radius; }
  void SetTransparency(double);
  double Area() const { return std::numbers::pi * radius_ * radius_; }

 private:
  double radius_;
};

class Point {
 public:
  Point() noexcept { puts("A point was created"); }
  ~Point() { puts("A point was destroyed"); }
  void Draw() const { printf("{Point}"); }
  constexpr double Area() const { return 0; }
};

void DoSomethingWithDrawable(std::proxy<FDrawable> p) {
  printf("The drawable is: ");
  p.invoke<Draw>();
  printf(", area = %f\n", p.invoke<Area>());
}

std::vector<std::string> ParseCommand(const std::string& s) {
  std::vector<std::string> result(1u);
  bool in_quote = false;
  std::size_t last_valid = s.find_last_not_of(' ');
  for (std::size_t i = 0u; i <= last_valid; ++i) {
    if (s[i] == '`' && i < last_valid) {
      result.back() += s[++i];
    } else if (s[i] == '"') {
      in_quote = !in_quote;
    } else if (s[i] == ' ' && !in_quote) {
      if (!result.back().empty()) {
        result.emplace_back();
      }
    } else {
      result.back() += s[i];
    }
  }
  if (result.back().empty()) {
    result.pop_back();
  }
  return result;
}

std::proxy<FDrawable> MakeDrawableFromCommand(const std::string& s) {
  std::vector<std::string> parsed = ParseCommand(s);
  if (!parsed.empty()) {
    if (parsed[0u] == "Rectangle") {
      if (parsed.size() == 3u) {
        static std::pmr::unsynchronized_pool_resource rectangle_memory_pool;
        std::pmr::polymorphic_allocator<> alloc{&rectangle_memory_pool};
        auto deleter = [alloc](Rectangle* ptr) mutable
            { alloc.delete_object<Rectangle>(ptr); };
        Rectangle* instance = alloc.new_object<Rectangle>();
        std::unique_ptr<Rectangle, decltype(deleter)> p{instance, deleter};
        p->SetWidth(std::stod(parsed[1u]));
        p->SetHeight(std::stod(parsed[2u]));
        return p;
      }
    } else if (parsed[0u] == "Circle") {
      if (parsed.size() == 2u) {
        Circle circle;
        circle.SetRadius(std::stod(parsed[1u]));
        return std::make_proxy<FDrawable>(circle);
      }
    } else if (parsed[0u] == "Point") {
      if (parsed.size() == 1u) {
        static Point instance;
        return &instance;
      }
    }
  }
  throw std::runtime_error{"Invalid command"};
}

int main() {
  std::proxy<FDrawable> p;
  p = MakeDrawableFromCommand("Rectangle 2 3");
  DoSomethingWithDrawable(std::move(p));
  p = MakeDrawableFromCommand("Circle 1");
  DoSomethingWithDrawable(std::move(p));
  p = MakeDrawableFromCommand("Point");
  DoSomethingWithDrawable(std::move(p));
  p = nullptr;  // The "Point" will not be destroyed here
  puts("p = nullptr was executed.");
  try {
    p = MakeDrawableFromCommand("triangle 2 3");
  } catch (const std::exception& e) {
    printf("Caught exception: %s\n", e.what());
  }
}
