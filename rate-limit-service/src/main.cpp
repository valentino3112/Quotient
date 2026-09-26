// SPDX-License-Identifier: Apache-2.0
//
// Composition root: the only place that constructs concrete classes and wires them.

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <csignal>
#include <memory>
#include <string>

#include "web/rate_limit_grpc_service.h"

namespace {

constexpr const char* kRlsAddress = "0.0.0.0:8081";  // moves to util/config in step 3
constexpr int kRlsMaxThreads = 32;
constexpr auto kShutdownGrace = std::chrono::seconds(5);

// Blocks until SIGINT (Ctrl+C) or SIGTERM (docker stop) arrives.
// The signals must already be blocked, see main().
int WaitForShutdownSignal(const sigset_t& signals) {
  int signal = 0;
  sigwait(&signals, &signal);
  return signal;
}

}  // namespace

int main() {
  //spdlog::cfg::load_env_levels(); // SPDLOG_LEVEL=debug
  spdlog::set_level(spdlog::level::debug); // Set *global* log level to debug

  // Block the shutdown signals before gRPC starts its threads, so every thread
  // inherits the mask and only our sigwait() receives them.
  //https://www.youtube.com/watch?v=J_Wq2Gb5hvc

  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

  quotient::web::RateLimitGrpcService rls_service;

  grpc::EnableDefaultHealthCheckService(true);
  grpc::reflection::InitProtoReflectionServerBuilderPlugin();


  grpc::ServerBuilder builder;
  builder.AddListeningPort(kRlsAddress, grpc::InsecureServerCredentials());
  builder.RegisterService(&rls_service);

  std::unique_ptr<grpc::Server> rls_server = builder.BuildAndStart();
  if (!rls_server) {
    spdlog::critical("failed to start RLS server on {}", kRlsAddress);
    return 1;
  }
  // Stub has nothing to load, so it is ready immediately.
  rls_server->GetHealthCheckService()->SetServingStatus(true);
  spdlog::info("RLS gRPC server listening on {}", kRlsAddress);

  int signal = WaitForShutdownSignal(shutdown_signals);
  spdlog::info("received signal {}, shutting down", signal);

  //Graceful shutdown
  rls_server->GetHealthCheckService()->SetServingStatus(false);
  rls_server->Shutdown(std::chrono::system_clock::now() + kShutdownGrace);
  spdlog::info("shutdown complete");
  return 0;
}
