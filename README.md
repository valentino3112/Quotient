# Quotient

A global rate limiting and quota service for [Envoy](https://www.envoyproxy.io/), written in C++20.

Envoy asks Quotient before forwarding each request: *"may this client make this call?"*.
Quotient answers `OK` or `OVER_LIMIT`, and Envoy turns `OVER_LIMIT` into an HTTP `429 Too Many
Requests`. Counters live in Redis, so any number of Quotient nodes enforce the same limits.
Tenants, API keys and policies live in PostgreSQL.

> **Status:** work in progress (course project, deadline 27 September 2026). See
> [Project status](#project-status) for what works today. At the moment Quotient answers `OK`
> to every request; real enforcement is being built.

## How it works

```mermaid
flowchart LR
    client([Client]) -->|HTTP :8080<br/>x-api-key| envoy[Envoy]
    envoy -->|allowed| backend[Backend API<br/>http-echo :5678]
    envoy -->|gRPC ShouldRateLimit<br/>:8081, 20 ms timeout| quotient[Quotient]
    quotient -->|Lua scripts<br/>per request| redis[(Redis<br/>counters)]
    quotient -.->|at startup / on change| postgres[(PostgreSQL<br/>tenants, keys, policies)]
    admin([Operator]) -.->|REST :8000<br/>planned| envoy
    envoy -.->|gRPC AdminService<br/>:8082, planned| quotient
```

1. **Hot path (every request).** Envoy's rate limit filter sends *descriptors*, for example
   `[api_key=qk_demo_acme]` or `[api_key=..., route=orders]`, to Quotient over gRPC, using
   Envoy's standard Rate Limit Service v3 API.
2. **Decision.** Quotient hashes the API key (SHA-256), maps it to its tenant, finds the
   most specific matching policy, and updates the counter in Redis with an atomic Lua script
   (fixed window or GCRA). Redis's own clock is used, so nodes never compare clocks.
3. **Control path.** Policies and key hashes are loaded from PostgreSQL into an in-memory
   snapshot. PostgreSQL is never called per request.
4. **Admin API (planned).** A gRPC `AdminService` manages tenants, keys and policies. Envoy
   exposes it as REST/JSON through its gRPC-JSON transcoder.

If Quotient is unreachable or too slow, Envoy lets the request through (fail open). Each
policy can also choose what happens when Redis is down: `fail_open` or `fail_closed`.

## Project status

| Area | State |
|---|---|
| Envoy RLS v3 proto subset + gRPC code generation | ✅ Done |
| gRPC server on :8081 with health checks, reflection, graceful shutdown | ✅ Done |
| Configuration from environment variables | ✅ Done |
| PostgreSQL schema and demo seed | ✅ Done |
| Docker Compose stack (PostgreSQL, Redis, Envoy, backend, Quotient) | ✅ Done |
| Envoy edge listener calling Quotient ("walking skeleton") | ✅ Done |
| Real enforcement: policies from PostgreSQL, Redis counters, 429s | 🚧 In progress |
| Admin API (gRPC + REST) with central error handling | ⏳ Planned |
| Live policy reload, metrics, circuit breaker, benchmarks | ⏳ After the deadline |

## Repository layout

One folder per service or sub-project:

```
rate-limit-service/   The Quotient service (C++20, CMake, vcpkg)
  proto/              Envoy RLS v3 subset (+ admin API later)
  src/web/            gRPC controllers: translate messages, no business logic
  src/service/        Business logic (decision engine, policies), domain errors
  src/entity/         Plain data types
  src/repository/     PostgreSQL and Redis access, behind interfaces
  src/util/           Configuration and helpers
  Dockerfile          Two-stage image build (builder + slim runtime)
database/             SQL schema (0001_init.sql) and demo data (0002_seed.sql)
deploy/               docker-compose.yml, .env.example
gateway/              Envoy configuration (envoy.yaml)
docs/                 Architecture documentation
Makefile              Shortcuts for the Compose stack
```

## Quickstart (Docker)

Requirements: Docker with Compose v2, `make`, `curl`.

```bash
make up        # build the images and start everything in the background
make ps        # all services up, postgres "healthy"
make smoke     # a request with and without an API key, plus Envoy's rate limit stats
```

The first build compiles all C++ dependencies with vcpkg and takes a long time (about
40 minutes on our machine). Later builds reuse the cached dependency layer.

Try it by hand:

```bash
curl -i localhost:8080/v1/items                                   # anonymous
curl -i -H 'x-api-key: qk_demo_acme' localhost:8080/v1/items      # demo tenant "acme"
curl -s localhost:9901/stats | grep ratelimit                     # Envoy's view of Quotient
make logs-quotient                                                # one line per call
```

Stop with `make down`. It also deletes the database volume, so the schema and seed run again
on the next start.

To override the dev defaults (database credentials, log level), copy `deploy/.env.example` to
`deploy/.env`. Never commit `.env`.

### Demo data

| Tenant | API key | Policy |
|---|---|---|
| acme | `qk_demo_acme` | `tenant-default`: GCRA 100/s |
| initech | `qk_demo_initech` | `tenant-default`: GCRA 100/s |
| globex | `qk_demo_globex` | `tenant-globex`: GCRA 20/s |
| *(no key or unknown key)* | | `anonymous`: fixed window 10/min, fail closed |

Every tenant also gets `orders-per-tenant` (GCRA 50/s) on `/v1/orders`. The database stores
only SHA-256 hashes of the keys.

## Building locally (development)

Requirements: Linux or WSL2, GCC with C++20, CMake ≥ 3.25, Ninja, and
[vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set. Dependencies are declared
in `rate-limit-service/vcpkg.json` and installed automatically on the first configure.

```bash
cd rate-limit-service
cmake --preset debug
cmake --build --preset debug

QUOTIENT_PG_URL=postgresql://quotient:quotient@localhost:5432/quotient \
SPDLOG_LEVEL=debug ./build/debug/src/quotient
```

Call it directly with [grpcurl](https://github.com/fullstorydev/grpcurl) (server reflection is
enabled, so no `.proto` files are needed):

```bash
grpcurl -plaintext localhost:8081 list
grpcurl -plaintext -d '{"domain":"api","descriptors":[{"entries":[{"key":"api_key","value":"qk_demo_acme"}]}]}' \
  localhost:8081 envoy.service.ratelimit.v3.RateLimitService/ShouldRateLimit
```

### Configuration

| Variable | Default | Meaning |
|---|---|---|
| `QUOTIENT_PG_URL` | *required* | PostgreSQL connection URL |
| `QUOTIENT_RLS_ADDR` | `0.0.0.0:8081` | Rate Limit Service gRPC listener |
| `QUOTIENT_REDIS_URL` | `tcp://redis:6379` | Redis address |
| `QUOTIENT_REDIS_TIMEOUT_MS` | `5` | Redis socket timeout (must stay well below Envoy's 20 ms) |
| `QUOTIENT_RLS_MAX_THREADS` | `32` | Maximum gRPC worker threads |
| `SPDLOG_LEVEL` | `info` | Log level; `debug` logs one line per rate limit call |

## Design principles

- **Layers.** `web` (gRPC controllers) → `service` (logic) → `repository` (I/O). `entity`
  holds plain data.
- **Inversion of control.** Dependencies are interfaces passed through constructors. Only
  `main.cpp` creates concrete classes and wires them together.
- **Logging.** spdlog only. API keys and tokens are never logged.
- **Errors.** Services throw domain exceptions. One central function turns them into gRPC
  status codes, which Envoy turns into HTTP errors with a message.
- **Stateless.** Nodes keep no per-client state. Counters are in Redis and configuration is
  in PostgreSQL, so nodes can be scaled freely.

## License

Apache-2.0 (see the SPDX headers in each source file). `proto/envoy/service/ratelimit/v3/rls.proto`
is derived from [Envoy](https://github.com/envoyproxy/envoy) (Apache-2.0).
