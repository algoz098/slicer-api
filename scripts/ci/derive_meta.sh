#!/usr/bin/env bash
set -euo pipefail

suffix="${ORCASLICER_SUFFIX:-}"

# Check if OrcaSlicer directory exists
if [[ ! -d "OrcaSlicer" ]]; then
  echo "ERROR: OrcaSlicer directory not found. Submodule may not be initialized." >&2
  echo "Current directory: $(pwd)" >&2
  echo "Contents:" >&2
  ls -la >&2
  exit 1
fi

# Check if version.inc exists
if [[ ! -f "OrcaSlicer/version.inc" ]]; then
  echo "ERROR: OrcaSlicer/version.inc not found" >&2
  echo "OrcaSlicer directory contents:" >&2
  ls -la OrcaSlicer/ >&2
  exit 1
fi

base_version=$(sed -nE 's/.*set\(SoftFever_VERSION "([0-9]+\.[0-9]+\.[0-9]+).*".*/\1/p' OrcaSlicer/version.inc || true)
if [[ -z "$base_version" ]]; then
  echo "ERROR: Could not parse OrcaSlicer version from version.inc" >&2
  echo "Content of OrcaSlicer/version.inc:" >&2
  cat OrcaSlicer/version.inc >&2
  exit 1
fi
version="${base_version}${suffix}"

# Detect local architecture and normalize
uname_arch=$(uname -m || echo "unknown")
case "$uname_arch" in
  arm64|aarch64) arch="arm64" ;;
  x86_64|amd64)  arch="amd64" ;;
  *)             arch="amd64" ;;
esac

# Print for local runs
echo "version=$version"
echo "arch=$arch"

# Export for GitHub Actions outputs
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    echo "version=$version"
    echo "arch=$arch"
  } >> "$GITHUB_OUTPUT"
fi

