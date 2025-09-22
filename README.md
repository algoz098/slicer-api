# OrcaSlicer CLI

## About this repository (WIP, AI-built)

This repository is a work in progress, implemented end-to-end by AI. It is intended to provide a reproducible, headless way to use OrcaSlicer capabilities via:
- A command-line interface (CLI) to slice STL/3MF and generate G-code
- A Node.js native addon (N-API) to programmatically slice from JavaScript
- An HTTP API (node-api) exposing slicing endpoints for automation and integration

Goals:
- Full parity with OrcaSlicer’s slicing pipeline for batch/headless scenarios
- First-class support for configuration overrides across CLI, addon, and HTTP
- Deterministic, testable builds and outputs

Caveats:
- As a WIP, surfaces, options, and behaviors may change while interfaces stabilize
- Build scripts, Docker images, and CI may evolve alongside the embedded OrcaSlicer version

## Docker images and usage

This repository provides a multi-stage Dockerfile to build different artifacts. There is no public registry image at this time; build locally with the targets below. The Dockerfile supports two workflows:

1) Build everything inside Docker (simple, but slow on first build)
- Set ENFORCE_PREBUILT_BASE=false to allow compiling dependencies and core.

2) Use prebuilt base images (fast, recommended in CI)
- Provide BASE_DEPS_IMAGE and BASE_CORE_IMAGE that already contain OrcaSlicer deps/core.
- Keep ENFORCE_PREBUILT_BASE=true to avoid accidental long builds.

Common build args:
- NODE_VERSION (default: 24)
- CI_MAX_JOBS (parallelism passed to Ninja/CMake)
- ENFORCE_PREBUILT_BASE (default: true)
- USE_PREBUILT_DEPS (default: false)
- BASE_DEPS_IMAGE, BASE_CORE_IMAGE (names of your prebuilt images)

Available build targets and recommended tags:
- target=base → Node addon prebuilds + resources + standalone CLI binary
  - docker build --target base -t orcaslicer-addon:base --build-arg ENFORCE_PREBUILT_BASE=false .
- target=cli → minimal image with only the standalone CLI and resources
  - docker build --target cli -t orcaslicer-cli:latest --build-arg ENFORCE_PREBUILT_BASE=false .
- target=addon-slim → scratch image layer carrying only the addon prebuilds (no JS app, no resources)
  - docker build --target addon-slim -t orcaslicer-addon:slim --build-arg ENFORCE_PREBUILT_BASE=false .

Example: run the CLI container
- The cli image uses /usr/local/bin/orcaslicer-cli as entrypoint.
- Mount input/output and run slice:

```bash
# Build (first time may take a while)
docker build --target cli -t orcaslicer-cli:latest --build-arg ENFORCE_PREBUILT_BASE=false .

# Run slicing
docker run --rm \
  -v "$PWD/example_files:/in:ro" \
  -v "$PWD/output_files:/out" \
  orcaslicer-cli:latest \
  slice \
  --input /in/3DBenchy.stl \
  --output /out/benchy.gcode \
  --printer "Bambu Lab X1 Carbon 0.4 nozzle" \
  --filament "Bambu PLA Basic @BBL X1C" \
  --process "0.20mm Standard @BBL X1C" \
  --set "sparse_infill_density=30,layer_height=0.24"
```

Example: consume the addon prebuilds in your application image

```dockerfile
# Your Dockerfile
FROM orcaslicer-addon:base AS base  # or target=addon-slim depending on your needs

# Copy only the native addon + engine
FROM node:24-bookworm-slim AS app
WORKDIR /app
COPY --from=base /opt/orca/OrcaSlicerCli/bindings/node/prebuilds ./prebuilds
# Or, if you used target=base, you can also use the embedded resources via ORCACLI_RESOURCES
ENV ORCACLI_RESOURCES=/opt/orca/OrcaSlicer/resources
```

Using prebuilt base images (advanced)

