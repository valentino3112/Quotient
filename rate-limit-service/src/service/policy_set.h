// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "entity/descriptor.h"
#include "entity/policy.h"

namespace quotient::service {

// Immutable snapshot of the configuration: all policies plus the API-key-hash
// to tenant map. Built once per (re)load, then only read, so any number of
// request threads can use it at the same time without locking.
class PolicySet {
 public:
  // tenant_by_key_hash: raw 32-byte SHA-256 of an active API key -> tenant slug.
  PolicySet(std::vector<entity::Policy> policies,
            std::unordered_map<std::string, std::string> tenant_by_key_hash, 
            std::int64_t version);

  // The index holds pointers into policies_, so copying or moving would leave
  // them dangling. Snapshots are shared through std::shared_ptr instead.
  PolicySet(const PolicySet&) = delete;
  PolicySet& operator=(const PolicySet&) = delete;

  // Replaces who the caller is with which tenant they belong to:
  //   api_key=<known key>        -> tenant=<slug>
  //   api_key=<unknown/revoked>  -> tenant=anonymous
  //   header_match=no_api_key    -> tenant=anonymous
  // Other entries are kept as they are. Raw keys never leave this function.
  entity::Descriptor Normalize(const entity::Descriptor& descriptor) const;

  // Most specific policy for a NORMALISED descriptor, or nullptr (unlimited).
  const entity::Policy* Match(std::string_view domain, const entity::Descriptor& descriptor) const;

  std::int64_t version() const { return version_; }
  std::size_t policy_count() const { return policies_.size(); }

 private:
  std::vector<entity::Policy> policies_;
  // "domain\nkey1|key2" -> candidate policies, most specific first.
  std::unordered_map<std::string, std::vector<const entity::Policy*>> index_;
  std::unordered_map<std::string, std::string> tenant_by_key_hash_;
  std::int64_t version_;
};

}  // namespace quotient::service
