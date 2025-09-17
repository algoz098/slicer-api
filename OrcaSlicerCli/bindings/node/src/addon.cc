#include <node_api.h>
#include <assert.h>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

#include <cstring>
#include <filesystem>
#if defined(_WIN32)
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

// Thin addon will dlopen the engine library at runtime; no direct core linkage.
static std::mutex g_mutex; // serialize heavy operations

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


// C FFI mirrors EngineAPI.hpp (kept locally to avoid compile-time dependency)
typedef void* orcacli_handle;
typedef struct { bool success; const char* message; const char* error_details; } orcacli_operation_result;
typedef struct { const char* filename; uint32_t object_count; uint32_t triangle_count; double volume; const char* bounding_box; bool is_valid; } orcacli_model_info;
typedef struct { const char* input_file; const char* output_file; const char* config_file; const char* preset_name; const char* printer_profile; const char* filament_profile; const char* process_profile; int32_t plate_index; bool verbose; bool dry_run; } orcacli_slice_params;

typedef orcacli_handle       (*PF_orcacli_create)();
typedef void                 (*PF_orcacli_destroy)(orcacli_handle);
typedef orcacli_operation_result (*PF_orcacli_initialize)(orcacli_handle, const char*);
typedef orcacli_operation_result (*PF_orcacli_load_model)(orcacli_handle, const char*);
typedef orcacli_model_info   (*PF_orcacli_get_model_info)(orcacli_handle);
typedef orcacli_operation_result (*PF_orcacli_slice)(orcacli_handle, const orcacli_slice_params*);
typedef const char*          (*PF_orcacli_version)();
typedef void                 (*PF_orcacli_free_string)(const char*);
typedef void                 (*PF_orcacli_free_model_info)(orcacli_model_info*);
typedef void                 (*PF_orcacli_free_result)(orcacli_operation_result*);

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
  PF_orcacli_free_string free_string = nullptr;
  PF_orcacli_free_model_info free_model_info = nullptr;
  PF_orcacli_free_result free_result = nullptr;
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
    candidates.push_back(p1.lexically_normal().string());
    candidates.push_back(p2.lexically_normal().string());
    candidates.push_back(p3.lexically_normal().string());
  }
  const char* last_err = nullptr; std::string last_path;
  for (const auto& p : candidates) {
#if defined(_WIN32)
    g_ffi.lib = (void*)LoadLibraryA(p.c_str());
    if (g_ffi.lib) break;
    last_err = nullptr; last_path = p;
#else
    g_ffi.lib = dlopen(p.c_str(), RTLD_NOW);
    if (g_ffi.lib) break;
    last_err = dlerror(); last_path = p;
#endif
  }
  if (!g_ffi.lib) {
    if (err_out) {
      std::string msg = "Failed to load engine library";
      if (!last_path.empty()) { msg += ": "; msg += last_path; }
      if (last_err) { msg += " — "; msg += last_err; }
      *err_out = msg;
    }
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
  g_ffi.free_string    = reinterpret_cast<PF_orcacli_free_string>(load_sym(g_ffi.lib, "orcacli_free_string"));
  g_ffi.free_model_info= reinterpret_cast<PF_orcacli_free_model_info>(load_sym(g_ffi.lib, "orcacli_free_model_info"));
  g_ffi.free_result    = reinterpret_cast<PF_orcacli_free_result>(load_sym(g_ffi.lib, "orcacli_free_result"));
  if (!g_ffi.create || !g_ffi.destroy || !g_ffi.initialize || !g_ffi.load_model || !g_ffi.get_model_info || !g_ffi.slice || !g_ffi.version || !g_ffi.free_string || !g_ffi.free_model_info || !g_ffi.free_result) {
    if (err_out) *err_out = "Missing symbols in engine library";
#if defined(_WIN32)
    FreeLibrary((HMODULE)g_ffi.lib); g_ffi = FFI{}; // reset
#else
    dlclose(g_ffi.lib); g_ffi = FFI{}; // reset
#endif
    return false;
  }
  g_ffi.inst = g_ffi.create();
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

static std::string get_string(napi_env env, napi_value v) {
  size_t len; NAPI_CALL(env, napi_get_value_string_utf8(env, v, nullptr, 0, &len));
  std::string s; s.resize(len);
  NAPI_CALL(env, napi_get_value_string_utf8(env, v, s.data(), len + 1, &len));
  return s;
}

static bool get_bool(napi_env env, napi_value v, bool* out) {
  bool b=false; if (napi_get_value_bool(env, v, &b) != napi_ok) return false; *out=b; return true;
}

// initialize({ resourcesPath?: string, verbose?: boolean })
static napi_value Initialize(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data;
  NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));

  std::string resourcesPath;
  bool verbose = false;

  if (argc >= 1) {
    napi_valuetype t; NAPI_CALL(env, napi_typeof(env, args[0], &t));
    if (t == napi_object) {
      napi_value v;
      bool has;
      NAPI_CALL(env, napi_has_named_property(env, args[0], "resourcesPath", &has));
      if (has) { NAPI_CALL(env, napi_get_named_property(env, args[0], "resourcesPath", &v)); resourcesPath = get_string(env, v); }
      NAPI_CALL(env, napi_has_named_property(env, args[0], "verbose", &has));
      if (has) { NAPI_CALL(env, napi_get_named_property(env, args[0], "verbose", &v)); (void)get_bool(env, v, &verbose); }
    }
  }

  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { napi_throw_error(env, nullptr, err.c_str()); return nullptr; }
  orcacli_operation_result r = g_ffi.initialize(g_ffi.inst, resourcesPath.c_str());
  if (!r.success) {
    std::string msg = r.message ? r.message : "initialize failed";
    if (g_ffi.free_result) g_ffi.free_result(&r);
    napi_throw_error(env, nullptr, msg.c_str());
    return nullptr;
  }
  if (g_ffi.free_result) g_ffi.free_result(&r);
  napi_value undef; NAPI_CALL(env, napi_get_undefined(env, &undef)); return undef;
}

