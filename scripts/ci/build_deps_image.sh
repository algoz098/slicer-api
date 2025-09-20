#!/usr/bin/env bash
set -euo pipefail

image="${1:-}"
if [[ -z "$image" ]]; then
  echo "Usage: $0 <image>" >&2
  exit 2
fi

docker buildx build \
  --platform linux/amd64 \
  --target deps \
  -t "$image" \
  --push \
  .

