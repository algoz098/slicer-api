#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for an engine instance
typedef void* orcacli_handle;

// Result type for operations
typedef struct {
    bool success;
    const char* message;        // optional; owned by library; free with orcacli_free_string or orcacli_free_result
    const char* error_details;  // optional; owned by library; free with orcacli_free_string or orcacli_free_result
    // Native engine statistics (present on successful slice if libslic3r available)
    double estimated_time_sec;   // <0 if not available
    double filament_used_grams;  // <0 if not available
} orcacli_operation_result;

// Model info for validation/introspection
typedef struct {
    const char* filename;     // owned by library; free via orcacli_free_model_info
    uint32_t object_count;
    uint32_t triangle_count;
    double   volume;
    const char* bounding_box; // owned by library; free via orcacli_free_model_info
    bool     is_valid;
} orcacli_model_info;

// Key/value override pair for config options
typedef struct {
    const char* key;   // non-owning pointer
    const char* value; // non-owning pointer
} orcacli_kv;

// Slicing parameters
typedef struct {
    const char* input_file;
    const char* output_file;
    const char* config_file;      // optional
    const char* preset_name;      // optional
    int32_t     plate_index;      // 1-based
    bool        verbose;
    bool        dry_run;
    // Behavior flags
    bool        center_on_bed;
    bool        auto_realign_if_needed; // realinha automaticamente na mesa se necess2rio
    // Base profile settings (applied before 3MF load; 3MF settings override these).
    // Use this for process/printer/filament profile defaults sent on-the-fly.
    const orcacli_kv* profile;        // optional
    int32_t     profile_count;        // number of entries in profile
    // Explicit user overrides (applied after 3MF load; highest priority, override 3MF settings).
    const orcacli_kv* overrides;      // optional
    int32_t     overrides_count;      // number of entries in overrides
} orcacli_slice_params;

// Lifecycle
orcacli_handle orcacli_create();
void orcacli_destroy(orcacli_handle h);

// Operations
orcacli_operation_result orcacli_initialize(orcacli_handle h, const char* resources_path);
orcacli_operation_result orcacli_load_model(orcacli_handle h, const char* filename);
orcacli_model_info       orcacli_get_model_info(orcacli_handle h);
orcacli_operation_result orcacli_slice(orcacli_handle h, const orcacli_slice_params* params);

// Vendor lazy loading (must be called after orcacli_initialize)
orcacli_operation_result orcacli_load_vendor(orcacli_handle h, const char* vendor_id);

// Metadata
const char* orcacli_version(); // static string, no free required

// Global logging control (toggle stdout/stderr redirection without touching orcaslicer/ sources)
void orcacli_set_logging_silenced(bool silent);

// Memory management helpers
void orcacli_free_string(const char* s);
void orcacli_free_model_info(orcacli_model_info* mi);
void orcacli_free_result(orcacli_operation_result* r);

#ifdef __cplusplus
} // extern "C"
#endif

