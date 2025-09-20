#!/usr/bin/env bash
set -euo pipefail

image="${1:-}"
core_image="${2:-}"
if [[ -z "$image" || -z "$core_image" ]]; then
  echo "Usage: $0 <image> <base_core_image>" >&2
  exit 2
fi

docker buildx build \
  --platform linux/amd64 \
  --target cli \
  --build-arg BASE_CORE_IMAGE="$core_image" \
  -t "$image" \
  --push \
  .

