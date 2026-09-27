// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <grpcpp/grpcpp.h>

#include <string_view>
#include <utility>

namespace quotient::web {

// THE central exception handler of the admin API. Call it from inside a catch
// block: it rethrows the current exception and converts it to a gRPC status
// with a clear message. Envoy's transcoder (convert_grpc_status: true) turns
// that status into the matching HTTP code with the message in a JSON body:
//
//   service::InvalidArgument -> INVALID_ARGUMENT -> HTTP 400
//   service::Unauthenticated -> UNAUTHENTICATED  -> HTTP 401
//   service::NotFound        -> NOT_FOUND        -> HTTP 404
//   service::AlreadyExists   -> ALREADY_EXISTS   -> HTTP 409
//   service::VersionConflict -> ABORTED          -> HTTP 409
//   service::Unavailable     -> UNAVAILABLE      -> HTTP 503
//   anything else            -> INTERNAL         -> HTTP 500 (details only in the log)
grpc::Status StatusFromCurrentException(std::string_view rpc_name);

// Runs one admin handler body and maps any exception it throws. Every admin
// RPC goes through this, so no handler needs its own try/catch.
template <typename Body>
grpc::Status HandleErrors(std::string_view rpc_name, Body&& body) {
  try {
    std::forward<Body>(body)();
    return grpc::Status::OK;
  } catch (...) {
    return StatusFromCurrentException(rpc_name);
  }
}

}  // namespace quotient::web
