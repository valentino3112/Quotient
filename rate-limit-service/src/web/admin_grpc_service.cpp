// SPDX-License-Identifier: Apache-2.0
#include "web/admin_grpc_service.h"

#include <openssl/crypto.h>

#include <string_view>
#include <utility>

#include "service/errors.h"
#include "web/error_mapping.h"

namespace quotient::web {

namespace admin = quotient::admin::v1;

namespace {

void ToProto(const entity::Tenant& tenant, admin::Tenant* proto) {
  proto->set_id(tenant.id);
  proto->set_slug(tenant.slug);
  proto->set_display_name(tenant.display_name);
  proto->mutable_created_at()->set_seconds(tenant.created_at_unix);
}

}  // namespace

AdminGrpcService::AdminGrpcService(service::TenantService& tenant_service,
                                   std::string admin_token)
    : tenant_service_(tenant_service), admin_token_(std::move(admin_token)) {}

void AdminGrpcService::Authenticate(const grpc::ServerContext& context) const {
  // REST clients send a normal HTTP header; Envoy forwards it as gRPC metadata.
  const auto& metadata = context.client_metadata();
  auto it = metadata.find("authorization");
  if (it == metadata.end()) throw service::Unauthenticated("missing authorization header");

  std::string_view value(it->second.data(), it->second.size());
  constexpr std::string_view kPrefix = "Bearer ";
  if (value.substr(0, kPrefix.size()) != kPrefix) {
    throw service::Unauthenticated("authorization header must be 'Bearer <token>'");
  }
  std::string_view token = value.substr(kPrefix.size());
  // Constant-time comparison: a normal == stops at the first wrong character,
  // so response times would leak how much of a guessed token was right.
  if (token.size() != admin_token_.size() ||
      CRYPTO_memcmp(token.data(), admin_token_.data(), token.size()) != 0) {
    throw service::Unauthenticated("invalid admin token");
  }
}

grpc::Status AdminGrpcService::CreateTenant(grpc::ServerContext* context,
                                            const admin::CreateTenantRequest* request,
                                            admin::Tenant* response) {
  return HandleErrors("CreateTenant", [&] {
    Authenticate(*context);
    ToProto(tenant_service_.Create(request->slug(), request->display_name()), response);
  });
}

grpc::Status AdminGrpcService::GetTenant(grpc::ServerContext* context,
                                         const admin::GetTenantRequest* request,
                                         admin::Tenant* response) {
  return HandleErrors("GetTenant", [&] {
    Authenticate(*context);
    ToProto(tenant_service_.Get(request->slug()), response);
  });
}

grpc::Status AdminGrpcService::ListTenants(grpc::ServerContext* context,
                                           const admin::ListTenantsRequest* /*request*/,
                                           admin::ListTenantsResponse* response) {
  return HandleErrors("ListTenants", [&] {
    Authenticate(*context);
    for (const entity::Tenant& tenant : tenant_service_.List()) {
      ToProto(tenant, response->add_tenants());
    }
  });
}

}  // namespace quotient::web
