# Quotient

A global rate limiting and quota service for [Envoy](https://www.envoyproxy.io/), written in C++20.

Envoy asks Quotient before forwarding each request: *"may this client make this call?"*.
Quotient answers `OK` or `OVER_LIMIT`, and Envoy turns `OVER_LIMIT` into an HTTP `429 Too Many
Requests`. Counters live in Redis, so any number of Quotient nodes enforce the same limits.
Tenants, API keys and policies live in PostgreSQL.

> **Status:** course project (deadline 27 September 2026). Rate limiting and a first version
> of the admin API (tenants, over gRPC and REST) work end to end. See
> [Project status](#project-status).

## How it works

```mermaid
flowchart LR
    client([Client]) -->|HTTP :8080<br/>x-api-key| envoy[Envoy]
    envoy -->|allowed| backend[Backend API<br/>http-echo :5678]
    envoy -->|gRPC ShouldRateLimit<br/>:8081, 20 ms timeout| quotient[Quotient]
    quotient -->|Lua scripts<br/>per request| redis[(Redis<br/>counters)]
    quotient -.->|at startup| postgres[(PostgreSQL<br/>tenants, keys, policies)]
    admin([Operator]) -->|REST :8000<br/>Bearer token| envoy
    envoy -->|gRPC AdminService<br/>:8082| quotient
    quotient -->|admin reads/writes| postgres
```

1. **Hot path (every request).** Envoy's rate limit filter sends *descriptors*, for example
   `[api_key=qk_demo_acme]` or `[api_key=..., route=orders]`, to Quotient over gRPC, using
   Envoy's standard Rate Limit Service v3 API.
2. **Decision.** Quotient hashes the API key (SHA-256), maps it to its tenant, finds the
   most specific matching policy, and updates the counter in Redis with an atomic Lua script
   (fixed window or GCRA). Redis's own clock is used, so nodes never compare clocks.
3. **Control path.** Policies and key hashes are loaded from PostgreSQL into an in-memory
   snapshot at startup. PostgreSQL is never called per request.
4. **Admin API.** A gRPC `AdminService` manages tenants (API keys and policies come
   later). Envoy exposes the same API as REST/JSON on port 8000 through its gRPC-JSON
   transcoder, using the `google.api.http` annotations in `admin.proto`. Errors are
   converted in one central place into gRPC status codes, which Envoy turns into HTTP
   errors with a message (for example `409` for a duplicate tenant).

If Quotient is unreachable or too slow, Envoy lets the request through (fail open). Each
policy also chooses what happens when Redis is down: `fail_open` (allow) or `fail_closed`
(reject).

## Project status

| Area | State |
|---|---|
| Envoy RLS v3 proto subset and gRPC code generation | Done |
| gRPC server on :8081 with health checks, reflection, graceful shutdown | Done |
| Configuration from environment variables | Done |
| PostgreSQL schema and demo seed | Done |
| Docker Compose stack (PostgreSQL, Redis, Envoy, backend, Quotient) | Done |
| Rate limiting: policies from PostgreSQL, Redis counters (GCRA, fixed window), 429s | Done |
| Failure modes when Redis is down (`fail_open`, `fail_closed`) | Done, see [known limitations](#known-limitations) |
| Admin API over gRPC and REST: tenants (create, get, list), bearer token, central error handling | Done |
| Admin API: API keys, policies, usage | After the deadline |
| Live policy reload, circuit breaker, metrics, benchmarks | After the deadline |

## Try it: from clone to 429

### 1. Requirements

- **Docker** with Compose v2. On Windows, use Docker Desktop with WSL integration enabled for
  your distribution, and run every command below inside WSL.
- `git`, `make` and `curl`.

### 2. Clone and start

```bash
git clone https://github.com/valentino3112/Quotient.git
cd Quotient
make up
```

`make up` builds the Quotient image and starts five containers: PostgreSQL, Redis, Envoy, the
backend and Quotient. **The first build takes a long time** (about 40 minutes on our machine)
because vcpkg compiles gRPC and the other C++ dependencies. Later builds reuse the cached
dependency layer and take seconds.

### 3. Check that everything is up

```bash
make ps
make logs-quotient      # Ctrl+C to stop following
```

All services should be `Up`, and PostgreSQL `healthy`. Quotient's log should end with:

```
loaded configuration version ...: 4 policies, 3 active API keys
Redis scripts loaded (...)
RLS gRPC server listening on 0.0.0.0:8081 (max 32 threads)
```

### 4. Make a request

```bash
curl -i -H 'x-api-key: qk_demo_globex' localhost:8080/v1/items
```

You get `200 OK` with `hello from the protected API` from the backend, and headers that show
which limit applied:

```
x-ratelimit-limit: 20, 20;w=1;name="tenant-globex"
x-ratelimit-remaining: 19
x-ratelimit-reset: 0
```

The demo tenants and their limits:

| Tenant | API key | Policy |
|---|---|---|
| acme | `qk_demo_acme` | `tenant-default`: GCRA 100 per second |
| initech | `qk_demo_initech` | `tenant-default`: GCRA 100 per second |
| globex | `qk_demo_globex` | `tenant-globex`: GCRA 20 per second |
| *(no key, or unknown key)* | | `anonymous`: fixed window 10 per minute, fail closed |

On `/v1/orders`, every tenant is also limited by `orders-per-tenant` (GCRA 50 per second).
The database stores only SHA-256 hashes of the keys.

### 5. Trigger a 429

**Anonymous traffic** (10 requests per minute): send 13 requests without a key.

```bash
for i in $(seq 13); do curl -s -o /dev/null -w '%{http_code}\n' localhost:8080/v1/items; done
```

The first 10 return `200`, the rest `429`. The window resets at the start of the next minute
(`x-ratelimit-reset` tells you how many seconds are left).

**Globex** (20 per second): send 60 requests, 20 at a time.

```bash
seq 60 | xargs -P 20 -I{} curl -s -o /dev/null -w '%{http_code}\n' \
  -H 'x-api-key: qk_demo_globex' localhost:8080/v1/items | sort | uniq -c
```

About 20 return `200` and the rest `429`. GCRA refills continuously (one request every
50 ms at 20 per second), so the exact number of `200`s is the burst of 20 plus whatever
refilled while the requests were being sent.

**Acme** (100 per second) is not affected by Globex's traffic: each tenant has its own counter.

```bash
curl -s -o /dev/null -w '%{http_code}\n' -H 'x-api-key: qk_demo_acme' localhost:8080/v1/items
```

### 6. Look inside

```bash
docker compose -f deploy/docker-compose.yml exec redis redis-cli --scan --pattern 'q:*'
curl -s localhost:9901/stats | grep ratelimit
```

- **Redis** holds one key per bucket, e.g. `q:{anonymous}:fw:anonymous:tenant=anonymous`.
  Raw API keys never reach Redis, only tenant names. GCRA keys expire as soon as the bucket
  is full again, so they only show up right after traffic.
- **Envoy's stats** count Quotient's answers: `ratelimit.ok` and `ratelimit.over_limit`.
  `ratelimit.error` counts calls where Quotient did not answer in time (Envoy then lets the
  request through).

### 7. Failure modes: stop Redis

```bash
docker compose -f deploy/docker-compose.yml stop redis
curl -s -o /dev/null -w 'acme: %{http_code}\n' -H 'x-api-key: qk_demo_acme' localhost:8080/v1/items
curl -s -o /dev/null -w 'anonymous: %{http_code}\n' localhost:8080/v1/items
docker compose -f deploy/docker-compose.yml start redis
```

Acme's policy is `fail_open`, so it keeps getting `200`. The anonymous policy is
`fail_closed`, so it gets `429`. After `start redis`, Quotient reloads its Lua scripts by
itself and everything is back to normal, with no restart.

### 8. Use the admin API (REST)

Every admin call needs the admin token (`dev-admin-token` in the dev setup).

```bash
TOKEN=dev-admin-token

# List and read tenants
curl -s -H "authorization: Bearer $TOKEN" localhost:8000/v1/tenants
curl -s -H "authorization: Bearer $TOKEN" localhost:8000/v1/tenants/acme

# Create a tenant (written to PostgreSQL)
curl -s -X POST -H "authorization: Bearer $TOKEN" -H 'content-type: application/json' \
  -d '{"slug": "hooli", "display_name": "Hooli"}' localhost:8000/v1/tenants
```

Errors come back as an HTTP status with a JSON message. Add `-w '%{http_code}\n'` to any
command to see the status:

```bash
# Duplicate tenant -> 409
curl -s -w '%{http_code}\n' -X POST -H "authorization: Bearer $TOKEN" \
  -H 'content-type: application/json' \
  -d '{"slug": "acme", "display_name": "Acme again"}' localhost:8000/v1/tenants
```

```
{
 "code": 6,
 "message": "tenant 'acme' already exists",
 "details": []
}
409
```

| Request | HTTP status | Message |
|---|---|---|
| Tenant slug already taken | 409 | `tenant 'acme' already exists` |
| `GET /v1/tenants/nope` | 404 | `tenant 'nope' not found` |
| Invalid slug, e.g. `Bad Slug!` | 400 | `slug must be 2-40 characters: lowercase letters, digits or '-'` |
| No token, or a wrong token | 401 | `missing authorization header` / `invalid admin token` |
| PostgreSQL down | 503 | `a dependency is unavailable, try again later` |

The same API is available as plain gRPC on port 8082 of the Quotient container, for example
with grpcurl from inside the Compose network or when running Quotient locally (see
[Building locally](#building-locally-development)).

### 9. Stop

```bash
make down
```

This also deletes the database volume, so the schema and seed run again on the next
`make up`.

To override the dev defaults (database credentials, log level), copy `deploy/.env.example` to
`deploy/.env`. Never commit `.env`.

## Known limitations

- **Redis stopped with `docker compose stop`.** Only the first requests after the stop
  follow `fail_closed`. After that, Quotient tries to reconnect to the host name `redis`,
  which Docker no longer resolves: the DNS lookup takes about 6.6 seconds (measured), Envoy
  gives up after 20 ms and lets the request through. A circuit breaker (skip Redis for a
  moment after a failure) fixes this and is planned.
- **`local_fallback`** currently behaves like `fail_open`.
- **The admin API** only manages tenants so far. API keys and policies are changed directly
  in PostgreSQL for now.
- **Policy changes** in PostgreSQL are picked up only when Quotient restarts
  (`docker compose -f deploy/docker-compose.yml restart quotient`).

## Repository layout

One folder per service or sub-project:

```
rate-limit-service/   The Quotient service (C++20, CMake, vcpkg)
  proto/              Envoy RLS v3 subset, admin API (admin.proto), vendored google/api
  scripts/lua/        Redis Lua scripts (GCRA, fixed window), embedded in the binary
  src/web/            gRPC controllers (RLS, admin) and the central error mapping
  src/service/        Business logic (decision engine, policies), domain errors
  src/entity/         Plain data types
  src/repository/     PostgreSQL and Redis access, behind interfaces
  src/util/           Configuration, clock, SHA-256
  Dockerfile          Two-stage image build (builder + slim runtime)
database/             SQL schema (0001_init.sql) and demo data (0002_seed.sql)
deploy/               docker-compose.yml, .env.example
gateway/              Envoy configuration: edge listener (:8080), REST admin listener (:8000)
docs/                 Architecture documentation
Makefile              Shortcuts for the Compose stack
```

## Building locally (development)

Requirements: Linux or WSL2, GCC with C++20, CMake 3.25 or newer, Ninja, and
[vcpkg](https://github.com/microsoft/vcpkg) with `VCPKG_ROOT` set. Dependencies are declared
in `rate-limit-service/vcpkg.json` and installed automatically on the first configure.

```bash
cd rate-limit-service
cmake --preset debug
cmake --build --preset debug
```

Run it against the PostgreSQL and Redis from the Compose stack (`make up` first):

```bash
QUOTIENT_PG_URL=postgresql://quotient:quotient@localhost:5432/quotient \
QUOTIENT_REDIS_URL=tcp://localhost:6379 \
QUOTIENT_ADMIN_TOKEN=dev-admin-token \
SPDLOG_LEVEL=debug ./build/debug/src/quotient
```

Call it directly with [grpcurl](https://github.com/fullstorydev/grpcurl) (server reflection is
enabled, so no `.proto` files are needed):

```bash
grpcurl -plaintext localhost:8081 list
grpcurl -plaintext -d '{"domain":"api","descriptors":[{"entries":[{"key":"api_key","value":"qk_demo_acme"}]}]}' \
  localhost:8081 envoy.service.ratelimit.v3.RateLimitService/ShouldRateLimit

# Admin API over gRPC (port 8082)
grpcurl -plaintext -H 'authorization: Bearer dev-admin-token' -d '{}' \
  localhost:8082 quotient.admin.v1.AdminService/ListTenants
```

### Configuration

| Variable | Default | Meaning |
|---|---|---|
| `QUOTIENT_PG_URL` | *required* | PostgreSQL connection URL |
| `QUOTIENT_ADMIN_TOKEN` | *required* | Bearer token for the admin API |
| `QUOTIENT_RLS_ADDR` | `0.0.0.0:8081` | Rate Limit Service gRPC listener |
| `QUOTIENT_ADMIN_ADDR` | `0.0.0.0:8082` | Admin API gRPC listener |
| `QUOTIENT_REDIS_URL` | `tcp://redis:6379` | Redis address |
| `QUOTIENT_REDIS_TIMEOUT_MS` | `5` | Redis command timeout (must stay well below Envoy's 20 ms) |
| `QUOTIENT_RLS_MAX_THREADS` | `32` | Maximum gRPC worker threads |
| `SPDLOG_LEVEL` | `info` | Log level; `debug` logs one line per rate limit call |

## Design principles

- **Layers.** `web` (gRPC controllers) -> `service` (logic) -> `repository` (I/O). `entity`
  holds plain data.
- **Inversion of control.** Dependencies are interfaces passed through constructors. Only
  `main.cpp` creates concrete classes and wires them together.
- **Logging.** spdlog only. API keys and tokens are never logged.
- **Errors.** Services throw domain exceptions (`service/errors.h`). One central function
  (`web/error_mapping.cpp`) turns them into gRPC status codes, which Envoy turns into HTTP
  errors with a message. The rate limit handler never throws: on an unexpected error it
  allows the request.
- **Stateless.** Nodes keep no per-client state. Counters are in Redis and configuration is
  in PostgreSQL, so nodes can be scaled freely.

## License

Apache-2.0 (see the SPDX headers in each source file). `proto/envoy/service/ratelimit/v3/rls.proto`
is derived from [Envoy](https://github.com/envoyproxy/envoy) (Apache-2.0).
