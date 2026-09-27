// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>

namespace quotient::util {

// Every "what time is it?" goes through this interface, so tests can pass a
// fake clock and control time instead of sleeping.
class Clock {
 public:
  virtual ~Clock() = default;
  // system_clock, because gRPC deadlines are system_clock time points.
  virtual std::chrono::system_clock::time_point Now() const = 0;
};

class SystemClock final : public Clock {
 public:
  std::chrono::system_clock::time_point Now() const override {
    return std::chrono::system_clock::now();
  }
};

}  // namespace quotient::util
