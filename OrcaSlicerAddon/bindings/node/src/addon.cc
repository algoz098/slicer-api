#include <node_api.h>
#include <assert.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>

#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <limits>

#include <cstring>
#include <filesystem>
#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif
#include <cstdio>
#include <chrono>

#include <fstream>
#include <iostream>
#include <regex>



// Global silent switch based on environment
static bool addon_is_silent() {
  static bool inited = false;
  static bool silent = false;
  if (!inited) {
    const char* v = std::getenv("ORCA_ADDON_LOG");
    const char* s = std::getenv("ORCACLI_SILENT");
    silent = (v && std::string(v) == "off") || (s && std::string(s) == "1");
    inited = true;
  }
  return silent;
}

#define ADDON_DEBUGF(...) do { if (!addon_is_silent()) { std::fprintf(stderr, __VA_ARGS__); std::fflush(stderr);} } while(0)


// Thin addon will dlopen the engine library at runtime; no direct core linkage.
static std::mutex g_mutex; // serialize heavy operations
static std::string g_current_resources; // track current resourcesPath for persistent sandbox

static constexpr int MAX_PENDING_WORK = 10;
static std::atomic<int> s_pending_work_count{0};


#define NAPI_CALL(env, call)                                                     \
  do {                                                                           \
    napi_status status = (call);                                                 \
    if (status != napi_ok) {                                                     \
      const napi_extended_error_info* info;                                      \
      napi_get_last_error_info((env), &info);                                    \
      const char* msg = info && info->error_message ? info->error_message : "napi error"; \
      napi_throw_error((env), nullptr, msg);                                     \
      return nullptr;                                                            \
    }                                                                            \
  } while(0)

// Same as NAPI_CALL but for void-returning scopes (e.g., lambdas)
#define NAPI_CALL_VOID(env, call)                                                \
  do {                                                                           \
    napi_status status = (call);                                                 \
    if (status != napi_ok) {                                                     \
      const napi_extended_error_info* info;                                      \
      napi_get_last_error_info((env), &info);                                    \
      const char* msg = info && info->error_message ? info->error_message : "napi error"; \
      napi_throw_error((env), nullptr, msg);                                     \
      return;                                                                    \
    }                                                                            \
  } while(0)


// C FFI mirrors EngineAPI.hpp (kept locally to avoid compile-time dependency)
typedef void* orcacli_handle;
typedef struct { bool success; const char* message; const char* error_details; double estimated_time_sec; double filament_used_grams; } orcacli_operation_result;
typedef struct { const char* filename; uint32_t object_count; uint32_t triangle_count; double volume; const char* bounding_box; bool is_valid; } orcacli_model_info;
// key/value override
typedef struct { const char* key; const char* value; } orcacli_kv;
typedef struct { const char* input_file; const char* output_file; const char* config_file; const char* preset_name; int32_t plate_index; bool verbose; bool dry_run; bool center_on_bed; bool auto_realign_if_needed; const orcacli_kv* profile; int32_t profile_count; const orcacli_kv* overrides; int32_t overrides_count; } orcacli_slice_params;

static_assert(sizeof(orcacli_operation_result) == 40, "ABI: orcacli_operation_result size mismatch with EngineAPI.hpp");
static_assert(sizeof(orcacli_model_info) == 40, "ABI: orcacli_model_info size mismatch with EngineAPI.hpp");
static_assert(sizeof(orcacli_slice_params) == 72, "ABI: orcacli_slice_params size mismatch with EngineAPI.hpp");
static_assert(sizeof(orcacli_kv) == sizeof(const char*) * 2, "ABI: orcacli_kv must be two pointers");

typedef orcacli_handle       (*PF_orcacli_create)();
typedef void                 (*PF_orcacli_destroy)(orcacli_handle);
typedef orcacli_operation_result (*PF_orcacli_initialize)(orcacli_handle, const char*);
typedef orcacli_operation_result (*PF_orcacli_load_model)(orcacli_handle, const char*);
typedef orcacli_model_info   (*PF_orcacli_get_model_info)(orcacli_handle);
typedef orcacli_operation_result (*PF_orcacli_slice)(orcacli_handle, const orcacli_slice_params*);
typedef const char*          (*PF_orcacli_version)();
typedef void                 (*PF_orcacli_set_logging_silenced)(bool);
typedef void                 (*PF_orcacli_free_string)(const char*);
typedef void                 (*PF_orcacli_free_model_info)(orcacli_model_info*);
typedef void                 (*PF_orcacli_free_result)(orcacli_operation_result*);
typedef orcacli_operation_result (*PF_orcacli_load_vendor)(orcacli_handle, const char*);

struct FFI {
  void* lib = nullptr;
  // functions
  PF_orcacli_create create = nullptr;
  PF_orcacli_destroy destroy = nullptr;
  PF_orcacli_initialize initialize = nullptr;
  PF_orcacli_load_model load_model = nullptr;
  PF_orcacli_get_model_info get_model_info = nullptr;
  PF_orcacli_slice slice = nullptr;
  PF_orcacli_version version = nullptr;
  PF_orcacli_set_logging_silenced set_logging_silenced = nullptr;
  PF_orcacli_free_string free_string = nullptr;
  PF_orcacli_free_model_info free_model_info = nullptr;
  PF_orcacli_free_result free_result = nullptr;
  PF_orcacli_load_vendor load_vendor = nullptr;
  // state
  orcacli_handle inst = nullptr;
};

