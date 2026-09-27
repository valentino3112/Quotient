// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <grpcpp/grpcpp.h>

#include <string>

#include "quotient/admin/v1/admin.grpc.pb.h"
#include "service/tenant_service.h"

namespace quotient::web {

// gRPC controller for the admin API (also served as REST by Envoy).
// Each handler: check the token, translate the request, call the service,
// translate the result. Errors go through HandleErrors (error_mapping.h).
class AdminGrpcService final : public quotient::admin::v1::AdminService::Service {
 public:
  AdminGrpcService(service::TenantService& tenant_service, std::string admin_token);

  grpc::Status CreateTenant(grpc::ServerContext* context,
                            const quotient::admin::v1::CreateTenantRequest* request,
                            quotient::admin::v1::Tenant* response) override;

  grpc::Status GetTenant(grpc::ServerContext* context,
                         const quotient::admin::v1::GetTenantRequest* request,
                         quotient::admin::v1::Tenant* response) override;

  grpc::Status ListTenants(grpc::ServerContext* context,
                           const quotient::admin::v1::ListTenantsRequest* request,
                           quotient::admin::v1::ListTenantsResponse* response) override;

 private:
  // Throws service::Unauthenticated unless the call carries
  // "authorization: Bearer <admin token>".
  void Authenticate(const grpc::ServerContext& context) const;

  service::TenantService& tenant_service_;
  std::string admin_token_;
};

}  // namespace quotient::web
