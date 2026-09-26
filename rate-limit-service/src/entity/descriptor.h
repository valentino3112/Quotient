// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace quotient::entity {


//https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/common/ratelimit/v3/ratelimit.proto#envoy-v3-api-msg-extensions-common-ratelimit-v3-ratelimitdescriptor
struct DescriptorEntry {
  std::string key;
  std::string value;
};

// An ordered list of entries, e.g. [api_key=K, route=orders]. Order matters:
// policies match only descriptors with the same keys in the same order.
struct Descriptor {
  std::vector<DescriptorEntry> entries;
};

}  // namespace quotient::entity