static FFI g_ffi;

static std::string module_dir_path() {
#if defined(_WIN32)
  HMODULE hMod = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)&module_dir_path, &hMod)) {
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(hMod, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
      std::filesystem::path p(buf);
      return p.parent_path().string();
    }
  }
  return std::string();
#else
  Dl_info info{};
  if (dladdr((void*)&module_dir_path, &info) && info.dli_fname) {
    std::filesystem::path p(info.dli_fname);
    return p.parent_path().string();
  }
  return std::string();
#endif
}

static bool ensure_engine_loaded(std::string* err_out) {
  if (g_ffi.lib) return true;
  ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: begin\n");
  const char* override = std::getenv("ORCACLI_ENGINE_PATH");
  std::vector<std::string> candidates;
  if (override && *override) candidates.emplace_back(override);
#ifdef __APPLE__
  const char* libname = "liborcacli_engine.dylib";
#else
  const char* libname = "liborcacli_engine.so";
#endif
  std::string mdir = module_dir_path();
  if (!mdir.empty()) {
    std::filesystem::path base(mdir);
    std::filesystem::path p1 = base / libname;                                  // <module>/lib...
    std::filesystem::path p2 = base / ".." / "src" / libname;                  // <module>/../src/lib...
    std::filesystem::path p3 = base / ".." / ".." / "src" / libname;          // <module>/../../src/lib...
    std::filesystem::path p4 = base / ".." / "bindings" / "node" / libname;   // <module>/../bindings/node/lib...
    // Also consider Ninja build output dir used in this repo layout
    // From bindings/node/build/Release we need 4 levels up to reach OrcaSlicerAddon/build-ninja/src
    std::filesystem::path p5 = base / ".." / ".." / ".." / ".." / "build-ninja" / "src" / libname; // <module>/../../../../build-ninja/src/lib...
    // Prefer the Ninja engine (freshly built in this monorepo) before local copies to avoid stale engines during dev
    candidates.push_back(p5.lexically_normal().string());
    candidates.push_back(p1.lexically_normal().string());
    candidates.push_back(p2.lexically_normal().string());
    candidates.push_back(p3.lexically_normal().string());
    candidates.push_back(p4.lexically_normal().string());
  }
  const char* last_err = nullptr; std::string last_path;
  for (const auto& p : candidates) {
#if defined(_WIN32)
    g_ffi.lib = (void*)LoadLibraryA(p.c_str());
    DWORD err = GetLastError();
    ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: try '%s' => %p (win)\n", p.c_str(), g_ffi.lib);
    if (g_ffi.lib) break;
    static std::string win_last_err;
    if (err != 0) {
      win_last_err = "Windows error " + std::to_string(err);
      last_err = win_last_err.c_str();
    } else {
      last_err = nullptr;
    }
    last_path = p;
#else
    g_ffi.lib = dlopen(p.c_str(), RTLD_NOW);
    const char* dlerr = g_ffi.lib ? nullptr : dlerror();
    ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: try '%s' => %p%s%s\n", p.c_str(), g_ffi.lib, dlerr?" err=":"", dlerr?dlerr:"");
    if (g_ffi.lib) break;
    last_err = dlerr; last_path = p;
#endif
  }
  if (!g_ffi.lib) {
    if (err_out) {
      std::string msg = "Failed to load engine library";
      if (!last_path.empty()) { msg += ": "; msg += last_path; }
      if (last_err) { msg += " — "; msg += last_err; }
      *err_out = msg;
    }
    ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: failed: %s\n", err_out?err_out->c_str():"(no err_out)");
    return false;
  }
#if defined(_WIN32)
  auto load_sym = [](void* lib, const char* name){ return (void*)GetProcAddress((HMODULE)lib, name); };
#else
  auto load_sym = [](void* lib, const char* name){ return dlsym(lib, name); };
#endif
  g_ffi.create         = reinterpret_cast<PF_orcacli_create>(load_sym(g_ffi.lib, "orcacli_create"));
  g_ffi.destroy        = reinterpret_cast<PF_orcacli_destroy>(load_sym(g_ffi.lib, "orcacli_destroy"));
  g_ffi.initialize     = reinterpret_cast<PF_orcacli_initialize>(load_sym(g_ffi.lib, "orcacli_initialize"));
  g_ffi.load_model     = reinterpret_cast<PF_orcacli_load_model>(load_sym(g_ffi.lib, "orcacli_load_model"));
  g_ffi.get_model_info = reinterpret_cast<PF_orcacli_get_model_info>(load_sym(g_ffi.lib, "orcacli_get_model_info"));
  g_ffi.slice          = reinterpret_cast<PF_orcacli_slice>(load_sym(g_ffi.lib, "orcacli_slice"));
  g_ffi.version        = reinterpret_cast<PF_orcacli_version>(load_sym(g_ffi.lib, "orcacli_version"));
  g_ffi.set_logging_silenced = reinterpret_cast<PF_orcacli_set_logging_silenced>(load_sym(g_ffi.lib, "orcacli_set_logging_silenced"));
  g_ffi.free_string    = reinterpret_cast<PF_orcacli_free_string>(load_sym(g_ffi.lib, "orcacli_free_string"));
  g_ffi.free_model_info= reinterpret_cast<PF_orcacli_free_model_info>(load_sym(g_ffi.lib, "orcacli_free_model_info"));
  g_ffi.free_result         = reinterpret_cast<PF_orcacli_free_result>(load_sym(g_ffi.lib, "orcacli_free_result"));
  g_ffi.load_vendor         = reinterpret_cast<PF_orcacli_load_vendor>(load_sym(g_ffi.lib, "orcacli_load_vendor"));
  // Relaxed symbol requirements: require core create/destroy; others optional for dev
  if (!g_ffi.create || !g_ffi.destroy) {
    if (err_out) *err_out = "Missing required core symbols in engine library (create/destroy)";
#if defined(_WIN32)
    FreeLibrary((HMODULE)g_ffi.lib); g_ffi = FFI{}; // reset
#else
    dlclose(g_ffi.lib); g_ffi = FFI{}; // reset
#endif
    return false;
  }
  ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: symbols loaded create=%p init=%p slice=%p version=%p free_result=%p\n", (void*)g_ffi.create, (void*)g_ffi.initialize, (void*)g_ffi.slice, (void*)g_ffi.version, (void*)g_ffi.free_result);
  // Log optional missing symbols for diagnostics (do not fail)
  auto log_missing = [&](const char* name, void* p){ if (!p) { ADDON_DEBUGF("DEBUG: [addon] engine missing optional symbol: %s\n", name); } };
  log_missing("orcacli_initialize", (void*)g_ffi.initialize);
  log_missing("orcacli_load_model", (void*)g_ffi.load_model);
  log_missing("orcacli_get_model_info", (void*)g_ffi.get_model_info);
  log_missing("orcacli_slice", (void*)g_ffi.slice);
  log_missing("orcacli_version", (void*)g_ffi.version);
  log_missing("orcacli_free_string", (void*)g_ffi.free_string);
  log_missing("orcacli_free_model_info", (void*)g_ffi.free_model_info);
  log_missing("orcacli_free_result", (void*)g_ffi.free_result);
  log_missing("orcacli_set_logging_silenced", (void*)g_ffi.set_logging_silenced);
  log_missing("orcacli_load_vendor", (void*)g_ffi.load_vendor);
  ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: calling create()...\n");
  g_ffi.inst = g_ffi.create();
  ADDON_DEBUGF("DEBUG: [addon] ensure_engine_loaded: create() => %p\n", g_ffi.inst);
  if (!g_ffi.inst) {
    if (err_out) *err_out = "Failed to create engine instance";
#if defined(_WIN32)
    FreeLibrary((HMODULE)g_ffi.lib); g_ffi = FFI{}; return false;
#else
    dlclose(g_ffi.lib); g_ffi = FFI{}; return false;
#endif
  }
  return true;
}

