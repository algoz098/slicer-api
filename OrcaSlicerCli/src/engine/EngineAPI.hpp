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
    const char* printer_profile;  // optional
    const char* filament_profile; // optional
    const char* process_profile;  // optional
    int32_t     plate_index;      // 1-based
    bool        verbose;
    bool        dry_run;
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

// Metadata
const char* orcacli_version(); // static string, no free required

// Memory management helpers
void orcacli_free_string(const char* s);
void orcacli_free_model_info(orcacli_model_info* mi);
void orcacli_free_result(orcacli_operation_result* r);

#ifdef __cplusplus
} // extern "C"
#endif

