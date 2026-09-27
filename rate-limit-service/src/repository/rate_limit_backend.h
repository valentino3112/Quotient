// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "entity/policy.h"

namespace quotient::repository {

// One counter update: "add `hits` to this bucket, if the limit allows it".
struct BucketRequest {
  std::string key;  // full Redis key, built by the service layer
  entity::Algorithm algorithm = entity::Algorithm::kFixedWindow;
  std::uint32_t limit = 0;          // requests per period
  std::chrono::milliseconds period{0};
  std::uint32_t burst = 0;          // GCRA only
  std::uint32_t hits = 1;
};

struct BucketResult {
  bool allowed = true;
  std::uint32_t remaining = 0;
  std::chrono::milliseconds reset_after{0};  // until the bucket is back to full
};

// Where counters live. An interface so the decision engine does not depend on
// Redis, and tests can use an in-memory fake.
class RateLimitBackend {
 public:
  virtual ~RateLimitBackend() = default;

  // Throws service::Unavailable if the backend is down or too slow; the
  // caller then applies the policy's failure mode.
  virtual BucketResult Hit(const BucketRequest& request) = 0;
};

}  // namespace quotient::repository
