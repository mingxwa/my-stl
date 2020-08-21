/**
 * Copyright (c) Microsoft Corporation. All rights reserved.
 * Author: Mingxin Wang (mingxwa@microsoft.com)
 */

#ifndef SRC_MAIN_EXPERIMENTAL_NULLARY_CALLABLE_INVOKING_CONSUMER_H_
#define SRC_MAIN_EXPERIMENTAL_NULLARY_CALLABLE_INVOKING_CONSUMER_H_

namespace std::experimental {

struct nullary_callable_invoking_consumer {
  template <class F>
  decltype(auto) consume(F* f) const noexcept { return (*f)(); }
};

}  // namespace std::experimental

#endif  // SRC_MAIN_EXPERIMENTAL_NULLARY_CALLABLE_INVOKING_CONSUMER_H_