static constexpr size_t MAX_STRING_BYTES = 64 * 1024 * 1024; // 64 MB

static std::string get_string(napi_env env, napi_value v) {
  // TEST ONLY: Avoid NAPI_CALL in helpers that don't return napi_value; handle errors locally.
  size_t len = 0;
  napi_status st = napi_get_value_string_utf8(env, v, nullptr, 0, &len);
  if (st != napi_ok) { napi_throw_type_error(env, nullptr, "expected string"); return std::string(); }
  if (len > MAX_STRING_BYTES) {
    napi_throw_range_error(env, nullptr, "String argument exceeds maximum length (64 MB)");
    return std::string();
  }
  // Allocate len+1 to accommodate the N-API null terminator, then shrink to actual length.
  std::string s; s.resize(len + 1);
  size_t written = 0; st = napi_get_value_string_utf8(env, v, s.data(), s.size(), &written);
  if (st != napi_ok) { napi_throw_type_error(env, nullptr, "failed to read string"); return std::string(); }
  if (written <= s.size()) s.resize(written);
  if (s.find('\0') != std::string::npos) {
    napi_throw_error(env, nullptr, "String argument contains null byte");
    return std::string();
  }
  return s;
}

static bool get_bool(napi_env env, napi_value v, bool* out) {
  bool b=false; if (napi_get_value_bool(env, v, &b) != napi_ok) return false; *out=b; return true;
}

