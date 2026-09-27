// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "entity/policy.h"

namespace quotient::repository {

// Everything the rate limit decision needs, as read from storage.
struct ConfigData {
  std::vector<entity::Policy> policies;
  // Raw 32-byte SHA-256 of each ACTIVE (not revoked) API key -> tenant slug.
  std::unordered_map<std::string, std::string> tenant_by_key_hash;
  std::int64_t version = 0;  // config_state.version, bumped on every change
};

// Source of the configuration. An interface, so services depend on this
// abstraction and tests can pass a fake instead of a real database.
class PolicyRepository {
 public:
  virtual ~PolicyRepository() = default;

  // Reads one consistent view of the configuration.
  // Throws service::Unavailable if the storage cannot be reached.
  virtual ConfigData LoadConfig() = 0;
};

}  // namespace quotient::repository
