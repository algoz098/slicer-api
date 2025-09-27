#!/usr/bin/env bash
set -euo pipefail

image="${1:-}"
if [[ -z "$image" ]]; then
  echo "Usage: $0 <image>" >&2
  exit 2
fi

# Auto-detect platform if not provided via env PLATFORM
PLATFORM="${PLATFORM:-}"
if [[ -z "$PLATFORM" ]]; then
  case "$(uname -m)" in
    arm64|aarch64) PLATFORM="linux/arm64" ;;
    x86_64|amd64)  PLATFORM="linux/amd64" ;;
    *)             PLATFORM="linux/amd64" ;;
  esac
fi
# If image exists locally and not forcing rebuild, push it directly
if [[ "${FORCE_REBUILD:-}" != "1" ]] && docker image inspect "$image" >/dev/null 2>&1; then
  echo "[INFO] Found local image: $image — pushing without rebuild. Set FORCE_REBUILD=1 to rebuild."
  docker push "$image"
  exit 0
fi

# Skip rebuild if image already exists in registry (unless FORCE_REBUILD=1)
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exists="$("$SCRIPT_DIR"/check_image_exists.sh "$image" | awk -F= '/^exists=/{print $2}')"
if [[ "${FORCE_REBUILD:-}" != "1" && "$exists" == "true" ]]; then
  echo "[INFO] Image already exists in registry: $image — skipping build. Set FORCE_REBUILD=1 to force rebuild."
  exit 0
fi


docker buildx build \
  --platform "$PLATFORM" \
  --target deps \
  -t "$image" \
  ${DOCKER_BUILD_ARGS:-} \
  ${CI_MAX_JOBS:+--build-arg CI_MAX_JOBS=${CI_MAX_JOBS}} \
  --push \
  .

