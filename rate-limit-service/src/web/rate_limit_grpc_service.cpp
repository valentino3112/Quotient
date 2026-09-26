// SPDX-License-Identifier: Apache-2.0
#include "web/rate_limit_grpc_service.h"

#include <spdlog/spdlog.h>

namespace quotient::web {

namespace rls = envoy::service::ratelimit::v3;

grpc::Status RateLimitGrpcService::ShouldRateLimit(grpc::ServerContext* /*context*/,
                                                   const rls::RateLimitRequest* request,
                                                   rls::RateLimitResponse* response) 
{
  if (request->domain().empty() || request->descriptors_size() == 0) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "domain and descriptors are required"};
  }

  // Debug level: this runs on every request. Never log descriptor values,
  // they contain API keys.
  spdlog::debug("ShouldRateLimit domain={} descriptors={}", request->domain(), request->descriptors_size());

  // Envoy expects one status per descriptor, in request order.
  for (int i = 0; i < request->descriptors_size(); ++i) {
    response->add_statuses()->set_code(rls::RateLimitResponse::OK);
  }
  response->set_overall_code(rls::RateLimitResponse::OK);
  return grpc::Status::OK;
}

}  // namespace quotient::web
