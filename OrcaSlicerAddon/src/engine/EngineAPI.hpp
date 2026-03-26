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
    // Display names for profiles in output 3MF (metadata only, does not load any preset)
    const char* printer_profile_name;  // optional - e.g. "My Printer" - sets printer_settings_id
    const char* filament_profile_name; // optional - e.g. "Generic PLA" - sets filament_settings_id
    const char* process_profile_name;  // optional - e.g. "High Quality" - sets print_settings_id
    int32_t     plate_index;      // 1-based
    bool        verbose;
    bool        dry_run;
    // 3MF transfer flags (all default to true if not provided by caller)
    bool        transfer_printer_customizations;
    bool        transfer_filament_customizations;
    bool        transfer_process_customizations;
    bool        transfer_project_overrides;
    // Behavior flags
    bool        center_on_bed;
    bool        auto_realign_if_needed; // realinha automaticamente na mesa se necess2rio
    // Optional config overrides (applied after profiles). The memory is owned by caller and must live through the call.
    const orcacli_kv* overrides;  // optional
    int32_t     overrides_count;  // number of entries in overrides
} orcacli_slice_params;

// Lifecycle
orcacli_handle orcacli_create();
void orcacli_destroy(orcacli_handle h);

// Operations
orcacli_operation_result orcacli_initialize(orcacli_handle h, const char* resources_path);
orcacli_operation_result orcacli_load_model(orcacli_handle h, const char* filename);
orcacli_model_info       orcacli_get_model_info(orcacli_handle h);
orcacli_operation_result orcacli_slice(orcacli_handle h, const orcacli_slice_params* params);

// Profile/vendor lazy loading (must be called after orcacli_initialize)
orcacli_operation_result orcacli_load_vendor(orcacli_handle h, const char* vendor_id);
orcacli_operation_result orcacli_load_printer_profile(orcacli_handle h, const char* name);
orcacli_operation_result orcacli_load_filament_profile(orcacli_handle h, const char* name);
orcacli_operation_result orcacli_load_process_profile(orcacli_handle h, const char* name);

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

