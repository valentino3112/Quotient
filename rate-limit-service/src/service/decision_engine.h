// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "entity/decision.h"
#include "entity/descriptor.h"
#include "repository/rate_limit_backend.h"
#include "service/policy_store.h"
#include "util/clock.h"

namespace quotient::service {

struct RateLimitOutcome {
  entity::DecisionCode overall = entity::DecisionCode::kOk;  // OVER_LIMIT if any is
  std::vector<entity::Decision> decisions;                  // one per descriptor, same order
};

// Decides, for each descriptor of one request, whether it is within its limit.
// All dependencies are injected: the engine does not know about gRPC,
// PostgreSQL or Redis, only about the interfaces.
class DecisionEngine {
 public:
  DecisionEngine(PolicyStore& policy_store, repository::RateLimitBackend& backend,
                 util::Clock& clock);

  // hits: how many requests to count (Envoy's hits_addend; 0 means 1).
  // deadline: when the caller (Envoy) stops waiting for our answer.
  RateLimitOutcome Decide(const std::string& domain,
                          const std::vector<entity::Descriptor>& descriptors, std::uint32_t hits,
                          std::chrono::system_clock::time_point deadline);

 private:
  entity::Decision DecideOne(const PolicySet& snapshot, const std::string& domain,
                             const entity::Descriptor& descriptor, std::uint32_t hits,
                             std::chrono::system_clock::time_point deadline);

  // The counter could not be updated: the policy's failure mode decides.
  entity::Decision ApplyFailureMode(const entity::Policy& policy, const std::string& reason);

  PolicyStore& policy_store_;
  repository::RateLimitBackend& backend_;
  util::Clock& clock_;
  // When Redis is down every request fails; warn at most once per second
  // instead of flooding the log.
  std::atomic<std::int64_t> last_warning_ms_{0};
};

}  // namespace quotient::service
