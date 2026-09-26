// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace quotient::entity {

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
