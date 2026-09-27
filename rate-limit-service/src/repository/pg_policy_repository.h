// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

#include "repository/policy_repository.h"

namespace quotient::repository {

// PolicyRepository backed by PostgreSQL (libpqxx).
// Opens a short-lived connection per load: loads are rare (startup, reload),
// and a pqxx::connection is not thread-safe, so nothing is shared.
class PgPolicyRepository final : public PolicyRepository {
 public:
  explicit PgPolicyRepository(std::string connection_url);

  ConfigData LoadConfig() override;

 private:
  std::string connection_url_;
};

}  // namespace quotient::repository
