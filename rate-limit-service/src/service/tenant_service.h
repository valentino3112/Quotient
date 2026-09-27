// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

#include "entity/tenant.h"
#include "repository/tenant_repository.h"

namespace quotient::service {

// Business rules for tenants. Throws domain exceptions (service/errors.h);
// the web layer turns them into gRPC/HTTP errors.
class TenantService {
 public:
  explicit TenantService(repository::TenantRepository& repository);

  // Throws InvalidArgument (bad slug or name) or AlreadyExists.
  entity::Tenant Create(const std::string& slug, const std::string& display_name);

  // Throws NotFound.
  entity::Tenant Get(const std::string& slug);

  std::vector<entity::Tenant> List();

 private:
  repository::TenantRepository& repository_;
};

}  // namespace quotient::service