// version(): string
static napi_value Version(napi_env env, napi_callback_info info) {
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { napi_throw_error(env, nullptr, err.c_str()); return nullptr; }
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
  auto r = g_ffi.load_model(g_ffi.inst, w->file.c_str());
  if (!r.success) { w->err = r.message ? r.message : "loadModel failed"; if (g_ffi.free_result) g_ffi.free_result(&r); return; }
  if (g_ffi.free_result) g_ffi.free_result(&r);
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

  auto* work = new InfoWork(); work->file = std::move(file);
  napi_value promise; NAPI_CALL(env, napi_create_promise(env, &work->deferred, &promise));
  napi_value resource_name; napi_create_string_utf8(env, "getModelInfo", NAPI_AUTO_LENGTH, &resource_name);
  NAPI_CALL(env, napi_create_async_work(env, nullptr, resource_name, InfoExecute, InfoComplete, work, &work->work));
  NAPI_CALL(env, napi_queue_async_work(env, work->work));
  return promise;
}

// slice(params): Promise<{output: string}>
struct SliceWork { napi_async_work work; napi_deferred deferred; struct { std::string input_file; std::string output_file; std::string printer_profile; std::string filament_profile; std::string process_profile; int plate_index=1; bool verbose=false; bool dry_run=false; } p; std::string err; };

static void SliceExecute(napi_env env, void* data) {
  SliceWork* w = static_cast<SliceWork*>(data);
  std::lock_guard<std::mutex> lk(g_mutex);
  std::string err;
  if (!ensure_engine_loaded(&err)) { w->err = err; return; }
  orcacli_slice_params p{};
  p.input_file = w->p.input_file.c_str();
  p.output_file = w->p.output_file.c_str();
  p.printer_profile = w->p.printer_profile.empty()?nullptr:w->p.printer_profile.c_str();
  p.filament_profile = w->p.filament_profile.empty()?nullptr:w->p.filament_profile.c_str();
  p.process_profile = w->p.process_profile.empty()?nullptr:w->p.process_profile.c_str();
  p.plate_index = w->p.plate_index;
  p.verbose = w->p.verbose;
  p.dry_run = w->p.dry_run;
  if (w->p.verbose) { fprintf(stderr, "DEBUG: [addon] calling g_ffi.slice input='%s' plate=%d\n", p.input_file ? p.input_file : "(null)", p.plate_index); fflush(stderr); }
  auto r = g_ffi.slice(g_ffi.inst, &p);
  if (w->p.verbose) { fprintf(stderr, "DEBUG: [addon] returned from g_ffi.slice (success=%d)\n", (int)r.success); fflush(stderr); }
  if (!r.success) w->err = r.message ? r.message : "slice failed";
  if (g_ffi.free_result) g_ffi.free_result(&r);
}