```bash
# Build deps/core once elsewhere and push to your registry, then reference here
# e.g., BASE_DEPS_IMAGE=ghcr.io/you/orca-deps:bookworm-amd64
#       BASE_CORE_IMAGE=ghcr.io/you/orca-core:bookworm-amd64

docker build \
  --target base \
  -t orcaslicer-addon:base \
  --build-arg BASE_DEPS_IMAGE=ghcr.io/you/orca-deps:bookworm-amd64 \
  --build-arg BASE_CORE_IMAGE=ghcr.io/you/orca-core:bookworm-amd64 \
  --build-arg ENFORCE_PREBUILT_BASE=true \
  --build-arg USE_PREBUILT_DEPS=true \
  .
```

A command-line interface for OrcaSlicer that slices STL/3MF files and generates real G-code without a GUI.

## Policy: no placeholders
- No fallbacks, hacks, temporary code, or synthetic integrations.
- Generating placeholders (e.g., fake G-code) is forbidden. If mandatory dependencies (like libslic3r) are unavailable, the build must FAIL.
- Execution must only produce real slicing results.

## Project structure

```
.
├── OrcaSlicer/          # Upstream OrcaSlicer source (do not modify here)
├── OrcaSlicerCli/       # CLI implementation
├── example_files/       # Example inputs
│   └── 3DBenchy.stl
├── output_files/        # Generated outputs
└── README.md
```

## Quick use

### Manual

```bash
cd OrcaSlicerCli/build
./bin/orcaslicer-cli slice --input ../../example_files/3DBenchy.stl --output ../../output_files/3DBenchy.gcode
```

- Export production 3MF (embedded G-code) from a .3mf project (plate 1):
```bash
./bin/orcaslicer-cli slice \
  --input ../../example_files/3DBenchy.3mf \
  --output ../../output_files/3DBenchy_plate_1.gcode.3mf \
  --plate 1
```

- Plate 2:
```bash
./bin/orcaslicer-cli slice \
  --input ../../example_files/3DBenchy.3mf \
  --output ../../output_files/3DBenchy_plate_2.gcode.3mf \
  --plate 2
```

Tip: the output file extension selects the format. If it ends with `.3mf`, the CLI packages G-code into a production 3MF (GUI parity: SaveStrategy `Silence|SplitModel|WithGcode|SkipModel|Zip64`). Otherwise it writes plain `.gcode`.

## Configuration overrides (CLI, Addon, HTTP API)

- CLI accepts overrides via `--set` with comma-separated `key=value` pairs:
  ```bash
  ./bin/orcaslicer-cli slice \
    --input ../../example_files/3DBenchy.3mf \
    --output ../../output_files/3DBenchy_plate_1.gcode.3mf \
    --plate 1 \
    --printer "Bambu Lab X1 Carbon 0.4 nozzle" \
    --set "sparse_infill_density=30,layer_height=0.24,curr_bed_type=High Temp Plate,first_layer_bed_temperature=65"
  ```
  Notes:
  - Keys match those in OrcaSlicer’s exported INI.
  - Precedence: `--set` > explicit profiles (printer/filament/process) > embedded presets in .3mf > defaults.
- Node addon (N-API) accepts `options`:
  ```js
  await orca.slice({
    input, output,
    options: { sparse_infill_density: 30, layer_height: 0.24, support_material: true }
  })
  ```
- HTTP API (node-api) accepts `options` in the JSON body (see tests under node-api/test/services/slicer).

See docs/OVERRIDES.md for the full list of keys, types, aliases, and rules.

## GUI parity for production 3MF (embedded G-code)

- The CLI follows the same flow as the GUI when exporting a production 3MF (Export plate sliced file):
  - PresetBundle → full_config_secure()
  - Print.apply() → Print.process() → Print.export_gcode()
  - Packaging via store_bbs_3mf() with SaveStrategy: Silence | SplitModel | WithGcode | SkipModel | Zip64
- The output extension controls the mode:
  - .gcode → plain G-code
  - .3mf → production 3MF with embedded G-code (gcode.3mf)
- Selecting plates in .3mf projects: use `--plate N` (1-based). The index is propagated to Print and Model same as the GUI, ensuring correct per-plate parameters (e.g., wipe_tower_x/y).

