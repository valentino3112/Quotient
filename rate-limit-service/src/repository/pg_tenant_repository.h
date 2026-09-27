// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "repository/tenant_repository.h"

namespace quotient::repository {

// TenantRepository backed by PostgreSQL. Like PgPolicyRepository, it opens a
// short-lived connection per call: admin traffic is rare, and a
// pqxx::connection must never be shared between threads.
class PgTenantRepository final : public TenantRepository {
 public:
  explicit PgTenantRepository(std::string connection_url);

  entity::Tenant Create(const std::string& slug, const std::string& display_name) override;
  std::optional<entity::Tenant> FindBySlug(const std::string& slug) override;
  std::vector<entity::Tenant> ListAll() override;

 private:
  std::string connection_url_;
};

}  // namespace quotient::repository
