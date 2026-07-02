# C++ Engine (CPP-ENGINE)

Documentation of the native C++ addon internals: the layers, contracts, and
memory mechanisms.

## Overview of the C++ layers

```
┌─────────────────────────────────────────────────────────────┐
│  orcaslicer_node.node  (addon.cc — N-API binding)           │
│  Thin wrapper: no slicing logic.                             │
│  - dlopen liborcacli_engine at runtime                       │
│  - Serializes via g_mutex                                    │
│  - Converts JS ↔ C structs                                   │
└───────────────────────────┬─────────────────────────────────┘
                            │ function pointers (dlsym)
┌───────────────────────────▼─────────────────────────────────┐
│  liborcacli_engine  (EngineAPI.cpp/.hpp — FFI extern "C")   │
│  - Opaque types: orcacli_handle = void*                      │
│  - C structs: orcacli_slice_params, orcacli_operation_result │
│  - C ↔ C++ conversion (AddonCore)                           │
│  - String allocation/deallocation (dup_cstr / free_*)        │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  AddonCore  (AddonCore.cpp/.hpp — namespace OrcaSlicerCli)  │
│  - PIMPL: public class + private AddonCore::Impl             │
│  - Orchestrates subsystems:                                  │
│    ├── Initialization  (libslic3r init, resources)           │
│    ├── ModelIO         (STL/3MF/OBJ loading)                 │
│    ├── ConfigManager   (config merge + application)          │
│    ├── SliceEngine     (runs slicing via libslic3r)          │
│    ├── PlateCentering  (bed centering/realign)               │
│    └── Utilities       (misc helpers)                        │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────▼─────────────────────────────────┐
│  libslic3r  (OrcaSlicer/src/libslic3r)                      │
│  Print, PrintConfig, GCode, Model, etc.                      │
└─────────────────────────────────────────────────────────────┘
```

## addon.cc — N-API binding

**File**: `OrcaSlicerAddon/bindings/node/src/addon.cc`

The entry point of the Node addon. It is deliberately "thin": it contains no
slicing logic, only marshalling between JavaScript and the C FFI.

### Registered functions (exported to JS)

Registered in `Init()` at line 791:

| JS function | Signature | Description |
|-------------|-----------|-------------|
| `initialize(opts?)` | `{resourcesPath?, verbose?, strict?}` → `void` | Loads the engine (dlopen) and initializes AddonCore |
| `slice(params)` | `SliceParams` → `Promise<SliceResult>` | Asynchronous slicing |
| `getModelInfo(file)` | `string` → `Promise<ModelInfo>` | Model info (async) |
| `version()` | `void` → `string` | Engine version |
| `loadVendor(vendorId)` | `string` → `void` | Loads a vendor's presets |
| `setLoggingSilenced(silent)` | `boolean` → `void` | Silences stdout/stderr |
| `shutdown()` | `void` → `void` | Destroys the engine instance |

### Global state

```cpp
static FFI g_ffi;               // lib handle + function pointers + instance
static std::mutex g_mutex;      // serializes heavy operations
static std::string g_current_resources;  // current resourcesPath
```

### ensure_engine_loaded() — line 188

Finds and loads `liborcacli_engine`:

1. Tries multiple candidate paths (env override, relative build dirs)
2. Resolves symbols via `dlsym`/`GetProcAddress`
3. Requires `orcacli_create` and `orcacli_destroy` (fails if missing)
4. Other symbols are optional (logged if missing)
5. **Auto-initializes** the engine by calling `orcacli_initialize(nullptr)` after creating

> This means `slice()` works **without** calling `initialize()` first.

### Async operations pattern

`slice()` and `getModelInfo()` use `napi_async_work`:

```
Slice() [main thread]
  ├─ Extract parameters from the JS object → SliceWork
  ├─ Create Promise + async_work
  └─ Enqueue work (returns the Promise immediately)

SliceExecute() [pool thread]
  ├─ lock(g_mutex)
  ├─ ensure_engine_loaded()
  ├─ Build orcacli_slice_params (kvs)
  ├─ g_ffi.slice(inst, &params)
  └─ unlock(g_mutex)

SliceComplete() [main thread]
  ├─ Resolve/reject the Promise
  └─ Delete async_work
```

