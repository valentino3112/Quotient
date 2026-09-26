// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>
#include <mutex>
#include <utility>

#include "service/policy_set.h"

namespace quotient::service {

// Holds the current configuration snapshot. Readers take a shared_ptr and keep
// using that snapshot for their whole request; a reload builds a new PolicySet
// and swaps the pointer. The old snapshot is freed when its last reader is
// done (read-copy-update). The mutex only guards the pointer copy, which takes
// nanoseconds, so requests never wait for a reload.
class PolicyStore {
 public:
  std::shared_ptr<const PolicySet> Snapshot() const {
    std::lock_guard lock(mutex_);
    return current_;
  }

  void Publish(std::shared_ptr<const PolicySet> next) {
    std::lock_guard lock(mutex_);
    current_ = std::move(next);
  }

 private:
  mutable std::mutex mutex_;
  std::shared_ptr<const PolicySet> current_;
};

}  // namespace quotient::service
