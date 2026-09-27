// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "repository/policy_repository.h"
#include "service/policy_store.h"

namespace quotient::service {

// Keeps the in-memory configuration snapshot up to date.
// Both dependencies are passed in (constructor injection): this class does not
// know or care that the repository is PostgreSQL.
class PolicyService {
 public:
  PolicyService(repository::PolicyRepository& repository, PolicyStore& store);

  // Loads the configuration, builds a new snapshot and publishes it.
  // Throws Unavailable if the repository cannot be reached; the previous
  // snapshot (if any) then stays in use.
  void Reload();

 private:
  repository::PolicyRepository& repository_;
  PolicyStore& store_;
};

}  // namespace quotient::service
