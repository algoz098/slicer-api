# OrcaSlicer CLI - Profiles Guide

## Available test scripts

### 1) `test_simple.sh`  Simple script
Basic quick test with profiles.

Default usage (uses the same profiles as the reference 3DBenchy.gcode):
```bash
./test_simple.sh
```

Custom profiles:
```bash
./test_simple.sh "PRINTER" "FILAMENT" "PROCESS"
```

Examples:
```bash
# Use default reference profiles
./test_simple.sh

# Use PLA Basic instead of Matte
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Basic @BBL X1C" "0.20mm Standard @BBL X1C"

# Use Fine quality
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Matte @BBL X1C" "0.15mm Fine @BBL X1C"

# Use ABS
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu ABS @BBL X1C" "0.20mm Standard @BBL X1C"
```

### 2) `test_slice.sh`  Full script with comparison
Performs slicing and detailed comparison with a reference file.

Usage:
```bash
./test_slice.sh [PRINTER] [FILAMENT] [PROCESS]
```

## Default profiles

By default the scripts use the same profiles as the reference `3DBenchy.gcode`:

- Printer: `Bambu Lab X1 Carbon 0.4 nozzle`
- Filament: `Bambu PLA Matte @BBL X1C`
- Process: `0.20mm Standard @BBL X1C`

## Common profiles

Printers:
- `Bambu Lab X1 Carbon 0.4 nozzle`
- `Bambu Lab X1 Carbon`
- `Bambu Lab X1 0.4 nozzle`
- `Bambu Lab P1S 0.4 nozzle`

Filaments:
- `Bambu PLA Matte @BBL X1C`
- `Bambu PLA Basic @BBL X1C`
- `Bambu ABS @BBL X1C`
- `Bambu PETG Basic @BBL X1C`

Processes:
- `0.20mm Standard @BBL X1C`
- `0.15mm Fine @BBL X1C`
- `0.28mm Draft @BBL X1C`
- `0.10mm Extra Fine @BBL X1C`

## Listing available profiles

```bash
# List all printers
cd build && ./bin/orcaslicer-cli list-profiles --type printer

# List all filaments
cd build && ./bin/orcaslicer-cli list-profiles --type filament

# List all processes
cd build && ./bin/orcaslicer-cli list-profiles --type process
```

## Quick overrides (CLI)

```bash
# Adjust infill density and layer height
./bin/orcaslicer-cli slice \
  --input ../../example_files/3DBenchy.stl \
  --output ../../output_files/test_slice.gcode \
  --set "infill_density=30,layer_height=0.24"
```

## Output files

- test_simple.sh: `../output_files/test_slice.gcode`
- test_slice.sh: `../output_files/test_with_profiles.gcode`

## Full test example

```bash
# 1) Test with default profiles (recommended)
./test_simple.sh

# 2) Test with detailed comparison
./test_slice.sh

# 3) Test with a different profile set
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Basic @BBL X1C" "0.15mm Fine @BBL X1C"
```
