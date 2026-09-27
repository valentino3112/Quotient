// SPDX-License-Identifier: Apache-2.0
#include "repository/pg_tenant_repository.h"

#include <pqxx/pqxx>
#include <utility>

#include "service/errors.h"

namespace quotient::repository {

namespace {

// Same columns, same order, in every query below.
constexpr const char* kColumns =
    "id::text, slug, display_name, extract(epoch FROM created_at)::bigint";

template <typename Row>
entity::Tenant ToTenant(const Row& row) {
  return {row[0].template as<std::string>(), row[1].template as<std::string>(),
          row[2].template as<std::string>(), row[3].template as<std::int64_t>()};
}

}  // namespace

PgTenantRepository::PgTenantRepository(std::string connection_url)
    : connection_url_(std::move(connection_url)) {}

entity::Tenant PgTenantRepository::Create(const std::string& slug,
                                          const std::string& display_name) {
  try {
    pqxx::connection connection(connection_url_);
    pqxx::work tx(connection);
    // $1, $2: parameters are sent separately from the SQL text, so a slug can
    // never be interpreted as SQL (no SQL injection).
    auto row = tx.exec(std::string("INSERT INTO tenants (slug, display_name) VALUES ($1, $2) "
                                   "RETURNING ") + kColumns,
                       pqxx::params{tx, slug, display_name})
                   .one_row();
    entity::Tenant tenant = ToTenant(row);
    tx.commit();
    return tenant;
  } catch (const pqxx::unique_violation&) {
    throw service::AlreadyExists("tenant '" + slug + "' already exists");
  } catch (const pqxx::broken_connection& e) {
    throw service::Unavailable(std::string("PostgreSQL unreachable: ") + e.what());
  }
}

std::optional<entity::Tenant> PgTenantRepository::FindBySlug(const std::string& slug) {
  try {
    pqxx::connection connection(connection_url_);
    pqxx::read_transaction tx(connection);
    auto result = tx.exec(std::string("SELECT ") + kColumns + " FROM tenants WHERE slug = $1",
                          pqxx::params{tx, slug});
    if (result.empty()) return std::nullopt;
    return ToTenant(result[0]);
  } catch (const pqxx::broken_connection& e) {
    throw service::Unavailable(std::string("PostgreSQL unreachable: ") + e.what());
  }
}

std::vector<entity::Tenant> PgTenantRepository::ListAll() {
  try {
    pqxx::connection connection(connection_url_);
    pqxx::read_transaction tx(connection);
    std::vector<entity::Tenant> tenants;
    for (const auto& row : tx.exec(std::string("SELECT ") + kColumns +
                                   " FROM tenants ORDER BY slug")) {
      tenants.push_back(ToTenant(row));
    }
    return tenants;
  } catch (const pqxx::broken_connection& e) {
    throw service::Unavailable(std::string("PostgreSQL unreachable: ") + e.what());
  }
}

}  // namespace quotient::repository