### Collecting options/custom/profile

`Slice()` collects key-value pairs from three fields of the params object:

| JS field | FFI destination | Application in AddonCore |
|----------|-----------------|--------------------------|
| `profile` | `orcacli_slice_params.profile` | `profile_settings` (before the 3MF) |
| `options` | `orcacli_slice_params.overrides` | `custom_settings` (after the 3MF, max priority) |
| `custom` | `orcacli_slice_params.overrides` | same as options (alias) |
| `customSettings` | `orcacli_slice_params.overrides` | same (weslicer alias) |

Values are coerced to strings:
- `string` → direct
- `boolean` → `"1"` / `"0"`
- `number` → `std::to_string`
- `array` → elements joined with `;` (OrcaSlicer convention for multi-extruder)

## EngineAPI.hpp/.cpp — C FFI

**File**: `OrcaSlicerAddon/src/engine/EngineAPI.hpp` (header)
**File**: `OrcaSlicerAddon/src/engine/EngineAPI.cpp` (implementation)

An `extern "C"` contract that lets the `.node` call the C++ without a compile-time
dependency (C++ headers are not needed in the binding).

### Exported types

```c
typedef void* orcacli_handle;  // opaque instance handle

typedef struct {
    bool success;
    const char* message;         // owned by lib, free with orcacli_free_result
    const char* error_details;   // owned by lib
    double estimated_time_sec;   // <0 if unavailable
    double filament_used_grams;  // <0 if unavailable
} orcacli_operation_result;

typedef struct {
    const char* filename;       // owned by lib
    uint32_t object_count;
    uint32_t triangle_count;
    double volume;
    const char* bounding_box;   // owned by lib
    bool is_valid;
} orcacli_model_info;

typedef struct { const char* key; const char* value; } orcacli_kv;

typedef struct {
    const char* input_file;
    const char* output_file;
    const char* config_file;
    const char* preset_name;
    int32_t plate_index;        // 1-based
    bool verbose, dry_run, center_on_bed, auto_realign_if_needed;
    const orcacli_kv* profile;  int32_t profile_count;   // before the 3MF
    const orcacli_kv* overrides; int32_t overrides_count; // after the 3MF
} orcacli_slice_params;
```

### Exported functions

| Function | Description |
|----------|-------------|
| `orcacli_create()` | Creates an instance (`new Engine`) |
| `orcacli_destroy(h)` | Destroys the instance |
| `orcacli_initialize(h, resources_path)` | Initializes AddonCore |
| `orcacli_load_model(h, filename)` | Loads a model |
| `orcacli_get_model_info(h)` | Returns info about the loaded model |
| `orcacli_slice(h, params)` | Runs slicing |
| `orcacli_load_vendor(h, vendor_id)` | Loads a vendor's presets |
| `orcacli_version()` | Static version string |
| `orcacli_set_logging_silenced(silent)` | Controls logging |
| `orcacli_free_string(s)` | Frees an allocated string |
| `orcacli_free_model_info(mi)` | Frees an info struct |
| `orcacli_free_result(r)` | Frees a result struct |

### Memory management

Convention: **whoever allocates, frees**.

- `dup_cstr(s)` — allocates with `malloc`, returns a `char*` owned by the lib
- Strings in result structs (`message`, `error_details`, `filename`,
  `bounding_box`) are allocated by the engine and must be freed by the caller
  via `orcacli_free_*`.
- Static strings (e.g. `orcacli_version()`) do not need freeing.
- The `.node` calls `orcacli_free_result()` after consuming the results.

> **Note**: in `addon.cc`, there are comments indicating that `free_result` for
> the result of `initialize` was temporarily disabled to avoid a suspected
> double-free in the engine. See lines 379-388.

