// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdexcept>

namespace quotient::service {

// Domain exceptions thrown by services and repositories. The web layer maps
// each one to a gRPC status in ONE place (web/error_mapping, step 4):
//   InvalidArgument -> INVALID_ARGUMENT   NotFound        -> NOT_FOUND
//   AlreadyExists   -> ALREADY_EXISTS     VersionConflict -> ABORTED
//   Unauthenticated -> UNAUTHENTICATED    Unavailable     -> UNAVAILABLE
// Anything else becomes INTERNAL.
class DomainError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class InvalidArgument : public DomainError {
 public:
  using DomainError::DomainError;
};

class NotFound : public DomainError {
 public:
  using DomainError::DomainError;
};

class AlreadyExists : public DomainError {
 public:
  using DomainError::DomainError;
};

class VersionConflict : public DomainError {
 public:
  using DomainError::DomainError;
};

class Unauthenticated : public DomainError {
 public:
  using DomainError::DomainError;
};

// A dependency (PostgreSQL, Redis) is down or too slow.
class Unavailable : public DomainError {
 public:
  using DomainError::DomainError;
};

}  // namespace quotient::service
