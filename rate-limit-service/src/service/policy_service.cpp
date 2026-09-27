// SPDX-License-Identifier: Apache-2.0
#include "service/policy_service.h"

#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace quotient::service {

PolicyService::PolicyService(repository::PolicyRepository& repository, PolicyStore& store)
    : repository_(repository), store_(store) {}

void PolicyService::Reload() {
  repository::ConfigData data = repository_.LoadConfig();
  for (const entity::Policy& policy : data.policies) {
    std::string pattern;
    for (const entity::DescriptorEntry& entry : policy.match) {
      if (!pattern.empty()) pattern += '|';
      pattern += entry.key + '=' + (entry.value.empty() ? "*" : entry.value);
    }
    spdlog::debug("policy {} domain={} match={} limit={}", policy.name, policy.domain, pattern,
                  policy.requests_per_unit);
  }
  std::size_t key_count = data.tenant_by_key_hash.size();
  auto snapshot = std::make_shared<const PolicySet>(
      std::move(data.policies), std::move(data.tenant_by_key_hash), data.version);
  spdlog::info("loaded configuration version {}: {} policies, {} active API keys",
               snapshot->version(), snapshot->policy_count(), key_count);
  store_.Publish(std::move(snapshot));
}

}  // namespace quotient::service