// initialize({ resourcesPath?: string, verbose?: boolean, strict?: boolean, vendors?: string[] })
static napi_value Initialize(napi_env env, napi_callback_info info) {

  // log para debug
  ADDON_DEBUGF("DEBUG: [addon] Initialize 4 ()\n");
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));

  std::string resourcesPath;
  bool strict = true; // API-only: default to strict no-autoload

  if (argc >= 1) {
    napi_valuetype t; NAPI_CALL(env, napi_typeof(env, args[0], &t));
    if (t == napi_object) {
      napi_value v;
      bool has;
      NAPI_CALL(env, napi_has_named_property(env, args[0], "resourcesPath", &has));
      if (has) { NAPI_CALL(env, napi_get_named_property(env, args[0], "resourcesPath", &v)); resourcesPath = get_string(env, v); }
      NAPI_CALL(env, napi_has_named_property(env, args[0], "strict", &has));
      if (has) { NAPI_CALL(env, napi_get_named_property(env, args[0], "strict", &v)); (void)get_bool(env, v, &strict); }
    }
  }

  // JSON on-the-fly mode only - no profile loading
  ADDON_DEBUGF("DEBUG: [addon] initialize options: strict=%d\n", (int)strict);

  ADDON_DEBUGF("DEBUG: [addon] before ensure_engine_loaded()\n");
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { napi_throw_error(env, nullptr, err.c_str()); return nullptr; }
  ADDON_DEBUGF("DEBUG: [addon] after ensure_engine_loaded()\n");
  // Initialize the engine with the provided resourcesPath (if any)

  if (g_ffi.initialize) {
    const char* rp = resourcesPath.empty() ? nullptr : resourcesPath.c_str();
    ADDON_DEBUGF("DEBUG: [addon] about to call g_ffi.initialize(rp=%s) init=%p free_result=%p\n", rp?rp:"(null)", (void*)g_ffi.initialize, (void*)g_ffi.free_result);
    auto r = g_ffi.initialize(g_ffi.inst, rp);
    ADDON_DEBUGF("DEBUG: [addon] g_ffi.initialize returned success=%d msg_ptr=%p details_ptr=%p\n", (int)r.success, (void*)r.message, (void*)r.error_details);
    if (!r.success) {
      std::string msg = r.message ? r.message : "initialize failed";
      if (r.error_details) { msg += " — "; msg += r.error_details; }
      if (g_ffi.free_result) { g_ffi.free_result(&r); }
      if (g_ffi.destroy) { g_ffi.destroy(g_ffi.inst); g_ffi.inst = nullptr; }
      napi_throw_error(env, nullptr, msg.c_str());

      return nullptr;
    }
    // After successful initialize, remember current resourcesPath
    g_current_resources = rp ? std::string(rp) : std::string();
    if (g_ffi.free_result) { g_ffi.free_result(&r); }
  }

  napi_value undef; NAPI_CALL(env, napi_get_undefined(env, &undef)); return undef;
}

// version(): string
static napi_value Version(napi_env env, napi_callback_info info) {
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { napi_throw_error(env, nullptr, err.c_str()); return nullptr; }
  if (!g_ffi.version) { napi_throw_error(env, nullptr, "version not available in engine"); return nullptr; }
  const char* v = g_ffi.version();
  napi_value js; NAPI_CALL(env, napi_create_string_utf8(env, v?v:"", NAPI_AUTO_LENGTH, &js)); return js;
}

// getModelInfo(file): Promise<ModelInfo>
struct InfoWork { napi_async_work work; napi_deferred deferred; std::string file; struct { std::string filename; uint32_t object_count=0; uint32_t triangle_count=0; double volume=0; std::string bounding_box; bool is_valid=false; } info; std::string err; };

static void InfoExecute(napi_env env, void* data) {
  InfoWork* w = static_cast<InfoWork*>(data);
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { w->err = err; return; }
  if (!g_ffi.load_model) { w->err = "load_model not available in engine"; return; }
  auto r = g_ffi.load_model(g_ffi.inst, w->file.c_str());
  if (!r.success) {
    if (r.error_details && r.error_details[0]) w->err = r.error_details;
    else if (r.message && r.message[0]) w->err = r.message;
    else w->err = "loadModel failed";
    if (g_ffi.free_result) g_ffi.free_result(&r);
    return;
  }
  if (g_ffi.free_result) g_ffi.free_result(&r);
  if (!g_ffi.get_model_info) { w->err = "get_model_info not available in engine"; return; }
  auto mi = g_ffi.get_model_info(g_ffi.inst);
  if (mi.filename) w->info.filename = mi.filename;
  w->info.object_count = mi.object_count;
  w->info.triangle_count = mi.triangle_count;
  w->info.volume = mi.volume;
  if (mi.bounding_box) w->info.bounding_box = mi.bounding_box;
  w->info.is_valid = mi.is_valid;
  if (g_ffi.free_model_info) g_ffi.free_model_info(&mi);
}

static void InfoComplete(napi_env env, napi_status status, void* data) {
  s_pending_work_count--;
  InfoWork* w = static_cast<InfoWork*>(data);
  if (status != napi_ok) { napi_value e; napi_create_string_utf8(env, "Async failure", NAPI_AUTO_LENGTH, &e); napi_reject_deferred(env, w->deferred, e); }
  else if (!w->err.empty()) { napi_value e; napi_create_string_utf8(env, w->err.c_str(), NAPI_AUTO_LENGTH, &e); napi_reject_deferred(env, w->deferred, e); }
  else {
    napi_value obj; napi_create_object(env, &obj);
    napi_value v;
    napi_create_string_utf8(env, w->info.filename.c_str(), NAPI_AUTO_LENGTH, &v); napi_set_named_property(env, obj, "filename", v);
    napi_create_uint32(env, (uint32_t)w->info.object_count, &v); napi_set_named_property(env, obj, "objectCount", v);
    napi_create_uint32(env, (uint32_t)w->info.triangle_count, &v); napi_set_named_property(env, obj, "triangleCount", v);
    napi_create_double(env, w->info.volume, &v); napi_set_named_property(env, obj, "volume", v);
    napi_create_string_utf8(env, w->info.bounding_box.c_str(), NAPI_AUTO_LENGTH, &v); napi_set_named_property(env, obj, "boundingBox", v);
    napi_get_boolean(env, w->info.is_valid, &v); napi_set_named_property(env, obj, "isValid", v);
    napi_resolve_deferred(env, w->deferred, obj);
  }
  napi_delete_async_work(env, w->work); delete w;
}

