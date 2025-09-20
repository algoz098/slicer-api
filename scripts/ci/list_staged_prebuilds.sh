#!/usr/bin/env bash
set -euo pipefail

echo "Platform: ${RUNNER_OS:-local}"
ls -R prebuilds || true

