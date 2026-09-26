// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <grpcpp/grpcpp.h>

#include "envoy/service/ratelimit/v3/rls.grpc.pb.h"

namespace quotient::web {

// gRPC controller for Envoy's Rate Limit Service. Translates requests and
// responses only; the decision itself will come from the service layer.
// Stub for now: every descriptor is OK.
class RateLimitGrpcService final
    : public envoy::service::ratelimit::v3::RateLimitService::Service {
 public:
  grpc::Status ShouldRateLimit(grpc::ServerContext* context,
                               const envoy::service::ratelimit::v3::RateLimitRequest* request,
                               envoy::service::ratelimit::v3::RateLimitResponse* response) override;
};

}  // namespace quotient::web
