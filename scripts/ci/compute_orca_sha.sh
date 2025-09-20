#!/usr/bin/env bash
set -euo pipefail

sha=$(git rev-parse HEAD:OrcaSlicer 2>/dev/null || git -C OrcaSlicer rev-parse HEAD)

# Print for local runs
echo "ORCA_SHA=$sha"

# Export for GitHub Actions
if [[ -n "${GITHUB_ENV:-}" ]]; then
  echo "ORCA_SHA=$sha" >> "$GITHUB_ENV"
fi

