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
FROM node:${NODE_VERSION}-bookworm AS builder

ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC

# System dependencies required by OrcaSlicer build scripts and toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
    autoconf \
    build-essential \
    cmake \
    eglexternalplatform-dev \
    extra-cmake-modules \
    file \
    gettext \
    git \
    libcurl4-openssl-dev \
    libdbus-1-dev \
    libglew-dev \
    libgstreamerd-3-dev \
    libgtk-3-dev \
    libmspack-dev \
    libsecret-1-dev \
    libspnav-dev \
    libssl-dev \
    libudev-dev \
    ninja-build \
    pkg-config \
    texinfo \
    wget \
  && rm -rf /var/lib/apt/lists/*

# Workdir for the monorepo
WORKDIR /opt/orca

# Copy only what we need for building the addon and resources
COPY OrcaSlicer ./OrcaSlicer
COPY OrcaSlicerCli ./OrcaSlicerCli

# Limit parallelism to reduce memory usage during native builds (avoid OOM in Docker Desktop)
ENV CMAKE_BUILD_PARALLEL_LEVEL=1

# Build third-party dependencies used by OrcaSlicer (downloads and compiles into OrcaSlicer/deps/build)
# Use clean build and strip any submodule .git pointer to avoid git apply errors inside deps
RUN bash -lc 'cd OrcaSlicer && rm -rf .git .gitmodules && ./build_linux.sh -d -c -r'

# Build the Node addon using cmake-js and stage prebuild artifacts
WORKDIR /opt/orca/OrcaSlicerCli/bindings/node
RUN npm ci \
 && npm run prebuild:all

# Optionally verify the prebuild exists for this platform
RUN test -f "prebuilds/${TARGETPLATFORM:-linux}-$(uname -m | sed s/x86_64/x64/ | sed s/aarch64/arm64/)"/orcaslicer_node.node || true

# Runtime/base layer that carries only what is needed to consume the addon
FROM node:${NODE_VERSION}-bookworm AS base
WORKDIR /opt/orca

# Provide resources path as default (can be overridden by consumers)
ENV ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources

# Copy resources and the staged Node addon package
COPY --from=builder /opt/orca/OrcaSlicer/resources ./OrcaSlicer/resources
COPY --from=builder /opt/orca/OrcaSlicerCli/bindings/node ./OrcaSlicerCli/bindings/node

# Optionally reduce weight by removing large build dirs (prebuilds already contains the .node and engine lib)
RUN rm -rf /opt/orca/OrcaSlicerCli/bindings/node/build || true

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
#   CMD ["node", "lib/index.js"]

