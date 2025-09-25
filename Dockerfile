# syntax=docker/dockerfile:1.4

# Base image that builds the OrcaSlicer Node addon (N-API) and stages it
# to prebuilds/<platform>-<arch>/ for reuse by other images.
#
# Usage:
#   docker build -t orca-addon:latest .
#   # Then FROM orca-addon:latest in other images and copy your app in
#
# Notes:
# - Targets Debian-based Node 24 (bookworm). Adjust NODE_VERSION if needed.
# - Installs build prerequisites based on OrcaSlicer/scripts/linux.d/debian
# - Builds OrcaSlicer dependencies, then builds the Node addon via cmake-js
# - Leaves prebuilt artifacts in OrcaSlicerCli/bindings/node/prebuilds/<platform>-<arch>/
# - Exposes ORCACLI_RESOURCES to point at the baked-in OrcaSlicer resources

ARG NODE_VERSION=24
# When provided, use the prebuilt build-deps image as base to speed up builds
ARG BASE_DEPS_IMAGE=debian:bookworm-slim
# Hint to skip rebuilding deps/toolchain if using the prebuilt base
ARG USE_PREBUILT_DEPS=false
ARG ENFORCE_PREBUILT_BASE=true

ARG BASE_CORE_IMAGE=debian:bookworm-slim

FROM ${BASE_DEPS_IMAGE} AS deps
ARG ENFORCE_PREBUILT_BASE
ARG BASE_DEPS_IMAGE
RUN bash -lc 'if [ "${ENFORCE_PREBUILT_BASE}" = "true" ] && [ -z "${BASE_DEPS_IMAGE}" ]; then echo "ERROR: BASE_DEPS_IMAGE is required. This build is configured to not compile OrcaSlicer deps inside Docker. Provide --build-arg BASE_DEPS_IMAGE=<image-with-deps> (built elsewhere) or set ENFORCE_PREBUILT_BASE=false to allow building deps here."; exit 10; fi'

ARG CI_MAX_JOBS


ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC

