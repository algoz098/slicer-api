# Slicer API

HTTP API for headless 3D model slicing using OrcaSlicer.

## Getting Started

```bash
cd node-api
npm ci
npm run compile
npm start
```

The server starts on `http://localhost:3030`.

### Environment Variables

| Variable | Description |
|----------|-------------|
| `ORCACLI_ADDON_DIR` | Path to addon bindings (default: auto-detect) |
| `ORCACLI_RESOURCES` | Path to OrcaSlicer resources |
| `PORT` | Server port (default: 3030) |

## API Endpoints

### POST /slicer/stl

Slice STL files and generate G-code.

**Request (JSON):**
```json
{
  "filePath": "/path/to/model.stl",
  "output": "/path/to/output.gcode",
  "config": {
    "printer_model": "Bambu Lab A1",
    "nozzle_diameter": 0.4,
    "layer_height": 0.2,
    "wall_loops": 2,
    "sparse_infill_density": 15,
    "filament_type": "PLA",
    "nozzle_temperature": 220,
    "bed_temperature": 60
  },
  "options": {
    "curr_bed_type": "High Temp Plate"
  }
}
```

**Request (Multipart):**
```bash
curl -X POST http://localhost:3030/slicer/stl \
  -F "file=@model.stl" \
  -F 'config={"layer_height":0.2}'
```

**Response:**
```json
{
  "id": "uuid",
  "outputPath": "/path/to/output.gcode",
  "gcode": "...",
  "usedOptions": ["layer_height", "wall_loops"],
  "ignoredOptions": [],
  "estimatedTimeSec": 3600,
  "filamentUsedGrams": 25.5
}
```

### POST /slicer/3mf

Slice 3MF files and generate production 3MF with embedded G-code.

**Request:**
```json
{
  "filePath": "/path/to/project.3mf",
  "output": "/path/to/output.gcode.3mf",
  "plate": 1,
  "center": true,
  "enableSupport": false,
  "config": {
    "layer_height": 0.2,
    "sparse_infill_density": 20
  }
}
```

**Response:**
```json
{
  "id": "uuid",
  "outputPath": "/path/to/output.gcode.3mf",
  "contentType": "application/vnd.ms-package.3dmanufacturing-3dmodel+xml",
  "size": 1234567,
  "dataBase64": "...",
  "usedOptions": ["layer_height"],
  "ignoredOptions": [],
  "estimatedTimeSec": 7200,
  "filamentUsedGrams": 50.0
}
```

## Request Fields

| Field | Type | Description |
|-------|------|-------------|
| `filePath` | string | Path to input file |
| `output` | string? | Output path (auto-generated if omitted) |
| `plate` | number? | 1-based plate index for multi-plate 3MF |
| `printerProfile` | string? | Printer profile name |
| `filamentProfile` | string? | Filament profile name |
| `processProfile` | string? | Process profile name |
| `config` | object? | Full configuration as JSON (highest priority) |
| `options` | object? | Individual parameter overrides |
| `center` | boolean? | Center object(s) on bed |
| `enableSupport` | boolean? | Enable support generation |

## Configuration Priority

`options` > `config` > `profiles` > `3MF embedded` > `defaults`

## Docker

```bash
docker build -t slicer-api -f Dockerfile .
docker run -p 3030:3030 slicer-api
```

## Testing

```bash
npm test
```
