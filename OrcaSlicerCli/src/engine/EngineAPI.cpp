#include "EngineAPI.hpp"

#include <string>
#include <memory>
#include <cstdlib>

#include <iostream>

#include "core/CliCore.hpp"
#include "Application.hpp"
namespace Slic3r { unsigned int level_string_to_boost(std::string level); void set_logging_level(unsigned int level); }


using OrcaSlicerCli::CliCore;

namespace {
struct Engine {
    CliCore core;
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
        return new Engine();
    } catch (...) {
        return nullptr;
    }
}

void orcacli_destroy(orcacli_handle h) {
    if (!h) return;
    Engine* e = static_cast<Engine*>(h);
    try { e->core.shutdown(); } catch (...) {}
    delete e;
}

static orcacli_operation_result make_result(const OrcaSlicerCli::CliCore::OperationResult& r) {
    orcacli_operation_result o{};
    o.success = r.success;
    o.message = r.message.empty() ? nullptr : dup_cstr(r.message);
    o.error_details = r.error_details.empty() ? nullptr : dup_cstr(r.error_details);
    return o;
}

orcacli_operation_result orcacli_initialize(orcacli_handle h, const char* resources_path) {
    if (!h) {
        return orcacli_operation_result{false, dup_cstr("invalid handle"), nullptr};
    }

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

    Engine* e = static_cast<Engine*>(h);
    auto res = e->core.initialize(resources_path ? std::string(resources_path) : std::string());
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
    // Early diagnostic logging to catch pre-core crashes
    if (params && params->verbose) {
        try {
            const char* in = (params && params->input_file) ? params->input_file : "(null)";
            int plate = params ? params->plate_index : -1;
            std::cout << "DEBUG: [C API] orcacli_slice enter: input='" << in << "' plate=" << plate << std::endl;
        } catch (...) { /* ignore logging failures */ }
    }
    if (!h || !params) {
        return orcacli_operation_result{false, dup_cstr("invalid args"), nullptr};
    }
    Engine* e = static_cast<Engine*>(h);
    CliCore::SlicingParams p;
    if (params->input_file)   p.input_file = params->input_file;
    if (params->output_file)  p.output_file = params->output_file;
    if (params->config_file)  p.config_file = params->config_file;
    if (params->preset_name)  p.preset_name = params->preset_name;
    if (params->printer_profile)  p.printer_profile = params->printer_profile;
    if (params->filament_profile) p.filament_profile = params->filament_profile;
    if (params->process_profile)  p.process_profile = params->process_profile;
    p.plate_index = params->plate_index;
    p.verbose = params->verbose;
    p.dry_run = params->dry_run;
    auto res = e->core.slice(p);
    return make_result(res);
}

#ifndef ORCACLI_VERSION_STRING
#define ORCACLI_VERSION_STRING "0.0.0-dev"
#endif
const char* orcacli_version() {
    return ORCACLI_VERSION_STRING;
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

