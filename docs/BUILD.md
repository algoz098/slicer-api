# Build System

How the project is compiled: CMake (C++), cmake-js (Node addon), Makefile (Docker).

## Overview of build targets

```
OrcaSlicer/                    (submodule, must be compiled first)
  └── build/                   (libslic3r.a, semver.a, deps)

OrcaSlicerAddon/
  ├── CMakeLists.txt           (addon root: finds OrcaSlicer)
  ├── src/CMakeLists.txt       (defines orcacli_core + orcacli_engine)
  └── bindings/node/CMakeLists.txt (defines orcaslicer_node)

CMake targets:
  orcacli_core    (STATIC lib)  ← AddonCore + utils, links libslic3r
  orcacli_engine  (SHARED lib)  ← EngineAPI.cpp, links orcacli_core
  orcaslicer_node (MODULE .node) ← addon.cc, does NOT link core (dlopen at runtime)
```

## Prerequisites

### Compiled OrcaSlicer

The `OrcaSlicer/` submodule must be compiled **before** the addon:

```bash
cd OrcaSlicer
./build_release_macos.sh    # macOS
# or
./build_linux.sh            # Linux
```

This produces:
- `OrcaSlicer/build/src/libslic3r/liblibslic3r.a`
- `OrcaSlicer/build/src/libslic3r/liblibslic3r_cgal.a`
- `OrcaSlicer/build/lib/libsemver.a`
- Deps in `OrcaSlicer/deps/build/{OrcaSlicer_dep,destdir}/usr/local/`

### System dependencies

- **CMake 3.20+** (for src/CMakeLists.txt)
- **C++17** (GCC 8+, Clang 8+, MSVC 2019+)
- **Node.js 16+** (addon), **24+** (node-api)
- **Boost**, **TBB**, **OpenVDB**, **NLopt**, **CGAL**, **OpenCV**, **OpenCASCADE**
  (all provided by the OrcaSlicer deps build)

## Local addon build (development)

```bash
cd OrcaSlicerAddon/bindings/node
npm ci                    # installs cmake-js, node-api-headers
npm run configure         # cmake configure (via cmake-js)
npm run build             # cmake build
```

### Important CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `ORCASLICER_ROOT_DIR` | `../OrcaSlicer` | Root of the OrcaSlicer submodule |
| `ORCACLI_BUILD_NODE_ADDON` | `OFF` | Builds the N-API binding |
| `ORCACLI_REQUIRE_LIBS` | `OFF` (`ON` in CI) | Fail if libslic3r/semver are not found |
| `ORCACLI_SKIP_CORE_BUILD` | `OFF` | Skip the core build (addon dlopens at runtime) |
| `ORCACLI_BUILD_TESTS` | `OFF` | Compile tests |
| `ORCACLI_ENABLE_ASAN` | `OFF` | AddressSanitizer (debug) |
| `ORCACLI_LIBSLIC3R_ROOT` | `""` | Override for the OrcaSlicer build dir |
| `ORCASLICER_ARCH` | auto | `arm64` or `x64` |

### Architecture detection

CMake detects the OrcaSlicer build dir:
- `OrcaSlicer/build/arm64/` → macOS ARM
- `OrcaSlicer/build/x64/` → x86_64
- Override: `ORCACLI_LIBSLIC3R_ROOT=/path/to/build`

### Memory optimization

`AddonCore.cpp` is compiled with `-O1` (not `-O3`) to avoid OOM on Docker
builders with little RAM. The other files use `-O3`.

### Build output

```
OrcaSlicerAddon/build/
├── src/
│   └── liborcacli_engine.{dylib,so}   ← shared engine
└── bindings/node/
    └── orcaslicer_node.node            ← N-API module
```

The engine is also copied to `bindings/node/build/bindings/node/` to simplify
runtime loading (via `ensure_engine_loaded()`).

## Dependency resolution (src/CMakeLists.txt)

`src/CMakeLists.txt` looks for libs in multiple paths (Linux, Windows, macOS):

```
${ORCASLICER_BUILD_DIR}/deps/usr/local/          ← Linux (destdir)
${ORCASLICER_ROOT_DIR}/deps/build/x64/OrcaSlicer_dep/usr/local/   ← Windows
${ORCASLICER_ROOT_DIR}/deps/build/OrcaSlicer_dep/usr/local/       ← macOS/legacy
${ORCASLICER_ROOT_DIR}/deps/build/destdir/usr/local/              ← Linux legacy
```

