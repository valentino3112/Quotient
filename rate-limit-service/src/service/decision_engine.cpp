// SPDX-License-Identifier: Apache-2.0
#include "service/decision_engine.h"

#include <spdlog/spdlog.h>

#include "service/errors.h"

namespace quotient::service {

namespace {

// Below this, Envoy will give up before Redis can answer: decide locally.
constexpr auto kMinTimeBudget = std::chrono::milliseconds(1);

std::chrono::milliseconds PeriodOf(entity::RateUnit unit) {
  switch (unit) {
    case entity::RateUnit::kSecond: return std::chrono::seconds(1);
    case entity::RateUnit::kMinute: return std::chrono::minutes(1);
    case entity::RateUnit::kHour: return std::chrono::hours(1);
    case entity::RateUnit::kDay: return std::chrono::hours(24);
  }
  return std::chrono::seconds(1);
}

// Redis key of the bucket: q:{<tenant>}:<algorithm>:<policy>:<k1>=<v1>|<k2>=<v2>
// - {tenant} is a Redis Cluster hash tag: all of a tenant's keys on one shard.
// - The algorithm is part of the key, so switching a policy from fixed window
//   (a hash) to GCRA (a string) never hits an old key of the wrong type.
// - Wildcard policies get one bucket per actual value, because the values
//   come from the normalised descriptor, not from the policy.
std::string BucketKey(const entity::Policy& policy, const entity::Descriptor& descriptor) {
  std::string tenant = "_";
  for (const entity::DescriptorEntry& entry : descriptor.entries) {
    if (entry.key == "tenant") tenant = entry.value;
  }
  std::string key = "q:{" + tenant + "}:";
  key += policy.algorithm == entity::Algorithm::kGcra ? "gcra" : "fw";
  key += ":" + policy.name + ":";
  for (std::size_t i = 0; i < descriptor.entries.size(); ++i) {
    if (i > 0) key += '|';
    key += descriptor.entries[i].key + "=" + descriptor.entries[i].value;
  }
  return key;
}

entity::AppliedLimit LimitOf(const entity::Policy& policy) {
  return {policy.name, policy.requests_per_unit, policy.unit};
}

}  // namespace

DecisionEngine::DecisionEngine(PolicyStore& policy_store, repository::RateLimitBackend& backend,
                               util::Clock& clock)
    : policy_store_(policy_store), backend_(backend), clock_(clock) {}

RateLimitOutcome DecisionEngine::Decide(const std::string& domain,
                                        const std::vector<entity::Descriptor>& descriptors,
                                        std::uint32_t hits,
                                        std::chrono::system_clock::time_point deadline) {
  // One snapshot for the whole request, even if a reload happens meanwhile.
  std::shared_ptr<const PolicySet> snapshot = policy_store_.Snapshot();
  RateLimitOutcome outcome;
  outcome.decisions.reserve(descriptors.size());
  for (const entity::Descriptor& descriptor : descriptors) {
    entity::Decision decision = snapshot
                                    ? DecideOne(*snapshot, domain, descriptor, hits, deadline)
                                    : entity::Decision{};  // no config yet: unlimited
    if (decision.code == entity::DecisionCode::kOverLimit) {
      outcome.overall = entity::DecisionCode::kOverLimit;
    }
    outcome.decisions.push_back(std::move(decision));
  }
  return outcome;
}

entity::Decision DecisionEngine::DecideOne(const PolicySet& snapshot, const std::string& domain,
                                           const entity::Descriptor& descriptor,
                                           std::uint32_t hits,
                                           std::chrono::system_clock::time_point deadline) {
  entity::Descriptor normalized = snapshot.Normalize(descriptor);
  const entity::Policy* policy = snapshot.Match(domain, normalized);
  if (policy == nullptr) return entity::Decision{};  // no policy: unlimited

  if (deadline - clock_.Now() < kMinTimeBudget) {
    return ApplyFailureMode(*policy, "no time left before the caller's deadline");
  }

  repository::BucketRequest request;
  request.key = BucketKey(*policy, normalized);
  request.algorithm = policy->algorithm;
  request.limit = policy->requests_per_unit;
  request.period = PeriodOf(policy->unit);
  request.burst = policy->burst.value_or(policy->requests_per_unit);
  request.hits = hits;

  try {
    repository::BucketResult result = backend_.Hit(request);
    entity::Decision decision;
    decision.code = result.allowed ? entity::DecisionCode::kOk : entity::DecisionCode::kOverLimit;
    decision.limit = LimitOf(*policy);
    decision.remaining = result.remaining;
    decision.reset_after = result.reset_after;
    return decision;
  } catch (const Unavailable& e) {
    return ApplyFailureMode(*policy, e.what());
  }
}

entity::Decision DecisionEngine::ApplyFailureMode(const entity::Policy& policy,
                                                  const std::string& reason) {
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    clock_.Now().time_since_epoch())
                    .count();
  auto last = last_warning_ms_.load();
  if (now_ms - last >= 1000 && last_warning_ms_.compare_exchange_strong(last, now_ms)) {
    spdlog::warn("counter unavailable, applying failure modes (policy {}): {}", policy.name,
                 reason);
  }

  entity::Decision decision;
  decision.limit = LimitOf(policy);
  switch (policy.failure_mode) {
    case entity::FailureMode::kFailClosed:
      decision.code = entity::DecisionCode::kOverLimit;  // safety first: reject
      break;
    case entity::FailureMode::kFailOpen:
    case entity::FailureMode::kLocalFallback:  // TODO(after deadline): in-memory limiter
      decision.code = entity::DecisionCode::kOk;  // availability first: allow
      break;
  }
  return decision;
}

}  // namespace quotient::service
