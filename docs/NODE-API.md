# Node API (HTTP)

Documentation of the Feathers.js + Koa HTTP service that exposes the addon via REST.

## Project structure

```
node-api/
├── src/
│   ├── app.ts              # Feathers/Koa app setup
│   ├── index.ts            # Entry point (listen)
│   ├── orca.ts             # Initializes the addon → app.set('orca')
│   ├── configuration.ts    # Config schema (TypeBox)
│   ├── declarations.ts     # Application types
│   ├── validators.ts       # AJV instances (dataValidator, queryValidator)
│   ├── logger.ts           # Winston logger
│   ├── hooks/
│   │   └── log-error.ts    # Global error-logging hook
│   └── services/
│       ├── index.ts                 # Registration of all services
│       ├── slicer/
│       │   ├── stl/                 # POST /slicer/stl
│       │   ├── 3mf/                 # POST /slicer/3mf
│       │   │   └── gcode-sanitizer.ts
│       │   └── model-info/          # POST /slicer/model-info
│       ├── profiles/                # GET  /profiles
│       ├── profile-converter/       # POST /profile-converter
│       └── medias/                  # GET  /medias (static)
├── config/
│   ├── default.json                 # host, port, public
│   ├── custom-environment-variables.json
│   └── test.json
└── package.json
```

## App setup (`app.ts`)

The Feathers/Koa app is configured in this order:

1. **configuration** — loads `config/*.json` validated by TypeBox
2. **cors()** — CORS enabled
3. **koaBody** — body parser with multipart support (`multipart: true`)
4. **files middleware** — maps `ctx.request.files` to `ctx.feathers.files`
5. **medias middleware** — intercepts GET `/medias/*` to stream files
6. **serveStatic** — serves `public/` if it exists
7. **errorHandler()** — Koa error handler
8. **parseAuthentication()** — auth parser
9. **loadOrca(app)** — loads and initializes the addon → `app.set('orca', orca)`
10. **rest()** — REST transport
11. **services** — registers all services
12. **hooks** — global `logError` hook (around)

## Addon initialization (`orca.ts`)

```typescript
const addonDir = process.env.ORCACLI_ADDON_DIR
  || path.resolve(__dirname, '../../OrcaSlicerAddon/bindings/node')
const orca = require(addonDir)

orca.initialize({ resourcesPath, verbose: false, strict: true })
app.set('orca', orca)               // addon accessible in the services
app.set('orca_resourcesPath', resourcesPath)  // for the profiles service
```

**Key points**:
- Forces `ORCACLI_PREFER_LOCAL=1` (local-first, avoids stale prebuilt)
- `strict: true` — no autoloading of vendors/profiles
- Changes `cwd` to `/tmp` during `initialize()` (avoids directory locking)

## Service convention

Each Feathers service follows the 4-file pattern:

| File | Responsibility |
|------|----------------|
| `*.class.ts` | Service class (business logic) |
| `*.schema.ts` | TypeBox schemas (request/response) + AJV validators |
| `*.shared.ts` | Types/schemas exportable to the client |
| `*.ts` | Service registration in the app (configure function) |

The addon is accessed via `this.options.app.get('orca')`.

## Endpoints

### POST /slicer/stl — STL slicing

**File**: `services/slicer/stl/stl.class.ts`

Slices an STL (or OBJ) file and returns G-code as text.

**Request body (JSON):**
```json
{
  "filePath": "/path/to/model.stl",
  "output": "/path/to/output.gcode",
  "plate": 1,
  "options": { "layer_height": 0.2 },
  "config": { "printer_model": "Bambu Lab A1", "sparse_infill_density": 15 }
}
```

**Request (multipart):**
```bash
curl -X POST http://localhost:3030/slicer/stl \
  -F "file=@model.stl" \
  -F 'config={"layer_height":0.2}'
```

**Response (200):**
```json
{
  "id": "uuid",
  "filename": "model.stl",
  "outputPath": "/tmp/orca-<uuid>.gcode",
  "gcode": "... the entire G-code as a string ...",
  "estimatedTimeSec": 3600,
  "filamentUsedGrams": 25.5
}
```

**Logic**:
1. Resolve the input file (multipart upload OR `filePath`)
2. Set output: `data.output` OR `os.tmpdir()/orca-<uuid>.gcode`
3. Merge `options` + `config` → `finalOptions` (config overrides options)
4. Call `orca.slice({ input, output, options: finalOptions, center: true, autoRealignIfNeeded: true })`
5. If a key from `options` (not `config`) was ignored → **HTTP 400**
6. Read the G-code from disk and return it as a string

**Errors**:
- `400 BadRequest` — invalid/ignored option key
- `500` — slicing error

---

### POST /slicer/3mf — 3MF slicing

**File**: `services/slicer/3mf/3mf.class.ts`

Slices a 3MF project and returns a `.gcode.3mf` (ZIP with embedded G-code) as base64.

**Request body (JSON):**
```json
{
  "filePath": "/path/to/project.3mf",
  "plate": 1,
  "config": { "layer_height": 0.2, "sparse_infill_density": 20 },
  "options": { "curr_bed_type": "High Temp Plate" }
}
```

> **Security**: the `output` field was removed from the schema. The output is
> always generated in `os.tmpdir()` to prevent *arbitrary file write*.

**Response (200):**
```json
{
  "id": "uuid",
  "filename": "project.3mf",
  "outputPath": "/tmp/orca-<uuid>.gcode.3mf",
  "contentType": "model/3mf",
  "size": 1234567,
  "dataBase64": "...base64 of the .gcode.3mf...",
  "usedOptions": ["layer_height"],
  "ignoredOptions": [],
  "estimatedTimeSec": 7200,
  "filamentUsedGrams": 50.0
}
```

