// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "entity/tenant.h"

namespace quotient::repository {

// Storage of tenants. All methods throw service::Unavailable if the storage
// cannot be reached.
class TenantRepository {
 public:
  virtual ~TenantRepository() = default;

  // Throws service::AlreadyExists if the slug is taken.
  virtual entity::Tenant Create(const std::string& slug, const std::string& display_name) = 0;

  virtual std::optional<entity::Tenant> FindBySlug(const std::string& slug) = 0;

  virtual std::vector<entity::Tenant> ListAll() = 0;
};

}  // namespace quotient::repository