## AddonCore — C++ facade

**File**: `OrcaSlicerAddon/src/core/AddonCore.hpp` (header)
**File**: `OrcaSlicerAddon/src/core/AddonCore.cpp` (implementation, PIMPL)

The main class that orchestrates all subsystems on top of libslic3r.

### Public structures

```cpp
struct OperationResult {
    bool success = false;
    std::string message;
    std::string error_details;
    double estimated_time_sec = -1.0;    // <0 if unavailable
    double filament_used_grams = -1.0;   // <0 if unavailable
};

struct SlicingParams {
    std::string input_file, output_file, config_file, preset_name;
    int plate_index = 1;                 // 1-based for multi-plate 3MF
    std::map<std::string,std::string> profile_settings;  // before the 3MF
    std::map<std::string,std::string> custom_settings;   // after the 3MF
    bool verbose=false, dry_run=false;
    bool center_on_bed=false, auto_realign_if_needed=false;
};

struct ModelInfo {
    std::string filename;
    size_t object_count, triangle_count;
    double volume;
    std::string bounding_box;
    bool is_valid;
    std::vector<std::string> warnings, errors;
};
```

### Main methods

| Method | Description |
|--------|-------------|
| `initialize(resources_path)` | Configures libslic3r (logging, resources) |
| `loadModel(filename)` | Loads STL/3MF/OBJ into the internal Model |
| `getModelInfo()` | Info about the loaded model |
| `slice(params)` | Full slicing pipeline |
| `loadConfig(config_file)` | Loads config from a file |
| `setConfigOption(key, value)` | Sets an individual option |
| `getConfigOption(key)` | Reads an individual option |
| `loadVendor(vendor_id)` | Loads a vendor's presets from resources |
| `setLoggingSilenced(silent)` | Suppresses/restores stdout/stderr |
| `getVersion()` | Version (static) |

### PIMPL

```cpp
class AddonCore {
public:
    // ... public methods ...
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;  // private implementation
};
```

`Impl` (in AddonCore.cpp) holds:
- Pointers to `Slic3r::Model`, `Slic3r::Print`, `Slic3r::DynamicPrintConfig`
- Initialization state
- Logger

### core/ subdirectories

| Subdir | Files | Responsibility |
|--------|-------|----------------|
| `core/init/` | `Initialization.cpp/.hpp` | libslic3r setup (resources, logging level) |
| `core/model/` | `ModelIO.cpp/.hpp` | STL/3MF/OBJ loading via libslic3r |
| `core/config/` | `ConfigManager.cpp/.hpp` | Config merge/application into PrintConfig |
| `core/slice/` | `SliceEngine.cpp/.hpp` | Slicing execution + G-code export |
| `core/plate/` | `PlateCentering.cpp/.hpp` | Bed centering and realign |
| `core/parts/` | — | Auxiliary components |
| `core/util/` | `Utilities.cpp/.hpp` | Misc helpers |

## utils/ — Logger and ErrorHandler

**Logger** (`utils/Logger.hpp/.cpp`):
- Singleton: `Logger::getInstance()`
- Macros: `LOG_DEBUG`, `LOG_INFO`, `LOG_ERROR`
- Level configurable via env (`ORCACLI_LOG_LEVEL`, `ORCACLI_QUIET`)

**ErrorHandler** (`utils/ErrorHandler.hpp/.cpp`):
- `ErrorCode` enum
- `handleError(code, message, details)`
- `safeExecute([&]() { ... })` — catches exceptions

## Configuration-priority behavior in C++

`AddonCore::slice()` applies configuration in this order (each layer overrides
the previous):

```
1. FullPrintConfig::defaults()     ← base fallback
2. profile_settings (params)       ← base profile, before the 3MF
3. embedded 3MF (if input is 3MF)  ← config that comes in the file
4. custom_settings (params)        ← explicit overrides, max priority
```

This ensures that:
- Profiles act as a base but are overridden by what is in the 3MF
- The user's `options` always win over everything (including the 3MF)
