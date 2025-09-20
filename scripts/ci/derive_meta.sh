#!/usr/bin/env bash
set -euo pipefail

suffix="${ORCASLICER_SUFFIX:-}"
base_version=$(sed -nE 's/.*set\(SoftFever_VERSION "([0-9]+\.[0-9]+\.[0-9]+).*".*/\1/p' OrcaSlicer/version.inc || true)
if [[ -z "$base_version" ]]; then
  echo "Could not parse OrcaSlicer version" >&2
  exit 1
fi
version="${base_version}${suffix}"

# Print for local runs
echo "version=$version"
echo "arch=amd64"

# Export for GitHub Actions outputs
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    echo "version=$version"
    echo "arch=amd64"
  } >> "$GITHUB_OUTPUT"
fi

