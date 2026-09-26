// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <string_view>

namespace quotient::util {

// Returns the raw 32-byte SHA-256 digest of `input` (not hex), matching what
// PostgreSQL's sha256() stores in api_keys.key_hash (bytea).
std::string Sha256(std::string_view input);

}  // namespace quotient::util
