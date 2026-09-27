// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace quotient::entity {

// One row of the tenants table: a customer of the protected API.
struct Tenant {
  std::string id;  // UUID
  std::string slug;
  std::string display_name;
  std::int64_t created_at_unix = 0;  // seconds since 1970-01-01 UTC
};

}  // namespace quotient::entity