# System dependencies required by OrcaSlicer build scripts and toolchain
RUN if [ "${USE_PREBUILT_DEPS}" = "true" ]; then \
      echo "Using prebuilt deps base; skipping toolchain install"; \
    else \
      apt-get update && apt-get install -y --no-install-recommends \
        autoconf \
        build-essential \
        ccache \
        cmake \
        eglexternalplatform-dev \
        extra-cmake-modules \
        file \
        gettext \
        git \
        ca-certificates \
        xz-utils \
        libcurl4-openssl-dev \
        libdbus-1-dev \
        libglew-dev \
        libgstreamerd-3-dev \
        libgtk-3-dev \
        libmspack-dev \
        libsecret-1-dev \
        libspnav-dev \
        libssl-dev \
        libtool \
        libudev-dev \
        ninja-build \
        pkg-config \
        texinfo \
        wget \
      && rm -rf /var/lib/apt/lists/*; \
    fi

# Install WebKitGTK dev (needed for wxWidgets webview) — try 4.0 first, then 4.1 on newer distros
RUN if [ "${USE_PREBUILT_DEPS}" = "true" ]; then \
      echo "Using prebuilt deps base; skipping webkit install"; \
    else \
      apt-get update \
      && (apt-get install -y --no-install-recommends libwebkit2gtk-4.0-dev \
          || apt-get install -y --no-install-recommends libwebkit2gtk-4.1-dev) \
      && rm -rf /var/lib/apt/lists/*; \
    fi

# Workdir for the monorepo
WORKDIR /opt/orca

# Copy full OrcaSlicer tree so deps image can also stage compiled core libs
COPY OrcaSlicer ./OrcaSlicer


# Enable ccache and configure cache directory (used across builds via BuildKit cache mount)
ENV CCACHE_DIR=/root/.ccache CCACHE_MAXSIZE=10G
RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache ccache -M 10G

# Build third-party dependencies used by OrcaSlicer (downloads and compiles into OrcaSlicer/deps/build)
# If using prebuilt deps base image, skip rebuilding here to save time
RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache --mount=type=cache,id=orcadeps-dlcache-amd64,target=/opt/orca/OrcaSlicer/deps/DL_CACHE bash -lc "set -e; if [ \"\$USE_PREBUILT_DEPS\" = \"true\" ]; then exit 0; fi; JOBS=\"\${CI_MAX_JOBS:-\$(nproc)}\"; cd OrcaSlicer/deps; cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=ON -DDEP_WX_GTK3=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache; cmake --build build --target deps --config Release --parallel \"\$JOBS\" -- -v"
# Core layer: build OrcaSlicer (libs) on top of deps
FROM deps AS core
WORKDIR /opt/orca
COPY OrcaSlicer ./OrcaSlicer
ARG CI_MAX_JOBS
ARG ENFORCE_PREBUILT_BASE
ARG BASE_CORE_IMAGE
RUN bash -lc 'if [ "${ENFORCE_PREBUILT_BASE}" = "true" ] && [ -z "${BASE_CORE_IMAGE}" ]; then echo "ERROR: BASE_CORE_IMAGE is required. This build is configured to not compile OrcaSlicer core inside Docker. Provide --build-arg BASE_CORE_IMAGE=<image-with-core> (built elsewhere) or set ENFORCE_PREBUILT_BASE=false to allow building core here."; exit 11; fi'

RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache bash -lc 'JOBS=${CI_MAX_JOBS:-$(nproc)}; cmake -S OrcaSlicer -B OrcaSlicer/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSLIC3R_STATIC=ON -DSLIC3R_GTK=3 -DCMAKE_PREFIX_PATH=/opt/orca/OrcaSlicer/deps/build/destdir/usr/local -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && cmake --build OrcaSlicer/build --config Release --parallel "$JOBS"'





ARG BASE_CORE_IMAGE
FROM ${BASE_CORE_IMAGE} AS builder
ARG CI_MAX_JOBS

WORKDIR /opt/orca
COPY OrcaSlicerCli ./OrcaSlicerCli

# Build the shared engine library (orcacli_engine) which the Node addon dlopens
RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache bash -lc 'JOBS=${CI_MAX_JOBS:-$(nproc)}; cmake -S OrcaSlicerCli -B OrcaSlicerCli/build -G Ninja -DORCACLI_REQUIRE_LIBS=ON && cmake --build OrcaSlicerCli/build --config Release --target orcacli_engine --parallel "$JOBS"'

# Build the Node addon using cmake-js and stage prebuild artifacts
WORKDIR /opt/orca/OrcaSlicerCli/bindings/node
# Ensure cmake-js build fails if full OrcaSlicer libs are not found (link real libslic3r)
ENV ORCACLI_REQUIRE_LIBS=ON
RUN npm install && npm run prebuild:all && \
    (command -v strip >/dev/null 2>&1 && strip -s prebuilds/*/orcaslicer_node.node prebuilds/*/liborcacli_engine.so || true)

# Optionally verify the prebuild exists for this platform
RUN test -f "prebuilds/${TARGETPLATFORM:-linux}-$(uname -m | sed s/x86_64/x64/ | sed s/aarch64/arm64/)"/orcaslicer_node.node || true

# Runtime/base layer that carries only what is needed to consume the addon
FROM node:${NODE_VERSION}-bookworm-slim AS base
WORKDIR /opt/orca

# Provide resources path as default (can be overridden by consumers)
ENV ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources

# Copy only resources and a minimal addon directory (index.js + prebuilds)
COPY --from=builder /opt/orca/OrcaSlicer/resources ./OrcaSlicer/resources
RUN mkdir -p /opt/orca/OrcaSlicerCli/bindings/node/prebuilds
COPY --from=builder /opt/orca/OrcaSlicerCli/bindings/node/index.js /opt/orca/OrcaSlicerCli/bindings/node/index.js
COPY --from=builder /opt/orca/OrcaSlicerCli/bindings/node/prebuilds /opt/orca/OrcaSlicerCli/bindings/node/prebuilds

# Also ship the standalone CLI executable built during cmake-js
COPY --from=builder /opt/orca/OrcaSlicerCli/bindings/node/build/bin/orcaslicer-cli /usr/local/bin/orcaslicer-cli

# Show what was produced (helps diagnosing during image build)
RUN ls -la /opt/orca/OrcaSlicerCli/bindings/node && \
    find /opt/orca/OrcaSlicerCli/bindings/node/prebuilds -maxdepth 2 -type f -print || true

# No default CMD — this image is intended to be a base layer.
# Example of consumption in a downstream Dockerfile:
#   FROM orca-addon:latest
#   WORKDIR /opt/orca
#   COPY node-api ./node-api
#   WORKDIR /opt/orca/node-api
#   RUN npm ci && npm run compile
#   ENV NODE_ENV=production
#   EXPOSE 3030


# Minimal runtime image that ships only the standalone CLI (no Node addon)
FROM debian:bookworm-slim AS cli
WORKDIR /opt/orca

# Provide resources path for the CLI
ENV ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources

# Copy only what the CLI needs to run
COPY --from=builder /opt/orca/OrcaSlicer/resources ./OrcaSlicer/resources
COPY --from=builder /opt/orca/OrcaSlicerCli/bindings/node/build/bin/orcaslicer-cli /usr/local/bin/orcaslicer-cli

# Keep it lean; install runtime essentials only if necessary (often not needed)
# RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*

# Default entrypoint is the CLI; consumers can override
ENTRYPOINT ["/usr/local/bin/orcaslicer-cli"]

# Minimal carrier image with only the Node addon prebuilds (no JS app code, no resources)
FROM scratch AS addon-slim
ENV ORCACLI_ADDON_DIR=/opt/orca/orcaslicer-addon
# Copy only the prebuilt native addon (.node) and engine .so
COPY --from=builder /opt/orca/OrcaSlicerCli/bindings/node/prebuilds ${ORCACLI_ADDON_DIR}/prebuilds