static napi_value GetModelInfo(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data; NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));
  if (argc < 1) { napi_throw_type_error(env, nullptr, "file path is required"); return nullptr; }
  std::string file = get_string(env, args[0]);

  int prev = s_pending_work_count.load();
  if (prev >= MAX_PENDING_WORK) {
    napi_throw_error(env, nullptr, "Too many pending slice requests. Please retry later.");
    return nullptr;
  }

  auto work = std::make_unique<InfoWork>(); work->file = std::move(file);
  napi_value promise; NAPI_CALL(env, napi_create_promise(env, &work->deferred, &promise));
  napi_value resource_name; napi_create_string_utf8(env, "getModelInfo", NAPI_AUTO_LENGTH, &resource_name);
  NAPI_CALL(env, napi_create_async_work(env, nullptr, resource_name, InfoExecute, InfoComplete, work.get(), &work->work));
  NAPI_CALL(env, napi_queue_async_work(env, work->work));
  s_pending_work_count.fetch_add(1);
  work.release();
  return promise;
}

// slice(params): Promise<{output: string}>
struct SliceWork {
  napi_async_work work; napi_deferred deferred;
  struct {
    std::string input_file; std::string output_file;
    int plate_index=1; bool verbose=false; bool dry_run=false;
    bool center_on_bed=false;
    bool auto_realign_if_needed=false;
  } p;
  // store profile (base) and options (explicit overrides) as separate arrays
  std::vector<std::pair<std::string,std::string>> profile_opts; // base profile → applied before 3MF
  std::vector<orcacli_kv> profile_kvs;
  std::vector<std::pair<std::string,std::string>> opts;         // explicit overrides → applied after 3MF
  std::vector<orcacli_kv> kvs;
  std::string err;
  std::string msg; // JSON from engine with used/ignored keys (on success)
  // native stats propagated from engine
  double est_time_sec = -1.0;
  double fil_used_grams = -1.0;
};

static void SliceExecute(napi_env env, void* data) {
  SliceWork* w = static_cast<SliceWork*>(data);
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { w->err = err; return; }
  orcacli_slice_params p{};
  p.input_file = w->p.input_file.c_str();
  p.output_file = w->p.output_file.c_str();
  // Display names for profiles in output 3MF (metadata only)
  p.plate_index = w->p.plate_index;
  p.verbose = w->p.verbose;
  p.dry_run = w->p.dry_run;
  p.auto_realign_if_needed            = w->p.auto_realign_if_needed;

  p.center_on_bed                     = w->p.center_on_bed;
  // Build profile array (base profile, applied before 3MF)
  if (!w->profile_opts.empty()) {
    w->profile_kvs.clear(); w->profile_kvs.reserve(w->profile_opts.size());
    for (auto &kv : w->profile_opts) {
      w->profile_kvs.push_back({ kv.first.c_str(), kv.second.c_str() });
    }
    p.profile = w->profile_kvs.data();
    if (w->profile_kvs.size() > (size_t)INT32_MAX) { w->err = "Too many profile entries"; return; }
    p.profile_count = (int32_t)w->profile_kvs.size();
  } else {
    p.profile = nullptr;
    p.profile_count = 0;
  }
  // Build overrides array (explicit overrides, applied after 3MF)
  if (!w->opts.empty()) {
    w->kvs.clear(); w->kvs.reserve(w->opts.size());
    for (auto &kv : w->opts) {
      orcacli_kv ckv{ kv.first.c_str(), kv.second.c_str() };
      w->kvs.push_back(ckv);
    }
    p.overrides = w->kvs.data();
    if (w->kvs.size() > (size_t)INT32_MAX) { w->err = "Too many override entries"; return; }
    p.overrides_count = (int32_t)w->kvs.size();
  } else {
    p.overrides = nullptr;
    p.overrides_count = 0;
  }
  if (!g_ffi.slice) { w->err = "slice not available in engine"; return; }
  if (w->p.verbose) { ADDON_DEBUGF("DEBUG: [addon] calling g_ffi.slice input='%s' plate=%d overrides=%d\n", p.input_file ? p.input_file : "(null)", p.plate_index, p.overrides_count); }
  auto r = g_ffi.slice(g_ffi.inst, &p);
  if (w->p.verbose) { ADDON_DEBUGF("DEBUG: [addon] returned from g_ffi.slice (success=%d)\n", (int)r.success); }
  if (!r.success) {
    if (r.error_details && r.error_details[0]) w->err = r.error_details;
    else if (r.message && r.message[0]) w->err = r.message;
    else w->err = "slice failed";
  } else {
    // capture JSON payload from engine (used/ignored overrides)
    if (r.message) { try { w->msg = r.message; } catch (...) {} }
    // capture native stats
    w->est_time_sec = r.estimated_time_sec;
    w->fil_used_grams = r.filament_used_grams;
  }
  if (g_ffi.free_result) g_ffi.free_result(&r);
}