Linked libs (static):
- **libslic3r**, **libslic3r_cgal**, **semver**
- **Boost** (filesystem, log, log_setup, thread, locale, nowide, chrono)
- **TBB**, **TBB malloc**
- **OpenCASCADE** (TKernel, TKBO, TKSTEP, ... ~30 libs)
- **miniz**, **PNG**, **Qhull**, **MPFR**, **GMP**, **JPEG**
- **NLopt**, **admesh**, **freetype**, **clipper**, **clipper2**, **qoi**
- **libnoise**, **glu-libtess**, **mcut**
- System: **expat**, **zlib**, **crypto** (OpenSSL)

> **macOS**: `libcrypto` is looked up in Homebrew (`/opt/homebrew/opt/openssl@3/lib`).
> Requires `brew install openssl@3`.
> Frameworks: **Foundation**, **ModelIO**, **iconv**, **IOKit**, **CoreFoundation**.

> **Linux**: **fontconfig** (via pkg-config), **dl**.

## Docker build (production)

The `Dockerfile` uses a multi-stage build:

```
deps        → compiles OrcaSlicer dependencies (Ninja)
core        → compiles libslic3r (Ninja, Release, static)
addon-core  → compiles orcacli_engine + orcaslicer_node (cmake-js)
addoncore   → base image with prebuilds (FROM addon-core image)
base        → runtime: Node.js slim + resources + prebuilds
addon-slim  → scratch: prebuilds only (carrier image)
```

### Makefile targets

| Target | Description |
|--------|-------------|
| `make show-meta` | Show OWNER/VERSION/ARCH and image names |
| `make build-all` | Local build of deps + core + addon-core (no push) |
| `make deps-build` | Build the deps stage |
| `make core-build` | Build the core stage |
| `make addon-core-build` | Build the addon-core stage |
| `make addon-base-build` | Build the addon base image |
| `make addon-slim-build` | Build the carrier image (scratch) |
| `make push-all` | Build + push of all images |
| `make push-all-local` | Push already-built local images |
| `make login-ghcr` | docker login ghcr.io (requires GHCR_USER/GHCR_TOKEN) |
| `make linux-build-inside-docker` | Reproduce the CI build locally |

### Makefile variables

| Variable | Default | Description |
|----------|---------|-------------|
| `REGISTRY` | `ghcr.io` | Docker registry |
| `OWNER` | auto (git remote) | Owner/org |
| `VERSION` | auto (`scripts/ci/derive_meta.sh`) | Derived version |
| `ARCH` | auto | `linux/arm64` or `linux/amd64` |
| `PLATFORM` | auto | Docker platform |
| `CI_MAX_JOBS` | `""` | Ninja/CMake parallelism limit |
| `LOW_MEM` | `""` | If `1`, forces `CI_MAX_JOBS=1` |

### Building the slicer-api image

```bash
# Full build (requires published deps/core/addon-core images)
docker build --target slicer-api -t slicer-api:latest .

# Or via Makefile
make build-all
```

## Build via CI

The scripts in `scripts/ci/` orchestrate the build on GitHub Actions:

- `build_deps_image.sh` — build + push the deps image
- `build_core_image.sh` — build + push the core image
- `derive_meta.sh` — derive VERSION and ARCH
- `check_image_exists.sh` — check whether an image exists in the registry
- `linux_build_inside_docker.sh` — reproduce the build inside a container

## Tests

### Addon tests

```bash
cd OrcaSlicerAddon/bindings/node
npm test    # runs: smoke, unit, options, e2e
```

Files in `test/`: `smoke.js`, `unit.js`, `options.js`, `e2e.js`,
`slice_compare.js`.

### API tests

```bash
cd node-api
npm test    # mocha with tsx, recursive test/
```

### Smoke test

```bash
cd node-api
npm run smoke:slicer    # scripts/smoke-slicer.ts
```

## Build troubleshooting

| Problem | Solution |
|---------|----------|
| `libslic3r not found` | Compile OrcaSlicer first |
| `Boost headers not found` | Check `deps/build/*/include` |
| `OpenSSL libcrypto not found` (macOS) | `brew install openssl@3` |
| OOM when compiling AddonCore.cpp | `CI_MAX_JOBS=1` or `LOW_MEM=1` |
| `.node` loads a stale engine | `ORCACLI_PREFER_LOCAL=1` (default in node-api) |
| `FATAL_ERROR: cmake version` (Windows) | Use CMake 3.13–3.31 |
