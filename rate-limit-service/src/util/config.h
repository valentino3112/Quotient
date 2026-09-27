// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <string>

namespace quotient::util {

// Process configuration, read once at startup from environment variables.
// Defaults follow docs/quotient-guide.md §17.11.
struct Config {
  std::string rls_address;                  // QUOTIENT_RLS_ADDR
  std::string admin_address;                // QUOTIENT_ADMIN_ADDR
  std::string admin_token;                  // QUOTIENT_ADMIN_TOKEN (required)
  std::string pg_url;                       // QUOTIENT_PG_URL (required)
  std::string redis_url;                    // QUOTIENT_REDIS_URL
  std::chrono::milliseconds redis_timeout;  // QUOTIENT_REDIS_TIMEOUT_MS
  int rls_max_threads;                      // QUOTIENT_RLS_MAX_THREADS
};

// Throws std::runtime_error naming the variable if one is missing or invalid.
Config LoadConfigFromEnv();

}  // namespace quotient::util
