#!/usr/bin/env bash
set -euo pipefail

image="${1:-}"
if [[ -z "$image" ]]; then
  echo "Usage: $0 <image-ref> [platform]" >&2
  exit 2
fi

# Expected platform (optional): from 2nd arg or env PLATFORM
expected_platform="${2:-${PLATFORM:-}}"

echo "Checking $image"${expected_platform:+" for platform $expected_platform"}

# Inspect the image manifest(s)
if ! output=$(docker buildx imagetools inspect "$image" 2>/dev/null); then
  echo "exists=false"
  [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "exists=false" >> "$GITHUB_OUTPUT"
  exit 0
fi

# If a specific platform is requested, verify it is present in the manifest list
if [[ -n "$expected_platform" ]]; then
  if echo "$output" | grep -qE "(^|[[:space:]])${expected_platform}([[:space:]]|$)"; then
    echo "exists=true"
    [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "exists=true" >> "$GITHUB_OUTPUT"
  else
    echo "exists=false"
    [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "exists=false" >> "$GITHUB_OUTPUT"
  fi
else
  # No platform asserted: tag exists
  echo "exists=true"
  [[ -n "${GITHUB_OUTPUT:-}" ]] && echo "exists=true" >> "$GITHUB_OUTPUT"
fi
