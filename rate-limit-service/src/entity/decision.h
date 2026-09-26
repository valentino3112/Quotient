// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "entity/policy.h"

namespace quotient::entity {

enum class DecisionCode { kOk, kOverLimit };
// https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/ratelimit/v3/rls.proto#service-ratelimit-v3-ratelimitresponse-ratelimit
// The limit that applied to a descriptor (reported back to Envoy).
struct AppliedLimit {
  std::string policy_name;
  std::uint32_t requests_per_unit = 0;
  RateUnit unit = RateUnit::kSecond;
};

//https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/ratelimit/v3/rls.proto#service-ratelimit-v3-ratelimitresponse-descriptorstatus
// The outcome for ONE descriptor.
struct Decision {
  DecisionCode code = DecisionCode::kOk;
  std::optional<AppliedLimit> limit;  // empty when no policy matched (unlimited)
  std::uint32_t remaining = 0;
  std::chrono::milliseconds reset_after{0};
};

}  // namespace quotient::entity