static void SliceComplete(napi_env env, napi_status status, void* data) {
  SliceWork* w = static_cast<SliceWork*>(data);
  if (status != napi_ok) { napi_value e; napi_create_string_utf8(env, "Async failure", NAPI_AUTO_LENGTH, &e); napi_reject_deferred(env, w->deferred, e); }
  else if (!w->err.empty()) { napi_value e; napi_create_string_utf8(env, w->err.c_str(), NAPI_AUTO_LENGTH, &e); napi_reject_deferred(env, w->deferred, e); }
  else { napi_value obj, v; napi_create_object(env, &obj); napi_create_string_utf8(env, w->p.output_file.c_str(), NAPI_AUTO_LENGTH, &v); napi_set_named_property(env, obj, "output", v); napi_resolve_deferred(env, w->deferred, obj);}
  napi_delete_async_work(env, w->work); delete w;
}

static napi_value Slice(napi_env env, napi_callback_info info) {
  size_t argc = 1; napi_value args[1]; napi_value thisArg; void* data; NAPI_CALL(env, napi_get_cb_info(env, info, &argc, args, &thisArg, &data));
  if (argc < 1) { napi_throw_type_error(env, nullptr, "params object is required"); return nullptr; }
  napi_value obj = args[0]; napi_valuetype t; NAPI_CALL(env, napi_typeof(env, obj, &t)); if (t != napi_object) { napi_throw_type_error(env, nullptr, "params must be object"); return nullptr; }

  auto* work = new SliceWork();
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
        if (vt == napi_number) { double d=0; napi_get_value_double(env, v, &d); dst = (int)d; }
      }
    }
  };
  auto set_bool = [&](const char* key, bool& dst){ bool has=false; napi_value v; napi_has_named_property(env, obj, key, &has); if(has){ napi_get_named_property(env, obj, key, &v); get_bool(env, v, &dst);} };

  set_str("input", work->p.input_file);
  set_str("output", work->p.output_file);
  set_str("printerProfile", work->p.printer_profile);
  set_str("filamentProfile", work->p.filament_profile);
  set_str("processProfile", work->p.process_profile);
  set_int("plate", work->p.plate_index);
  set_bool("verbose", work->p.verbose);
  set_bool("dryRun", work->p.dry_run);
  if (work->p.verbose) {
    fprintf(stderr, "DEBUG: [addon] Slice() scheduling: input='%s' output='%s' plate=%d\n",
            work->p.input_file.c_str(), work->p.output_file.c_str(), work->p.plate_index);
    fflush(stderr);
  }


  if (work->p.input_file.empty()) { delete work; napi_throw_type_error(env, nullptr, "params.input is required"); return nullptr; }

  napi_value promise; NAPI_CALL(env, napi_create_promise(env, &work->deferred, &promise));
  napi_value resource_name; napi_create_string_utf8(env, "slice", NAPI_AUTO_LENGTH, &resource_name);
  NAPI_CALL(env, napi_create_async_work(env, nullptr, resource_name, SliceExecute, SliceComplete, work, &work->work));
  NAPI_CALL(env, napi_queue_async_work(env, work->work));
  return promise;
}

// shutdown(): cleans up engine state deterministically
static napi_value Shutdown(napi_env env, napi_callback_info info) {
  (void)info;
  std::lock_guard<std::mutex> lk(g_mutex);
  if (g_ffi.inst && g_ffi.destroy) {
    try { g_ffi.destroy(g_ffi.inst); } catch (...) {}
    g_ffi.inst = nullptr;
  }
  // Keep the library handle loaded; subsequent initialize can reuse it.
  napi_value undef; napi_get_undefined(env, &undef); return undef;
}

static napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor props[] = {
    {"initialize", 0, Initialize, 0, 0, 0, napi_default, 0},
    {"shutdown",   0, Shutdown,   0, 0, 0, napi_default, 0},
    {"version",    0, Version,    0, 0, 0, napi_default, 0},
    {"getModelInfo", 0, GetModelInfo, 0, 0, 0, napi_default, 0},
    {"slice",      0, Slice,      0, 0, 0, napi_default, 0},
  };
  NAPI_CALL(env, napi_define_properties(env, exports, sizeof(props)/sizeof(props[0]), props));
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

