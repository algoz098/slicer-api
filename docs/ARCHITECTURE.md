# Architecture

Overview of the layers, data flow, and design decisions of slicer-api.

## Layer diagram

```
┌─────────────────────────────────────────────────────────────┐
│                      HTTP Client                            │
└──────────────────────────┬──────────────────────────────────┘
                           │ POST /slicer/stl, /slicer/3mf ...
┌──────────────────────────▼──────────────────────────────────┐
│  node-api  (TypeScript, Feathers.js 5 + Koa)                │
│  ┌─────────────┐  ┌─────────────┐  ┌────────────────────┐   │
│  │ app.ts      │  │ orca.ts     │  │ services/          │   │
│  │ (Koa setup) │  │ (init addon)│  │  slicer/stl        │   │
│  │             │  │ app.set(    │  │  slicer/3mf        │   │
│  │             │  │  'orca',x)  │  │  slicer/model-info │   │
│  │             │  │             │  │  profiles          │   │
│  │             │  │             │  │  profile-converter │   │
│  │             │  │             │  │  medias            │   │
│  └─────────────┘  └─────────────┘  └─────────┬──────────┘   │
└──────────────────────────────────────────────┼──────────────┘
                                               │ require(addonDir)
┌──────────────────────────────────────────────▼──────────────┐
│  bindings/node/index.js  (CommonJS loader)                   │
│  Resolves the .node (prebuilt vs local build) and exports it │
│  Attaches KlipperClient / SliceAndSend                       │
└──────────────────────────────────────────────┬──────────────┘
                                               │ dlopen at runtime
┌──────────────────────────────────────────────▼──────────────┐
│  orcaslicer_node.node  (N-API, addon.cc)                     │
│  - Initializes via ensure_engine_loaded()                    │
│  - Global mutex serializes heavy operations                  │
│  - slice()/getModelInfo() run in napi_async_work             │
└──────────────────────────────────────────────┬──────────────┘
                                               │ dlopen liborcacli_engine
┌──────────────────────────────────────────────▼──────────────┐
│  liborcacli_engine.{dylib,so}  (EngineAPI.cpp, extern "C")   │
│  - C ↔ C++ FFI bridge                                        │
│  - Type conversion (orcacli_* ↔ AddonCore::*)               │
│  - Memory management (dup_cstr, free_*)                       │
└──────────────────────────────────────────────┬──────────────┘
                                               │
┌──────────────────────────────────────────────▼──────────────┐
│  AddonCore (C++, namespace OrcaSlicerCli)                    │
│  - PIMPL: AddonCore + AddonCore::Impl                        │
│  - Orchestrates: ModelIO, ConfigManager, SliceEngine,        │
│    PlateCentering, Initialization                            │
│  - Uses FullPrintConfig::defaults() as fallback              │
└──────────────────────────────────────────────┬──────────────┘
                                               │
┌──────────────────────────────────────────────▼──────────────┐
│  libslic3r  (OrcaSlicer/src/libslic3r submodule)            │
│  The actual slicing engine                                   │
└──────────────────────────────────────────────────────────────┘
```

## Why dlopen at runtime?

The `.node` (N-API) does **not** link directly against `orcacli_core`. Instead,
it `dlopen`s `liborcacli_engine` on first use. Reason:

- `libslic3r` (static) has heavy **static initializers** that run when the
  library is loaded. If the `.node` linked directly, those initializers would
  run at `require()` time — possibly before Node is ready, causing crashes.
- With `dlopen`, loading is **deferred** until the first real call
  (`initialize()`, `slice()`, etc.), inside a controlled context.

The `ensure_engine_loaded()` function in `addon.cc:188` searches for the engine
across several candidate paths (relative to the module directory, build dirs, etc.).

## Why a global mutex?

`g_mutex` in `addon.cc` serializes **all** operations that touch the engine:

- `libslic3r` is **not thread-safe** for concurrent slicing operations
  (it uses globals and shared state internally).
