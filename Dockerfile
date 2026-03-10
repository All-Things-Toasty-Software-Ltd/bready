# Copyright 2026 All Things Toasty Software Ltd
#
# Multi-stage Dockerfile for the Bready Odoo integration Discord bot.
# Works with both Docker and Podman.
#
# Build
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    gcc \
    g++ \
    git \
    ca-certificates \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev \
    libopus-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF && \
    cmake --build build --parallel

# Runtime
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
    libcurl4 \
    libssl3 \
    zlib1g \
    libopus0 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create a dedicated non-root user for running the bot.
RUN useradd --system --no-create-home --shell /usr/sbin/nologin botuser && \
    mkdir -p /data && chown botuser:botuser /data

WORKDIR /app

COPY --from=builder /src/build/bready /app/bready

# Point all three data-file env vars at the persistent volume path so they
# survive container restarts without any extra configuration.
ENV BOT_DATA_PATH=/data/user_links.json
ENV BOT_BRIDGE_DATA_PATH=/data/bridges.json
ENV BOT_BRIDGE_DB_DATA_PATH=/data/bridge_dbs.json

USER botuser

# /data holds user_links.json, bridges.json, and bridge_dbs.json.
# Mount a host directory or named volume here so the files persist across
# restarts and are accessible to the admin.
VOLUME ["/data"]

ENTRYPOINT ["/app/bready"]