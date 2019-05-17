/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P1172_MEMORY_ALLOCATOR_H_
#define SRC_MAIN_P1172_MEMORY_ALLOCATOR_H_

#include <cstdlib>

namespace std {

class global_memory_allocator {
 public:
  template <size_t SIZE, size_t ALIGN>
  void* allocate() const { return malloc(SIZE); }

  template <size_t SIZE, size_t ALIGN>
  void deallocate(void* p) const { free(p); }

  template <size_t SIZE, size_t ALIGN>
  void* allocate(size_t n) const { return malloc(n * SIZE); }

  template <size_t SIZE, size_t ALIGN>
  void deallocate(void* p, size_t) const { free(p); }
};

}  // namespace std

#endif  // SRC_MAIN_P1172_MEMORY_ALLOCATOR_H_
