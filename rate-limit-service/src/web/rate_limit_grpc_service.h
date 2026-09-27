// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <grpcpp/grpcpp.h>

#include "envoy/service/ratelimit/v3/rls.grpc.pb.h"
#include "service/decision_engine.h"

namespace quotient::web {

// gRPC controller for Envoy's Rate Limit Service. Only translates between
// protobuf messages and the service layer; the decision itself is made by
// the DecisionEngine.
class RateLimitGrpcService final
    : public envoy::service::ratelimit::v3::RateLimitService::Service {
 public:
  explicit RateLimitGrpcService(service::DecisionEngine& engine);

  // Never throws: any unexpected error answers OK (fail open) and is logged.
  grpc::Status ShouldRateLimit(grpc::ServerContext* context,
                               const envoy::service::ratelimit::v3::RateLimitRequest* request,
                               envoy::service::ratelimit::v3::RateLimitResponse* response) override;

 private:
  service::DecisionEngine& engine_;
};

}  // namespace quotient::web
