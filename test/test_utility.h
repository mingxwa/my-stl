/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_TEST_TEST_UTILITY_H_
#define SRC_TEST_TEST_UTILITY_H_

#include <cstdio>
#include <random>
#include <chrono>
#include <atomic>
#include <thread>

namespace test {

namespace detail {

struct random_engine_wrapper {
  random_engine_wrapper() : engine_(static_cast<unsigned int>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count())
          * seed_id.fetch_add(1u, std::memory_order_relaxed)) {}

  std::mt19937 engine_;
  static inline std::atomic_uint seed_id{1u};
};
inline thread_local random_engine_wrapper wrapper;

}  // namespace detail

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

inline std::mt19937& get_random_engine() { return detail::wrapper.engine_; }

inline int random_int(int min, int max) {
  return std::uniform_int_distribution<int>{min, max}(get_random_engine());
}

inline void mock_execution(int time_ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(time_ms));
}

inline void mock_random_execution(int min_time_ms, int max_time_ms) {
  mock_execution(random_int(min_time_ms, max_time_ms));
}

}  // namespace test

#endif  // SRC_TEST_TEST_UTILITY_H_
