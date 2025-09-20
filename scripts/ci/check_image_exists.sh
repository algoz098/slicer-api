#!/usr/bin/env bash
set -euo pipefail

image="${1:-}"
if [[ -z "$image" ]]; then
  echo "Usage: $0 <image-ref>" >&2
  exit 2
fi

echo "Checking $image"
if docker buildx imagetools inspect "$image" >/dev/null 2>&1; then
  echo "exists=true"
  [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "exists=true" >> "$GITHUB_OUTPUT"
else
  echo "exists=false"
  [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "exists=false" >> "$GITHUB_OUTPUT"
fi

