#!/usr/bin/env bash
set -euo pipefail

image="${1:-}"
base_deps="${2:-}"
if [[ -z "$image" || -z "$base_deps" ]]; then
  echo "Usage: $0 <image> <base_deps_image>" >&2
  exit 2
fi

docker buildx build \
  --platform linux/amd64 \
  --target core \
  --build-arg BASE_DEPS_IMAGE="$base_deps" \
  --build-arg USE_PREBUILT_DEPS=true \
  -t "$image" \
  --push \
  .

