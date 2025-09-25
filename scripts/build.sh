#!/usr/bin/env bash
set -e
set -u
(set -o pipefail) 2>/dev/null || true

# Orchestrator for building the addon prebuilds (addon-slim) using prebuilt deps/core
# Already-built images live in the registry; this script computes their tags locally
# and passes them as Docker build-args. No parameters are accepted.

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# Optional env overrides
DOCKER_BUILD_FLAGS="${DOCKER_BUILD_FLAGS:-}"
PLATFORM="${PLATFORM:-}"
TAG_ADDON_BASE="${TAG_ADDON_BASE:-orca-addon:base}"
TAG_ADDON_SLIM="${TAG_ADDON_SLIM:-orca-addon:addon-slim}"
ORCASLICER_SUFFIX="${ORCASLICER_SUFFIX:-a}"
REGISTRY="${REGISTRY:-ghcr.io}"

# Derive owner (lowercased) from git remote or env fallback
OWNER_FROM_GIT="$(git config --get remote.origin.url 2>/dev/null | sed -E 's#.*[:/]([^/]+)/[^/]+(.git)?#\1#' || true)"
OWNER_LOWER="$(echo "${OWNER_FROM_GIT:-${GITHUB_REPOSITORY_OWNER:-}}" | tr '[:upper:]' '[:lower:]')"
if [[ -z "$OWNER_LOWER" ]]; then
  echo "[ERRO] Não foi possível deduzir o owner (GitHub org/usuário). Configure GITHUB_REPOSITORY_OWNER ou git remote." >&2
  exit 2
fi

# Derive version and arch using the same helper as CI/Makefile
META_OUT="$(ORCASLICER_SUFFIX="$ORCASLICER_SUFFIX" bash scripts/ci/derive_meta.sh)"
VERSION="$(echo "$META_OUT" | awk -F= '/^version=/{print $2}')"
ARCH="$(echo "$META_OUT" | awk -F= '/^arch=/{print $2}')"
if [[ -z "$VERSION" || -z "$ARCH" ]]; then
  echo "[ERRO] Falha ao derivar VERSION/ARCH a partir de scripts/ci/derive_meta.sh" >&2
  echo "$META_OUT" >&2
  exit 3
fi

# Compose tag names exactly like CI
declare -r DEPS_TAG="${REGISTRY}/${OWNER_LOWER}/orcaslicer-build-deps:${VERSION}-${ARCH}"
declare -r CORE_TAG="${REGISTRY}/${OWNER_LOWER}/orcaslicer-core:${VERSION}-${ARCH}"

echo "[INFO] Using prebuilt images from registry:"
echo "       DEPS = ${DEPS_TAG}"
echo "       CORE = ${CORE_TAG}"

echo "[INFO] Building ADDON BASE image (target 'base'): ${TAG_ADDON_BASE}"

build_cmd() {
  if [[ -n "$PLATFORM" ]]; then
    echo "docker buildx build --platform ${PLATFORM} --load ${DOCKER_BUILD_FLAGS}"
  else
    echo "docker build ${DOCKER_BUILD_FLAGS}"
  fi
}

# Build base (contains resources + prebuilds) so dev can consume it directly
cmd_base="$(build_cmd)"
cmd_base+=" --target base"
cmd_base+=" --build-arg ENFORCE_PREBUILT_BASE=true"
cmd_base+=" --build-arg USE_PREBUILT_DEPS=true"
cmd_base+=" --build-arg BASE_DEPS_IMAGE=${DEPS_TAG}"
cmd_base+=" --build-arg BASE_CORE_IMAGE=${CORE_TAG}"
cmd_base+=" -t ${TAG_ADDON_BASE} ."

echo "+ $cmd_base"
eval "$cmd_base"

# Build addon-slim (just prebuilds) as well

echo "[INFO] Building ADDON-SLIM image (minimal addon-only stage 'addon-slim'): ${TAG_ADDON_SLIM}"

cmd="$(build_cmd)"
cmd+=" --target addon-slim"
cmd+=" --build-arg ENFORCE_PREBUILT_BASE=true"
cmd+=" --build-arg USE_PREBUILT_DEPS=true"
cmd+=" --build-arg BASE_DEPS_IMAGE=${DEPS_TAG}"
cmd+=" --build-arg BASE_CORE_IMAGE=${CORE_TAG}"
cmd+=" -t ${TAG_ADDON_SLIM} ."

echo "+ $cmd"
eval "$cmd"