### Comparison/validation rules (parity)
When comparing embedded G-code with GUI-generated files:
- Ignore in HEADER: the “generated by …” line (timestamp) and “model label id”.
- Require real parity of times: “model printing time” and “total estimated time”.
- Require full parity of CONFIG_BLOCK (including per-plate wipe_tower_x/y).
- In the G-code body, ignore M73 progress lines (environment-dependent).

### Automated parity test
There is a test that generates .gcode.3mf for plates 1 and 2 from example_files/3DBenchy.3mf and compares with reference files under comparable_files/:

```bash
cd OrcaSlicerCli
bash test_3mf_production.sh
```

Expected output:
- “Embedded G-code identical to reference (with allowed normalizations)” for each plate.

Note: Reference files were produced with the same OrcaSlicer (GUI) build. If you update OrcaSlicer, regenerate the references to maintain parity.

## Build

### Prerequisites

- macOS with Xcode Command Line Tools
- CMake 3.13+ (`/Applications/CMake.app/Contents/bin/cmake`)
- ARM64 (Apple Silicon)

### Compile

```bash
cd OrcaSlicerCli/build
rm -rf *
/Applications/CMake.app/Contents/bin/cmake ..
make -j4
```

## Project status

### Implemented

- STL loading using `Slic3r::TriangleMesh::ReadSTLFile()`
- Complete configuration via `DynamicPrintConfig::full_print_config()` (503 options)
- Full OrcaSlicer slicing pipeline
  - Model/config application
  - Slicing (walls, surfaces, infill)
  - Support detection
  - Conflict checks
- G-code generation (valid, complete)
- CLI commands `slice` and `info`

### Known issues

- Segfault on shutdown after generating G-code; the file is created correctly
- G-code is temporarily saved as `.tmp` and needs renaming (the test scripts handle this)

### Fixed

- filename_format template parsing: set `{input_filename_base}.gcode`
- Absolute/relative paths supported
- Output directory-only support (auto file naming)

### Results

- Input: `3DBenchy.stl` (225,154 triangles, 15,550.4 mm³)
- Output: `3DBenchy.gcode` (~3.4MB)
- Processing time: ~2–3s
- Peak memory: ~200MB

## Technical details

Dependencies successfully integrated from OrcaSlicer: libslic3r, libslic3r_cgal, Boost, Eigen, OpenCASCADE, TBB, PNG/JPEG/FreeType/Expat/Crypto, etc.

Default config example: layer_height=0.2mm, perimeters=3, infill=20%, nozzle=0.4mm, PLA 1.75mm, temps 210/60°C.

Architecture:
```
CliCore.cpp
├── initializeSlic3r()
├── loadModelFromFile()
├── performSlicing()
└── getModelInformation()
```

### Multi-plate 3MF (plate normalization)

- 3MF projects often store multiple plates in a logical grid with a gap between plates.
- The GUI normalizes the selected plate by removing the global grid offset and re-centering on the bed.
- The CLI reproduces this behavior with `--plate` for `.3mf` files by deriving stride from bed size and the logical gap and recenters accordingly (not applicable to STL/OBJ).
- Result: selected plate G-code opens in the GUI centered on the bed, matching GUI output.

## Example logs

```
2025-09-15 15:32:53.205 [000INFO] Starting slice operation...
DEBUG: Loaded full print config with 503 options
DEBUG: Model has 1 objects
DEBUG: Apply completed successfully
DEBUG: Print processing completed
[info] Slicing process finished. Resident memory: 199MB; Peak memory usage: 201MB
Slicing completed successfully
Output file: output_files/3DBenchy.gcode (~3.4M)
```

## Next steps

1. Investigate and fix the shutdown segfault
2. Broader configuration via CLI
3. Additional input formats beyond STL
4. Performance and memory optimizations

## License

This project uses OrcaSlicer source code. Refer to OrcaSlicer’s original licenses for details.

---

Built with OrcaSlicer libslic3r
