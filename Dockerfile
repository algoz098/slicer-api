# Global ARGs used in FROM or stage selection must be declared before the first FROM
ARG BASE_ADDON_CORE_IMAGE=scratch

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
# - Leaves prebuilt artifacts in OrcaSlicerAddon/bindings/node/prebuilds/<platform>-<arch>/
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
ARG CMAKE_OLEVEL=2
ARG LD_NO_KEEP_MEMORY=false


ENV DEBIAN_FRONTEND=noninteractive \
    TZ=Etc/UTC

# System dependencies required by OrcaSlicer build scripts and toolchain
RUN if [ "${USE_PREBUILT_DEPS}" = "true" ]; then \
      echo "Using prebuilt deps base; skipping toolchain install"; \
    else \
      apt-get update && apt-get install -y --no-install-recommends \
        autoconf \
        automake \
        m4 \
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
RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache --mount=type=cache,id=orcadeps-dlcache-amd64,target=/opt/orca/OrcaSlicer/deps/DL_CACHE --mount=type=cache,id=tmp-orca-amd64,target=/opt/tmp bash -lc "set -e; export TMPDIR=/opt/tmp; if [ \"\$USE_PREBUILT_DEPS\" = \"true\" ]; then exit 0; fi; JOBS=\"\${CI_MAX_JOBS:-\$(nproc)}\"; OLV=\"\${CMAKE_OLEVEL:-2}\"; LDFLAGS=\"\"; if [ \"\${LD_NO_KEEP_MEMORY:-false}\" = \"true\" ]; then LDFLAGS=\"-Wl,--no-keep-memory\"; fi; cd OrcaSlicer/deps; cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_VERBOSE_MAKEFILE=ON -DDEP_WX_GTK3=ON -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_FLAGS_RELEASE=\"-O\${OLV} -g0 -fno-lto\" -DCMAKE_CXX_FLAGS_RELEASE=\"-O\${OLV} -g0 -fno-lto\" -DCMAKE_EXE_LINKER_FLAGS_RELEASE=\"\${LDFLAGS}\" -DCMAKE_SHARED_LINKER_FLAGS_RELEASE=\"\${LDFLAGS}\" -DCMAKE_C_ARCHIVE_CREATE=\"<CMAKE_AR> qcT <TARGET> <OBJECTS>\" -DCMAKE_C_ARCHIVE_APPEND=\"<CMAKE_AR> qT <TARGET> <OBJECTS>\" -DCMAKE_C_ARCHIVE_FINISH=\"<CMAKE_RANLIB> <TARGET>\" -DCMAKE_CXX_ARCHIVE_CREATE=\"<CMAKE_AR> qcT <TARGET> <OBJECTS>\" -DCMAKE_CXX_ARCHIVE_APPEND=\"<CMAKE_AR> qT <TARGET> <OBJECTS>\" -DCMAKE_CXX_ARCHIVE_FINISH=\"<CMAKE_RANLIB> <TARGET>\"; cmake --build build --target deps --config Release --parallel \"\$JOBS\" -- -v"
# Core layer: build OrcaSlicer (libs) on top of deps
FROM deps AS core
WORKDIR /opt/orca
ARG CI_MAX_JOBS
ARG ENFORCE_PREBUILT_BASE
ARG BASE_CORE_IMAGE
RUN bash -lc 'if [ "${ENFORCE_PREBUILT_BASE}" = "true" ] && [ -z "${BASE_CORE_IMAGE}" ]; then echo "ERROR: BASE_CORE_IMAGE is required. This build is configured to not compile OrcaSlicer core inside Docker. Provide --build-arg BASE_CORE_IMAGE=<image-with-core> (built elsewhere) or set ENFORCE_PREBUILT_BASE=false to allow building core here."; exit 11; fi'

# Preserve compiled deps (OrcaSlicer_dep contains Boost, wxWidgets, etc.) before COPY overwrites OrcaSlicer
# Note: older builds used "destdir", newer ones use "OrcaSlicer_dep"
RUN mkdir -p /tmp/orca_deps_backup && \
    if [ -d OrcaSlicer/deps/build/OrcaSlicer_dep ]; then \
      cp -a OrcaSlicer/deps/build/OrcaSlicer_dep /tmp/orca_deps_backup/; \
    fi && \
    if [ -d OrcaSlicer/deps/build/destdir ]; then \
      cp -a OrcaSlicer/deps/build/destdir /tmp/orca_deps_backup/; \
    fi

COPY OrcaSlicer ./OrcaSlicer

# Restore compiled deps after COPY
RUN mkdir -p OrcaSlicer/deps/build && \
    if [ -d /tmp/orca_deps_backup/OrcaSlicer_dep ]; then \
      cp -a /tmp/orca_deps_backup/OrcaSlicer_dep OrcaSlicer/deps/build/; \
    fi && \
    if [ -d /tmp/orca_deps_backup/destdir ]; then \
      cp -a /tmp/orca_deps_backup/destdir OrcaSlicer/deps/build/; \
    fi && \
    rm -rf /tmp/orca_deps_backup && \
    echo "Deps restored. Contents:" && ls -la OrcaSlicer/deps/build/ || true

RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache --mount=type=cache,id=tmp-orca-amd64,target=/opt/tmp bash -lc 'export TMPDIR=/opt/tmp; JOBS=${CI_MAX_JOBS:-$(nproc)}; cmake -S OrcaSlicer -B OrcaSlicer/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSLIC3R_STATIC=ON -DSLIC3R_GTK=3 -DCMAKE_PREFIX_PATH=/opt/orca/OrcaSlicer/deps/build/destdir/usr/local -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache && cmake --build OrcaSlicer/build --config Release --parallel "$JOBS"'





ARG BASE_CORE_IMAGE
FROM ${BASE_CORE_IMAGE} AS addon-core
ARG CI_MAX_JOBS
# Ensure Node.js and npm are available to build the addon
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    nodejs \
    npm \
    clang \
  && rm -rf /var/lib/apt/lists/*


WORKDIR /opt/orca
COPY OrcaSlicerAddon ./OrcaSlicerAddon

# Build the shared engine library (orcacli_engine) which the Node addon dlopens
RUN --mount=type=cache,id=ccache-orca-amd64,target=/root/.ccache --mount=type=cache,id=tmp-orca-amd64,target=/opt/tmp bash -lc 'export TMPDIR=/opt/tmp; JOBS=${CI_MAX_JOBS:-$(nproc)}; cmake -S OrcaSlicerAddon -B OrcaSlicerAddon/build -G Ninja -DORCACLI_REQUIRE_LIBS=ON && cmake --build OrcaSlicerAddon/build --config Release --target orcacli_engine --parallel "$JOBS"'

# Build the Node addon using cmake-js and stage prebuild artifacts
WORKDIR /opt/orca/OrcaSlicerAddon/bindings/node
# Ensure cmake-js build fails if full OrcaSlicer libs are not found (link real libslic3r)
ENV ORCACLI_REQUIRE_LIBS=ON
RUN npm install && npm run prebuild:all && \
    (command -v strip >/dev/null 2>&1 && strip -s prebuilds/*/orcaslicer_node.node prebuilds/*/liborcacli_engine.so || true)

# Optionally verify the prebuild exists for this platform
RUN test -f "prebuilds/${TARGETPLATFORM:-linux}-$(uname -m | sed s/x86_64/x64/ | sed s/aarch64/arm64/)"/orcaslicer_node.node || true

# External prebuilt addon-core image (provides prebuilt addon and CLI built on top of CORE)
FROM ${BASE_ADDON_CORE_IMAGE} AS addoncore

# Runtime/base layer that carries only what is needed to consume the addon
FROM node:${NODE_VERSION}-bookworm-slim AS base
WORKDIR /opt/orca

# Provide resources path as default (can be overridden by consumers)
ENV ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources

# Install runtime libraries required by the engine
RUN apt-get update && apt-get install -y --no-install-recommends \
      libstdc++6 \
      libgcc-s1 \
      libexpat1 \
      libssl3 \
      libfontconfig1 \
      libtbb12 \
      libgomp1 \
      libglu1-mesa \
      libglew2.2 \
    && rm -rf /var/lib/apt/lists/*

# Copy only resources and a minimal addon directory (index.js + prebuilds)
COPY --from=addoncore /opt/orca/OrcaSlicer/resources ./OrcaSlicer/resources
RUN mkdir -p /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds
COPY --from=addoncore /opt/orca/OrcaSlicerAddon/bindings/node/index.js /opt/orca/OrcaSlicerAddon/bindings/node/index.js
COPY --from=addoncore /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds

# Also ship the standalone CLI executable built during cmake-js

# Show what was produced (helps diagnosing during image build)
RUN ls -la /opt/orca/OrcaSlicerAddon/bindings/node && \
    find /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds -maxdepth 2 -type f -print || true

# Verify that liborcacli_engine.so exists in prebuilds
RUN if [ ! -f /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds/linux-x64/liborcacli_engine.so ]; then \
      echo "ERROR: liborcacli_engine.so not found in prebuilds/linux-x64/"; \
      echo "Available files:"; \
      find /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds -type f; \
      exit 1; \
    fi

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

# Minimal carrier image with only the Node addon prebuilds (no JS app code, no resources)
FROM scratch AS addon-slim
ENV ORCACLI_ADDON_DIR=/opt/orca/orcaslicer-addon
# Copy only the prebuilt native addon (.node) and engine .so
COPY --from=addoncore /opt/orca/OrcaSlicerAddon/bindings/node/prebuilds ${ORCACLI_ADDON_DIR}/prebuilds

