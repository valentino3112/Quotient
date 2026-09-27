// SPDX-License-Identifier: Apache-2.0
#include "web/error_mapping.h"

#include <spdlog/spdlog.h>

#include <exception>

#include "service/errors.h"

namespace quotient::web {

grpc::Status StatusFromCurrentException(std::string_view rpc_name) {
  try {
    throw;  // rethrow the exception being handled, to find out its type
  } catch (const service::InvalidArgument& e) {
    return {grpc::StatusCode::INVALID_ARGUMENT, e.what()};
  } catch (const service::Unauthenticated& e) {
    return {grpc::StatusCode::UNAUTHENTICATED, e.what()};
  } catch (const service::NotFound& e) {
    return {grpc::StatusCode::NOT_FOUND, e.what()};
  } catch (const service::AlreadyExists& e) {
    return {grpc::StatusCode::ALREADY_EXISTS, e.what()};
  } catch (const service::VersionConflict& e) {
    return {grpc::StatusCode::ABORTED, e.what()};
  } catch (const service::Unavailable& e) {
    spdlog::warn("{}: {}", rpc_name, e.what());
    return {grpc::StatusCode::UNAVAILABLE, "a dependency is unavailable, try again later"};
  } catch (const std::exception& e) {
    // A bug: log the details, but never send internals to the client.
    spdlog::error("{} failed: {}", rpc_name, e.what());
    return {grpc::StatusCode::INTERNAL, "internal error"};
  } catch (...) {
    spdlog::error("{} failed with an unknown exception", rpc_name);
    return {grpc::StatusCode::INTERNAL, "internal error"};
  }
}

}  // namespace quotient::web
