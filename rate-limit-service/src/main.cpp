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

#include "util/config.h"
#include "web/rate_limit_grpc_service.h"

namespace {

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
  spdlog::cfg::load_env_levels();  // SPDLOG_LEVEL=debug shows one line per RLS call

  quotient::util::Config config;
  try {
    config = quotient::util::LoadConfigFromEnv();
  } catch (const std::exception& e) {
    spdlog::critical("invalid configuration: {}", e.what());
    return 1;
  }

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

  // Caps the worker threads so a traffic spike cannot exhaust the machine.
  grpc::ResourceQuota quota("rls");
  quota.SetMaxThreads(config.rls_max_threads);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(config.rls_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&rls_service);
  builder.SetResourceQuota(quota);

  std::unique_ptr<grpc::Server> rls_server = builder.BuildAndStart();
  if (!rls_server) {
    spdlog::critical("failed to start RLS server on {}", config.rls_address);
    return 1;
  }
  // Stub has nothing to load, so it is ready immediately.
  rls_server->GetHealthCheckService()->SetServingStatus(true);
  spdlog::info("RLS gRPC server listening on {} (max {} threads)", config.rls_address, config.rls_max_threads);

  int signal = WaitForShutdownSignal(shutdown_signals);
  spdlog::info("received signal {}, shutting down", signal);

  //Graceful shutdown
  rls_server->GetHealthCheckService()->SetServingStatus(false);
  rls_server->Shutdown(std::chrono::system_clock::now() + kShutdownGrace);
  spdlog::info("shutdown complete");
  return 0;
}
