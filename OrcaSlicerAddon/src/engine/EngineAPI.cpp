#include "EngineAPI.hpp"

#include <string>
#include <memory>
#include <cstdlib>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/AddonCore.hpp"
#include "utils/Logger.hpp"
#ifdef HAVE_LIBSLIC3R
namespace Slic3r { unsigned int level_string_to_boost(std::string level); void set_logging_level(unsigned int level); }
#endif


using OrcaSlicerCli::AddonCore;

namespace {
struct Engine {
    AddonCore core;
};

static char* dup_cstr(const std::string& s) {
    char* out = (char*)std::malloc(s.size() + 1);
    if (!out) return nullptr;
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}
}

extern "C" {

orcacli_handle orcacli_create() {
    try {
        Engine* engine = new Engine();
        LOG_DEBUG("orcacli_create: engine created");
        return engine;
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("orcacli_create: failed: ") + e.what());
        return nullptr;
    } catch (...) {
        LOG_ERROR("orcacli_create: failed: unknown error");
        return nullptr;
    }
}

void orcacli_destroy(orcacli_handle h) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    try { e->core.shutdown(); } catch (...) {}
    delete e;
}

static orcacli_operation_result make_result(const OrcaSlicerCli::AddonCore::OperationResult& r) {
    orcacli_operation_result o{};
    o.success = r.success;
    o.message = r.message.empty() ? nullptr : dup_cstr(r.message);
    o.error_details = r.error_details.empty() ? nullptr : dup_cstr(r.error_details);
    // propagate native stats
    o.estimated_time_sec = r.estimated_time_sec;
    o.filament_used_grams = r.filament_used_grams;
    return o;
}

orcacli_operation_result orcacli_initialize(orcacli_handle h, const char* resources_path) {
    LOG_DEBUG(std::string("orcacli_initialize: resources_path=") + (resources_path ? resources_path : "(null)"));

    if (!h) {
        return orcacli_operation_result{false, dup_cstr("invalid handle"), nullptr};
    }

#ifdef HAVE_LIBSLIC3R
    // Configure libslic3r logging level from environment.
    try {
        unsigned int level = 1; // default to 'error'
        const char* q = std::getenv("ORCACLI_QUIET");
        const char* lvl = std::getenv("ORCACLI_LOG_LEVEL");
        if (lvl && *lvl) {
            std::string s(lvl);
            bool all_digits = !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
            if (all_digits) {
                long v = std::strtol(lvl, nullptr, 10);
                if (v < 0) v = 0; if (v > 5) v = 5;
                level = static_cast<unsigned int>(v);
            } else {
                // accept strings: fatal,error,warning,info,debug,trace
                for (auto &c : s) c = (char)std::tolower((unsigned char)c);
                level = Slic3r::level_string_to_boost(s);
            }
        }
        if (q && *q && std::string(q) != "0") level = 1; // quiet => errors only
        Slic3r::set_logging_level(level);
    } catch (...) { /* ignore logging setup errors */ }
#endif

    Engine* e = static_cast<Engine*>(h);
    auto res = e->core.initialize(resources_path ? std::string(resources_path) : std::string());
    LOG_DEBUG(std::string("orcacli_initialize: success=") + (res.success ? "1" : "0") + " msg='" + res.message + "'");

    return make_result(res);
}

orcacli_operation_result orcacli_load_model(orcacli_handle h, const char* filename) {
    if (!h || !filename) {
        return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    }
    Engine* e = static_cast<Engine*>(h);
    auto res = e->core.loadModel(filename);
    return make_result(res);
}

orcacli_model_info orcacli_get_model_info(orcacli_handle h) {
    orcacli_model_info out{};
    if (!h) return out;
    Engine* e = static_cast<Engine*>(h);
    auto mi = e->core.getModelInfo();
    out.filename = dup_cstr(mi.filename);
    out.object_count = (uint32_t)mi.object_count;
    out.triangle_count = (uint32_t)mi.triangle_count;
    out.volume = mi.volume;
    out.bounding_box = dup_cstr(mi.bounding_box);
    out.is_valid = mi.is_valid;
    return out;
}

