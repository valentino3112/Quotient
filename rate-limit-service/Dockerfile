# SPDX-License-Identifier: Apache-2.0
#
# Build context is the REPO ROOT (see deploy/docker-compose.yml: `context: ..`),
# because the vcpkg toolchain and proto generation expect the layout described
# in CLAUDE.md §5. Build with e.g.:
#   docker build -f rate-limit-service/Dockerfile -t quotient .
#
# Two stages:
#   builder  - full toolchain + vcpkg, compiles the "release" CMake preset
#   runtime  - just the resulting binary on a slim base
#
# vcpkg.json is copied and `vcpkg install` run BEFORE the rest of the source is
# copied in, so that the 20-40 minute first-time dependency build (guide/CLAUDE.md
# §4) stays in its own Docker layer and is reused by the cache on every later
# build that doesn't touch rate-limit-service/vcpkg.json.

# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

# Same package list as CLAUDE.md §4 (WSL2 dev setup), so "it builds in Docker"
# and "it builds on my machine" mean the same thing.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential ninja-build pkg-config cmake git curl zip unzip tar ca-certificates \
      linux-libc-dev bison flex autoconf autoconf-archive automake libtool \
      python3 python3-venv \
    && rm -rf /var/lib/apt/lists/*

# Pin vcpkg itself; the actual dependency versions come from vcpkg.json's
# builtin-baseline, not from this commit.
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth 1 https://github.com/microsoft/vcpkg "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
ENV PATH="$VCPKG_ROOT:$PATH"

WORKDIR /build/rate-limit-service

# --- dependency layer: cached as long as vcpkg.json is unchanged ---
COPY rate-limit-service/vcpkg.json ./
# See CLAUDE.md §4: lower VCPKG_MAX_CONCURRENCY if a port gets OOM-killed.
RUN vcpkg install --triplet x64-linux

# --- source layer: rebuilds only this and below when source changes ---
COPY rate-limit-service/ ./
RUN cmake --preset release -DVCPKG_INSTALLED_DIR=/build/rate-limit-service/vcpkg_installed \
    && cmake --build --preset release

# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

# ca-certificates: harmless now (RLS/admin use InsecureServerCredentials in the
# dev demo) and needed the day TLS is turned on. No other packages: vcpkg's
# x64-linux triplet builds static libraries, so grpc/protobuf/openssl/libpqxx/
# redis-plus-plus/spdlog are already linked into the binary.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --no-create-home --shell /usr/sbin/nologin quotient

COPY --from=builder /build/rate-limit-service/build/release/src/quotient /usr/local/bin/quotient

USER quotient
# Quotient handles SIGTERM itself (main.cpp), so no init wrapper is needed.
ENTRYPOINT ["/usr/local/bin/quotient"]
