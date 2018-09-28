/**
 * Copyright (c) 2017-2018 Mingxin Wang. All rights reserved.
 */

#ifndef SRC_MAIN_P1172_MEMORY_ALLOCATOR_H_
#define SRC_MAIN_P1172_MEMORY_ALLOCATOR_H_

#include <cstdlib>
#include <type_traits>

namespace std {

class memory_allocator {
 public:
  template <size_t SIZE, size_t ALIGN>
  void* allocate(integral_constant<size_t, SIZE>,
      integral_constant<size_t, ALIGN>) { return malloc(SIZE); }

  template <size_t SIZE, size_t ALIGN>
  void deallocate(void* p, integral_constant<size_t, SIZE>,
      integral_constant<size_t, ALIGN>) { free(p); }

  template <size_t SIZE, size_t ALIGN>
  void* allocate(size_t n, integral_constant<size_t, SIZE>,
      integral_constant<size_t, ALIGN>) { return malloc(n * SIZE); }

  template <size_t SIZE, size_t ALIGN>
  void deallocate(void* p, size_t, integral_constant<size_t, SIZE>,
      integral_constant<size_t, ALIGN>) { free(p); }
};

}  // namespace std

#endif  // SRC_MAIN_P1172_MEMORY_ALLOCATOR_H_
