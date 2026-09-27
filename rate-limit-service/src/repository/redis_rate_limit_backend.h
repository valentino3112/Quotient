// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sw/redis++/redis++.h>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

#include "repository/rate_limit_backend.h"

namespace quotient::repository {

// RateLimitBackend on Redis: each Hit runs one atomic Lua script (EVALSHA).
// Thread-safe: redis++ keeps a connection pool, and the script SHAs never
// change after LoadScripts() (a SHA1 depends only on the script text).
class RedisRateLimitBackend final : public RateLimitBackend {
 public:
  // url: e.g. "tcp://redis:6379". timeout applies to every command.
  RedisRateLimitBackend(const std::string& url, 
                        std::chrono::milliseconds timeout,
                        std::size_t pool_size);

  // SCRIPT LOAD both scripts. Call once at startup, before serving.
  // Throws service::Unavailable if Redis cannot be reached.
  void LoadScripts();

  BucketResult Hit(const BucketRequest& request) override;

 private:
  // EVALSHA; if Redis lost its script cache (restart, failover), load the
  // script again and retry once.
  std::vector<long long> Eval(std::string_view script, const std::string& sha,
                              const std::string& key, const std::vector<std::string>& args);

  std::string url_;
  sw::redis::Redis redis_;  // hot path: pooled connections, short timeouts
  std::string gcra_sha_;
  std::string fixed_window_sha_;
};

}  // namespace quotient::repository
