# Parameter Overrides (CLI, Addon and HTTP API)

This document describes how to send slicing parameter overrides and which keys are accepted across all interfaces:
- CLI: orcaslicer-cli
- Node Addon (N-API): require('@orcaslicer/cli')
- HTTP API (node-api)

Important summary:
- We accept ALL configuration keys recognized by OrcaSlicer (libslic3r) in the version embedded in this repository.
- The keys are the same as those that appear in the INI exported by the OrcaSlicer GUI and in PrintConfig.cpp (DynamicPrintConfig).
- We also offer compatibility aliases for common keys from other slicers (table below).
- Precedence: overrides (CLI --set / addon options / API options) > explicit profiles (printer/filament/process) > settings embedded in the 3MF > defaults.
- Errors: unknown key or invalid value results in CLI failure (exit code != 0), rejection in the Addon (throw), and HTTP 400 in the API.


## Usage formats

- CLI
  ```bash
  ./orcaslicer-cli slice \
    --input path/to/model.stl \
    --output out.gcode \
    --printer "Bambu Lab X1 Carbon 0.4 nozzle" \
    --set "sparse_infill_density=30,layer_height=0.24,skirt_loops=1,infill_direction=45"
  ```
  Notes:
  - Pass multiple `k=v` pairs separated by commas within a single `--set` option.
  - Quote values when they contain spaces. Example: `--set "curr_bed_type=High Temp Plate"`.

- Node Addon (N-API)
  ```js
  const { slice } = require('OrcaSlicerCli/bindings/node')
  await slice({
    input: 'path/to/model.stl',
    output: 'out.gcode',
    printerProfile: 'Bambu Lab X1 Carbon 0.4 nozzle',
    filamentProfile: 'Bambu PLA Basic @BBL X1C',
    processProfile: '0.20mm Standard @BBL X1C',
    options: {
      sparse_infill_density: 30,
      layer_height: 0.24,
      skirt_loops: 1,
      infill_direction: 45
    }
  })
  ```

- HTTP API (node-api)
  ```json
  {
    "filePath": "path/to/model.stl",
    "printerProfile": "Bambu Lab X1 Carbon 0.4 nozzle",
    "filamentProfile": "Bambu PLA Basic @BBL X1C",
    "processProfile": "0.20mm Standard @BBL X1C",
    "options": {
      "sparse_infill_density": 30,
      "layer_height": 0.24,
      "skirt_loops": 1,
      "infill_direction": 45
    }
  }
  ```


## Value types

- Boolean: `true`/`false` (or `1`/`0`).
- Percentage (coPercent): use a number with `%` (e.g., `30%`). Numbers without `%` may be accepted as a shortcut in some fields; however, prefer `%` when the option is percentage-based in Orca.
- Number (coInt / coFloat): integers or decimals (dot as separator). Example: `0.24`.
- Enumerations (coEnum): accepted values are the labels/serializations used by Orca. Example: `sparse_infill_pattern: "grid"`.
- Text (coString / coStrings): use strings. For multiple values, follow the encoding expected by Orca (for example, comma-separated lists when applicable).

Tip: check the CONFIG_BLOCK inside the generated G-code; it shows the effective configuration and helps confirm the applied key/value.


## Accepted keys (scope)

We accept all configuration keys exposed by OrcaSlicer’s `DynamicPrintConfig` (libslic3r). Representative examples:

- Heights and layers: `layer_height`, `top_shell_layers`, `bottom_shell_layers`
- Walls: `wall_loops`, `wall_sequence`, `outer_wall_line_width`, `inner_wall_line_width`
- Infill (sparse and solid): `sparse_infill_density`, `sparse_infill_pattern`, `infill_direction`, `solid_infill_direction`, `internal_solid_infill_pattern`, `sparse_infill_line_width`, `internal_solid_infill_line_width`
- First layer: `initial_layer_height`, `initial_layer_line_width`, `initial_layer_print_speed`
- Temperature: `nozzle_temperature`, `bed_temperature`, `first_layer_temperature`, `first_layer_bed_temperature`
- Cooling: `fan_speedup_time`, `overhang_fan_speed`, `reduce_fan_stop_start_freq`
- Materials/extruders: `wall_filament`, `sparse_infill_filament`, `solid_infill_filament`
- Supports: `support_material`, `support_pattern`, `support_overhang_angle`
- Speeds and accelerations: `default_speed`, `sparse_infill_speed`, `internal_solid_infill_speed`, `acceleration`, `jerk`
- Misc: `brim_width`, `skirt_loops`, `seam_position`, `ironing_angle`, `ironing_pattern`

Note: the full list is extensive (500+ options) and depends on the OrcaSlicer version included here. Refer to the official sources below for the exact list for your version.


## Official sources / How to discover all keys

1) Export an INI from the OrcaSlicer GUI (File → Export Config). The keys shown there are accepted as overrides.
2) Check the OrcaSlicer code in this repository, file:
   - `OrcaSlicer/src/libslic3r/PrintConfig.cpp` (search for `this->add("…", …)`).
3) Inspect the CONFIG_BLOCK of the generated G-code — useful to validate the application of the override.

These sources ensure synchronization with the embedded version, avoiding documentation drift.


## Compatibility aliases (mappings)

We accept the aliases below and map them to equivalent Orca keys:

- `perimeters` → `wall_loops`
- `top_solid_layers` → `top_shell_layers`
- `bottom_solid_layers` → `bottom_shell_layers`
- `skirts` → `skirt_loops`
- `infill_pattern` → `sparse_infill_pattern`
- `external_perimeters_first` → `wall_sequence` (mapped to: `outer wall/inner wall` or `inner wall/outer wall`)
- `fill_angle` → `infill_direction`
- `fan_always_on` → `reduce_fan_stop_start_freq`

If an alias is not in the table, use the native OrcaSlicer key (as per INI/PrintConfig.cpp).


## Validation rules and error messages

- Unknown keys or keys incompatible with the current preset/configuration will result in an error:
  - CLI: returns exit code != 0 with a detailed message
  - Addon: Promise rejected with a message containing the problematic key/value
  - HTTP API: 400 Bad Request with message “Invalid override option(s): …”
- Out-of-range values or invalid enums also fail.
- When applicable, some values will be normalized internally by Orca (e.g., limits, conversions), reflected in the CONFIG_BLOCK.


## Additional examples

- Percentages and enums:
  ```bash
  --set "sparse_infill_density=15%,sparse_infill_pattern=grid"
  ```
- Wall tweaks and overlap:
  ```bash
  --set "wall_loops=3,infill_wall_overlap=15%"
  ```
- Cooling targeted at overhangs/bridges:
  ```bash
  --set "overhang_fan_speed=80"
  ```


## Questions and contributions

If you find a key that is valid in the INI/PrintConfig.cpp but does not work as an override, open an issue with:
- repository version/commit,
- a minimal example (CLI `--set` or API JSON body),
- a snippet of the CONFIG_BLOCK from the generated G-code.

This helps maintain full parity with the OrcaSlicer GUI.

