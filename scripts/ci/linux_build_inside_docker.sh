#!/usr/bin/env bash
set -euo pipefail

workspace="${1:-$PWD}"

docker run --rm \
  -e GITHUB_ACTIONS=true \
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
    git lfs pull || true
    # Limit parallelism to keep memory under control in CI runners
    export CMAKE_BUILD_PARALLEL_LEVEL=1
    export MAKEFLAGS=-j1
    export NINJAFLAGS=-j1
    # Build OrcaSlicer (full)
    cd OrcaSlicer
    chmod +x build_linux.sh
    ./build_linux.sh -u
    ./build_linux.sh -dsi
    # Build addon prebuild
    cd /work/OrcaSlicerCli/bindings/node
    export CMAKE_JS_CMAKE_ARGS="--CDORCACLI_BUILD_NODE_ADDON=ON --CDORCASLICER_ROOT_DIR=/work/OrcaSlicer --CDORCACLI_REQUIRE_LIBS=ON"
    npm ci
    npm run prebuild:all
  '