- Even with slicing running in `napi_async_work` (Node's thread pool), only
  **one slice at a time** executes at the engine level.

```cpp
// addon.cc — pattern used in all heavy functions
std::lock_guard<std::mutex> lk(g_mutex);
```

## Data flow: STL slice

```
1. POST /slicer/stl
   Body: { filePath, config: {layer_height: 0.2, ...}, options: {...} }

2. SlicerStlService.create()                          [stl.class.ts]
   ├─ Resolve file (multipart upload OR filePath)
   ├─ Merge options + config → finalOptions
   │     (config overrides options)
   ├─ Set output in os.tmpdir() or data.output
   └─ Call the addon:

3. orca.slice({                                       [via app.get('orca')]
     input, output, options: finalOptions,
     center: true, autoRealignIfNeeded: true
   })

4. index.js → orcaslicer_node.node                    [N-API binding]
   addon.cc: Slice() enqueues napi_async_work

5. SliceExecute() [work thread]                       [addon.cc]
   ├─ lock(g_mutex)
   ├─ ensure_engine_loaded()
   ├─ Build orcacli_slice_params (override kvs)
   └─ g_ffi.slice(engine_inst, &params)               [FFI → EngineAPI]

6. orcacli_slice() → AddonCore::slice()               [EngineAPI.cpp]
   ├─ loadModel (if not yet loaded)
   ├─ Apply profile_settings (before the 3MF)
   ├─ Apply custom_settings/overrides (after the 3MF)
   ├─ center_on_bed / auto_realign if requested
   ├─ Run slicing via libslic3r (Print, GCode export)
   └─ Return {success, message, estimated_time, filament_grams}

7. SliceComplete() [main thread]                      [addon.cc]
   ├─ Resolve Promise with {output, usedOptions, ignoredOptions, stats}
   └─ Delete async_work

8. SlicerStlService.create() continues                [stl.class.ts]
   ├─ Check ignoredOptions (throw 400 if a valid option was ignored)
   ├─ Read the G-code from disk
   └─ Return {id, filename, outputPath, gcode, estimatedTimeSec, ...}
```

## Data flow: 3MF slice

Similar to STL, but with important differences:

```
1. POST /slicer/3mf
   Body: { filePath, plate: 1, config: {...}, options: {...} }

2. Slicer3MfService.create()                          [3mf.class.ts]
   ├─ Copy input to os.tmpdir() (safe path)
   ├─ Validate that input is a valid ZIP/3MF (JSZip)
   ├─ Build profileSettings from config (+ curr_bed_type default)
   ├─ sanitizeBblGcodeTemplates(profileSettings)
   │     (strips BBL-proprietary variables from the G-code template)
   ├─ OUTPUT IS ALWAYS IN os.tmpdir() — ignores the user's data.output
   └─ Call orca.slice({ input, output, plate, profile, options })

3-7. Same N-API → engine → AddonCore → libslic3r flow

   Difference in AddonCore:
   ├─ Load the 3MF (which may contain embedded config)
   ├─ profile applied BEFORE the 3MF (3MF overrides profile)
   └─ options applied AFTER the 3MF (highest priority)

8. Slicer3MfService.create() continues                [3mf.class.ts]
   ├─ Validate that output is a valid ZIP with embedded G-code
   ├─ Encode content as base64
   └─ Return {id, filename, outputPath, dataBase64, size, ...}
```

## Engine path resolution

`ensure_engine_loaded()` looks for `liborcacli_engine` in this order:

1. `ORCACLI_ENGINE_PATH` (env override, if set)
2. `<module_dir>/../../../../build-ninja/src/` (Ninja monorepo build)
3. `<module_dir>/liborcacli_engine.{dylib,so}` (next to the .node)
4. `<module_dir>/../src/` (CMake build layout)
5. `<module_dir>/../../src/` (monorepo layout)
6. `<module_dir>/../bindings/node/` (CMake output)

Required symbols: `orcacli_create`, `orcacli_destroy`.
The rest are optional (logged if missing, do not fail loading).

## .node resolution (JS loader)

`bindings/node/index.js` looks for the `.node` in this order (local-first):

1. `build/Release/orcaslicer_node.node` (cmake-js default)
2. `build/bindings/node/orcaslicer_node.node` (cmake-js custom output)
3. `../../build/bindings/node/orcaslicer_node.node` (monorepo build)
4. `prebuilds/<platform>-<arch>/orcaslicer_node.node` (npm prebuilt)

> Set `ORCACLI_PREFER_PREBUILT=1` to reverse the order and use prebuilt first.
> `node-api` forces local-first by setting `ORCACLI_PREFER_LOCAL=1` in `orca.ts`.

## Threading model

```
Main thread (Node Event Loop)
  ├── require('orcaslicer-addon')  → loads the .node, does NOT load the engine yet
  ├── orca.initialize()            → ensure_engine_loaded() [dlopen] + init
  ├── orca.slice(params)           → creates Promise + async_work, returns immediately
  └── ... other HTTP requests are served normally

Node thread pool (libuv)
  ├── napi_async_work "slice"      → SliceExecute()
  │     ├── lock(g_mutex)          ← BLOCKS if another slice is running
  │     ├── AddonCore::slice()     ← CPU-bound, may take seconds/minutes
  │     └── unlock(g_mutex)
  └── napi_async_work "getModelInfo" → InfoExecute() (same pattern)
```

**Implication**: multiple concurrent HTTP requests are implicitly queued by the
mutex. The API responds, but slicing is serial.
