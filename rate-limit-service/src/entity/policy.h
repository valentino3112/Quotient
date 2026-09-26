// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "entity/descriptor.h"

namespace quotient::entity {

enum class Algorithm { kFixedWindow, kGcra };
enum class RateUnit { kSecond, kMinute, kHour, kDay };
enum class FailureMode { kFailOpen, kFailClosed, kLocalFallback };

// One row of the policies table.
struct Policy {
  std::string name;
  std::string domain;
  // Ordered pattern. An empty value is a wildcard: one bucket per distinct value.
  std::vector<DescriptorEntry> match;
  Algorithm algorithm = Algorithm::kFixedWindow;
  std::uint32_t requests_per_unit = 0;
  RateUnit unit = RateUnit::kSecond;
  std::optional<std::uint32_t> burst;  // GCRA only; defaults to requests_per_unit
  FailureMode failure_mode = FailureMode::kFailOpen;
};

}  // namespace quotient::entity