static void SliceComplete(napi_env env, napi_status status, void* data) {
  s_pending_work_count--;
  SliceWork* w = static_cast<SliceWork*>(data);
  if (status != napi_ok) { napi_value e; napi_create_string_utf8(env, "Async failure", NAPI_AUTO_LENGTH, &e); napi_reject_deferred(env, w->deferred, e); }
  else if (!w->err.empty()) { napi_value e; napi_create_string_utf8(env, w->err.c_str(), NAPI_AUTO_LENGTH, &e); napi_reject_deferred(env, w->deferred, e); }
  else {
    napi_value obj, v;
    napi_create_object(env, &obj);
    // Always include output path (echoed from params)
    napi_create_string_utf8(env, w->p.output_file.c_str(), NAPI_AUTO_LENGTH, &v);
    napi_set_named_property(env, obj, "output", v);

    // If the engine returned a JSON payload in message, parse and surface arrays
    if (!w->msg.empty()) {
      try {
        napi_value global;
        if (napi_get_global(env, &global) == napi_ok) {
          napi_value JSON_obj; bool ok1 = (napi_get_named_property(env, global, "JSON", &JSON_obj) == napi_ok);
          napi_value parse_fn; bool ok2 = ok1 && (napi_get_named_property(env, JSON_obj, "parse", &parse_fn) == napi_ok);
          napi_valuetype tparse; bool ok3 = ok2 && (napi_typeof(env, parse_fn, &tparse) == napi_ok) && (tparse == napi_function);
          if (ok3) {
            napi_value arg;
            if (napi_create_string_utf8(env, w->msg.c_str(), NAPI_AUTO_LENGTH, &arg) == napi_ok) {
              napi_value parsed;
              napi_status parseStatus = napi_call_function(env, JSON_obj, parse_fn, 1, &arg, &parsed);
              if (parseStatus == napi_ok) {
                // used -> usedOptions
                bool has=false; napi_value arr;
                if (napi_has_named_property(env, parsed, "used", &has) == napi_ok && has) {
                  if (napi_get_named_property(env, parsed, "used", &arr) == napi_ok) {
                    bool isArr=false; if (napi_is_array(env, arr, &isArr) == napi_ok && isArr) {
                      napi_set_named_property(env, obj, "usedOptions", arr);
                    }
                  }
                }
                // ignored -> ignoredOptions
                has=false;
                if (napi_has_named_property(env, parsed, "ignored", &has) == napi_ok && has) {
                  if (napi_get_named_property(env, parsed, "ignored", &arr) == napi_ok) {
                    bool isArr=false; if (napi_is_array(env, arr, &isArr) == napi_ok && isArr) {
                      napi_set_named_property(env, obj, "ignoredOptions", arr);
                    }
                  }
                }
              } else {
                napi_value dummy;
                napi_get_and_clear_last_exception(env, &dummy);
                ADDON_DEBUGF("DEBUG: [addon] Failed to parse engine JSON metadata\n");
              }
            }
          }
        }
      } catch (...) {
        ADDON_DEBUGF("DEBUG: [addon] Exception while parsing engine JSON metadata\n");
      }
    }


    // Native stats from engine (no parsing)
    if (w->est_time_sec >= 0.0) { napi_value nv; napi_create_double(env, w->est_time_sec, &nv); napi_set_named_property(env, obj, "estimatedTimeSec", nv); }
    if (w->fil_used_grams >= 0.0) { napi_value nv; napi_create_double(env, w->fil_used_grams, &nv); napi_set_named_property(env, obj, "filamentUsedGrams", nv); }

    napi_resolve_deferred(env, w->deferred, obj);
  }
  napi_delete_async_work(env, w->work); delete w;
}

