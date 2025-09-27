#!/usr/bin/env bash
set -euo pipefail

# Ensure symlink so code can require ../../../../../OrcaSlicerCli/bindings/node
mkdir -p /workspace/OrcaSlicerCli/bindings
if [ ! -e /workspace/OrcaSlicerCli/bindings/node ]; then
  ln -s /opt/orca/OrcaSlicerCli/bindings/node /workspace/OrcaSlicerCli/bindings/node
fi

cd /workspace/node-api

# Optional: use npm cache if host mounted
export npm_config_cache=${npm_config_cache:-/root/.npm}

# Smart install: reuse node_modules if lock hash unchanged
LOCK_FILE="package-lock.json"
HASH_FILE="node_modules/.lock_hash"
NEED_INSTALL=1
if [ -d node_modules ] && [ -f "$LOCK_FILE" ] && [ -f "$HASH_FILE" ]; then
  CURR_HASH=$(sha256sum "$LOCK_FILE" | awk '{print $1}')
  PREV_HASH=$(cat "$HASH_FILE" || true)
  if [ "$CURR_HASH" = "$PREV_HASH" ]; then
    NEED_INSTALL=0
  fi
fi

if [ "$NEED_INSTALL" -eq 1 ]; then
  if [ -f "$LOCK_FILE" ]; then
    npm ci || npm install
  else
    npm install
  fi
  if [ -f "$LOCK_FILE" ]; then
    mkdir -p node_modules
    sha256sum "$LOCK_FILE" | awk '{print $1}' > "$HASH_FILE"
  fi
fi

# Improve file watching on mounted volumes
export CHOKIDAR_USEPOLLING=${CHOKIDAR_USEPOLLING:-1}
export CHOKIDAR_INTERVAL=${CHOKIDAR_INTERVAL:-250}

# If engine path not set or missing, try to auto-detect a valid liborcacli_engine.so
if [ -z "${ORCACLI_ENGINE_PATH:-}" ] || [ ! -f "$ORCACLI_ENGINE_PATH" ]; then
  for cand in \
    "/opt/orca/OrcaSlicerCli/bindings/node/prebuilds/$(node -p "process.platform+'-'+process.arch")/liborcacli_engine.so" \
    "/opt/orca/OrcaSlicerCli/bindings/node/liborcacli_engine.so" \
    "/opt/orca/OrcaSlicerCli/build/bindings/node/liborcacli_engine.so" \
    "/opt/orca/OrcaSlicerCli/build/src/liborcacli_engine.so" \
    "/opt/orca/OrcaSlicerCli/build-ninja/src/liborcacli_engine.so"; do
    if [ -f "$cand" ]; then export ORCACLI_ENGINE_PATH="$cand"; break; fi
  done
fi

echo "[entry] ORCACLI_ENGINE_PATH=${ORCACLI_ENGINE_PATH:-<unset>}"
if [ -n "${ORCACLI_ENGINE_PATH:-}" ] && [ -f "$ORCACLI_ENGINE_PATH" ]; then
  echo "[entry] engine file exists; ldd output:" && ldd "$ORCACLI_ENGINE_PATH" || true
  ls -la "$(dirname "$ORCACLI_ENGINE_PATH")" || true
fi

# Start with hot-reload
exec npx nodemon --legacy-watch --watch src --ext ts,js,json -x ts-node src/index.ts

