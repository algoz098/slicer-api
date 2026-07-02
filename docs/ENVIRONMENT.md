# Environment Variables

All environment variables read by the stack (addon + API).

## Quick summary

| Variable | Layer | Description |
|----------|-------|-------------|
| `ORCACLI_RESOURCES` | node-api | Path to `OrcaSlicer/resources` |
| `ORCACLI_ADDON_DIR` | node-api | Path to the addon's `bindings/node` |
| `ORCACLI_PREFER_LOCAL` | node-api | Forces local-first resolution of the `.node` |
| `ORCACLI_PREFER_PREBUILT` | addon (JS) | Forces prebuilt-first resolution of the `.node` |
| `ORCACLI_ENGINE_PATH` | addon (C++) | Override for the engine path (`dlopen`) |
| `ORCACLI_LOG_LEVEL` | addon (C++) | Log level (0-5 or string) |
| `ORCACLI_QUIET` | addon (C++) | Quiet mode (errors only) |
| `ORCA_ADDON_LOG` | addon (JS/C++) | `off` silences addon logs |
| `ORCACLI_SILENT` | addon (JS/C++) | `1` silences addon I/O |
| `PORT` / `HOST` | node-api | Server port and host |

## Details

### node-api (TypeScript)

#### `ORCACLI_RESOURCES`
- **Read in**: `src/orca.ts:16`
- **Default**: `../../OrcaSlicer/resources` (relative to `src/`)
- **Description**: Path to the OrcaSlicer `resources` directory. Contains
  profiles, textures, SLA patterns, etc. Passed to
  `orca.initialize({ resourcesPath })` and exposed as
  `app.get('orca_resourcesPath')`.
- **In Docker**: `/opt/orca/OrcaSlicer/resources`

#### `ORCACLI_ADDON_DIR`
- **Read in**: `src/orca.ts:11`
- **Default**: `../../OrcaSlicerAddon/bindings/node` (relative to `src/`)
- **Description**: Path to the Node binding directory. `orca.ts` does
  `require(addonDir)`, which loads `index.js` → resolves the `.node`.
- **In Docker (slim)**: `/opt/orca/orcaslicer-addon`

#### `ORCACLI_PREFER_LOCAL`
- **Read in**: `src/orca.ts:7`
- **Default**: Set to `'1'` automatically if neither `PREFER_LOCAL` nor
  `PREFER_PREBUILT` is defined.
- **Description**: Forces `index.js` to resolve the local (build) `.node`
  before the prebuilt one. Avoids using stale npm binaries during dev.

#### `PORT`
- **Default**: `3030` (from `config/default.json`)
- **Description**: HTTP server port.

#### `HOST`
- **Default**: `localhost` (from `config/default.json`)
- **Description**: Server bind host.

### Addon — JavaScript (`index.js`)

#### `ORCACLI_PREFER_PREBUILT`
- **Default**: unset
- **Description**: If `'1'`, reverses the `.node` resolution order:
  prebuilt first (`prebuilds/<plat>-<arch>/`), then the local build.
  Useful in production with npm install.

#### `ORCA_ADDON_LOG`
- **Default**: unset
- **Description**: If `'off'`, silences the debug logs from `index.js`
  (`DEBUG: [addon-js] ...`).

#### `ORCACLI_SILENT`
- **Default**: unset
- **Description**: If `'1'`, enables quiet mode in `index.js` (suppresses
  writes to stdout/stderr during the `.node` require).

### Addon — C++ (`addon.cc`)

#### `ORCACLI_ENGINE_PATH`
- **Read in**: `addon.cc:191` (`ensure_engine_loaded`)
- **Default**: unset
- **Description**: Explicit override of the
  `liborcacli_engine.{dylib,so}` path. If set, it is the **first** candidate
  tried by `dlopen`. Useful for debugging with ASAN or custom engines.

#### `ORCA_ADDON_LOG` (C++ layer)
- **Read in**: `addon.cc:33` (`addon_is_silent`)
- **Description**: If `'off'`, silences the `ADDON_DEBUGF` output from `addon.cc`.

#### `ORCACLI_SILENT` (C++ layer)
- **Read in**: `addon.cc:34`
- **Description**: If `'1'`, same effect as `ORCA_ADDON_LOG=off`.

### Addon — Engine (`EngineAPI.cpp` / libslic3r)

#### `ORCACLI_LOG_LEVEL`
- **Read in**: `EngineAPI.cpp:80` (during `orcacli_initialize`)
- **Default**: `1` (error)
- **Accepted values**:
  - Numeric: `0` (fatal) to `5` (trace)
  - Strings: `fatal`, `error`, `warning`, `info`, `debug`, `trace`
- **Description**: libslic3r logging level via `Slic3r::set_logging_level()`.

#### `ORCACLI_QUIET`
- **Read in**: `EngineAPI.cpp:79`
- **Default**: unset
- **Description**: If set and not `"0"`, forces `level = 1` (errors only).
  Overrides `ORCACLI_LOG_LEVEL`.

#### `SLIC3R_LOGLEVEL`
- **Read by**: libslic3r directly (not by our code)
- **Description**: Alternative log level read by libslic3r in some code paths.
  For controlled logging, prefer `ORCACLI_LOG_LEVEL`.

## Build variables (CI / Docker)

These are used during the build, not at runtime:

| Variable | Default | Description |
|----------|---------|-------------|
| `CI_MAX_JOBS` | `""` (auto) | Ninja/CMake parallelism in Docker |
| `LOW_MEM` | `""` | If `1`, forces `CI_MAX_JOBS=1` |
| `CMAKE_OLEVEL` | `2` | Optimization level in the deps build |
| `LD_NO_KEEP_MEMORY` | `false` | If `true`, `-Wl,--no-keep-memory` at link time |
| `GHCR_USER` | — | GitHub Container Registry user |
| `GHCR_TOKEN` | — | GHCR token |
| `ORCACLI_REQUIRE_LIBS` | `OFF` (`ON` in CI) | Fail the build if libslic3r is not found |
| `ENFORCE_PREBUILT_BASE` | `true` | Require prebuilt base images in Docker |

## Examples

### Local development (macOS)

```bash
# node-api in dev
cd node-api
ORCACLI_RESOURCES=../OrcaSlicer/resources npm run dev
```

### Production (Docker)

```bash
docker build --target slicer-api -t slicer-api:latest .
docker run -p 3030:3030 slicer-api:latest
# ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources (already set in the image)
```

### Debug with verbose logging

```bash
ORCACLI_LOG_LEVEL=debug npm run dev
# Or more granular:
ORCACLI_LOG_LEVEL=trace npm run dev
```

### Debug engine loading

```bash
# See the paths tried by dlopen
node -e "require('./OrcaSlicerAddon/bindings/node')"
# Logs: "DEBUG: [addon] ensure_engine_loaded: try '<path>' => <ptr>"

# Explicit engine override
ORCACLI_ENGINE_PATH=/path/to/liborcacli_engine.dylib node app.js
```

### Quiet mode (production)

```bash
ORCA_ADDON_LOG=off ORCACLI_QUIET=1 npm start
```