static napi_value Slice(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data; NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));
  if (argc < 1) { napi_throw_type_error(env, nullptr, "params object is required"); return nullptr; }
  napi_value obj = args[0]; napi_valuetype t; NAPI_CALL(env, napi_typeof(env, obj, &t)); if (t != napi_object) { napi_throw_type_error(env, nullptr, "params must be object"); return nullptr; }

  auto work = std::make_unique<SliceWork>();
  // Robust getters: only read strings if value is actually a string; ignore undefined/null.
  auto set_str = [&](const char* key, std::string& dst){
    bool has=false; napi_value v; napi_has_named_property(env, obj, key, &has);
    if (has) {
      napi_get_named_property(env, obj, key, &v);
      napi_valuetype vt; if (napi_typeof(env, v, &vt) == napi_ok && vt == napi_string) {
        dst = get_string(env, v);
      }
    }
  };
  auto set_int = [&](const char* key, int& dst){
    bool has=false; napi_value v; napi_has_named_property(env, obj, key, &has);
    if (has) {
      napi_get_named_property(env, obj, key, &v);
      napi_valuetype vt; if (napi_typeof(env, v, &vt) == napi_ok) {
        if (vt == napi_number) { double d=0; napi_get_value_double(env, v, &d); if (!std::isfinite(d)) return; if (d < (double)std::numeric_limits<int>::min() || d > (double)std::numeric_limits<int>::max()) return; dst = static_cast<int>(d); }
      }
    }
  };
  auto set_bool = [&](const char* key, bool& dst){ bool has=false; napi_value v; napi_has_named_property(env, obj, key, &has); if(has){ napi_get_named_property(env, obj, key, &v); get_bool(env, v, &dst);} };

  set_str("input", work->p.input_file);
  set_str("output", work->p.output_file);
  set_int("plate", work->p.plate_index);
  set_bool("verbose", work->p.verbose);
  set_bool("dryRun", work->p.dry_run);
  // Behavior flags
  set_bool("autoRealignIfNeeded", work->p.auto_realign_if_needed);
  set_bool("center", work->p.center_on_bed);

  // Collect options from params.options and params.custom
  // Supports: string, number, boolean, and arrays (serialized with semicolon separator for OrcaSlicer)
  auto collect_kv = [&](napi_value mapObj){
    if (!mapObj) return;
    napi_valuetype vt; if (napi_typeof(env, mapObj, &vt) != napi_ok || vt != napi_object) return;
    napi_value names; NAPI_CALL_VOID(env, napi_get_property_names(env, mapObj, &names));
    uint32_t len=0; NAPI_CALL_VOID(env, napi_get_array_length(env, names, &len));
    for (uint32_t i=0;i<len;++i){
      napi_value k; NAPI_CALL_VOID(env, napi_get_element(env, names, i, &k));
      std::string key = get_string(env, k);
      napi_value v; NAPI_CALL_VOID(env, napi_get_named_property(env, mapObj, key.c_str(), &v));
      napi_valuetype vt2; NAPI_CALL_VOID(env, napi_typeof(env, v, &vt2));
      std::string sval;
      if (vt2 == napi_string) {
        sval = get_string(env, v);
      } else if (vt2 == napi_boolean) {
        bool b=false; get_bool(env, v, &b); sval = b?"1":"0";
      } else if (vt2 == napi_number) {
        double d=0; napi_get_value_double(env, v, &d); if (!std::isfinite(d)) continue; sval = std::to_string(d);
      } else {
        // Check if it's an array - serialize elements with comma separator
        bool is_array = false;
        napi_is_array(env, v, &is_array);
        if (is_array) {
          uint32_t arr_len = 0;
          napi_get_array_length(env, v, &arr_len);
          for (uint32_t j = 0; j < arr_len; ++j) {
            napi_value elem;
            napi_get_element(env, v, j, &elem);
            napi_valuetype elem_type;
            napi_typeof(env, elem, &elem_type);
            std::string elem_str;
            if (elem_type == napi_string) {
              elem_str = get_string(env, elem);
            } else if (elem_type == napi_number) {
              double d = 0; napi_get_value_double(env, elem, &d); if (!std::isfinite(d)) continue; elem_str = std::to_string(d);
            } else if (elem_type == napi_boolean) {
              bool b = false; get_bool(env, elem, &b); elem_str = b ? "1" : "0";
            } else {
              continue;
            }
            if (sval.size() + elem_str.size() + 1 > MAX_STRING_BYTES) {
              continue;
            }
            if (!sval.empty()) sval += ",";
            sval += elem_str;
          }
        } else {
          continue; // ignore other types
        }
      }
      work->opts.emplace_back(std::move(key), std::move(sval));
    }
  };
  bool has=false; napi_value map;
  // params.profile → base profile, applied before 3MF (3MF overrides these)
  auto collect_profile_kv = [&](napi_value mapObj){
    if (!mapObj) return;
    napi_valuetype vt; if (napi_typeof(env, mapObj, &vt) != napi_ok || vt != napi_object) return;
    napi_value names; NAPI_CALL_VOID(env, napi_get_property_names(env, mapObj, &names));
    uint32_t len=0; NAPI_CALL_VOID(env, napi_get_array_length(env, names, &len));
    for (uint32_t i=0;i<len;++i){
      napi_value k; NAPI_CALL_VOID(env, napi_get_element(env, names, i, &k));
      std::string key = get_string(env, k);
      napi_value v; NAPI_CALL_VOID(env, napi_get_named_property(env, mapObj, key.c_str(), &v));
      napi_valuetype vt2; NAPI_CALL_VOID(env, napi_typeof(env, v, &vt2));
      std::string sval;
      if (vt2 == napi_string) {
        sval = get_string(env, v);
      } else if (vt2 == napi_boolean) {
        bool b=false; get_bool(env, v, &b); sval = b?"1":"0";
      } else if (vt2 == napi_number) {
        double d=0; napi_get_value_double(env, v, &d); if (!std::isfinite(d)) continue; sval = std::to_string(d);
      } else {
        bool is_array = false;
        napi_is_array(env, v, &is_array);
        if (is_array) {
          uint32_t arr_len = 0;
          napi_get_array_length(env, v, &arr_len);
          for (uint32_t j = 0; j < arr_len; ++j) {
            napi_value elem; napi_get_element(env, v, j, &elem);
            napi_valuetype et; napi_typeof(env, elem, &et);
            std::string es;
            if (et == napi_string) es = get_string(env, elem);
            else if (et == napi_number) { double d=0; napi_get_value_double(env, elem, &d); if (!std::isfinite(d)) continue; es=std::to_string(d); }
            else if (et == napi_boolean) { bool b=false; get_bool(env, elem, &b); es=b?"1":"0"; }
            else continue;
            if (sval.size() + es.size() + 1 > MAX_STRING_BYTES) {
              continue;
            }
            if (!sval.empty()) sval += ",";
            sval += es;
          }
        } else { continue; }
      }
      work->profile_opts.emplace_back(std::move(key), std::move(sval));
    }
  };
  napi_has_named_property(env, obj, "profile", &has); if (has) { napi_get_named_property(env, obj, "profile", &map); collect_profile_kv(map); }
  // params.options / params.custom → explicit overrides, applied after 3MF (highest priority)
  napi_has_named_property(env, obj, "options", &has); if (has) { napi_get_named_property(env, obj, "options", &map); collect_kv(map); }
  napi_has_named_property(env, obj, "custom", &has);  if (has) { napi_get_named_property(env, obj, "custom",  &map); collect_kv(map); }
  // Also support "customSettings" as an alias for "custom" (used by weslicer API)
  napi_has_named_property(env, obj, "customSettings", &has); if (has) { napi_get_named_property(env, obj, "customSettings", &map); collect_kv(map); }

  {
    bool has_exception = false;
    napi_is_exception_pending(env, &has_exception);
    if (has_exception) {
      napi_value ex;
      napi_get_and_clear_last_exception(env, &ex);
      work.reset();
      napi_throw_error(env, nullptr, "Failed to process slice options");
      return nullptr;
    }
  }

  if (work->p.verbose) {
    ADDON_DEBUGF("DEBUG: [addon] Slice() scheduling: input='%s' output='%s' plate=%d opts=%zu\n",
            work->p.input_file.c_str(), work->p.output_file.c_str(), work->p.plate_index, work->opts.size());
  }

  if (work->p.input_file.empty()) { napi_throw_type_error(env, nullptr, "params.input is required"); return nullptr; }
  if (work->p.output_file.empty()) { napi_throw_type_error(env, nullptr, "params.output is required"); return nullptr; }

  int prev = s_pending_work_count.load();
  if (prev >= MAX_PENDING_WORK) {
    napi_throw_error(env, nullptr, "Too many pending slice requests. Please retry later.");
    return nullptr;
  }

  napi_value promise; NAPI_CALL(env, napi_create_promise(env, &work->deferred, &promise));
  napi_value resource_name; napi_create_string_utf8(env, "slice", NAPI_AUTO_LENGTH, &resource_name);
  NAPI_CALL(env, napi_create_async_work(env, nullptr, resource_name, SliceExecute, SliceComplete, work.get(), &work->work));
  NAPI_CALL(env, napi_queue_async_work(env, work->work));
  s_pending_work_count.fetch_add(1);
  work.release();
  return promise;
}

