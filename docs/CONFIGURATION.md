# Configuration

How slicing configuration flows through the stack and which keys are available.

## Fundamental principle: "on-the-fly" mode

The addon operates in **pure on-the-fly mode**: no vendor bundle or profile is
loaded at initialization. All configuration is passed on **every call** to
`slice()`. The fallback for unspecified keys is `FullPrintConfig::defaults()`
from libslic3r.

```
DON'T:  orca.loadVendor('BBL')  ← advanced/lazy-loading use only
DO:     orca.slice({ options: { printer_model: 'Bambu Lab A1', ... } })
```

## Configuration priority

From lowest to highest (highest wins):

```
┌─────────────────────────────────────────────────┐
│ 4. FullPrintConfig::defaults()                  │  ← base fallback
├─────────────────────────────────────────────────┤
│ 3. profile / config  (base profile)             │  ← applied BEFORE the 3MF
├─────────────────────────────────────────────────┤
│ 2. embedded 3MF  (config inside the file)       │  ← overrides profile
├─────────────────────────────────────────────────┤
│ 1. options / custom  (explicit overrides)       │  ← AFTER the 3MF, wins over all
└─────────────────────────────────────────────────┘
```

### Mapping across the different levels

| Level (HTTP) | JS field | N-API field (addon.cc) | FFI field | AddonCore |
|--------------|----------|------------------------|-----------|-----------|
| Max override | `options` | `overrides` | `orcacli_slice_params.overrides` | `custom_settings` |
| Max override | `custom` | `overrides` (alias) | same | same |
| Max override | `customSettings` | `overrides` (alias) | same | same |
| Base profile | `config` (STL) | `overrides`* | `overrides`* | `custom_settings`* |
| Base profile | `config` (3MF) | `profile` | `orcacli_slice_params.profile` | `profile_settings` |

> *In the **STL** service, `config` and `options` are merged into `finalOptions`
> and passed as `overrides` (both at maximum priority). There is no embedded 3MF
> in STL, so the profile/override distinction does not apply.
>
> In the **3MF** service, `config` becomes `profile` (before the 3MF) and
> `options` becomes `overrides` (after the 3MF), preserving the priority
> semantics.

## Common configuration keys

