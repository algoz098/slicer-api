#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$PWD}"

HOST_UID=$(id -u)
HOST_GID=$(id -g)

docker run --rm \
  -e GITHUB_ACTIONS=true \
  -e HOST_UID="$HOST_UID" \
  -e HOST_GID="$HOST_GID" \
  -v "$workspace":/work \
  -w /work \
  ubuntu:22.04 bash -lc '
    set -euo pipefail
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    # Base tools + sudo + build tools required by Orca deps (autoreconf/aclocal, libtool, gettext, texinfo, file)
    apt-get install -y build-essential ninja-build curl git git-lfs pkg-config ca-certificates gnupg cmake sudo autoconf automake libtool gettext texinfo file
    # Node.js 20 (for cmake-js / npm)
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    apt-get install -y nodejs
    git lfs install || true
    # LFS objects already materialized on host via volume mount; no need to pull inside container
    # Limit parallelism to keep memory under control in CI runners (ubuntu-22.04 has 4 vCPUs / ~16GB RAM)
    export CMAKE_BUILD_PARALLEL_LEVEL=2
    export MAKEFLAGS=-j2
    export NINJAFLAGS=-j2
    # Build OrcaSlicer (full): -u installs system deps, -dsi builds deps+source+appimage, -r skips RAM/disk check
    cd OrcaSlicer
    chmod +x build_linux.sh
    ./build_linux.sh -u
    ./build_linux.sh -rdsi
    # Build addon prebuild
    cd /work/OrcaSlicerAddon/bindings/node
    npm ci
    npm run prebuild:all
    # Fix ownership so host CI user can read the output files
    chown -R "${HOST_UID}:${HOST_GID}" /work/OrcaSlicerAddon/bindings/node/prebuilds || true
  '

