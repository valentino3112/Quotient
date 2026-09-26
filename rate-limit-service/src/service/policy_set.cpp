// SPDX-License-Identifier: Apache-2.0
#include "service/policy_set.h"

#include <algorithm>
#include <utility>

#include "util/sha256.h"

namespace quotient::service {

namespace {

constexpr std::string_view kAnonymousTenant = "anonymous";

// Index key: the domain plus the ordered list of keys, e.g. "api\ntenant|route".
std::string IndexKey(std::string_view domain, const std::vector<entity::DescriptorEntry>& entries) {
  std::string key(domain);
  key += '\n';
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (i > 0) key += '|';
    key += entries[i].key;
  }
  return key;
}

// True if `a` is more specific than `b` (both have the same keys). Compared
// position by position, so an exact value in an earlier key weighs more:
// (tenant=acme, route=*) beats (tenant=*, route=orders).
bool MoreSpecific(const entity::Policy* a, const entity::Policy* b) {
  for (std::size_t i = 0; i < a->match.size(); ++i) {
    bool a_exact = !a->match[i].value.empty();
    bool b_exact = !b->match[i].value.empty();
    if (a_exact != b_exact) return a_exact;
  }
  return false;
}

bool Matches(const entity::Policy& policy, const entity::Descriptor& descriptor) {
  // Keys are already equal (same index bucket); compare the non-wildcard values.
  for (std::size_t i = 0; i < policy.match.size(); ++i) {
    const std::string& wanted = policy.match[i].value;
    if (!wanted.empty() && wanted != descriptor.entries[i].value) return false;
  }
  return true;
}

}  // namespace

PolicySet::PolicySet(std::vector<entity::Policy> policies,
                     std::unordered_map<std::string, std::string> tenant_by_key_hash,
                     std::int64_t version)
    : policies_(std::move(policies)),
      tenant_by_key_hash_(std::move(tenant_by_key_hash)),
      version_(version) {
  // policies_ is complete and never modified again, so these pointers stay valid.
  for (const entity::Policy& policy : policies_) {
    index_[IndexKey(policy.domain, policy.match)].push_back(&policy);
  }
  for (auto& [key, candidates] : index_) {
    std::sort(candidates.begin(), candidates.end(), MoreSpecific);
  }
}

entity::Descriptor PolicySet::Normalize(const entity::Descriptor& descriptor) const {
  entity::Descriptor normalized;
  normalized.entries.reserve(descriptor.entries.size());
  for (const entity::DescriptorEntry& entry : descriptor.entries) {
    if (entry.key == "api_key") {
      auto it = tenant_by_key_hash_.find(util::Sha256(entry.value));
      std::string tenant =
          it != tenant_by_key_hash_.end() ? it->second : std::string(kAnonymousTenant);
      normalized.entries.push_back({"tenant", std::move(tenant)});
    } else if (entry.key == "header_match" && entry.value == "no_api_key") {
      normalized.entries.push_back({"tenant", std::string(kAnonymousTenant)});
    } else {
      normalized.entries.push_back(entry);
    }
  }
  return normalized;
}

const entity::Policy* PolicySet::Match(std::string_view domain,
                                       const entity::Descriptor& descriptor) const {
  auto it = index_.find(IndexKey(domain, descriptor.entries));
  if (it == index_.end()) return nullptr;
  for (const entity::Policy* candidate : it->second) {  // most specific first
    if (Matches(*candidate, descriptor)) return candidate;
  }
  return nullptr;
}

}  // namespace quotient::service