These are the most-used keys (they map directly to libslic3r's `PrintConfig`):

### Printer / bed

| Key | Type | Example | Description |
|-----|------|---------|-------------|
| `printer_model` | string | `"Bambu Lab A1"` | Printer model |
| `printer_technology` | string | `"FFF"` | Technology (FFF/SLA) |
| `nozzle_diameter` | number | `0.4` | Nozzle diameter (mm) |
| `printable_area` | string | `"0x0,256x0,256x256,0x256"` | Printable area (polygon) |
| `printable_height` | number | `256` | Maximum height (mm) |
| `curr_bed_type` | string | `"High Temp Plate"` | Bed type |
| `bed_exclude_area` | string | `""` | Excluded bed area |

### Quality / process

| Key | Type | Example | Description |
|-----|------|---------|-------------|
| `layer_height` | number | `0.2` | Layer height (mm) |
| `wall_loops` | number | `2` | Number of perimeters |
| `sparse_infill_density` | number | `15` | Infill density (%) |
| `sparse_infill_pattern` | string | `"grid"` | Infill pattern |
| `top_shell_layers` | number | `4` | Solid top layers |
| `bottom_shell_layers` | number | `3` | Solid bottom layers |
| `brim_type` | string | `"outer_and_inner"` | Brim type |
| `brim_width` | number | `6` | Brim width (mm) |

### Temperatures

| Key | Type | Example | Description |
|-----|------|---------|-------------|
| `nozzle_temperature` | number | `220` | Nozzle temp — PLA (°C) |
| `nozzle_temperature_range_low` | number | `190` | Minimum range (°C) |
| `nozzle_temperature_range_high` | number | `230` | Maximum range (°C) |
| `bed_temperature` | number | `60` | Bed temp (°C) |

### Filament

| Key | Type | Example | Description |
|-----|------|---------|-------------|
| `filament_type` | string | `"PLA"` | Filament type |
| `filament_diameter` | number | `1.75` | Diameter (mm) |
| `filament_density` | number | `1.24` | Density (g/cm³) |
| `filament_colour` | string/array | `"#FFFFFF"` | Filament colour(s) |

### Multi-extruder (arrays)

Multi-extruder keys accept **arrays** that the addon serializes with `;`
(OrcaSlicer convention):

```json
{
  "filament_colour": ["#FF0000", "#00FF00"],
  "nozzle_temperature": [220, 240]
}
```

Serializes to: `"#FF0000;#00FF00"` and `"220.000000;240.000000"`.

### G-code templates (BBL caveat)

| Key | Description |
|-----|-------------|
| `machine_start_gcode` | Start G-code |
| `machine_end_gcode` | End G-code |
| `change_filament_gcode` | Filament-change G-code |
| `filament_start_gcode` | Start G-code (filament) |
| `filament_end_gcode` | End G-code (filament) |
| `layer_change_gcode` | Layer-change G-code |
| `before_layer_change_gcode` | Before layer change |
| `time_lapse_gcode` | Time-lapse G-code |
| `printing_by_object_gcode` | Per-object G-code |

> **BBL caveat**: BBL profiles use proprietary variables
> (`flush_volumetric_speeds`, `flush_temperatures`) in these templates.
> `gcode-sanitizer.ts` replaces them with literals before calling the addon.
> See `docs/NODE-API.md`.

## Profiles

### Resources structure

```
OrcaSlicer/resources/profiles/<vendor>/
├── <vendor>.json              # vendor manifest
├── machine/
│   ├── <printer>.json
│   └── fdm_machine_common.json   # base profile (inherits)
├── filament/
│   └── <filament>.json
└── process/
    └── <process>.json
```

### Profile inheritance

Profiles can inherit from others via the `inherits` field:

```json
{
  "name": "Bambu Lab A1 0.4 nozzle",
  "inherits": "fdm_machine_common",
  "printer_model": "Bambu Lab A1",
  "nozzle_diameter": "0.4"
}
```

The `GET /profiles` service resolves the inheritance chain recursively
(with anti-circular protection).

### Base profiles (non-instantiable)

Profiles with `instantiation: "false"` or a name starting with `fdm_` are
ignored in the listing (they are bases for inheritance, not usable directly).

### Example profiles

Printers: `Bambu Lab X1 Carbon 0.4 nozzle`, `Bambu Lab A1 0.4 nozzle`,
`Bambu Lab P1S 0.4 nozzle`

Filaments: `Bambu PLA Matte @BBL X1C`, `Bambu PLA Basic @BBL X1C`,
`Bambu ABS @BBL X1C`, `Bambu PETG Basic @BBL X1C`

Processes: `0.20mm Standard @BBL X1C`, `0.15mm Fine @BBL X1C`,
`0.28mm Draft @BBL X1C`

## Key validation

The addon returns `usedOptions` (applied keys) and `ignoredOptions`
(unrecognized keys).

- In the **STL** service: if a key from `options` (not `config`) appears in
  `ignoredOptions` → **HTTP 400 BadRequest**.
- In the **3MF** service: same check for `options`.
- Ignored `config` keys are silent (they are the base profile and may contain
  metadata).

## Complete examples

### Minimal STL

```json
{
  "filePath": "/data/model.stl",
  "config": {
    "printer_model": "Bambu Lab A1",
    "nozzle_diameter": 0.4,
    "printable_area": "0x0,256x0,256x256,0x256",
    "printable_height": 256,
    "layer_height": 0.2,
    "wall_loops": 2,
    "sparse_infill_density": 15,
    "filament_type": "PLA",
    "nozzle_temperature": 220,
    "bed_temperature": 60
  }
}
```

### 3MF with overrides

```json
{
  "filePath": "/data/project.3mf",
  "plate": 1,
  "config": {
    "printer_model": "Bambu Lab X1 Carbon",
    "curr_bed_type": "High Temp Plate"
  },
  "options": {
    "layer_height": 0.16,
    "sparse_infill_density": 20
  }
}
```

### Via the addon (Node.js)

```javascript
const orca = require('orcaslicer-addon')

await orca.slice({
  input: 'model.stl',
  output: 'output.gcode',
  options: {
    printer_model: 'Bambu Lab A1',
    layer_height: 0.2,
    sparse_infill_density: 15,
    nozzle_temperature: 220,
    bed_temperature: 60
  }
})
```
