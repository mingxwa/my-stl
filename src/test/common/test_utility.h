/**
 * Copyright (c) 2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_TEST_COMMON_TEST_UTILITY_H_
#define SRC_TEST_COMMON_TEST_UTILITY_H_

#include <cstdio>
#include <chrono>

namespace test {

class time_recorder {
 public:
  explicit time_recorder(const char* name) : name_(name),
      when_(std::chrono::system_clock::now()) {
    printf("Time recorder for event [%s] has been established.\n", name_);
  }

  void record() const {
    std::chrono::duration<int, std::milli> result = std::chrono::duration_cast<
        std::chrono::milliseconds>(std::chrono::system_clock::now() - when_);
    printf("%dms elapsed for event [%s].\n", result.count(), name_);
  }

  void reset() {
    when_ = std::chrono::system_clock::now();
    printf("Time recorder for event [%s] has been reset.\n", name_);
  }

 private:
  const char* const name_;
  std::chrono::time_point<std::chrono::system_clock> when_;
};

}  // namespace test

#endif  // SRC_TEST_COMMON_TEST_UTILITY_H_
