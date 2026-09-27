// SPDX-License-Identifier: Apache-2.0
#include "web/rate_limit_grpc_service.h"

#include <spdlog/spdlog.h>

#include <vector>

namespace quotient::web {

namespace rls = envoy::service::ratelimit::v3;

//https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/ratelimit/v3/rls.proto

namespace {

rls::RateLimitResponse::Code ToProto(entity::DecisionCode code) {
  return code == entity::DecisionCode::kOverLimit ? rls::RateLimitResponse::OVER_LIMIT
                                                  : rls::RateLimitResponse::OK;
}

rls::RateLimitResponse::RateLimit::Unit ToProto(entity::RateUnit unit) {
  switch (unit) {
    case entity::RateUnit::kSecond: return rls::RateLimitResponse::RateLimit::SECOND;
    case entity::RateUnit::kMinute: return rls::RateLimitResponse::RateLimit::MINUTE;
    case entity::RateUnit::kHour: return rls::RateLimitResponse::RateLimit::HOUR;
    case entity::RateUnit::kDay: return rls::RateLimitResponse::RateLimit::DAY;
  }
  return rls::RateLimitResponse::RateLimit::UNKNOWN;
}

// Protobuf -> entities.
std::vector<entity::Descriptor> FromProto(const rls::RateLimitRequest& request) {
  std::vector<entity::Descriptor> descriptors;
  descriptors.reserve(request.descriptors_size());
  for (const rls::RateLimitDescriptor& proto : request.descriptors()) {
    entity::Descriptor descriptor;
    for (const auto& entry : proto.entries()) {
      descriptor.entries.push_back({entry.key(), entry.value()});
    }
    descriptors.push_back(std::move(descriptor));
  }
  return descriptors;
}

// Entities -> protobuf. Envoy builds the x-ratelimit-* headers from these.
void ToProto(const service::RateLimitOutcome& outcome, rls::RateLimitResponse* response) {
  response->set_overall_code(ToProto(outcome.overall));
  for (const entity::Decision& decision : outcome.decisions) {
    rls::RateLimitResponse::DescriptorStatus* status = response->add_statuses();
    status->set_code(ToProto(decision.code));
    if (!decision.limit) continue;  // unlimited: nothing more to report
    status->mutable_current_limit()->set_name(decision.limit->policy_name);
    status->mutable_current_limit()->set_requests_per_unit(decision.limit->requests_per_unit);
    status->mutable_current_limit()->set_unit(ToProto(decision.limit->unit));
    status->set_limit_remaining(decision.remaining);
    auto millis = decision.reset_after.count();
    status->mutable_duration_until_reset()->set_seconds(millis / 1000);
    status->mutable_duration_until_reset()->set_nanos(static_cast<int>(millis % 1000) * 1000000);
  }
}

}  // namespace

RateLimitGrpcService::RateLimitGrpcService(service::DecisionEngine& engine) : engine_(engine) {}

grpc::Status RateLimitGrpcService::ShouldRateLimit(grpc::ServerContext* context,
                                                   const rls::RateLimitRequest* request,
                                                   rls::RateLimitResponse* response) {
  if (request->domain().empty() || request->descriptors_size() == 0) {
    return {grpc::StatusCode::INVALID_ARGUMENT, "domain and descriptors are required"};
  }

  // Debug level: this runs on every request. Never log descriptor values,
  // they contain API keys.
  spdlog::debug("ShouldRateLimit domain={} descriptors={}", request->domain(),
                request->descriptors_size());

  try {
    std::uint32_t hits = request->hits_addend() == 0 ? 1 : request->hits_addend();
    service::RateLimitOutcome outcome =
        engine_.Decide(request->domain(), FromProto(*request), hits, context->deadline());
    ToProto(outcome, response);
  } catch (const std::exception& e) {
    // A bug must never block traffic: answer OK for every descriptor.
    spdlog::error("ShouldRateLimit failed, allowing the request: {}", e.what());
    response->Clear();
    for (int i = 0; i < request->descriptors_size(); ++i) {
      response->add_statuses()->set_code(rls::RateLimitResponse::OK);
    }
    response->set_overall_code(rls::RateLimitResponse::OK);
  }
  return grpc::Status::OK;
}

}  // namespace quotient::web
