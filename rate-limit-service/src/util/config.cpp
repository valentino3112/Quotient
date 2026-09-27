// SPDX-License-Identifier: Apache-2.0
#include "util/config.h"

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

namespace quotient::util {

namespace {

std::optional<std::string> GetEnv(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') return std::nullopt;
  return std::string(value);
}

std::string GetString(const char* name, const std::string& fallback) {
  return GetEnv(name).value_or(fallback);
}

std::string GetRequiredString(const char* name) {
  auto value = GetEnv(name);
  if (!value) throw std::runtime_error(std::string(name) + " is required");
  return *value;
}

int GetPositiveInt(const char* name, int fallback) {
  auto value = GetEnv(name);
  if (!value) return fallback;
  try {
    std::size_t used = 0;
    int parsed = std::stoi(*value, &used);
    if (used == value->size() && parsed > 0) return parsed;
  } catch (const std::exception&) {
    // fall through to the error below
  }
  throw std::runtime_error(std::string(name) + " must be a positive integer, got '" + *value + "'");
}

}  // namespace

Config LoadConfigFromEnv() {
  Config config;
  config.rls_address = GetString("QUOTIENT_RLS_ADDR", "0.0.0.0:8081");
  config.admin_address = GetString("QUOTIENT_ADMIN_ADDR", "0.0.0.0:8082");
  config.admin_token = GetRequiredString("QUOTIENT_ADMIN_TOKEN");
  config.pg_url = GetRequiredString("QUOTIENT_PG_URL");
  config.redis_url = GetString("QUOTIENT_REDIS_URL", "tcp://redis:6379");
  config.redis_timeout = std::chrono::milliseconds(GetPositiveInt("QUOTIENT_REDIS_TIMEOUT_MS", 5));
  config.rls_max_threads = GetPositiveInt("QUOTIENT_RLS_MAX_THREADS", 32);
  return config;
}

}  // namespace quotient::util
