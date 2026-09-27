// SPDX-License-Identifier: Apache-2.0
#include "repository/redis_rate_limit_backend.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <iterator>
#include <string_view>

#include "repository/lua_scripts.h"
#include "service/errors.h"

namespace quotient::repository {

namespace {

// Opening a TCP connection is slow the first time (measured up to ~90 ms in
// Docker for a cold connection), so it gets its own, larger timeout. When Redis
// is down the connection is refused at once, so this rarely costs anything.
constexpr auto kConnectTimeout = std::chrono::milliseconds(50);
// Script loading happens once at startup, off the hot path, on a brand-new
// connection: give it plenty of time.
constexpr auto kStartupTimeout = std::chrono::milliseconds(1000);

sw::redis::Redis MakeRedis(const std::string& url, std::chrono::milliseconds command_timeout, std::size_t pool_size) 
{
  sw::redis::ConnectionOptions connection = sw::redis::Uri(url).connection_options();
  connection.connect_timeout = std::max(kConnectTimeout, command_timeout);
  // Every command must finish well inside Envoy's 20 ms budget; if Redis is
  // slower than this we give up and let the policy's failure mode decide.
  connection.socket_timeout = command_timeout;

  sw::redis::ConnectionPoolOptions pool;
  pool.size = pool_size;  // about one connection per gRPC worker thread
  pool.wait_timeout = command_timeout;  // never queue long for a free connection
  return sw::redis::Redis(connection, pool);
}

// This redis++ build uses its own StringView type rather than std::string_view.
sw::redis::StringView ToRedis(std::string_view s) { return {s.data(), s.size()}; }

std::uint32_t ClampRemaining(long long value) {
  return static_cast<std::uint32_t>(std::max(0LL, value));
}

std::chrono::milliseconds MicrosToMillisRoundUp(long long micros) {
  return std::chrono::milliseconds((std::max(0LL, micros) + 999) / 1000);
}

}  // namespace

RedisRateLimitBackend::RedisRateLimitBackend(const std::string& url, std::chrono::milliseconds timeout, std::size_t pool_size)
    : url_(url), redis_(MakeRedis(url, timeout, pool_size)) {}

void RedisRateLimitBackend::LoadScripts() {
  try {
    // A separate one-connection client with a startup timeout: the hot-path
    // client's 5 ms is too tight for the very first reply on a new connection.
    sw::redis::Redis loader = MakeRedis(url_, kStartupTimeout, 1);
    gcra_sha_ = loader.script_load(ToRedis(lua::kGcra));
    fixed_window_sha_ = loader.script_load(ToRedis(lua::kFixedWindow));
  } catch (const sw::redis::Error& e) {
    throw service::Unavailable(std::string("Redis unreachable: ") + e.what());
  }
  spdlog::info("Redis scripts loaded (gcra {}, fixed_window {})", gcra_sha_.substr(0, 8),
               fixed_window_sha_.substr(0, 8));
}

std::vector<long long> RedisRateLimitBackend::Eval(std::string_view script,
                                                   const std::string& sha,
                                                   const std::string& key,
                                                   const std::vector<std::string>& args) {
  std::vector<std::string> keys = {key};
  std::vector<long long> reply;
  try {
    try {
      redis_.evalsha(sha, keys.begin(), keys.end(), args.begin(), args.end(),
                     std::back_inserter(reply));
    } catch (const sw::redis::ReplyError& e) {
      if (std::string_view(e.what()).substr(0, 8) != "NOSCRIPT") throw;
      // Redis restarted and forgot the script. Loading it again gives the
      // same SHA (it depends only on the text), so no state changes here.
      spdlog::warn("Redis lost its Lua scripts, reloading");
      redis_.script_load(ToRedis(script));
      reply.clear();
      redis_.evalsha(sha, keys.begin(), keys.end(), args.begin(), args.end(),
                     std::back_inserter(reply));
    }
  } catch (const sw::redis::Error& e) {
    throw service::Unavailable(std::string("Redis error: ") + e.what());
  }
  return reply;
}

BucketResult RedisRateLimitBackend::Hit(const BucketRequest& request) 
{
  const std::string hits = std::to_string(request.hits);
  BucketResult result;

  if (request.algorithm == entity::Algorithm::kGcra) {
    // Emission interval T: the time one request "costs", e.g. 100/s -> 10000 us.
    auto period_us = std::chrono::duration_cast<std::chrono::microseconds>(request.period);
    long long emission_us = period_us.count() / std::max<std::uint32_t>(request.limit, 1);
    // Reply: {allowed, remaining, retry_after_us, reset_after_us}
    auto reply = Eval(lua::kGcra, gcra_sha_, request.key,
                      {std::to_string(emission_us), std::to_string(request.burst), hits});
    if (reply.size() != 4) throw service::Unavailable("unexpected reply from gcra.lua");
    result.allowed = reply[0] == 1;
    result.remaining = ClampRemaining(reply[1]);
    result.reset_after = MicrosToMillisRoundUp(reply[3]);
  } else {
    // Reply: {allowed, remaining, reset_after_ms}
    auto reply = Eval(lua::kFixedWindow, fixed_window_sha_, request.key,
                      {std::to_string(request.limit), std::to_string(request.period.count()), hits});
    if (reply.size() != 3) throw service::Unavailable("unexpected reply from fixed_window.lua");
    result.allowed = reply[0] == 1;
    result.remaining = ClampRemaining(reply[1]);
    result.reset_after = std::chrono::milliseconds(std::max(0LL, reply[2]));
  }
  return result;
}

}  // namespace quotient::repository