// loadVendor(vendorId: string): void
static napi_value LoadVendor(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));
  if (argc < 1) { napi_throw_type_error(env, nullptr, "vendorId is required"); return nullptr; }
  std::string vendorId = get_string(env, args[0]);
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { napi_throw_error(env, nullptr, err.c_str()); return nullptr; }
  if (!g_ffi.load_vendor) { napi_throw_error(env, nullptr, "loadVendor not available in this engine build"); return nullptr; }
  auto r = g_ffi.load_vendor(g_ffi.inst, vendorId.c_str());
  if (!r.success) {
    std::string msg;
    if (r.error_details && r.error_details[0]) msg = r.error_details;
    else if (r.message && r.message[0]) msg = r.message;
    else msg = "loadVendor failed";
    if (g_ffi.free_result) g_ffi.free_result(&r);
    napi_throw_error(env, nullptr, msg.c_str()); return nullptr;
  }
  if (g_ffi.free_result) g_ffi.free_result(&r);
  napi_value undef; NAPI_CALL(env, napi_get_undefined(env, &undef)); return undef;
}

// shutdown(): cleans up engine state deterministically
static napi_value Shutdown(napi_env env, napi_callback_info info) {
  (void)info;
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_ffi.lib && g_ffi.set_logging_silenced) {
    try { g_ffi.set_logging_silenced(false); } catch (...) {}
  }
  if (g_ffi.inst && g_ffi.destroy) {
    try { g_ffi.destroy(g_ffi.inst); } catch (...) {}
    g_ffi.inst = nullptr;
  }
  g_current_resources.clear();
  // Reset the FFI so ensure_engine_loaded will reload
  if (g_ffi.lib) {
#if defined(_WIN32)
    FreeLibrary((HMODULE)g_ffi.lib);
#else
    dlclose(g_ffi.lib);
#endif
    g_ffi = FFI{};
  }
  napi_value undef; napi_get_undefined(env, &undef); return undef;
}

// setLoggingSilenced(silent: boolean)
static napi_value SetLoggingSilenced(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));
  if (argc < 1) { napi_throw_type_error(env, nullptr, "silent boolean is required"); return nullptr; }
  bool silent=false; if (!get_bool(env, args[0], &silent)) { napi_throw_type_error(env, nullptr, "silent must be boolean"); return nullptr; }
  if (g_ffi.lib && g_ffi.set_logging_silenced) {
    try { g_ffi.set_logging_silenced(silent); } catch (...) {}
  }
  napi_value undef; NAPI_CALL(env, napi_get_undefined(env, &undef)); return undef;
}

static napi_value Init(napi_env env, napi_value exports) {
  // Marker log to verify we're running the freshly built addon and where it lives
  std::string mdir = module_dir_path();
  ADDON_DEBUGF("DEBUG: [addon] Init loaded (module_dir=%s, built=%s %s)\n", mdir.c_str(), __DATE__, __TIME__);

  napi_property_descriptor props[] = {
    {"initialize", 0, Initialize, 0, 0, 0, napi_default, 0},
    {"shutdown",   0, Shutdown,   0, 0, 0, napi_default, 0},
    {"version",    0, Version,    0, 0, 0, napi_default, 0},
    {"getModelInfo", 0, GetModelInfo, 0, 0, 0, napi_default, 0},
    {"slice",      0, Slice,      0, 0, 0, napi_default, 0},
    {"setLoggingSilenced", 0, SetLoggingSilenced, 0, 0, 0, napi_default, 0},
    {"loadVendor",          0, LoadVendor,          0, 0, 0, napi_default, 0},
  };
  NAPI_CALL(env, napi_define_properties(env, exports, sizeof(props)/sizeof(props[0]), props));
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

