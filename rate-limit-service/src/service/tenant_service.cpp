// SPDX-License-Identifier: Apache-2.0
#include "service/tenant_service.h"

#include <spdlog/spdlog.h>

#include <algorithm>

#include "service/errors.h"

namespace quotient::service {

namespace {

// Same rule as the CHECK constraint in database/0001_init.sql, checked here
// first so the caller gets a clear message instead of a database error.
bool IsValidSlug(const std::string& slug) {
  return slug.size() >= 2 && slug.size() <= 40 &&
         std::all_of(slug.begin(), slug.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
         });
}

}  // namespace

TenantService::TenantService(repository::TenantRepository& repository)
    : repository_(repository) {}

entity::Tenant TenantService::Create(const std::string& slug, const std::string& display_name) {
  if (!IsValidSlug(slug)) {
    throw InvalidArgument("slug must be 2-40 characters: lowercase letters, digits or '-'");
  }
  // "anonymous" is the pseudo-tenant for requests without a valid API key.
  if (slug == "anonymous") throw InvalidArgument("slug 'anonymous' is reserved");
  if (display_name.empty()) throw InvalidArgument("display_name is required");

  entity::Tenant tenant = repository_.Create(slug, display_name);
  spdlog::info("tenant created: {}", tenant.slug);
  return tenant;
}

entity::Tenant TenantService::Get(const std::string& slug) {
  auto tenant = repository_.FindBySlug(slug);
  if (!tenant) throw NotFound("tenant '" + slug + "' not found");
  return *tenant;
}

std::vector<entity::Tenant> TenantService::List() { return repository_.ListAll(); }

}  // namespace quotient::service
