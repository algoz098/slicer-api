#!/usr/bin/env bash
set -euo pipefail

version="${1:-}"
arch="${2:-}"
owner="${3:-}"
if [[ -z "$version" || -z "$arch" || -z "$owner" ]]; then
  echo "Usage: $0 <version> <arch> <repository_owner>" >&2
  exit 2
fi

echo "Skipping Linux build: deps image exists in GHCR: ghcr.io/$owner/orcaslicer-build-deps:$version-$arch"

