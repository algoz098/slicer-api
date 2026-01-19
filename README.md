# OrcaSlicer Addon

Headless slicing powered by OrcaSlicer, available as:
- **Node.js native addon** (N-API) for programmatic slicing
- **HTTP API** (node-api) for REST-based automation

## Repository Structure

```
.
+-- OrcaSlicer/              # Upstream OrcaSlicer source (submodule)
+-- OrcaSlicerAddon/         # Node.js addon implementation
|   +-- bindings/node/       # N-API bindings (npm: orcaslicer-addon)
|   +-- src/                 # C++ core engine
+-- node-api/                # HTTP API service (Feathers.js)
+-- example_files/           # Sample STL/3MF files
+-- output_files/            # Generated outputs
+-- docs/                    # Additional documentation
```

## Quick Start: Node.js Addon

### Installation

```bash
npm install orcaslicer-addon
```

### Basic Usage

```javascript
const orca = require('orcaslicer-addon')

orca.initialize({
  resourcesPath: '/path/to/OrcaSlicer/resources',
  verbose: false
})

const result = await orca.slice({
  input: 'model.stl',
  output: 'output.gcode',
  options: {
    printer_model: 'Bambu Lab A1',
    nozzle_diameter: 0.4,
    printable_area: '0x0,256x0,256x256,0x256',
    printable_height: 256,
    layer_height: 0.2,
    wall_loops: 2,
    sparse_infill_density: 15,
    filament_type: 'PLA',
    nozzle_temperature: 220,
    bed_temperature: 60
  }
})

console.log('Output:', result.output)
console.log('Estimated time:', result.estimatedTimeSec, 'seconds')
console.log('Filament used:', result.filamentUsedGrams, 'grams')
```

### SliceParams

| Field | Type | Description |
|-------|------|-------------|
| `input` | string | Input file path (.stl, .3mf, .obj) |
| `output` | string? | Output path (auto-generated if omitted) |
| `plate` | number? | 1-based plate index for multi-plate 3MF |
| `printerProfile` | string? | Printer profile name |
| `filamentProfile` | string? | Filament profile name |
| `processProfile` | string? | Process profile name |
| `options` | object? | Configuration overrides (key-value pairs) |
| `center` | boolean? | Center object(s) on bed (default: false) |
| `autoRealignIfNeeded` | boolean? | Auto-realign if out of bounds |
| `verbose` | boolean? | Enable debug output |
| `dryRun` | boolean? | Validate without generating output |

### Slice Result

| Field | Type | Description |
|-------|------|-------------|
| `output` | string | Path to generated file |
| `usedOptions` | string[]? | Config keys that were applied |
| `ignoredOptions` | string[]? | Config keys that were ignored |
| `estimatedTimeSec` | number? | Estimated print time in seconds |
| `filamentUsedGrams` | number? | Estimated filament usage in grams |

## Quick Start: HTTP API

### Running the Server

```bash
cd node-api
npm ci
npm run compile
npm start
```

### POST /slicer/stl

```bash
curl -X POST http://localhost:3030/slicer/stl \
  -H "Content-Type: application/json" \
  -d '{
    "filePath": "/path/to/model.stl",
    "output": "/path/to/output.gcode",
    "config": {
      "printer_model": "Bambu Lab A1",
      "layer_height": 0.2,
      "sparse_infill_density": 15
    }
  }'
```

### POST /slicer/3mf

```bash
curl -X POST http://localhost:3030/slicer/3mf \
  -H "Content-Type: application/json" \
  -d '{
    "filePath": "/path/to/project.3mf",
    "output": "/path/to/output.gcode.3mf",
    "plate": 1,
    "center": true,
    "config": {
      "layer_height": 0.2,
      "sparse_infill_density": 20
    }
  }'
```

## Docker

```bash
docker build --target slicer-api -t slicer-api:latest .
docker run -p 3030:3030 slicer-api:latest
```

## Configuration

Priority order: `options` > `config` > `profiles` > `3MF embedded` > `defaults`

See [docs/OVERRIDES.md](docs/OVERRIDES.md) for the complete list of configuration keys.

### Common Keys

| Key | Type | Description |
|-----|------|-------------|
| `printer_model` | string | Printer model name |
| `nozzle_diameter` | number | Nozzle diameter in mm |
| `layer_height` | number | Layer height in mm |
| `wall_loops` | number | Number of perimeters |
| `sparse_infill_density` | number | Infill percentage (0-100) |
| `nozzle_temperature` | number | Nozzle temp in Celsius |
| `bed_temperature` | number | Bed temp in Celsius |
| `curr_bed_type` | string | Bed plate type |

## Output Formats

- `.gcode` - Plain G-code file
- `.gcode.3mf` or `.3mf` - Production 3MF with embedded G-code

## License

This project uses OrcaSlicer source code. See OrcaSlicer's original licenses.