orcacli_operation_result orcacli_slice(orcacli_handle h, const orcacli_slice_params* params) {
    if (!h || !params) {
        return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    }

    if (params->verbose) {
        LOG_DEBUG(std::string("orcacli_slice: input='") + (params->input_file ? params->input_file : "(null)") +
                  "' plate=" + std::to_string(params->plate_index));
    }

    Engine* e = static_cast<Engine*>(h);
    AddonCore::SlicingParams p;
    if (params->input_file)   p.input_file = params->input_file;
    if (params->output_file)  p.output_file = params->output_file;
    if (params->config_file)  p.config_file = params->config_file;
    if (params->preset_name)  p.preset_name = params->preset_name;
    // Display names for profiles in output 3MF (metadata only)
    if (params->printer_profile_name)  p.printer_profile_name = params->printer_profile_name;
    if (params->filament_profile_name) p.filament_profile_name = params->filament_profile_name;
    if (params->process_profile_name)  p.process_profile_name = params->process_profile_name;
    p.plate_index = params->plate_index;
    p.verbose = params->verbose;
    p.dry_run = params->dry_run;
    // Forward 3MF transfer flags
    p.transfer_printer_customizations  = params->transfer_printer_customizations;
    p.transfer_filament_customizations = params->transfer_filament_customizations;
    p.transfer_process_customizations  = params->transfer_process_customizations;
    p.transfer_project_overrides       = params->transfer_project_overrides;
    // Behavior flags
    p.center_on_bed = params->center_on_bed;
    p.auto_realign_if_needed = params->auto_realign_if_needed;
    // Forward overrides into SlicingParams.custom_settings
    if (params->overrides && params->overrides_count > 0) {
        if (params->verbose) {
            LOG_DEBUG(std::string("orcacli_slice: overrides_count=") + std::to_string(params->overrides_count));
        }
        for (int32_t i = 0; i < params->overrides_count; ++i) {
            const orcacli_kv& kv = params->overrides[i];
            if (kv.key && kv.value) {
                if (params->verbose) {
                    LOG_DEBUG(std::string("orcacli_slice: override[") + std::to_string(i) + "]: '" + kv.key + "'='" + kv.value + "'");
                }
                p.custom_settings[std::string(kv.key)] = std::string(kv.value);
            }
        }
    }

    auto res = e->core.slice(p);
    return make_result(res);
}

orcacli_operation_result orcacli_load_vendor(orcacli_handle h, const char* vendor_id) {
    if (!h || !vendor_id) return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    Engine* e = static_cast<Engine*>(h);
    return make_result(e->core.loadVendor(vendor_id));
}

orcacli_operation_result orcacli_load_printer_profile(orcacli_handle h, const char* name) {
    if (!h || !name) return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    Engine* e = static_cast<Engine*>(h);
    return make_result(e->core.loadPrinterProfile(name));
}

orcacli_operation_result orcacli_load_filament_profile(orcacli_handle h, const char* name) {
    if (!h || !name) return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    Engine* e = static_cast<Engine*>(h);
    return make_result(e->core.loadFilamentProfile(name));
}

orcacli_operation_result orcacli_load_process_profile(orcacli_handle h, const char* name) {
    if (!h || !name) return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    Engine* e = static_cast<Engine*>(h);
    return make_result(e->core.loadProcessProfile(name));
}

#ifndef ORCACLI_VERSION_STRING
#define ORCACLI_VERSION_STRING "0.0.0-dev"
#endif
const char* orcacli_version() {
    return ORCACLI_VERSION_STRING;
}

void orcacli_set_logging_silenced(bool silent) {
    try {
        AddonCore::setLoggingSilenced(silent);
    } catch (...) { /* ignore */ }
#ifdef HAVE_LIBSLIC3R
    // Also tone down libslic3r logger when silenced (errors only)
    try { if (silent) Slic3r::set_logging_level(1); } catch (...) {}
#endif
}

void orcacli_free_string(const char* s) {
    if (s) std::free((void*)s);
}

void orcacli_free_model_info(orcacli_model_info* mi) {
    if (!mi) return;
    if (mi->filename) orcacli_free_string(mi->filename);
    if (mi->bounding_box) orcacli_free_string(mi->bounding_box);
    mi->filename = nullptr;
    mi->bounding_box = nullptr;
}

void orcacli_free_result(orcacli_operation_result* r) {
    if (!r) return;
    if (r->message) orcacli_free_string(r->message);
    if (r->error_details) orcacli_free_string(r->error_details);
    r->message = nullptr;
    r->error_details = nullptr;
}

} // extern "C"