**Logic**:
1. Copy input to `os.tmpdir()` (safe path)
2. Validate that input is a valid ZIP/3MF (JSZip)
3. Build `profileSettings` from `config` + `curr_bed_type: 'High Temp Plate'`
4. **`sanitizeBblGcodeTemplates(profileSettings)`** — strips BBL-proprietary variables
5. Adjust `flush_volumes_matrix` if inconsistent with the filament count
6. Output always in `os.tmpdir()` (ignores `data.output`)
7. Call `orca.slice({ input, output, plate, profile: profileSettings, options })`
8. Validate output: ZIP with embedded G-code in `Metadata/*.gcode`
9. Encode the content as base64

**config → profile/options mapping**:
- `config` → `profile` (base profile, applied **before** the 3MF)
- `options` → `options` (overrides, applied **after** the 3MF)

**Special errors** (with `code`):
| code | Condition |
|------|-----------|
| `OBJECTS_OUT_OF_BOUNDS` | Objects outside the print area |
| `SLICING_ERROR` | libslic3r slicing error |
| `GCODE_EXPORT_FAILED` | G-code export failure |
| `GCODE_FILE_TOO_SMALL` | Generated file too small |

---

### POST /slicer/model-info — Model info

**File**: `services/slicer/model-info/model-info.class.ts`

Returns information about a 3D model without slicing.

**Request:**
```json
{ "filePath": "/path/to/model.stl" }
```

Or multipart: `-F "file=@model.stl"`

**Response:**
```json
{
  "id": "uuid",
  "filename": "model.stl",
  "objectCount": 1,
  "triangleCount": 2852,
  "volume": 13.47,
  "boundingBox": "0,0,0 - 48,25.5,24",
  "isValid": true
}
```

**Logic**: copies to temp, calls `orca.getModelInfo(path)`, cleans up temp.

---

### GET /profiles — List and resolve profiles

**File**: `services/profiles/profiles.class.ts`

Lists and resolves profiles (printer/filament/process) from the OrcaSlicer
resources directory.

**List profiles:**
```
GET /profiles?type=machine&vendor=BBL
```

**Response:**
```json
[
  { "name": "Bambu Lab A1 0.4 nozzle", "type": "machine", "vendor": "BBL", "inherits": "...", "config": {} }
]
```

**Get a resolved profile (with inheritance):**
```
GET /profiles/Bambu%20Lab%20A1%200.4%20nozzle?type=machine&vendor=BBL
```

**Response:**
```json
{
  "name": "Bambu Lab A1 0.4 nozzle",
  "type": "machine",
  "vendor": "BBL",
  "inherits": "fdm_machine_common",
  "config": { "printer_model": "Bambu Lab A1", "printable_area": "...", ... }
}
```

**Logic**:
- Builds an index by scanning `resources/profiles/<vendor>/{machine,filament,process}/*.json`
- Resolves the inheritance chain (`inherits`) recursively (with anti-circular protection)
- Ignores base profiles (`instantiation: "false"` or a name starting with `fdm_`)
- In-memory cache (invalidated if resourcesPath changes)

---

### POST /profile-converter — Convert profiles

**File**: `services/profile-converter/profile-converter.class.ts`

Converts profiles exported from OrcaSlicer (JSON or ZIP) into an `options` map
usable by the slicing endpoints.

**Request:**
```json
{
  "type": "printer",
  "data": "/path/to/profile.json"
}
```

Accepts: a file path, a JSON object, a JSON string, a base64 ZIP, or a multipart upload.

**Response:**
```json
{
  "options": {
    "printer_model": "Bambu Lab A1",
    "nozzle_diameter": 0.4,
    "...": "..."
  }
}
```

**Logic**:
- Detects the format (JSON, ZIP/.orca_*, base64, path)
- Extracts JSON from inside the ZIP if needed (JSZip)
- "Flattens" the preset: 1-element arrays → scalar, metadata removed
- Coerces values (string → number/boolean where appropriate)

---

### GET /medias — Static files

**File**: `services/medias/medias.class.ts`

Service to serve generated files (e.g. G-code) for download.

The middleware in `app.ts` intercepts GETs on `/medias/*` and streams the file
with `Content-Disposition: attachment`.

## gcode-sanitizer.ts

**File**: `services/slicer/3mf/gcode-sanitizer.ts`

Sanitizes BBL-proprietary G-code templates **before** passing them to the addon.

**Problem**: BBL profiles contain variables such as `flush_volumetric_speeds`
and `flush_temperatures` that do not exist in the OrcaSlicer fork used here. The
libslic3r `PlaceholderParser` throws a "Not a variable name" error.

**Solution**: replaces those variables with safe literals in the templates:

```javascript
sanitizeBblGcodeTemplates(profileSettings)
// Replaces in: machine_start_gcode, machine_end_gcode,
//   change_filament_gcode, filament_start_gcode, filament_end_gcode,
//   layer_change_gcode, time_lapse_gcode, printing_by_object_gcode,
//   before_layer_change_gcode
```

It also wraps accesses to `previous_extruder` (which starts at -1) with a guard
`previous_extruder >= 0` to avoid negative indices into vectors.

## Hooks

### log-error (`hooks/log-error.ts`)

A global `around` hook that logs errors from all services via Winston.

## Validation

AJV validators in `validators.ts`:
- `dataValidator` — validates request bodies (`coerceTypes: true`)
- `queryValidator` — validates query params

TypeBox schemas per service in `*.schema.ts`.
