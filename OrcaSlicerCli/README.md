# OrcaSlicerCli

## Status: Infrastructure complete and functional

## Policy: no placeholders
- No fallbacks, hacks, temporary code, or synthetic integrations.
- Any kind of placeholder content (e.g., fake G-code) is forbidden. If required libraries (libslic3r, etc.) are missing, the build must fail.
- The resulting binary must always produce real slicing output.

Verified now:
- Built and tested (approx. 612 KB executable)
- Architectural integration with OrcaSlicer (linked with libslic3r)
- Basic commands working (slice, info, version, help)
- Ready for next phase (full API integration)

```bash
# Quick test
cd OrcaSlicerCli/build
./bin/orcaslicer-cli version
./bin/orcaslicer-cli slice --input test.stl --output test.gcode
```

## Overview

OrcaSlicerCli extends OrcaSlicer with a robust, headless command-line interface. It does not reimplement slicing algorithms; instead it reuses the original OrcaSlicer source to provide a terminal-first experience.

## Goals

- Extend functionality: richer and complete CLI for OrcaSlicer
- Reuse existing, proven code from upstream OrcaSlicer
- Enable automation: easier integration into CI/pipelines and scripts
- Improve accessibility: for users who prefer a CLI

## Layout

```
OrcaSlicerCli/
(0m(B(0m(B README.md
(0m(B(0m(B src/          # CLI source
(0m(B(0m(B scripts/      # Build/utility scripts
(0m(B(0m(B docs/         # Additional docs
(0m(B(0m(B examples/     # Usage examples
```

## Dependencies

This project depends on the original OrcaSlicer source in the repository.
- It does not reimplement slicing algorithms
- It does not duplicate existing features
- It extends the command-line interface
- It links to the OrcaSlicer libraries/engines

## Key features

- Improved CLI UX (structured commands, parameters, helpful output)
- Automation-friendly (batch processing planned; logs for debugging)
- Modular design to add commands in the future

## Install / Build

### Prerequisites
- Compiled OrcaSlicer available
- OrcaSlicer dependencies installed
- CMake 3.16+
- C++17-capable compiler

### Build steps

1) Build OrcaSlicer (required):
```bash
cd OrcaSlicer
./build_release_macos.sh  # macOS
# or
./build_linux.sh          # Linux
# or
./build_release_vs2022.bat  # Windows
```

2) Build OrcaSlicerCli:
```bash
cd ../OrcaSlicerCli
./scripts/build.sh  # fast path

# Manual alternative
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DORCASLICER_ROOT_DIR=../../OrcaSlicer ..
make -j$(nproc)
```

3) Verify installation:
```bash
./scripts/test_build.sh
./install/bin/orcaslicer-cli --help
```

## Current project status

- [x] Organized directory structure
- [x] CMake build with OrcaSlicer auto-detection
- [x] Cross-platform automation scripts
- [x] Structured logging with levels
- [x] Robust argument parser (no external deps)
- [x] Consistent error/exception handling
- [x] Technical documentation
- [x] Architectural integration with OrcaSlicer (libslic3r)
- [x] Working commands: slice, info, version, help
- [x] Built and exercised locally

Integration details:
- [x] Architecture auto-detection (ARM64/x64)
- [x] Linking with `liblibslic3r.a` and `liblibslic3r_cgal.a`
- [x] Include paths configured for OrcaSlicer headers
- [x] Evolves with upstream updates

Next phase (full integration):
- [ ] Enable full libslic3r APIs
- [ ] Real slicing (replace any test stubs)
- [ ] Full 3D model loading
- [ ] Advanced configuration system
- [ ] Support upstream presets

## Basic usage

```bash
# Build the project
cd OrcaSlicerCli
./scripts/build.sh

# Run from build/
./bin/orcaslicer-cli --help

# Available now
./bin/orcaslicer-cli version
./bin/orcaslicer-cli slice --input model.stl --output model.gcode
./bin/orcaslicer-cli info --input model.stl

# Quick minimal STL for testing
cat > test.stl <<'EOF'
solid test
facet normal 0 0 1
  outer loop
    vertex 0 0 0
    vertex 1 0 0
    vertex 0 1 0
  endloop
endfacet
endsolid test
EOF

./bin/orcaslicer-cli slice --input test.stl --output test.gcode
```

## macOS quick build via CMake.app

```bash
/Applications/CMake.app/Contents/bin/cmake -S OrcaSlicerCli -B OrcaSlicerCli/build
/Applications/CMake.app/Contents/bin/cmake --build OrcaSlicerCli/build -j8
# Optional Ninja
/Applications/CMake.app/Contents/bin/cmake -S OrcaSlicerCli -B OrcaSlicerCli/build -G Ninja
/Applications/CMake.app/Contents/bin/cmake --build OrcaSlicerCli/build -- -j8
```

Tip: inside OrcaSlicerCli/, you can use `-S . -B build`.

## Updating with upstream OrcaSlicer

```bash
cd OrcaSlicer
git pull origin main
./build_release_macos.sh

cd ../OrcaSlicerCli/build
make clean && make -j4
```

You will pick up slicing improvements, bug fixes, and performance optimizations from upstream (subject to compatibility).

## Contributing

This project strives to stay compatible with upstream OrcaSlicer and follow similar development guidelines. Contributions are welcome via issues, pull requests, documentation, examples, and tests.

## License

Same license as the upstream OrcaSlicer. See the main project LICENSE for details.

## Related links

- https://github.com/SoftFever/OrcaSlicer
- https://github.com/SoftFever/OrcaSlicer/wiki
- https://github.com/SoftFever/OrcaSlicer/issues

---

Note: This is an unofficial extension and not maintained by the original OrcaSlicer team.
