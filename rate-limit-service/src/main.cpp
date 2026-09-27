// SPDX-License-Identifier: Apache-2.0
//
// Composition root: the only place that constructs concrete classes and wires them.

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <spdlog/cfg/env.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "repository/pg_policy_repository.h"
#include "repository/pg_tenant_repository.h"
#include "repository/redis_rate_limit_backend.h"
#include "service/decision_engine.h"
#include "service/errors.h"
#include "service/policy_service.h"
#include "service/policy_store.h"
#include "service/tenant_service.h"
#include "util/clock.h"
#include "util/config.h"
#include "web/admin_grpc_service.h"
#include "web/rate_limit_grpc_service.h"

namespace {

constexpr auto kShutdownGrace = std::chrono::seconds(5);
constexpr int kStartupLoadAttempts = 10;
constexpr int kAdminMaxThreads = 4;  // admin traffic is rare

// Runs a startup step that needs PostgreSQL or Redis. They may still be
// starting (docker compose up), so retry with exponential backoff:
// 0.5 s, 1 s, 2 s, 4 s, then every 5 s. Returns false after the last attempt.
template <typename Step>
bool RetryAtStartup(std::string_view what, Step&& step) {
  auto delay = std::chrono::milliseconds(500);
  for (int attempt = 1; attempt <= kStartupLoadAttempts; ++attempt) {
    try {
      step();
      return true;
    } catch (const quotient::service::Unavailable& e) {
      spdlog::warn("{}: attempt {}/{} failed: {}", what, attempt, kStartupLoadAttempts, e.what());
    }
    std::this_thread::sleep_for(delay);
    delay = std::min(delay * 2, std::chrono::milliseconds(5000));
  }
  return false;
}

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

  // Wiring: concrete classes are created here and passed down as interfaces.
  quotient::repository::PgPolicyRepository policy_repository(config.pg_url);
  quotient::service::PolicyStore policy_store;
  quotient::service::PolicyService policy_service(policy_repository, policy_store);
  quotient::repository::RedisRateLimitBackend rate_limit_backend(
      config.redis_url, config.redis_timeout, static_cast<std::size_t>(config.rls_max_threads));

  // Before the gRPC server starts: it only accepts calls once the policies and
  // the Redis scripts are loaded.
  try {
    if (!RetryAtStartup("loading configuration from PostgreSQL",
                        [&] { policy_service.Reload(); }) ||
        !RetryAtStartup("loading Lua scripts into Redis",
                        [&] { rate_limit_backend.LoadScripts(); })) {
      spdlog::critical("startup failed: a dependency stayed unreachable, giving up");
      return 1;
    }
  } catch (const std::exception& e) {  // e.g. an SQL error: a bug, retrying will not help
    spdlog::critical("startup failed: {}", e.what());
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

  quotient::util::SystemClock clock;
  quotient::service::DecisionEngine decision_engine(policy_store, rate_limit_backend, clock);
  quotient::web::RateLimitGrpcService rls_service(decision_engine);

  quotient::repository::PgTenantRepository tenant_repository(config.pg_url);
  quotient::service::TenantService tenant_service(tenant_repository);
  quotient::web::AdminGrpcService admin_service(tenant_service, config.admin_token);

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
  // Policies and scripts are loaded (see above), so we are ready.
  rls_server->GetHealthCheckService()->SetServingStatus(true);
  spdlog::info("RLS gRPC server listening on {} (max {} threads)", config.rls_address, config.rls_max_threads);

  // A separate server and port for the admin API, with its own small thread
  // pool, so admin calls can never slow down rate limit decisions.
  grpc::ResourceQuota admin_quota("admin");
  admin_quota.SetMaxThreads(kAdminMaxThreads);
  grpc::ServerBuilder admin_builder;
  admin_builder.AddListeningPort(config.admin_address, grpc::InsecureServerCredentials());
  admin_builder.RegisterService(&admin_service);
  admin_builder.SetResourceQuota(admin_quota);
  std::unique_ptr<grpc::Server> admin_server = admin_builder.BuildAndStart();
  if (!admin_server) {
    spdlog::critical("failed to start admin server on {}", config.admin_address);
    return 1;
  }
  spdlog::info("admin gRPC server listening on {}", config.admin_address);

  int signal = WaitForShutdownSignal(shutdown_signals);
  spdlog::info("received signal {}, shutting down", signal);

  //Graceful shutdown
  rls_server->GetHealthCheckService()->SetServingStatus(false);
  auto shutdown_deadline = std::chrono::system_clock::now() + kShutdownGrace;
  admin_server->Shutdown(shutdown_deadline);
  rls_server->Shutdown(shutdown_deadline);
  spdlog::info("shutdown complete");
  return 0;
}
