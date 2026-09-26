// SPDX-License-Identifier: Apache-2.0
#include "util/sha256.h"

#include <openssl/evp.h>

#include <stdexcept>

namespace quotient::util {

std::string Sha256(std::string_view input) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (EVP_Digest(input.data(), input.size(), digest, &length, EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("EVP_Digest(SHA-256) failed");
  }
  return std::string(reinterpret_cast<const char*>(digest), length);
}

}  // namespace quotient::util
