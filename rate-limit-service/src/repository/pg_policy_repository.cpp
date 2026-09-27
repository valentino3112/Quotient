// SPDX-License-Identifier: Apache-2.0
#include "repository/pg_policy_repository.h"

#include <spdlog/spdlog.h>

#include <optional>
#include <pqxx/pqxx>
#include <string_view>
#include <utility>

#include "service/errors.h"

namespace quotient::repository {

namespace {

// Converts the text of the PostgreSQL enums (algorithm, rate_unit,
// failure_mode) into our C++ enums. nullopt = a value we do not support.
std::optional<entity::Algorithm> ParseAlgorithm(std::string_view s) {
  if (s == "fixed_window") return entity::Algorithm::kFixedWindow;
  if (s == "gcra") return entity::Algorithm::kGcra;
  return std::nullopt;  // 'leased' is lease mode, implemented after the deadline
}

std::optional<entity::RateUnit> ParseUnit(std::string_view s) {
  if (s == "second") return entity::RateUnit::kSecond;
  if (s == "minute") return entity::RateUnit::kMinute;
  if (s == "hour") return entity::RateUnit::kHour;
  if (s == "day") return entity::RateUnit::kDay;
  return std::nullopt;
}

std::optional<entity::FailureMode> ParseFailureMode(std::string_view s) {
  if (s == "fail_open") return entity::FailureMode::kFailOpen;
  if (s == "fail_closed") return entity::FailureMode::kFailClosed;
  if (s == "local_fallback") return entity::FailureMode::kLocalFallback;
  return std::nullopt;
}

}  // namespace

PgPolicyRepository::PgPolicyRepository(std::string connection_url)
    : connection_url_(std::move(connection_url)) {}

ConfigData PgPolicyRepository::LoadConfig() {
  try {
    pqxx::connection connection(connection_url_);
    // REPEATABLE READ: all queries below see the same snapshot of the data,
    // even if an admin changes a policy halfway through the load.
    pqxx::transaction<pqxx::isolation_level::repeatable_read, pqxx::write_policy::read_only> tx(
        connection);

    ConfigData data;
    data.version = tx.query_value<std::int64_t>("SELECT version FROM config_state");

    // 1) The policy rows.
    std::unordered_map<std::string, std::size_t> index_by_name;
    for (const auto& row :
         tx.exec("SELECT name, domain, algorithm::text, requests_per_unit, unit::text, burst, "
                 "failure_mode::text FROM policies ORDER BY name")) {
      auto name = row[0].as<std::string>();
      auto algorithm = ParseAlgorithm(row[2].as<std::string>());
      auto unit = ParseUnit(row[4].as<std::string>());
      auto failure_mode = ParseFailureMode(row[6].as<std::string>());
      if (!algorithm || !unit || !failure_mode) {
        spdlog::warn("skipping policy '{}': unsupported algorithm, unit or failure mode", name);
        continue;
      }

      entity::Policy policy;
      policy.name = name;
      policy.domain = row[1].as<std::string>();
      policy.algorithm = *algorithm;
      policy.requests_per_unit = row[3].as<std::uint32_t>();
      policy.unit = *unit;
      if (!row[5].is_null()) policy.burst = row[5].as<std::uint32_t>();
      policy.failure_mode = *failure_mode;

      index_by_name[name] = data.policies.size();
      data.policies.push_back(std::move(policy));
    }

    // 2) Their match patterns. `match` is a JSONB array like
    //    [{"key":"tenant","value":""},{"key":"route","value":"orders"}];
    //    PostgreSQL unpacks it into one row per entry, in array order.
    for (const auto& row :
         tx.exec("SELECT p.name, m.entry->>'key', coalesce(m.entry->>'value', '') "
                 "FROM policies p, jsonb_array_elements(p.match) WITH ORDINALITY AS m(entry, ord) "
                 "ORDER BY p.name, m.ord")) {
      auto it = index_by_name.find(row[0].as<std::string>());
      if (it == index_by_name.end()) continue;  // policy was skipped above
      data.policies[it->second].match.push_back({row[1].as<std::string>(), row[2].as<std::string>()});
    }

    // 3) Active API keys (hashes only) and the tenant each belongs to.
    for (const auto& row : tx.exec(
             "SELECT k.key_hash, t.slug FROM api_keys k JOIN tenants t ON t.id = k.tenant_id "
             "WHERE k.revoked_at IS NULL")) {
      auto hash = row[0].as<pqxx::bytes>();
      data.tenant_by_key_hash.emplace(
          std::string(reinterpret_cast<const char*>(hash.data()), hash.size()),
          row[1].as<std::string>());
    }

    tx.commit();
    return data;
  } catch (const pqxx::broken_connection& e) {
    // Never log connection_url_: it contains the database password.
    throw service::Unavailable(std::string("PostgreSQL unreachable: ") + e.what());
  }
}

}  // namespace quotient::repository
