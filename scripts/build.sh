#!/usr/bin/env bash
set -euo pipefail

# Simple orchestrator for Docker builds in this repo.
# Modes:
#   full        -> Build everything (deps + OrcaSlicer + engine + node addon + final base layer)
#   orcaslicer  -> Build ONLY the OrcaSlicer application using its upstream Dockerfile
#   continue    -> Continue build reusing cached layers for deps/OrcaSlicer; rebuild engine/addon if needed
#   addon-slim  -> Build minimal base with ONLY the addon prebuilds (.node + engine .so), no JS app/resources
#
# Usage examples:
#   scripts/build.sh full
#   scripts/build.sh orcaslicer
#   scripts/build.sh continue
#
# Optional env vars:
#   TAG_FULL            default: orca-addon:latest         (final image)
#   TAG_ORCASLICER      default: orcaslicer-app:latest     (OrcaSlicer-only image)
#   DOCKER_BUILD_FLAGS  extra flags to forward to `docker build` (e.g., --progress=plain)
#   PLATFORM            target platform for buildx (e.g., linux/amd64). If set, uses buildx.
#
# Notes:
# - The root Dockerfile already limits native parallelism to reduce memory.
# - "continue" relies on Docker layer cache: if OrcaSlicer sources não mudarem, as etapas
#   dos deps e do OrcaSlicer serão reaproveitadas; mudanças no OrcaSlicerCli forçam rebuild
#   apenas do engine/addon.

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

MODE="${1:-}"  # required
TAG_FULL="${TAG_FULL:-orca-addon:latest}"
TAG_ORCASLICER="${TAG_ORCASLICER:-orcaslicer-build-deps:3.0.1a}"
TAG_ADDON_SLIM="${TAG_ADDON_SLIM:-orca-addon:addon-slim}"

DOCKER_BUILD_FLAGS="${DOCKER_BUILD_FLAGS:-}"
PLATFORM="${PLATFORM:-}"

usage() {
  cat <<EOF
Usage: scripts/build.sh <full|orcaslicer|continue|addon-slim>

Modes:
  full        Build tudo com o Dockerfile raiz e gera a imagem final (${TAG_FULL}).
  orcaslicer  Constrói somente as dependências de build do OrcaSlicer (stage 'orcaslicer') e tagueia como ${TAG_ORCASLICER} (padrão: orcaslicer-build-deps:3.0.1a).
  continue    Continua o build usando cache do OrcaSlicer; recompila engine/addon conforme necessário.
  addon-slim  Constrói a imagem mínima com apenas o addon pré-compilado (.node + liborcacli_engine.so) (${TAG_ADDON_SLIM}).

  TAG_ADDON_SLIM=${TAG_ADDON_SLIM}

Env:
  TAG_FULL=${TAG_FULL}
  TAG_ORCASLICER=${TAG_ORCASLICER}
  DOCKER_BUILD_FLAGS='${DOCKER_BUILD_FLAGS}'
  PLATFORM='${PLATFORM}'
EOF
}

require_mode() {
  if [[ -z "$MODE" ]]; then
    usage
    exit 1
  fi
  case "$MODE" in
    full|orcaslicer|continue|addon-slim) ;;
    -h|--help|help) usage; exit 0 ;;
    *) echo "[ERRO] Modo desconhecido: $MODE" >&2; usage; exit 1 ;;
  esac
}

has_image() {
  local image="$1"
  if docker image inspect "$image" >/dev/null 2>&1; then
    return 0
  else
    return 1
  fi
}

build_cmd() {
  # Choose docker build or buildx depending on PLATFORM
  if [[ -n "$PLATFORM" ]]; then
    echo "docker buildx build --platform ${PLATFORM} --load ${DOCKER_BUILD_FLAGS}"
  else
    echo "docker build ${DOCKER_BUILD_FLAGS}"
  fi
}

build_full() {
  echo "[INFO] Building FULL image: ${TAG_FULL}"
  local cmd
  cmd="$(build_cmd)"
  # --cache-from ajuda o Docker a reaproveitar camadas de builds anteriores se existirem
  if has_image "$TAG_FULL"; then
    cmd+=" --cache-from ${TAG_FULL}"
  fi
  cmd+=" --target base -t ${TAG_FULL} ."
  echo "+ $cmd"
  eval "$cmd"
}

build_orcaslicer_only() {
  echo "[INFO] Building ONLY OrcaSlicer app image (multi-stage target 'orcaslicer'): ${TAG_ORCASLICER}"
  local cmd
  cmd="$(build_cmd)"
  cmd+=" --target orcaslicer -t ${TAG_ORCASLICER} ."
  echo "+ $cmd"
  eval "$cmd"
}

build_continue_addon() {
  echo "[INFO] Continuing build (reuse cache for deps/OrcaSlicer), target: ${TAG_FULL}"
  local cmd
  cmd="$(build_cmd)"
  # Reaproveita cache da imagem final anterior se existir.
  if has_image "$TAG_FULL"; then
    cmd+=" --cache-from ${TAG_FULL}"
  fi
  cmd+=" --target base -t ${TAG_FULL} ."
  echo "+ $cmd"
  eval "$cmd"
}

build_addon_slim() {
  echo "[INFO] Building ADDON-SLIM image (minimal addon-only stage 'addon-slim'): ${TAG_ADDON_SLIM}"
  local cmd
  cmd="$(build_cmd)"
  cmd+=" --target addon-slim -t ${TAG_ADDON_SLIM} ."
  echo "+ $cmd"
  eval "$cmd"
}

main() {
  require_mode
  case "$MODE" in
    full)        build_full ;;
    orcaslicer)  build_orcaslicer_only ;;
    continue)    build_continue_addon ;;
    addon-slim)  build_addon_slim ;;
  esac
}

main "$@"

