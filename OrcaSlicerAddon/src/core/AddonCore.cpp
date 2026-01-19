#include "AddonCore.hpp"
#include "core/util/Utilities.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <exception>
#include <cmath>
#include <chrono>

#include <algorithm>
#include <cctype>

#include <set>

#include <optional>

#include <string>
#include <vector>
#include <libslic3r/Arrange.hpp>
#include <libslic3r/ModelArrange.hpp>

#include <limits>
#include <cstdlib>
#include <cstdint>

#include <array>
#include <map>

// Signal handling for segfault debugging
#include <csignal>
#include <cstring>
#include <execinfo.h>
#include <unistd.h>

#include <fcntl.h>


#if !HAVE_LIBSLIC3R
#error "libslic3r is required. Placeholders are not allowed."
#endif


#if HAVE_LIBSLIC3R


// Global silent switch (shared with JS env)
static bool addon_is_silent() {
    const char* v = std::getenv("ORCA_ADDON_LOG");
    const char* s = std::getenv("ORCACLI_SILENT");
    return (v && std::string(v) == "off") || (s && std::string(s) == "1");
}

// Runtime-configurable global IO silencer. Does NOT touch orcaslicer/ sources.
// It redirects both C++ iostreams and POSIX fds to/from /dev/null and can be toggled on/off.
namespace {
    static bool s_silenced = false;
    static std::streambuf* s_orig_cout = nullptr;
    static std::streambuf* s_orig_cerr = nullptr;
    static std::ofstream   s_devnull_stream; // keep open while silenced
    static int             s_saved_stdout = -1;
    static int             s_saved_stderr = -1;
    static int             s_devnull_fd   = -1;
}

void OrcaSlicerCli::AddonCore::setLoggingSilenced(bool silent) {
    if (silent == s_silenced) return;

    if (silent) {
        // Open /dev/null once
        try {
            if (!s_devnull_stream.is_open()) s_devnull_stream.open("/dev/null");
        } catch (...) {}
        if (s_devnull_fd < 0) { s_devnull_fd = ::open("/dev/null", O_WRONLY); }
        // Save originals once
        if (!s_orig_cout) s_orig_cout = std::cout.rdbuf();
        if (!s_orig_cerr) s_orig_cerr = std::cerr.rdbuf();
        if (s_saved_stdout < 0) s_saved_stdout = ::dup(STDOUT_FILENO);
        if (s_saved_stderr < 0) s_saved_stderr = ::dup(STDERR_FILENO);
        // Redirect
        try { std::cout.rdbuf(s_devnull_stream.rdbuf()); } catch (...) {}
        try { std::cerr.rdbuf(s_devnull_stream.rdbuf()); } catch (...) {}
        if (s_devnull_fd >= 0) {
            ::dup2(s_devnull_fd, STDOUT_FILENO);
            ::dup2(s_devnull_fd, STDERR_FILENO);
        }
        s_silenced = true;
    } else {
        // Restore C++ streams
        if (s_orig_cout) { try { std::cout.rdbuf(s_orig_cout); } catch (...) {} }
        if (s_orig_cerr) { try { std::cerr.rdbuf(s_orig_cerr); } catch (...) {} }
        // Restore POSIX fds
        if (s_saved_stdout >= 0) { ::dup2(s_saved_stdout, STDOUT_FILENO); ::close(s_saved_stdout); s_saved_stdout = -1; }
        if (s_saved_stderr >= 0) { ::dup2(s_saved_stderr, STDERR_FILENO); ::close(s_saved_stderr); s_saved_stderr = -1; }
        s_silenced = false;
    }
}

// OrcaSlicer includes
#include <algorithm>
#include <cctype>
#include <clocale>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/3mf.hpp"

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"
    #include "libslic3r/Geometry.hpp"
    #include "libslic3r/BuildVolume.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "libslic3r/Layer.hpp"

#endif
#if HAVE_LIBSLIC3R
#include "core/plate/PlateCentering.hpp"
#include "core/init/Initialization.hpp"
#include "core/model/ModelIO.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/slice/SliceEngine.hpp"
#include "core/util/Utilities.hpp"
using OrcaSlicerCli::util::dbg_log;
#if HAVE_LIBSLIC3R
using OrcaSlicerCli::util::bed_temp_key_for;
#endif


#endif


#if HAVE_LIBSLIC3R
// bed_temp_key_for centralized in core/util/Utilities
#endif


// dbg_log centralized in core/util/Utilities

// Helper: Sanitize a DynamicPrintConfig to ensure all options have compatible types.
// This prevents "Comparing incompatible types" errors when print->apply() compares configs.
// For each key, we check if the type matches what's expected by the print_config_def.
// If not, we try to convert by serializing/deserializing, or we remove the problematic key.
static void sanitize_config_types(Slic3r::DynamicPrintConfig& cfg) {
    const Slic3r::ConfigDef& def = Slic3r::print_config_def;

    std::vector<std::string> keys_to_fix;

    for (const auto& key : cfg.keys()) {
        const Slic3r::ConfigOption* opt = cfg.optptr(key);
        if (!opt) continue;

        const Slic3r::ConfigOptionDef* opt_def = def.get(key);
        if (!opt_def) {
            // Key not in definition - may be custom, skip
            continue;
        }

        if (opt->type() != opt_def->type) {
            keys_to_fix.push_back(key);
        }
    }

    for (const auto& key : keys_to_fix) {
        try {
            const Slic3r::ConfigOption* opt = cfg.optptr(key);
            const Slic3r::ConfigOptionDef* opt_def = def.get(key);
            if (!opt || !opt_def) continue;

            // Serialize and deserialize to convert type
            std::string serialized = opt->serialize();
            std::cout << "DEBUG: sanitize_config_types: fixing type for '" << key
                      << "' (type " << (int)opt->type() << " -> " << (int)opt_def->type
                      << ") value='" << serialized << "'" << std::endl;

            // Create new option with correct type and deserialize
            Slic3r::ConfigOption* new_opt = opt_def->create_default_option();
            if (new_opt) {
                try {
                    new_opt->deserialize(serialized, Slic3r::ForwardCompatibilitySubstitutionRule::Enable);
                    cfg.set_key_value(key, new_opt);
                } catch (...) {
                    delete new_opt;
                    // If conversion fails, erase the key
                    cfg.erase(key);
                    std::cout << "DEBUG: sanitize_config_types: removed key '" << key << "' (conversion failed)" << std::endl;
                }
            }
        } catch (...) {
            try { cfg.erase(key); } catch (...) {}
        }
    }

    if (!keys_to_fix.empty()) {
        std::cout << "DEBUG: sanitize_config_types: fixed " << keys_to_fix.size() << " type mismatches" << std::endl;
    }
}


namespace OrcaSlicerCli {



/**
 * @brief Private implementation class for AddonCore
 */
class AddonCore::Impl {
public:
    bool initialized = false;
    std::string resources_path;
    int plate_id = 0; // 0-based plate index for .3mf projects

    // Slice-time flags controlling how 3MF customizations are transferred
    bool transfer_printer_customizations = true;
    bool transfer_filament_customizations = true;
    bool transfer_process_customizations = true;
    bool transfer_project_overrides = true;
    // Behavior flags
    bool center_on_bed = false;
    bool auto_realign_if_needed = false;

    // Multi-material detection
    size_t detected_extruders = 0;
    std::vector<std::string> saved_filament_colours;  // Preserve 3MF colors from preset overwrites
    std::string saved_change_filament_gcode;  // Preserve 3MF change_filament_gcode (critical for Bambu AMS)

    std::string last_error;
    // Last computed native statistics (reset each slice attempt)
    double last_estimated_time_sec = -1.0;
    double last_filament_used_grams = -1.0;

#if HAVE_LIBSLIC3R
    std::unique_ptr<Slic3r::Model> model;
    std::unique_ptr<Slic3r::Print> print;
    std::unique_ptr<Slic3r::DynamicPrintConfig> config;
    // Whether current 3MF contains embedded presets (print/filament/printer) imported from GUI
    bool has_project_embedded_presets = false;

    // Important: app_config must be destroyed after preset_bundle
    Slic3r::AppConfig app_config;
    Slic3r::PresetBundle preset_bundle;
    std::set<std::string> loaded_vendors;
    // Preset names embedded in a 3MF project (if any). Used for auto-apply when no CLI presets are provided.
    std::string project_printer_preset;
    std::string project_print_preset;
    std::string project_filament_preset;
    // Custom display names for profiles in output 3MF (set via SlicingParams)
    // These override the default/project names for metadata display only
    std::string custom_printer_profile_name;
    std::string custom_filament_profile_name;
    std::string custom_process_profile_name;
        // Snapshot of 3MF project-level parameter overrides and their keys (detected during load)
        Slic3r::DynamicPrintConfig      project_cfg_after_3mf;
        Slic3r::t_config_option_keys    project_overrides_keys;
        // Snapshot of 3MF print-level overrides (differences against selected base print preset)
        Slic3r::DynamicPrintConfig      print_cfg_overrides;
        Slic3r::t_config_option_keys    print_overrides_keys;

        // Plate-derived hints from BBL 3MF metadata
        std::string plate_printer_model_id;   // e.g., "A1", "X1C", etc.
        std::string plate_nozzle_variant;     // e.g., "0.4"
        // Total number of plates in current 3MF project (0 if not a 3MF or unknown)
        int total_plates_count = 0;
        // Plate data loaded from 3MF (contains per-plate config)
        Slic3r::PlateDataPtrs plate_data_src;


#endif

    Impl() = default;
    ~Impl() = default; // Cleanup is performed explicitly via AddonCore::shutdown()


    #if HAVE_LIBSLIC3R
        // Compute and set plate_origin from model instances (assembly offsets) so that G-code is plate-local.
        bool compute_and_set_plate_origin_from_model_instances()
        {
            return OrcaSlicerCli::plate::compute_and_set_plate_origin_from_model_instances(model.get(), print.get(), config.get());
        }

        // Center currently loaded plate content onto the bed center by adjusting plate_origin
        bool center_plate_origin_to_bed_center()
        {
            return OrcaSlicerCli::plate::center_plate_origin_to_bed_center(model.get(), print.get(), config.get());
        }

        // Shift instances so their center aligns with bed center (used for non-BBL vendors when center_on_bed=true)
        bool center_instances_on_bed_center()
        {
            return OrcaSlicerCli::plate::center_instances_on_bed_center(model.get(), config.get());
        }



        // Normalize model instances into plate-local coordinates by removing the logical grid stride.
        bool normalize_model_instances_to_plate_local()
        {
            return OrcaSlicerCli::plate::normalize_model_instances_to_plate_local(model.get(), config.get());
        }
    #endif

    void cleanup() {
#if HAVE_LIBSLIC3R
        OrcaSlicerCli::init::cleanup(print, model, config);
#endif
    }



    bool initializeSlic3r(const std::string& resources_path) {
        try {
            this->resources_path = resources_path;
#if HAVE_LIBSLIC3R
            return OrcaSlicerCli::init::initialize_slic3r(resources_path, app_config, preset_bundle,
                                                          loaded_vendors, config, model, print, last_error);
#else
            last_error = "libslic3r not available";
            return false;
#endif
        } catch (const std::exception& e) {
            last_error = std::string("Failed to initialize: ") + e.what();
            return false;
        }
    }

    bool loadModelFromFile(const std::string& filename) {
        if (!std::filesystem::exists(filename)) {
            last_error = "File not found: " + filename;
            return false;
        }

        std::filesystem::path file_path(filename);
        std::cout << "DEBUG: loadModelFromFile: '" << filename << "' ext='" << file_path.extension().string() << "' plate_id=" << plate_id << std::endl;
        std::string extension = file_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (extension != ".3mf" && extension != ".stl" && extension != ".obj") {
            last_error = "Unsupported file format: " + extension;
            return false;
        }

#if HAVE_LIBSLIC3R
        try {
            // Clear existing model
            model->clear_objects();

            // Load model based on extension
            if (extension == ".stl") {
                if (!OrcaSlicerCli::model::load_stl(filename, *model, last_error)) {
                    return false;
                }
            } else if (extension == ".3mf") {
                if (!OrcaSlicerCli::model::load_3mf_project(
                        filename,
                        plate_id,
                        *model,
                        *config,
                        preset_bundle,
                        plate_data_src,
                        has_project_embedded_presets,
                        project_printer_preset,
                        project_print_preset,
                        project_filament_preset,
                        plate_printer_model_id,
                        plate_nozzle_variant,
                        total_plates_count,
                        this->detected_extruders,
                        this->saved_filament_colours,
                        this->saved_change_filament_gcode,
                        project_cfg_after_3mf,
                        project_overrides_keys,
                        print_cfg_overrides,
                        print_overrides_keys,
                        transfer_printer_customizations,
                        transfer_filament_customizations,
                        transfer_process_customizations,
                        transfer_project_overrides,
                        last_error)) {
                    return false;
                }
            }






            // GUI parity: do not normalize instances here. Use only plate_origin for plate-local coordinates.
            // Keep instances in assembly space and apply the offset only during G-code export.
            std::cout << "DEBUG: 3MF project preset names captured: printer='" << project_printer_preset
                      << "', print='" << project_print_preset
                      << "', filament='" << project_filament_preset << "'" << std::endl;

            // Ensure model has objects and default instances
            if (!OrcaSlicerCli::model::ensure_default_instances(*model, last_error)) {
                return false;
            }

            return true;

	        }

        catch (const std::exception& e) {
            last_error = std::string("Error loading model: ") + e.what();
            return false;

        }
#else

        last_error = "libslic3r not available";
        return false;
#endif
    }

    // Signal handler for segmentation faults
    static void segfault_handler(int sig) {
        std::cerr << "\n========================================" << std::endl;
        std::cerr << "SEGMENTATION FAULT CAUGHT!" << std::endl;
        std::cerr << "Signal: " << sig << std::endl;
        std::cerr << "========================================" << std::endl;

        // Print stack trace
        void* array[50];
        size_t size = backtrace(array, 50);

        std::cerr << "Stack trace (" << size << " frames):" << std::endl;
        char** symbols = backtrace_symbols(array, size);

        if (symbols) {
            for (size_t i = 0; i < size; i++) {
                std::cerr << "  [" << i << "] " << symbols[i] << std::endl;
            }
            free(symbols);
        } else {
            std::cerr << "  (Unable to get symbols)" << std::endl;
        }

        std::cerr << "========================================" << std::endl;
        std::cerr << "This crash occurs at 71% during 'Detect overhangs for auto-lift'" << std::endl;
        std::cerr << "after copy_layers_overhang_from_shared_object() completes." << std::endl;
        std::cerr << "========================================" << std::endl;

        // Exit with error code
        _exit(1);
    }

    bool performSlicing(const std::string& output_file) {
#if HAVE_LIBSLIC3R
        // Install signal handler for segfaults
        signal(SIGSEGV, segfault_handler);
        signal(SIGABRT, segfault_handler);

        try {
            std::cout << "🔍 [TRACE 32] INSIDE performSlicing()" << std::endl;

            // reset last-known stats at the beginning of a slicing run
            last_estimated_time_sec = -1.0;
            last_filament_used_grams = -1.0;
            if (!model || model->objects.empty()) {
                last_error = "No model loaded for slicing";
                return false;
            }

            std::cout << "DEBUG: Starting slicing process..." << std::endl;
            std::cout << "DEBUG: Model has " << model->objects.size() << " objects" << std::endl;
            std::cout << "DEBUG: Config is " << (config ? "valid" : "null") << std::endl;
            std::cout << "DEBUG: Print is " << (print ? "valid" : "null") << std::endl;

            // CRITICAL: Check multi-material config at the start of performSlicing
            std::cout << "🔍 [TRACE 33] Config check inside performSlicing:" << std::endl;
            if (config) {
                // DEBUG: Check printable_area at start of performSlicing
                auto* pa_start = config->opt<Slic3r::ConfigOptionPoints>("printable_area", false);
                if (pa_start) {
                    std::cout << "  [START performSlicing] printable_area has " << pa_start->values.size() << " points: ";
                    for (const auto& pt : pa_start->values) {
                        std::cout << "(" << pt(0) << "," << pt(1) << ") ";
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "  [START performSlicing] printable_area is NULL!" << std::endl;
                }
                std::cout << "  single_extruder_multi_material = " << config->opt_bool("single_extruder_multi_material") << std::endl;
                std::cout << "  enable_prime_tower = " << config->opt_bool("enable_prime_tower") << std::endl;
                if (auto* fil_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false)) {
                    std::cout << "  filament_colour count = " << fil_colour->values.size() << std::endl;
                }
                if (auto* fil_diameter = config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false)) {
                    std::cout << "  filament_diameter count = " << fil_diameter->values.size() << std::endl;
                }
            }
            // Log currently selected presets inside PresetBundle
            std::cout << "DEBUG: Selected printer preset: " << preset_bundle.printers.get_selected_preset_name() << std::endl;
            std::cout << "DEBUG: Selected print preset:   " << preset_bundle.prints.get_selected_preset_name() << std::endl;
            if (!preset_bundle.filament_presets.empty())
                std::cout << "DEBUG: Selected filament[0]:   " << preset_bundle.filament_presets[0] << std::endl;

            // ========================================================================
            // GUI PARITY: Create FRESH Print object for each slicing
            // The GUI creates a new Print for each plate (see PartPlate.cpp:3157, 3602, 5155)
            // This prevents dangling m_shared_object pointers when print->apply() deletes old PrintObjects
            // MUST BE DONE FIRST, before any code that accesses print object!
            // ========================================================================
            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout << "DEBUG: Creating FRESH Print object (GUI parity)" << std::endl;

            // Destroy old Print if it exists
            if (print) {
                std::cout << "DEBUG: Destroying old Print object at " << print.get() << std::endl;
                delete print.release();  // Release ownership and delete manually
                std::cout << "DEBUG: Destroyed old Print object" << std::endl;
            }

            // Create new Print object (GUI parity: use new directly, not make_unique)
            print.reset(new Slic3r::Print());
            std::cout << "DEBUG: Created new Print object at " << print.get() << std::endl;

            // Configure the Print object
            try {
                bool is_bbl = preset_bundle.is_bbl_vendor();
                print->is_BBL_printer() = is_bbl;
                std::cout << "DEBUG: is_BBL_printer set to " << (is_bbl ? "true" : "false") << std::endl;
            } catch (...) {
                std::cout << "WARN: Failed to set is_BBL_printer flag" << std::endl;
            }

            // Set plate_origin and plate_index (will be updated later based on model instances)
            print->set_plate_origin(Slic3r::Vec3d(0, 0, 0));
            print->set_plate_index(0);
            std::cout << "DEBUG: Set initial plate_origin and plate_index" << std::endl;

            // Set cancel callback
            print->set_cancel_callback([](){
                // Empty callback - we don't support cancellation in CLI yet
                // But we MUST set this to avoid segfault in throw_if_canceled()
            });
            std::cout << "DEBUG: set_cancel_callback() configured" << std::endl;

            // IMPORTANT: Initialize cancel status to NOT_CANCELED
            print->restart();
            std::cout << "DEBUG: cancel_status initialized to NOT_CANCELED" << std::endl;

            std::cout << "DEBUG: ========================================" << std::endl;

            // GUI parity: do not normalize instances; rely solely on plate_origin to localize coordinates.
            // This matches the GUI's Export plate sliced file path, which keeps model instances in assembly space
            // and uses plate_origin to generate plate-local G-code.

            // GUI parity: do not normalize instances; set plate_origin from assembly offsets.
            std::cout << "DEBUG: GUI parity: will set plate_origin from instance assembly offsets" << std::endl;

            // GUI parity: compute and set plate_origin BEFORE process, based on instance assembly offsets or plate index stride
            {
                try {
                    // Derive bed size from printable area to compute logical stride
                    Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                    if (!bed_pts.empty()) {
                        long minx = std::numeric_limits<long>::max();
                        long maxx = std::numeric_limits<long>::min();
                        long miny = std::numeric_limits<long>::max();
                        long maxy = std::numeric_limits<long>::min();
                        for (const auto &p : bed_pts) { if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y(); }
                        const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
                        const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
                        if (bed_w_mm > 0.0 && bed_d_mm > 0.0) {
                            constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
                            const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                            const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                            const int total = (total_plates_count > 0 ? total_plates_count : 1);
                            const int cols = (int)std::ceil(std::sqrt((double)total));
                            const int idx0 = (plate_id > 0 ? plate_id - 1 : 0);
                            const int row = idx0 / cols;
                            const int col = idx0 % cols;
            // CRITICAL FIX: Apply plate config over full_config (GUI parity)
            // The GUI does: full_config.apply(m_config, true) before calling print->apply()
            // See: OrcaSlicer/src/slic3r/GUI/PartPlate.cpp line 2893
            // This ensures plate-specific overrides are applied
            if (!this->plate_data_src.empty()) {
                int idx_i = plate_id;
                if (idx_i < 0) idx_i = 0;
                int max_i = (int)this->plate_data_src.size() - 1;
                if (idx_i > max_i) idx_i = max_i;
                size_t idx = (size_t)idx_i;
                Slic3r::PlateData* pd = this->plate_data_src[idx];
                if (pd != nullptr && !pd->config.empty()) {
                    std::cout << "DEBUG: Applying plate config over full_config (GUI parity)" << std::endl;
                    // Apply key by key to handle type mismatches gracefully
                    auto plate_keys = pd->config.keys();
                    size_t applied = 0;
                    for (const auto& key : plate_keys) {
                        try {
                            std::vector<std::string> single_key = {key};
                            config->apply_only(pd->config, single_key, /*ignore_nonexistent=*/true);
                            ++applied;
                        } catch (const std::exception& e) {
                            std::cout << "WARN: Skipping plate config key '" << key << "': " << e.what() << std::endl;
                        }
                    }
                    std::cout << "DEBUG: Plate config applied successfully (" << applied << "/" << plate_keys.size() << " keys)" << std::endl;
                } else {
                    std::cout << "DEBUG: No plate config to apply (plate_data is empty or null)" << std::endl;
                }
            }

            // DEBUG: dump a couple of key values just before apply()
            try {
                auto dump_one = [&](const char* k){ if (const Slic3r::ConfigOption* o = config->optptr(k)) std::cout << "DEBUG: before_apply[" << k << "] = " << o->serialize() << std::endl; };
                dump_one("sparse_infill_density");
                dump_one("top_shell_layers");
            } catch (...) {}
            // NOTE: Do NOT re-apply project-level overrides here.
            // Precedence is centralized in AddonCore::slice(): options > print-dirty (3MF) > project-overrides (3MF) > presets.
            // Re-applying project_cfg_after_3mf at this late stage would overwrite print-dirty keys again.
            // Intentionally disabled to preserve correct precedence.
            // if (transfer_project_overrides) {
            //     try { config->apply(project_cfg_after_3mf, /*ignore_nonexistent=*/true); std::cout << "DEBUG: (disabled) would enforce project_cfg_after_3mf before apply()" << std::endl; } catch (...) {}
            // }


                            // Ensure selected plate index is propagated to Print & Model for GUI parity
                            model->curr_plate_index = idx0;
                            print->set_plate_index(idx0);
                            if (center_on_bed) {
                                bool is_bbl = false; try { is_bbl = preset_bundle.is_bbl_vendor(); } catch (...) {}
                                if (!is_bbl) {
                                    // Log instance offsets BEFORE centering
                                    for (const auto* obj : model->objects) {
                                        for (const auto* inst : obj->instances) {
                                            Slic3r::Vec3d off = inst->get_offset();
                                            std::cout << "DEBUG: [BEFORE center_instances] obj='" << obj->name << "' inst_offset=(" << off(0) << "," << off(1) << "," << off(2) << ")" << std::endl;
                                        }
                                    }
                                    (void)center_instances_on_bed_center();
                                    print->set_plate_origin(Slic3r::Vec3d(0.0, 0.0, 0.0));
                                    // Log instance offsets after centering
                                    for (const auto* obj : model->objects) {
                                        for (const auto* inst : obj->instances) {
                                            Slic3r::Vec3d off = inst->get_offset();
                                            std::cout << "DEBUG: [AFTER center_instances] obj='" << obj->name << "' inst_offset=(" << off(0) << "," << off(1) << "," << off(2) << ")" << std::endl;
                                        }
                                    }
                                    std::cout << "DEBUG: center_on_bed (NON-BBL, BEFORE process) => instances centered; plate_origin=(0,0)" << std::endl;
                                } else {
                                    (void)center_plate_origin_to_bed_center();
                                    auto po = print->get_plate_origin();
                                    std::cout << "DEBUG: center_on_bed (BEFORE process) => plate_origin=(" << po(0) << "," << po(1) << ")" << std::endl;
                                }
                            } else {
                                // First, try to compute from real instance assembly offsets
                                bool ok = compute_and_set_plate_origin_from_model_instances();
                                if (!ok) {
                                    // Deterministic fallback: use plate index and positive stride (writer subtracts this offset)
                                    const double origin_x =  (col * stride_x);
                                    const double origin_y = -(row * stride_y);
                                    print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
                                    std::cout << "DEBUG: plate_origin (from plate index, fallback, BEFORE process) => origin=(" << origin_x << "," << origin_y
                                              << ") stride=(" << stride_x << "," << stride_y << ") idx=" << idx0 << " cols=" << cols
                                              << " total=" << total << std::endl;
                                } else {
                                    auto po = print->get_plate_origin();
                                    std::cout << "DEBUG: plate_origin (from instances, BEFORE process) => (" << po(0) << "," << po(1) << ")" << std::endl;
                                }
                            }
                        }
                    }
                } catch (const std::exception &e) {
                    std::cout << "WARN: set_plate_origin (BEFORE process) failed: " << e.what() << std::endl;
                }
            }
            try { if (const auto* o = config->optptr("seam_position")) std::cout << "DEBUG: before_apply[seam_position]=" << o->serialize() << std::endl; } catch (...) {}

            // ========================================================================
            // GUI PARITY NOTE: The GUI resets gcode_result before apply()
            // See: OrcaSlicer/src/slic3r/GUI/BackgroundSlicingProcess.cpp line 709
            // We don't have a separate gcode_result object in CLI, so we skip this
            // ========================================================================
            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout << "DEBUG: Preparing to apply() (GUI parity)" << std::endl;
            std::cout << "DEBUG: ========================================" << std::endl;

            // Apply model and config to print
            // GUI PARITY: BackgroundSlicingProcess::apply() creates a new_config and applies plate config over it
            // See: OrcaSlicer/src/slic3r/GUI/BackgroundSlicingProcess.cpp lines 685-692
            // The GUI does:
            //   DynamicPrintConfig new_config = config;
            //   new_config.apply(*m_current_plate->config());
            //   m_print->apply(model, new_config);
            // This ensures plate-specific overrides are applied BEFORE print->apply()

            std::cout << "DEBUG: Creating config with plate overrides (GUI parity)..." << std::endl;
            Slic3r::DynamicPrintConfig apply_config = *config;

            // Apply plate config if available (GUI parity)
            if (!plate_data_src.empty() && plate_id > 0 && plate_id <= (int)plate_data_src.size()) {
                const auto& pd = plate_data_src[plate_id - 1];
                if (pd && !pd->config.empty()) {
                    std::cout << "DEBUG: Applying plate config overrides from plate " << plate_id << std::endl;
                    // Apply key by key to handle type mismatches gracefully
                    auto plate_keys = pd->config.keys();
                    for (const auto& key : plate_keys) {
                        try {
                            std::vector<std::string> single_key = {key};
                            apply_config.apply_only(pd->config, single_key, /*ignore_nonexistent=*/true);
                        } catch (const std::exception& e) {
                            std::cout << "WARN: Skipping plate config key '" << key << "': " << e.what() << std::endl;
                        }
                    }
                }
            }

            // CRITICAL FIX: Resize filament arrays BEFORE apply() to prevent crash
            // The crash happens because OrcaSlicer creates wipe_volumes matrix based on filament_colour.size()
            // but then tries to access it with extruder IDs that may be out of bounds
            std::cout << "========================================" << std::endl;
            std::cout << "CRITICAL FIX: Adjusting filament arrays BEFORE apply()" << std::endl;
            std::cout << "========================================" << std::endl;

            auto* fil_colour_pre = apply_config.opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
            auto* fil_diameter_pre = apply_config.opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
            auto* fil_type_pre = apply_config.opt<Slic3r::ConfigOptionStrings>("filament_type", false);
            auto* fil_density_pre = apply_config.opt<Slic3r::ConfigOptionFloats>("filament_density", false);
            auto* fil_cost_pre = apply_config.opt<Slic3r::ConfigOptionFloats>("filament_cost", false);

            // Count how many extruders will actually be used by counting unique extruder IDs in the model
            std::set<int> used_extruders;
            for (const auto* obj : model->objects) {
                for (const auto& volume : obj->volumes) {
                    used_extruders.insert(volume->extruder_id()); // extruder_id() is 0-based
                }
            }
            size_t max_extruder_needed = used_extruders.empty() ? 1 : (*used_extruders.rbegin() + 1);

            std::cout << "========================================" << std::endl;
            std::cout << "🎨 [COLOR DEBUG] Extruder Usage Analysis" << std::endl;
            std::cout << "Used extruders: ";
            for (int e : used_extruders) std::cout << e << " ";
            std::cout << std::endl;
            std::cout << "Max extruder needed: " << max_extruder_needed << std::endl;
            std::cout << "detected_extruders (from 3MF): " << detected_extruders << std::endl;
            std::cout << "========================================" << std::endl;

            if (fil_colour_pre) {
                std::cout << "🎨 [COLOR DEBUG] filament_colour BEFORE trimming: " << fil_colour_pre->values.size() << " colors: ";
                for (const auto& c : fil_colour_pre->values) std::cout << c << " ";
                std::cout << std::endl;

                if (fil_colour_pre->values.size() > max_extruder_needed) {
                    std::cout << "🎨 [COLOR DEBUG] ⚠️  TRIMMING filament_colour: " << fil_colour_pre->values.size() << " -> " << max_extruder_needed << std::endl;
                    fil_colour_pre->values.resize(max_extruder_needed);

                    std::cout << "🎨 [COLOR DEBUG] filament_colour AFTER trimming: " << fil_colour_pre->values.size() << " colors: ";
                    for (const auto& c : fil_colour_pre->values) std::cout << c << " ";
                    std::cout << std::endl;
                } else {
                    std::cout << "🎨 [COLOR DEBUG] ✅ No trimming needed (size matches or is less than max_extruder_needed)" << std::endl;
                }
            } else {
                std::cout << "🎨 [COLOR DEBUG] ⚠️  filament_colour is NULL!" << std::endl;
            }

            if (fil_diameter_pre) {
                std::cout << "filament_diameter BEFORE: " << fil_diameter_pre->values.size() << std::endl;
                if (fil_diameter_pre->values.size() > max_extruder_needed) {
                    std::cout << "TRIMMING filament_diameter: " << fil_diameter_pre->values.size() << " -> " << max_extruder_needed << std::endl;
                    fil_diameter_pre->values.resize(max_extruder_needed);
                }
            }

            if (fil_type_pre) {
                std::cout << "filament_type BEFORE: " << fil_type_pre->values.size() << std::endl;
                if (fil_type_pre->values.size() > max_extruder_needed) {
                    std::cout << "TRIMMING filament_type: " << fil_type_pre->values.size() << " -> " << max_extruder_needed << std::endl;
                    fil_type_pre->values.resize(max_extruder_needed);
                }
            }

            if (fil_density_pre) {
                if (fil_density_pre->values.size() > max_extruder_needed) {
                    std::cout << "TRIMMING filament_density: " << fil_density_pre->values.size() << " -> " << max_extruder_needed << std::endl;
                    fil_density_pre->values.resize(max_extruder_needed);
                }
            }

            if (fil_cost_pre) {
                if (fil_cost_pre->values.size() > max_extruder_needed) {
                    std::cout << "TRIMMING filament_cost: " << fil_cost_pre->values.size() << " -> " << max_extruder_needed << std::endl;
                    fil_cost_pre->values.resize(max_extruder_needed);
                }
            }

            std::cout << "========================================" << std::endl;

            // CRITICAL: Sanitize config types before apply() to prevent "Comparing incompatible types" errors
            // This can happen when 3MF was created with a different OrcaSlicer version that had different types
            std::cout << "DEBUG: Sanitizing config types before apply()..." << std::endl;
            sanitize_config_types(apply_config);

            // DEBUG: Log printable_area in apply_config before print->apply()
            try {
                auto* pa_cfg = apply_config.opt<Slic3r::ConfigOptionPoints>("printable_area", false);
                if (pa_cfg) {
                    std::cout << "DEBUG: [before print->apply] apply_config.printable_area has " << pa_cfg->values.size() << " points: ";
                    for (const auto& pt : pa_cfg->values) {
                        std::cout << "(" << pt(0) << "," << pt(1) << ") ";
                    }
                    std::cout << std::endl;
                } else {
                    std::cout << "DEBUG: [before print->apply] apply_config.printable_area is NULL!" << std::endl;
                }
            } catch (...) {}

            std::cout << "DEBUG: Applying model and config to print..." << std::endl;

            try {
                print->apply(*model, apply_config);
                std::cout << "DEBUG: apply() completed" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "ERROR: apply() failed: " << e.what() << std::endl;
                throw;
            }

            // NOTE: Do NOT trim filament arrays after apply()!
            // The Print object has internal references that depend on the config passed to apply().
            // Modifying the arrays after apply() can cause segfaults.
            // The OrcaSlicer GUI does NOT trim arrays - it keeps them at the size from the 3MF.
            // See: OrcaSlicer/src/slic3r/GUI/PartPlate.cpp lines 2892-2897

            // DEBUG: Log extruder info for troubleshooting
            try {
                auto extruder_vec = print->extruders();
                std::cout << "DEBUG: print->extruders() returned " << extruder_vec.size() << " extruders: ";
                for (auto id : extruder_vec) std::cout << id << " ";
                std::cout << std::endl;

                auto* fil_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
                auto* fil_diameter = config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
                auto* fil_type = config->opt<Slic3r::ConfigOptionStrings>("filament_type", false);

                std::cout << "DEBUG: Config array sizes: colour=" << (fil_colour ? fil_colour->values.size() : 0)
                          << " diameter=" << (fil_diameter ? fil_diameter->values.size() : 0)
                          << " type=" << (fil_type ? fil_type->values.size() : 0) << std::endl;

                // Check region configs for wall_filament and sparse_infill_filament values
                if (!print->objects().empty() && !print->objects()[0]->layers().empty()) {
                    const auto& first_layer = print->objects()[0]->layers()[0];
                    if (!first_layer->regions().empty()) {
                        for (size_t i = 0; i < first_layer->regions().size(); ++i) {
                            const auto& region = first_layer->regions()[i]->region();
                            std::cout << "DEBUG: Region " << i << " wall_filament=" << region.config().wall_filament.value
                                      << " sparse_infill_filament=" << region.config().sparse_infill_filament.value << std::endl;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "WARN: Failed to log extruder info: " << e.what() << std::endl;
            }

            // ADDON-SPECIFIC: Promote object/region config values into full_print_config for G-code metadata
            // This ensures that 3MF overrides (seam_position, bottom_surface_pattern, etc.) appear in G-code headers
            // WITHOUT modifying OrcaSlicer source code (PrintApply.cpp)
            try {
                auto& fpc = const_cast<Slic3r::DynamicPrintConfig&>(print->full_print_config());
                auto promote = [&fpc](const char* key, const Slic3r::ConfigBase& src) {
                    if (const Slic3r::ConfigOption* o = src.option(key)) {
                        fpc.set_key_value(key, o->clone());
                    }
                };
                // Object-scoped keys
                promote("seam_position", print->default_object_config());
                // Region-scoped keys
                promote("bottom_surface_pattern", print->default_region_config());
                promote("sparse_infill_pattern", print->default_region_config());
                promote("top_surface_pattern", print->default_region_config());
                promote("internal_solid_infill_pattern", print->default_region_config());
                std::cout << "DEBUG: Promoted object/region config values into full_print_config for metadata" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "WARN: Failed to promote config values: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "WARN: Failed to promote config values (unknown error)" << std::endl;
            }

            // Check if PrintObjects have the correct config values
            try {
                const auto& objects = print->objects();
                for (size_t i = 0; i < objects.size(); ++i) {
                    const auto* obj = objects[i];
                    if (obj) {
                        const auto& obj_cfg = obj->config();
                        // Check printing regions within this object
                        for (size_t r = 0; r < obj->num_printing_regions(); ++r) {
                            const auto& region = obj->printing_region(r);
                            const auto& reg_cfg = region.config();
                        }
                    }
                }
            } catch (const std::exception& e) {
            }

            try { if (const auto* o = config->optptr("seam_position")) std::cout << "TESTE AQUI AGORA >>>   config[seam_position] = " << o->serialize() << std::endl; } catch (...) {}
            try { if (const auto* o = config->optptr("bottom_surface_pattern")) std::cout << "TESTE AQUI AGORA >>>   config[bottom_surface_pattern] = " << o->serialize() << std::endl; } catch (...) {}
            try { if (const auto* o = config->optptr("sparse_infill_pattern")) std::cout << "TESTE AQUI AGORA >>>   config[sparse_infill_pattern] = " << o->serialize() << std::endl; } catch (...) {}

            try {
                const auto& print_config = print->config();
                if (const auto* o = print_config.optptr("seam_position")) std::cout << "TESTE AQUI AGORA >>>   print.config[seam_position] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   print.config[seam_position] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read print.config[seam_position]" << std::endl; }
            try {
                const auto& print_config = print->config();
                if (const auto* o = print_config.optptr("bottom_surface_pattern")) std::cout << "TESTE AQUI AGORA >>>   print.config[bottom_surface_pattern] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   print.config[bottom_surface_pattern] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read print.config[bottom_surface_pattern]" << std::endl; }
            try {
                const auto& print_config = print->config();
                if (const auto* o = print_config.optptr("sparse_infill_pattern")) std::cout << "TESTE AQUI AGORA >>>   print.config[sparse_infill_pattern] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   print.config[sparse_infill_pattern] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read print.config[sparse_infill_pattern]" << std::endl; }

            try {
                const auto& region_config = print->default_region_config();
                if (const auto* o = region_config.optptr("seam_position")) std::cout << "TESTE AQUI AGORA >>>   region_config[seam_position] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   region_config[seam_position] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read region_config[seam_position]" << std::endl; }
            try {
                const auto& region_config = print->default_region_config();
                if (const auto* o = region_config.optptr("bottom_surface_pattern")) std::cout << "TESTE AQUI AGORA >>>   region_config[bottom_surface_pattern] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   region_config[bottom_surface_pattern] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read region_config[bottom_surface_pattern]" << std::endl; }
            try {
                const auto& region_config = print->default_region_config();
                if (const auto* o = region_config.optptr("sparse_infill_pattern")) std::cout << "TESTE AQUI AGORA >>>   region_config[sparse_infill_pattern] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   region_config[sparse_infill_pattern] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read region_config[sparse_infill_pattern]" << std::endl; }

            try {
                const auto& object_config = print->default_object_config();
                if (const auto* o = object_config.optptr("seam_position")) std::cout << "TESTE AQUI AGORA >>>   object_config[seam_position] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   object_config[seam_position] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read object_config[seam_position]" << std::endl; }
            try {
                const auto& object_config = print->default_object_config();
                if (const auto* o = object_config.optptr("bottom_surface_pattern")) std::cout << "TESTE AQUI AGORA >>>   object_config[bottom_surface_pattern] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   object_config[bottom_surface_pattern] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read object_config[bottom_surface_pattern]" << std::endl; }
            try {
                const auto& object_config = print->default_object_config();
                if (const auto* o = object_config.optptr("sparse_infill_pattern")) std::cout << "TESTE AQUI AGORA >>>   object_config[sparse_infill_pattern] = " << o->serialize() << std::endl;
                else std::cout << "TESTE AQUI AGORA >>>   object_config[sparse_infill_pattern] = NOT FOUND" << std::endl;
            } catch (...) { std::cout << "TESTE AQUI AGORA >>>   Failed to read object_config[sparse_infill_pattern]" << std::endl; }

            // Re-assert plate_origin AFTER apply, BEFORE process (apply may reset internal state)

	            // Sync wipe tower positions from project_config into Model (GUI parity)
	            // BUT: if center_on_bed is true, the wipe tower position was recalculated during rearrange,
	            // so we should NOT restore the original 3MF position here
	            if (!center_on_bed) {
	                try {
	                    const Slic3r::DynamicConfig &proj_cfg = preset_bundle.project_config;
	                    const auto *tx = proj_cfg.option<Slic3r::ConfigOptionFloats>("wipe_tower_x");
	                    const auto *ty = proj_cfg.option<Slic3r::ConfigOptionFloats>("wipe_tower_y");
	                    if (tx && ty && tx->values.size() == ty->values.size()) {
	                        model->wipe_tower.positions.clear();
	                        model->wipe_tower.positions.resize(tx->values.size());
	                        for (size_t i = 0; i < tx->values.size(); ++i) {
	                            model->wipe_tower.positions[i] = Slic3r::Vec2d(tx->get_at(i), ty->get_at(i));
	                        }
	                    }
	                } catch (...) {}
	            } else {
	                std::cout << "DEBUG: center_on_bed=true, skipping wipe tower position sync from 3MF (position was recalculated)" << std::endl;
	            }

            {
                try {
                    Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                    if (!bed_pts.empty()) {
                        long minx = std::numeric_limits<long>::max();
                        long maxx = std::numeric_limits<long>::min();
                        long miny = std::numeric_limits<long>::max();
                        long maxy = std::numeric_limits<long>::min();
                        for (const auto &p : bed_pts) { if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y(); }
                        const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
                        const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
                        if (bed_w_mm > 0.0 && bed_d_mm > 0.0) {
                            constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
                            const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                            const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                            const int total = (total_plates_count > 0 ? total_plates_count : 1);
                            const int cols = (int)std::ceil(std::sqrt((double)total));
                            const int idx0 = (plate_id > 0 ? plate_id - 1 : 0);
                            const int row = idx0 / cols;
                            const int col = idx0 % cols;
                            if (center_on_bed) {
                                bool is_bbl = false; try { is_bbl = preset_bundle.is_bbl_vendor(); } catch (...) {}
                                if (!is_bbl) {
                                    // Already centered by instance shift; keep plate_origin at zero
                                    std::cout << "DEBUG: center_on_bed (NON-BBL, AFTER apply) => keeping instances-centered; plate_origin=(0,0)" << std::endl;
                                } else {
                                    (void)center_plate_origin_to_bed_center();
                                    auto po = print->get_plate_origin();
                                    std::cout << "DEBUG: center_on_bed (AFTER apply) => plate_origin=(" << po(0) << "," << po(1) << ")" << std::endl;
                                }
                            } else {
                                bool ok = compute_and_set_plate_origin_from_model_instances();
                                if (!ok) {
                                    const double origin_x =  (col * stride_x);
                                    const double origin_y = -(row * stride_y);
                                    print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
                                    std::cout << "DEBUG: plate_origin (fallback, AFTER apply) => (" << origin_x << "," << origin_y << ")" << std::endl;
                                } else {
                                    auto po = print->get_plate_origin();
                                    std::cout << "DEBUG: plate_origin (from instances, AFTER apply) => (" << po(0) << "," << po(1) << ")" << std::endl;
                                }
                            }
                        }
                    }
                } catch (const std::exception &e) {
                    std::cout << "WARN: set_plate_origin (AFTER apply) failed: " << e.what() << std::endl;
                }
            }


            // Process the print (this does the actual slicing)
            try {
                const auto &fpc = print->full_print_config();
            } catch (...) {}

            std::cout << "DEBUG: Starting print processing..." << std::endl;

            // DEBUG: Log state before process()
            try {
                std::cout << "=== STATE BEFORE PROCESS() ===" << std::endl;
                std::cout << "Print object address: " << print.get() << std::endl;
                std::cout << "Print objects count: " << print->objects().size() << std::endl;
                std::cout << "Model curr_plate_index: " << model->curr_plate_index << std::endl;

                // Check if wipe tower is enabled
                auto* enable_wipe_tower = config->opt<Slic3r::ConfigOptionBool>("enable_prime_tower", false);
                std::cout << "enable_prime_tower: " << (enable_wipe_tower && enable_wipe_tower->value ? "true" : "false") << std::endl;

                // Check print sequence
                auto* print_seq = config->opt<Slic3r::ConfigOptionEnum<Slic3r::PrintSequence>>("print_sequence", false);
                if (print_seq) {
                    std::cout << "print_sequence: " << static_cast<int>(print_seq->value) << std::endl;
                }

                std::cout << "print->has_wipe_tower(): " << (print->has_wipe_tower() ? "true" : "false") << std::endl;

                std::cout << "==============================" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "WARN: Failed to log pre-process state: " << e.what() << std::endl;
            }

            // NOTE: Print object was already created at the beginning of performSlicing()
            // This is necessary to avoid accessing null pointer in earlier code
            // VALIDATION + OPTIONAL AUTO-REALIGN: ensure all elements fit the printable area, or auto-realign if enabled
            try {
                const auto &pc = print->config();

                // DEBUG: Log printable_area being used for validation
                std::cout << "DEBUG: [check_outside] Using printable_area from print->config(): ";
                for (const auto& pt : pc.printable_area.values) {
                    std::cout << "(" << pt(0) << "," << pt(1) << ") ";
                }
                std::cout << std::endl;
                std::cout << "DEBUG: [check_outside] printable_height=" << pc.printable_height << std::endl;

                Slic3r::BuildVolume build_volume(pc.printable_area.values, pc.printable_height, {}, {});
                std::cout << "DEBUG: [check_outside] BuildVolume valid=" << (build_volume.valid() ? "true" : "false") << std::endl;
                if (build_volume.valid()) {
                    auto check_outside = [&](std::string &msg)->bool{
                        const Slic3r::Vec3d po = print->get_plate_origin();
                        const Slic3r::Vec3d shift_xy(-po(0), -po(1), 0.0);
                        std::cout << "DEBUG: [check_outside] plate_origin=(" << po(0) << "," << po(1) << "," << po(2) << ")" << std::endl;
                        // Check model instances
                        for (const auto *obj : model->objects) {
                            if (!obj) continue;
                            for (const auto *inst : obj->instances) {
                                if (!inst) continue;
                                // Log instance offset for debugging
                                Slic3r::Vec3d inst_offset = inst->get_offset();
                                std::cout << "DEBUG: [check_outside] obj='" << obj->name << "' inst_offset=(" << inst_offset(0) << "," << inst_offset(1) << "," << inst_offset(2) << ")" << std::endl;
                                Slic3r::BoundingBoxf3 bb = obj->instance_bounding_box(*inst);
                                Slic3r::BoundingBoxf3 bb_local(bb.min + shift_xy, bb.max + shift_xy);
                                std::cout << "DEBUG: [check_outside] obj='" << obj->name << "' bb_local min=(" << bb_local.min(0) << "," << bb_local.min(1) << ") max=(" << bb_local.max(0) << "," << bb_local.max(1) << ")" << std::endl;
                                auto state = build_volume.volume_state_bbox(bb_local, /*ignore_bottom=*/true);
                                if (state != Slic3r::BuildVolume::ObjectState::Inside) {
                                    std::string name = obj->name.empty() ? std::string("objeto") : obj->name;
                                    msg = std::string("Elementos fora da área de impressão: '") + name + "' " +
                                          (state == Slic3r::BuildVolume::ObjectState::Colliding ? "parcialmente" : "totalmente") +
                                          " fora da área para o perfil selecionado.";
                                    return true;
                                }
                            }
                        }
                        // Check Prime/Wipe Tower
                        bool prime_enabled = false; try { prime_enabled = pc.enable_prime_tower.getBool(); } catch (...) {}
                        if (prime_enabled || print->has_wipe_tower()) {
                            float wtx = 0.f, wty = 0.f;
                            try { wtx = pc.wipe_tower_x.get_at(0); } catch (...) {}
                            try { wty = pc.wipe_tower_y.get_at(0); } catch (...) {}
                            Slic3r::Vec3d pt(double(wtx), double(wty), 0.0);
                            Slic3r::BoundingBoxf3 wtbb(pt, pt);
                            auto state = build_volume.volume_state_bbox(wtbb, /*ignore_bottom=*/true);
                            if (state != Slic3r::BuildVolume::ObjectState::Inside) {
                                msg = "Elementos fora da área de impressão: Prime Tower fora da área válida para o perfil selecionado.";
                                return true;
                            }
                        }
                        return false; // all inside
                    };

                    std::string oob_msg;
                    bool is_outside = check_outside(oob_msg);
                    std::cout << "DEBUG: [check_outside] is_outside=" << (is_outside ? "true" : "false") << ", msg='" << oob_msg << "'" << std::endl;
                    if (is_outside) {
                        if (auto_realign_if_needed) {
                            bool is_bbl = false; try { is_bbl = preset_bundle.is_bbl_vendor(); } catch (...) {}
                            bool fixed = false;

                            // Simple direct repositioning: place instances in a grid on the bed
                            // This is more reliable than the arrange algorithm for on-the-fly mode
                            try {
                                Slic3r::Points bed_pts_for_center = Slic3r::get_bed_shape(*config);
                                Slic3r::BoundingBox bed_bb(bed_pts_for_center);
                                double bed_width = Slic3r::unscale<double>(bed_bb.size().x());
                                double bed_height = Slic3r::unscale<double>(bed_bb.size().y());
                                double bed_min_x = Slic3r::unscale<double>(bed_bb.min.x());
                                double bed_min_y = Slic3r::unscale<double>(bed_bb.min.y());
                                double bed_center_x = Slic3r::unscale<double>(bed_bb.center().x());
                                double bed_center_y = Slic3r::unscale<double>(bed_bb.center().y());

                                std::cout << "DEBUG: [simple_reposition] bed_size=(" << bed_width << "x" << bed_height
                                          << ") bed_min=(" << bed_min_x << "," << bed_min_y
                                          << ") bed_center=(" << bed_center_x << "," << bed_center_y << ")" << std::endl;

                                // Collect all instances and their bounding boxes
                                std::vector<std::pair<Slic3r::ModelInstance*, Slic3r::BoundingBoxf3>> instances_with_bb;
                                for (auto* obj : model->objects) {
                                    for (auto* inst : obj->instances) {
                                        auto bb = inst->transform_bounding_box(obj->bounding_box_exact());
                                        instances_with_bb.push_back({inst, bb});
                                    }
                                }

                                // Calculate total width needed for all instances in a row
                                double total_width = 0;
                                double max_height = 0;
                                const double spacing = 5.0; // 5mm spacing between objects
                                for (const auto& [inst, bb] : instances_with_bb) {
                                    total_width += bb.size().x() + spacing;
                                    max_height = std::max(max_height, bb.size().y());
                                }
                                total_width -= spacing; // Remove last spacing

                                std::cout << "DEBUG: [simple_reposition] total_width=" << total_width << ", max_height=" << max_height << std::endl;

                                // Check if all instances fit in a single row
                                bool fits_in_row = (total_width <= bed_width - 10) && (max_height <= bed_height - 10);

                                if (fits_in_row) {
                                    // Place all instances in a centered row
                                    // We need to calculate positions based on the object's mesh bounding box
                                    // (not the transformed bounding box which includes the current offset)

                                    // First, get the mesh bounding boxes (without instance transform)
                                    // Note: For the same object with multiple instances, the mesh_bb is the same
                                    // The mesh_bb is the bounding box of the raw mesh, centered at origin
                                    std::vector<Slic3r::BoundingBoxf3> mesh_bbs;
                                    for (const auto& [inst, bb] : instances_with_bb) {
                                        // Get the object's raw mesh bounding box (without any transforms)
                                        auto* obj = inst->get_object();
                                        Slic3r::BoundingBoxf3 mesh_bb = obj->raw_bounding_box();
                                        mesh_bbs.push_back(mesh_bb);
                                        std::cout << "DEBUG: [simple_reposition] raw_mesh_bb center=(" << mesh_bb.center().x() << "," << mesh_bb.center().y()
                                                  << ") size=(" << mesh_bb.size().x() << "," << mesh_bb.size().y() << ")" << std::endl;
                                    }

                                    double start_x = bed_center_x - total_width / 2.0;
                                    double current_x = start_x;

                                    for (size_t i = 0; i < instances_with_bb.size(); ++i) {
                                        auto& [inst, bb] = instances_with_bb[i];
                                        const auto& mesh_bb = mesh_bbs[i];
                                        double obj_width = mesh_bb.size().x();

                                        // Calculate new position for the object center
                                        double new_center_x = current_x + obj_width / 2.0;
                                        double new_center_y = bed_center_y;

                                        // The offset should place the mesh center at new_center
                                        // new_center = offset + mesh_center
                                        // offset = new_center - mesh_center
                                        Slic3r::Vec3d old_offset = inst->get_offset();
                                        Slic3r::Vec3d mesh_center = mesh_bb.center();

                                        double new_offset_x = new_center_x - mesh_center.x();
                                        double new_offset_y = new_center_y - mesh_center.y();

                                        Slic3r::Vec3d new_offset(new_offset_x, new_offset_y, old_offset.z());
                                        inst->set_offset(new_offset);

                                        std::cout << "DEBUG: [simple_reposition] moved instance from (" << old_offset.x() << "," << old_offset.y()
                                                  << ") to (" << new_offset.x() << "," << new_offset.y()
                                                  << ") new_center=(" << new_center_x << "," << new_center_y << ")" << std::endl;

                                        current_x += obj_width + spacing;
                                    }
                                    fixed = true;
                                } else {
                                    // Try to fit each instance individually at bed center
                                    // This is a fallback for when objects don't fit in a row
                                    for (auto& [inst, bb] : instances_with_bb) {
                                        double obj_width = bb.size().x();
                                        double obj_height = bb.size().y();

                                        // Check if this single object fits on the bed
                                        if (obj_width <= bed_width - 10 && obj_height <= bed_height - 10) {
                                            Slic3r::Vec3d old_offset = inst->get_offset();
                                            Slic3r::Vec3d bb_center = bb.center();

                                            // Calculate local center (center relative to offset)
                                            double local_center_x = bb_center.x() - old_offset.x();
                                            double local_center_y = bb_center.y() - old_offset.y();

                                            // New offset to place center at bed center
                                            double new_offset_x = bed_center_x - local_center_x;
                                            double new_offset_y = bed_center_y - local_center_y;

                                            Slic3r::Vec3d new_offset(new_offset_x, new_offset_y, old_offset.z());
                                            inst->set_offset(new_offset);

                                            std::cout << "DEBUG: [simple_reposition] centered instance from (" << old_offset.x() << "," << old_offset.y()
                                                      << ") to (" << new_offset.x() << "," << new_offset.y() << ")" << std::endl;
                                            fixed = true;
                                        } else {
                                            std::cout << "WARN: [simple_reposition] object too large for bed: " << obj_width << "x" << obj_height << std::endl;
                                        }
                                    }
                                }

                                // Invalidate bounding boxes after repositioning
                                for (auto* obj : model->objects) {
                                    obj->invalidate_bounding_box();
                                }

                                // Reset plate_origin to (0,0,0) since objects are now in absolute bed coordinates
                                // This is critical: after simple_reposition, objects are placed at absolute bed coords
                                // (e.g., (57.5, 90)), so plate_origin must be zero for check_outside to work correctly
                                if (fixed) {
                                    print->set_plate_origin(Slic3r::Vec3d(0.0, 0.0, 0.0));
                                    std::cout << "DEBUG: [simple_reposition] reset plate_origin to (0,0,0) after repositioning" << std::endl;
                                }

                                // Re-apply print with updated model transforms
                                if (fixed) {
                                    try {
                                        Slic3r::DynamicPrintConfig apply_cfg = *config;
                                        print->apply(*model, apply_cfg);
                                    } catch (const std::exception &e) {
                                        std::cout << "WARN: apply() after simple_reposition failed: " << e.what() << std::endl;
                                    }
                                }

                            } catch (const std::exception &e) {
                                std::cout << "WARN: simple_reposition failed: " << e.what() << std::endl;
                                fixed = false;
                            }

                            // Skip the complex arrange algorithm - use simple repositioning above
                            // The arrange algorithm was returning bed_idx=-1 for all items
                            if (false) {
                            // Use the same arrangement algorithm as the GUI (libnest2d-based)
                            // Build bed shape from current config and run arrange_objects
                            try {
                                // Build selected items (instances) and run full arrange pipeline like GUI
                                Slic3r::ModelInstancePtrs instances;
                                auto selected = Slic3r::get_arrange_polys(*model, instances);

                                // Reset translations to bed center so arrange can reposition from scratch
                                // This is necessary because the current positions may be outside the bed
                                // Get bed center from config
                                Slic3r::Points bed_pts_for_center = Slic3r::get_bed_shape(*config);
                                Slic3r::BoundingBox bed_bb(bed_pts_for_center);
                                Slic3r::Point bed_center = bed_bb.center();
                                std::cout << "DEBUG: [arrange] bed_center=(" << Slic3r::unscale<double>(bed_center.x()) << "," << Slic3r::unscale<double>(bed_center.y()) << ")" << std::endl;

                                for (auto& ap : selected) {
                                    // Get the bounding box of the polygon
                                    Slic3r::BoundingBox poly_bb(ap.poly.contour.points);
                                    std::cout << "DEBUG: [arrange] item poly bb min=(" << Slic3r::unscale<double>(poly_bb.min.x()) << "," << Slic3r::unscale<double>(poly_bb.min.y())
                                              << ") max=(" << Slic3r::unscale<double>(poly_bb.max.x()) << "," << Slic3r::unscale<double>(poly_bb.max.y()) << ")" << std::endl;
                                    std::cout << "DEBUG: [arrange] resetting item translation from ("
                                              << Slic3r::unscale<double>(ap.translation.x()) << ","
                                              << Slic3r::unscale<double>(ap.translation.y()) << ") to bed_center" << std::endl;
                                    // Set translation to bed center so the item starts inside the bed
                                    ap.translation = bed_center;
                                    ap.bed_idx = Slic3r::arrangement::UNARRANGED;
                                }

                                Slic3r::arrangement::ArrangeParams params;
                                params.min_obj_distance = 0;
                                params.allow_rotations  = false;
                                params.do_final_align   = true;

                                // Add a timeout-based stop condition to prevent infinite loops
                                // Timeout: 30 seconds max for the arrange operation
                                auto arrange_start_time = std::chrono::steady_clock::now();
                                const auto arrange_timeout = std::chrono::seconds(30);
                                params.stopcondition = [arrange_start_time, arrange_timeout]() -> bool {
                                    auto elapsed = std::chrono::steady_clock::now() - arrange_start_time;
                                    bool should_stop = elapsed > arrange_timeout;
                                    if (should_stop) {
                                        std::cout << "WARN: Arrange operation timed out after 30 seconds" << std::endl;
                                    }
                                    return should_stop;
                                };
                                // Align to Y axis for i3 style printers if available
                                try {
                                    if (const auto *ps = config->option<Slic3r::ConfigOptionEnum<Slic3r::PrinterStructure>>("printer_structure"))
                                        params.align_to_y_axis = (ps->value == Slic3r::PrinterStructure::psI3);
                                } catch (...) {}
                                // Sequential print detection via print_sequence
                                try {
                                    if (const auto *seq = config->option<Slic3r::ConfigOptionEnum<Slic3r::PrintSequence>>("print_sequence"))
                                        params.is_seq_print = (seq->value == Slic3r::PrintSequence::ByObject);
                                } catch (...) {}

                                // Update params and selected inflation per GUI helpers
                                Slic3r::arrangement::update_arrange_params(params, config.get(), selected);
                                Slic3r::arrangement::update_selected_items_inflation(selected, config.get(), params);
                                Slic3r::arrangement::update_selected_items_axis_align(selected, config.get(), params);

                                // Build shrunk bed and prepare wipe/prime tower exclusion like GUI
                                // Determine if prime tower is needed/enabled and create a reserved region for it
                                try {
                                    bool prime_enabled = false;
                                    try { prime_enabled = config->opt_bool("enable_prime_tower"); } catch (...) {}
                                    if (prime_enabled || (print && print->has_wipe_tower())) {
                                        // Tower size from config
                                        double tower_w = 0.0;
                                        if (auto *pw = config->opt<Slic3r::ConfigOptionFloat>("prime_tower_width", false)) tower_w = pw->value;
                                        double tower_brim = 0.0;
                                        if (auto *pb = config->opt<Slic3r::ConfigOptionFloat>("prime_tower_brim_width", false)) tower_brim = pb->value;
                                        // params.brim_skirt_distance is in internal coords; convert to mm for arithmetic below
                                        const double skirt_mm = Slic3r::unscale<double>(params.brim_skirt_distance);
                                        const double tower_margin = std::max(1.0, skirt_mm);
                                        const double half_w = 0.5 * (tower_w + 2.0 * tower_brim);

                                        // Shrunk bed polygon and bbox (scaled ints)
                                        Slic3r::Points bed_pts_for_wt = Slic3r::arrangement::get_shrink_bedpts(config.get(), params);
                                        if (bed_pts_for_wt.empty()) {
                                            std::cout << "DEBUG: get_shrink_bedpts returned empty; skipping prime tower exclusion" << std::endl;
                                        } else {
                                        Slic3r::BoundingBox bedbb_scaled = Slic3r::Polygon(bed_pts_for_wt).bounding_box();
                                        // Compute a target position near top-right inside bed with margin (mm)
                                        const double minx_mm = Slic3r::unscale<double>(bedbb_scaled.min.x());
                                        const double miny_mm = Slic3r::unscale<double>(bedbb_scaled.min.y());
                                        const double maxx_mm = Slic3r::unscale<double>(bedbb_scaled.max.x());
                                        const double maxy_mm = Slic3r::unscale<double>(bedbb_scaled.max.y());

                                        // lower-left anchoring: subtract full width (2*half_w)
                                        double tx_mm = maxx_mm - (2.0 * half_w + tower_margin);
                                        double ty_mm = maxy_mm - (2.0 * half_w + tower_margin);

                                        // If config already has tower position, prefer keeping it if inside bed
                                        // BUT: if center_on_bed is true, objects were repositioned so we should
                                        // recalculate tower position to avoid collision with moved objects
                                        float conf_wtx = 0.f, conf_wty = 0.f;
                                        bool has_conf_pos = false;
                                        if (!center_on_bed) {
                                            try { if (auto *wx = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_x", false)) { if (!wx->values.empty()) { conf_wtx = wx->values[0]; has_conf_pos = true; } } } catch (...) {}
                                            try { if (auto *wy = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_y", false)) { if (!wy->values.empty()) { conf_wty = wy->values[0]; has_conf_pos = has_conf_pos && true; } } } catch (...) {}
                                        } else {
                                            std::cout << "DEBUG: center_on_bed=true, ignoring existing wipe_tower position to recalculate" << std::endl;
                                        }

                                        // Check if existing conf position is inside shrunk bed; if so, use it
                                        if (has_conf_pos) {
                                            // existing config is already in local bed coords (mm)
                                            Slic3r::Vec3d pt_local(double(conf_wtx), double(conf_wty), 0.0);
                                            Slic3r::BoundingBoxf3 wtbb_local(pt_local, pt_local);
                                            Slic3r::BuildVolume bv(pc.printable_area.values, double(pc.printable_height), {}, {});
                                            auto st = bv.volume_state_bbox(wtbb_local, /*ignore_bottom=*/true);
                                            if (st == Slic3r::BuildVolume::ObjectState::Inside) {
                                                tx_mm = pt_local(0); ty_mm = pt_local(1);
                                            }
                                        }

                                        // Create an exclusion rectangle polygon around (tx_mm, ty_mm)
                                        if (half_w > 0.0) {
                                            const coord_t dx = Slic3r::scaled<coord_t>(half_w);
                                            const coord_t dy = Slic3r::scaled<coord_t>(half_w);

                                            Slic3r::arrangement::ArrangePolygon wt_ap;
                                            wt_ap.name = "WipeTower";
                                            wt_ap.is_virt_object = true;
                                            wt_ap.is_wipe_tower = true;
                                            ++wt_ap.priority;
                                            Slic3r::Polygon r;
                                            r.points.clear();
                                            // lower-left anchored rectangle of size (2*dx, 2*dy)
                                            r.points.push_back(Slic3r::Point(0, 0));
                                            r.points.push_back(Slic3r::Point(2*dx, 0));
                                            r.points.push_back(Slic3r::Point(2*dx, 2*dy));
                                            r.points.push_back(Slic3r::Point(0, 2*dy));
                                            wt_ap.poly.contour = std::move(r);
                                            // translation is lower-left corner in local bed coords
                                            wt_ap.translation.x() = Slic3r::scaled<coord_t>(tx_mm);
                                            wt_ap.translation.y() = Slic3r::scaled<coord_t>(ty_mm);
                                            params.excluded_regions.emplace_back(std::move(wt_ap));

                                            // Persist tower position into config/project in LOCAL bed coords (mm)
                                            const float new_wtx = float(tx_mm);
                                            const float new_wty = float(ty_mm);
                                            Slic3r::ConfigOptionFloats wtx, wty;
                                            wtx.values = { new_wtx }; wty.values = { new_wty };
                                            try { config->set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx)); } catch (...) {}
                                            try { config->set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty)); } catch (...) {}
                                            try { preset_bundle.project_config.set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx)); } catch (...) {}
                                            try { preset_bundle.project_config.set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty)); } catch (...) {}
                                        }
                                        }

                                    }
                                } catch (...) {}

                                Slic3r::Points bed_pts = Slic3r::arrangement::get_shrink_bedpts(config.get(), params);


                                std::cout << "DEBUG: arrange: selected=" << selected.size() << ", excludes=" << params.excluded_regions.size() << ", bedpts=" << bed_pts.size() << std::endl;
                                std::cout << "DEBUG: arrange params: bed_shrink_x=" << params.bed_shrink_x << ", bed_shrink_y=" << params.bed_shrink_y << std::endl;
                                for (size_t i = 0; i < bed_pts.size(); ++i) {
                                    std::cout << "DEBUG: bed_pts[" << i << "]=(" << Slic3r::unscale<double>(bed_pts[i].x()) << "," << Slic3r::unscale<double>(bed_pts[i].y()) << ")" << std::endl;
                                }
                                size_t dbg_n = std::min<size_t>(selected.size(), 3);
                                for (size_t i = 0; i < dbg_n; ++i) {
                                    Slic3r::BoundingBox bb(selected[i].poly.contour.points);
                                    auto w = bb.size().x(); auto h = bb.size().y();
                                    std::cout << "DEBUG: selected[" << i << "] bb w=" << w << " h=" << h << std::endl;
                                }

                                // Build bed and arrange
                                // Excluded regions already sized (wipe tower includes brim); no inflation on excluded regions here.
                                Slic3r::arrangement::arrange(selected, params.excluded_regions, bed_pts, params);

                                // Log arrange results
                                for (size_t i = 0; i < selected.size(); ++i) {
                                    const auto& ap = selected[i];
                                    std::cout << "DEBUG: [arrange result] item[" << i << "] bed_idx=" << ap.bed_idx
                                              << " translation=(" << Slic3r::unscale<double>(ap.translation.x()) << "," << Slic3r::unscale<double>(ap.translation.y()) << ")"
                                              << " rotation=" << ap.rotation << std::endl;
                                }

                                // Apply transforms back to instances
                                (void)Slic3r::apply_arrange_polys(selected, instances, nullptr);

                                fixed = true; // We attempted to arrange; validation below will confirm
                                // Re-apply print with updated model transforms to mirror GUI behavior
                                try {
                                    Slic3r::DynamicPrintConfig apply2 = *config;
                                    print->apply(*model, apply2);
                                } catch (const std::exception &e) {
                                    std::cout << "WARN: apply() after arrange failed: " << e.what() << std::endl;
                                }

                                // After apply(), the wipe tower position may have been reset from project_config.
                                // If center_on_bed is true, we need to re-force the calculated wipe tower position.
                                if (center_on_bed) {
                                    // Read the wipe tower position we saved in config earlier (before apply)
                                    float saved_wtx = 0.f, saved_wty = 0.f;
                                    bool has_saved_pos = false;
                                    try {
                                        if (auto* wtxopt = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_x", false)) {
                                            if (!wtxopt->values.empty()) { saved_wtx = wtxopt->values[0]; has_saved_pos = true; }
                                        }
                                        if (auto* wtyopt = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_y", false)) {
                                            if (!wtyopt->values.empty()) { saved_wty = wtyopt->values[0]; }
                                        }
                                    } catch (...) {}

                                    if (has_saved_pos) {
                                        std::cout << "DEBUG: Re-applying wipe tower position after apply(): [" << saved_wtx << "," << saved_wty << "]" << std::endl;
                                        // Update model->wipe_tower.positions to reflect the calculated position
                                        if (!model->wipe_tower.positions.empty()) {
                                            model->wipe_tower.positions[0] = Slic3r::Vec2d(saved_wtx, saved_wty);
                                        } else {
                                            model->wipe_tower.positions.push_back(Slic3r::Vec2d(saved_wtx, saved_wty));
                                        }
                                        // Also update project_config to be consistent
                                        try {
                                            Slic3r::ConfigOptionFloats wtx_new, wty_new;
                                            wtx_new.values = { saved_wtx };
                                            wty_new.values = { saved_wty };
                                            preset_bundle.project_config.set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx_new));
                                            preset_bundle.project_config.set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty_new));
                                        } catch (...) {}
                                    }
                                }

                            } catch (...) {
                                fixed = false;
                            }
                            } // end if (false) - skip arrange algorithm
                            std::cout << "DEBUG: [simple_reposition] fixed=" << (fixed ? "true" : "false") << std::endl;
                            // Log instance offsets after arrange
                            for (const auto* obj : model->objects) {
                                for (const auto* inst : obj->instances) {
                                    Slic3r::Vec3d off = inst->get_offset();
                                    std::cout << "DEBUG: [AFTER arrange] obj='" << obj->name << "' inst_offset=(" << off(0) << "," << off(1) << "," << off(2) << ")" << std::endl;
                                }
                            }
                            if (fixed) {
                                oob_msg.clear();
                                bool still_outside = check_outside(oob_msg);
                                std::cout << "DEBUG: [arrange] after arrange, still_outside=" << (still_outside ? "true" : "false") << ", msg='" << oob_msg << "'" << std::endl;
                                if (!still_outside) {
                                    std::cout << "INFO: Realinhamento automático aplicado com sucesso para caber na mesa." << std::endl;
                                } else {
                                    // Ainda fora: se for Prime Tower, tentar reposicionar automaticamente
                                    // Evitar o centro se os objetos estao centralizados - usar canto superior direito
                                    bool tried_repos = false;
                                    if (oob_msg.find("Prime Tower") != std::string::npos) {
                                        tried_repos = true;
                                        try {
                                            double minx = std::numeric_limits<double>::max();
                                            double miny = std::numeric_limits<double>::max();
                                            double maxx = std::numeric_limits<double>::lowest();
                                            double maxy = std::numeric_limits<double>::lowest();
                                            for (const auto &pt : pc.printable_area.values) {
                                                minx = std::min(minx, (double)pt(0));
                                                miny = std::min(miny, (double)pt(1));
                                                maxx = std::max(maxx, (double)pt(0));
                                                maxy = std::max(maxy, (double)pt(1));
                                            }

                                            // Get wipe tower size from config
                                            double tower_w = 35.0;
                                            try { if (auto *pw = config->opt<Slic3r::ConfigOptionFloat>("prime_tower_width", false)) tower_w = pw->value; } catch (...) {}

                                            // Try to find a collision-free position for the wipe tower
                                            // Priority: top-right corner, then bottom-right, top-left, bottom-left
                                            std::vector<std::pair<double, double>> candidate_positions = {
                                                { maxx - tower_w - 5.0, maxy - tower_w - 5.0 },  // Top-right
                                                { maxx - tower_w - 5.0, miny + 5.0 },            // Bottom-right
                                                { minx + 5.0, maxy - tower_w - 5.0 },            // Top-left
                                                { minx + 5.0, miny + 5.0 }                       // Bottom-left
                                            };

                                            // Calculate objects bounding boxes for collision check
                                            std::vector<Slic3r::BoundingBoxf3> obj_bboxes;
                                            for (const auto* obj : model->objects) {
                                                for (size_t i = 0; i < obj->instances.size(); ++i) {
                                                    obj_bboxes.push_back(obj->instance_bounding_box(i));
                                                }
                                            }

                                            double best_wtx = candidate_positions[0].first;
                                            double best_wty = candidate_positions[0].second;
                                            bool found_free = false;

                                            for (const auto& pos : candidate_positions) {
                                                double wt_x = pos.first;
                                                double wt_y = pos.second;

                                                // Check if this position collides with any object
                                                bool collides = false;
                                                for (const auto& bb : obj_bboxes) {
                                                    if (wt_x < bb.max.x() && wt_x + tower_w > bb.min.x() &&
                                                        wt_y < bb.max.y() && wt_y + tower_w > bb.min.y()) {
                                                        collides = true;
                                                        break;
                                                    }
                                                }

                                                if (!collides) {
                                                    best_wtx = wt_x;
                                                    best_wty = wt_y;
                                                    found_free = true;
                                                    std::cout << "DEBUG: Found collision-free wipe tower position: [" << best_wtx << "," << best_wty << "]" << std::endl;
                                                    break;
                                                }
                                            }

                                            if (!found_free) {
                                                std::cout << "WARN: All candidate positions collide with objects, using top-right corner" << std::endl;
                                            }

                                            const float new_wtx = float(best_wtx);
                                            const float new_wty = float(best_wty);

                                            Slic3r::ConfigOptionFloats wtx, wty;
                                            wtx.values = { new_wtx };
                                            wty.values = { new_wty };
                                            try { config->set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx)); } catch (...) {}
                                            try { config->set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty)); } catch (...) {}
                                            try { preset_bundle.project_config.set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx)); } catch (...) {}
                                            try { preset_bundle.project_config.set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty)); } catch (...) {}

                                            try {
                                                Slic3r::DynamicPrintConfig apply2 = *config;
                                                print->apply(*model, apply2);
                                            } catch (const std::exception &e) {
                                                std::cout << "WARN: re-apply após reposicionar Prime Tower falhou: " << e.what() << std::endl;
                                            }

                                            oob_msg.clear();
                                            if (!check_outside(oob_msg)) {
                                                std::cout << "INFO: Prime Tower reposicionada automaticamente para o centro da mesa." << std::endl;
                                            } else {
                                                last_error = oob_msg + " Mesmo após realinhamento e reposicionamento da Prime Tower.";
                                                return false;
                                            }
                                        } catch (const std::exception &e) {
                                        	last_error = std::string("Falha ao reposicionar Prime Tower automaticamente: ") + e.what();
                                            return false;
                                        }
                                    }

                                    if (!tried_repos) {
                                        last_error = oob_msg + " Mesmo após realinhamento automático.";
                                        return false;
                                    }
                                }
                            } else {
                                // Se o realinhamento padrão falhar, ainda tenta reposicionar Prime Tower
                                if (oob_msg.find("Prime Tower") != std::string::npos) {
                                    try {
                                        double minx = std::numeric_limits<double>::max();
                                        double miny = std::numeric_limits<double>::max();
                                        double maxx = std::numeric_limits<double>::lowest();
                                        double maxy = std::numeric_limits<double>::lowest();
                                        for (const auto &pt : pc.printable_area.values) {
                                            minx = std::min(minx, (double)pt(0));
                                            miny = std::min(miny, (double)pt(1));
                                            maxx = std::max(maxx, (double)pt(0));
                                            maxy = std::max(maxy, (double)pt(1));
                                        }

                                        // Get wipe tower size from config
                                        double tower_w = 35.0;
                                        try { if (auto *pw = config->opt<Slic3r::ConfigOptionFloat>("prime_tower_width", false)) tower_w = pw->value; } catch (...) {}

                                        // Find collision-free position for wipe tower
                                        std::vector<std::pair<double, double>> candidate_positions = {
                                            { maxx - tower_w - 5.0, maxy - tower_w - 5.0 },
                                            { maxx - tower_w - 5.0, miny + 5.0 },
                                            { minx + 5.0, maxy - tower_w - 5.0 },
                                            { minx + 5.0, miny + 5.0 }
                                        };

                                        std::vector<Slic3r::BoundingBoxf3> obj_bboxes;
                                        for (const auto* obj : model->objects) {
                                            for (size_t i = 0; i < obj->instances.size(); ++i) {
                                                obj_bboxes.push_back(obj->instance_bounding_box(i));
                                            }
                                        }

                                        double best_wtx = candidate_positions[0].first;
                                        double best_wty = candidate_positions[0].second;

                                        for (const auto& pos : candidate_positions) {
                                            double wt_x = pos.first;
                                            double wt_y = pos.second;
                                            bool collides = false;
                                            for (const auto& bb : obj_bboxes) {
                                                if (wt_x < bb.max.x() && wt_x + tower_w > bb.min.x() &&
                                                    wt_y < bb.max.y() && wt_y + tower_w > bb.min.y()) {
                                                    collides = true;
                                                    break;
                                                }
                                            }
                                            if (!collides) {
                                                best_wtx = wt_x;
                                                best_wty = wt_y;
                                                break;
                                            }
                                        }

                                        const float new_wtx = float(best_wtx);
                                        const float new_wty = float(best_wty);

                                        Slic3r::ConfigOptionFloats wtx, wty;
                                        wtx.values = { new_wtx };
                                        wty.values = { new_wty };
                                        try { config->set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx)); } catch (...) {}
                                        try { config->set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty)); } catch (...) {}
                                        try { preset_bundle.project_config.set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx)); } catch (...) {}
                                        try { preset_bundle.project_config.set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty)); } catch (...) {}

                                        try {
                                            Slic3r::DynamicPrintConfig apply2 = *config;
                                            print->apply(*model, apply2);
                                        } catch (const std::exception &e) {
                                            std::cout << "WARN: re-apply após reposicionar Prime Tower falhou: " << e.what() << std::endl;
                                        }

                                        oob_msg.clear();
                                        if (!check_outside(oob_msg)) {
                                            std::cout << "INFO: Prime Tower reposicionada automaticamente para posicao livre." << std::endl;
                                        } else {
                                            last_error = oob_msg + " Mesmo após tentar reposicionar automaticamente a Prime Tower.";
                                            return false;
                                        }
                                    } catch (const std::exception &e) {
                                        last_error = std::string("Falha ao reposicionar Prime Tower automaticamente: ") + e.what();
                                        return false;
                                    }
                                } else {
                                    last_error = oob_msg + " (falha ao realinhar automaticamente)";
                                    return false;
                                }
                            }
                        } else {
                            last_error = oob_msg;
                            return false;
                        }
                    }
                }
            } catch (const std::exception &e) {
                std::cout << "WARN: build-volume precheck falhou: " << e.what() << std::endl;
            }


            // Set up status callback to track where segfault occurs
            print->set_status_callback([](const Slic3r::PrintBase::SlicingStatus& status) {
                std::cout << "PROCESS-STATUS: " << status.percent << "% - " << status.text << std::endl;
                std::cout.flush();
            });

            // DEBUG: Log state before process() - FOCUS ON MEMORY ADDRESSES
            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout << "DEBUG: ABOUT TO CALL print->process()" << std::endl;
            std::cout << "DEBUG: Print object address: " << print.get() << std::endl;
            std::cout << "DEBUG: Print objects count: " << print->objects().size() << std::endl;

            // Log each PrintObject's memory address and validity
            for (size_t i = 0; i < print->objects().size(); ++i) {
                const auto* obj = print->objects()[i];
                std::cout << "DEBUG: PrintObject[" << i << "] address: " << obj << std::endl;

                // Try to access the object to see if it's valid
                try {
                    std::cout << "DEBUG: PrintObject[" << i << "] layer_count: " << obj->layer_count() << std::endl;
                    std::cout << "DEBUG: PrintObject[" << i << "] has shared_object: " << (obj->get_shared_object() ? "YES" : "NO") << std::endl;
                } catch (...) {
                    std::cout << "ERROR: PrintObject[" << i << "] is INVALID or CORRUPTED!" << std::endl;
                }
            }

            std::cout << "DEBUG: ========================================" << std::endl;

            dbg_log("🔍 [PROCESS] Calling print->process()");

            // Add detailed logging to track exactly where segfault occurs
            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout << "DEBUG: ENTERING print->process() NOW" << std::endl;
            std::cout << "DEBUG: Print address: " << print.get() << std::endl;
            std::cout << "DEBUG: Print has_wipe_tower(): " << print->has_wipe_tower() << std::endl;
            std::cout << "DEBUG: Print config().enable_prime_tower: " << print->config().enable_prime_tower.value << std::endl;
            std::cout << "DEBUG: Print config().single_extruder_multi_material: " << print->config().single_extruder_multi_material.value << std::endl;
            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout << "DEBUG: COMPREHENSIVE STATE CHECK BEFORE process()" << std::endl;
            std::cout << "DEBUG: ========================================" << std::endl;

            // 1. Print object basic state
            std::cout << "DEBUG: Print address: " << print.get() << std::endl;
            std::cout << "DEBUG: Print objects count: " << print->objects().size() << std::endl;
            std::cout << "DEBUG: Print technology: " << (print->technology() == Slic3r::ptFFF ? "FFF" : "SLA") << std::endl;

            // 2. Print state flags
            std::cout << "DEBUG: Print canceled: " << (print->canceled() ? "YES" : "NO") << std::endl;
            std::cout << "DEBUG: Print finished: " << (print->finished() ? "YES" : "NO") << std::endl;

            // 3. Wipe tower state
            std::cout << "DEBUG: has_wipe_tower: " << (print->has_wipe_tower() ? "YES" : "NO") << std::endl;
            std::cout << "DEBUG: config.enable_prime_tower: " << print->config().enable_prime_tower.value << std::endl;
            std::cout << "DEBUG: config.single_extruder_multi_material: " << print->config().single_extruder_multi_material.value << std::endl;

            // 4. Model state
            std::cout << "DEBUG: Model address: " << model.get() << std::endl;
            std::cout << "DEBUG: Model objects count: " << model->objects.size() << std::endl;

            // 5. Check each PrintObject and its relationships
            for (size_t i = 0; i < print->objects().size(); i++) {
                auto* obj = print->objects()[i];
                std::cout << "DEBUG: PrintObject[" << i << "] address: " << obj << std::endl;
                std::cout << "DEBUG: PrintObject[" << i << "] model_object: " << obj->model_object() << std::endl;
                std::cout << "DEBUG: PrintObject[" << i << "] model_object->name: " << obj->model_object()->name << std::endl;
                std::cout << "DEBUG: PrintObject[" << i << "] instances count: " << obj->instances().size() << std::endl;
                std::cout << "DEBUG: PrintObject[" << i << "] layer_count: " << obj->layer_count() << std::endl;
                std::cout << "DEBUG: PrintObject[" << i << "] support_layer_count: " << obj->support_layer_count() << std::endl;

                // Check if this object has a shared object
                bool has_shared = false;
                try {
                    // We can't directly access m_shared_object, but we can check layer count
                    // If layer_count is 0 before process(), it will be set during process()
                    has_shared = (obj->layer_count() > 0);
                } catch (...) {
                    has_shared = false;
                }
                std::cout << "DEBUG: PrintObject[" << i << "] likely_has_shared: " << (has_shared ? "YES" : "NO") << std::endl;
                std::cout.flush();
            }

            // 6. Config state
            std::cout << "DEBUG: Config address: " << config.get() << std::endl;
            auto* fil_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
            auto* fil_diameter = config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
            std::cout << "DEBUG: Config filament_colour count: " << (fil_colour ? fil_colour->values.size() : 0) << std::endl;
            std::cout << "DEBUG: Config filament_diameter count: " << (fil_diameter ? fil_diameter->values.size() : 0) << std::endl;

            // 7. Print internal state (what we can access)
            std::cout << "DEBUG: Print extruders count: " << print->extruders().size() << std::endl;
            // Note: Print doesn't have a regions() method, regions are per PrintObject

            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout << "DEBUG: STATE CHECK COMPLETE - CALLING process()" << std::endl;
            std::cout << "DEBUG: ========================================" << std::endl;
            std::cout.flush();

            try {
                std::cout << "DEBUG: ========================================" << std::endl;
                std::cout << "DEBUG: CALLING print->process() NOW" << std::endl;
                std::cout << "DEBUG: ========================================" << std::endl;

                // CRITICAL: Log model characteristics that might cause issues
                std::cout << "DEBUG: MODEL CHARACTERISTICS:" << std::endl;
                std::cout << "DEBUG:   Total model objects: " << model->objects.size() << std::endl;
                std::cout << "DEBUG:   Total print objects: " << print->objects().size() << std::endl;
                std::cout << "DEBUG:   Extruders count: " << print->extruders().size() << std::endl;
                std::cout << "DEBUG:   Has wipe tower: " << (print->has_wipe_tower() ? "YES" : "NO") << std::endl;

                // Count objects that will share geometry (check if multiple PrintObjects reference same ModelObject)
                std::set<const Slic3r::ModelObject*> unique_model_objects;
                for (const auto* obj : print->objects()) {
                    unique_model_objects.insert(obj->model_object());
                }
                int total_print_objects = print->objects().size();
                int unique_geometries = unique_model_objects.size();
                int shared_count = total_print_objects - unique_geometries;

                std::cout << "DEBUG:   Unique geometries: " << unique_geometries << std::endl;
                std::cout << "DEBUG:   Total print objects: " << total_print_objects << std::endl;
                std::cout << "DEBUG:   Objects that will share geometry: " << shared_count << std::endl;

                // Check model complexity (vertex count)
                size_t total_vertices = 0;
                size_t total_triangles = 0;
                for (const auto* obj : model->objects) {
                    for (const auto& volume : obj->volumes) {
                        if (volume->mesh().its.vertices.size() > 0) {
                            total_vertices += volume->mesh().its.vertices.size();
                            total_triangles += volume->mesh().its.indices.size();
                        }
                    }
                }
                std::cout << "DEBUG:   Total vertices: " << total_vertices << std::endl;
                std::cout << "DEBUG:   Total triangles: " << total_triangles << std::endl;

                // DEBUG: Log final filament_colour state before process()
                try {
                    const auto& fpc = print->full_print_config();
                    if (auto* fil_colour_final = fpc.opt<Slic3r::ConfigOptionStrings>("filament_colour")) {
                        std::cout << "🎨 [COLOR DEBUG] FINAL filament_colour before process() (" << fil_colour_final->values.size() << "): ";
                        for (const auto& c : fil_colour_final->values) std::cout << c << " ";
                        std::cout << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "🎨 [COLOR DEBUG] Failed to read final filament_colour: " << e.what() << std::endl;
                }

                std::cout << "DEBUG: ========================================" << std::endl;
                std::cout.flush();

                // ============================================================
                // PRE-SLICE VALIDATION: Check object positioning and collisions
                // ============================================================
                std::cout << "DEBUG: ========================================" << std::endl;
                std::cout << "DEBUG: PRE-SLICE VALIDATION CHECK" << std::endl;
                std::cout << "DEBUG: ========================================" << std::endl;

                try {
                    // Build the BuildVolume from printable_area
                    std::vector<Slic3r::Vec2d> printable_area;
                    double printable_height = 256.0; // default

                    try {
                        if (auto* pa = config->opt<Slic3r::ConfigOptionPoints>("printable_area", false)) {
                            for (const auto& pt : pa->values) {
                                printable_area.push_back(Slic3r::Vec2d(pt.x(), pt.y()));
                            }
                        }
                        if (auto* ph = config->opt<Slic3r::ConfigOptionFloat>("printable_height", false)) {
                            printable_height = ph->value;
                        }
                    } catch (...) {}

                    if (printable_area.size() >= 3) {
                        Slic3r::BuildVolume build_volume(printable_area, printable_height, {}, {});
                        Slic3r::BoundingBoxf3 bed_bb = build_volume.bounding_volume();

                        std::cout << "DEBUG: Build volume: [" << bed_bb.min.x() << ", " << bed_bb.min.y() << "] to ["
                                  << bed_bb.max.x() << ", " << bed_bb.max.y() << "] height=" << printable_height << std::endl;

                        std::vector<std::string> validation_errors;
                        std::vector<std::string> validation_warnings;

                        // Check each object instance position
                        for (const auto* obj : model->objects) {
                            for (size_t inst_idx = 0; inst_idx < obj->instances.size(); ++inst_idx) {
                                const auto* inst = obj->instances[inst_idx];

                                // Get instance bounding box in world coordinates
                                Slic3r::BoundingBoxf3 inst_bb = obj->instance_bounding_box(inst_idx);

                                // Check against build volume
                                auto state = build_volume.volume_state_bbox(inst_bb, /*ignore_bottom=*/true);

                                std::string obj_name = obj->name.empty() ? ("Object_" + std::to_string(inst_idx)) : obj->name;
                                std::string inst_name = obj_name + " (instance " + std::to_string(inst_idx) + ")";

                                std::cout << "DEBUG: Checking " << inst_name << ": bb=[" << inst_bb.min.x() << "," << inst_bb.min.y()
                                          << " to " << inst_bb.max.x() << "," << inst_bb.max.y() << "]";

                                switch (state) {
                                    case Slic3r::BuildVolume::ObjectState::Inside:
                                        std::cout << " -> INSIDE (OK)" << std::endl;
                                        break;
                                    case Slic3r::BuildVolume::ObjectState::Colliding:
                                        std::cout << " -> COLLIDING (ERROR)" << std::endl;
                                        validation_errors.push_back(inst_name + " is colliding with the build volume boundary");
                                        break;
                                    case Slic3r::BuildVolume::ObjectState::Outside:
                                        std::cout << " -> OUTSIDE (ERROR)" << std::endl;
                                        validation_errors.push_back(inst_name + " is completely outside the print bed");
                                        break;
                                    case Slic3r::BuildVolume::ObjectState::Below:
                                        std::cout << " -> BELOW (WARNING)" << std::endl;
                                        validation_warnings.push_back(inst_name + " is below the print bed");
                                        break;
                                    case Slic3r::BuildVolume::ObjectState::Limited:
                                        std::cout << " -> LIMITED (WARNING)" << std::endl;
                                        validation_warnings.push_back(inst_name + " is in a limited area");
                                        break;
                                }

                                // Additional check: explicit coordinate bounds
                                if (inst_bb.min.x() < bed_bb.min.x() - 1.0 || inst_bb.max.x() > bed_bb.max.x() + 1.0 ||
                                    inst_bb.min.y() < bed_bb.min.y() - 1.0 || inst_bb.max.y() > bed_bb.max.y() + 1.0) {
                                    std::string coord_error = inst_name + " extends outside print area: object=[" +
                                        std::to_string(inst_bb.min.x()) + "," + std::to_string(inst_bb.min.y()) + " to " +
                                        std::to_string(inst_bb.max.x()) + "," + std::to_string(inst_bb.max.y()) + "], bed=[" +
                                        std::to_string(bed_bb.min.x()) + "," + std::to_string(bed_bb.min.y()) + " to " +
                                        std::to_string(bed_bb.max.x()) + "," + std::to_string(bed_bb.max.y()) + "]";
                                    std::cout << "DEBUG: " << coord_error << std::endl;
                                    // Only add if not already detected
                                    if (state == Slic3r::BuildVolume::ObjectState::Inside) {
                                        validation_errors.push_back(coord_error);
                                    }
                                }
                            }
                        }

                        // Check wipe tower position if enabled
                        if (print->has_wipe_tower()) {
                            double wt_x = 0, wt_y = 0;
                            try {
                                if (auto* wtx = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_x", false)) {
                                    if (!wtx->values.empty()) wt_x = wtx->values[0];
                                }
                                if (auto* wty = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_y", false)) {
                                    if (!wty->values.empty()) wt_y = wty->values[0];
                                }
                            } catch (...) {}

                            double wt_width = 60.0; // default
                            try {
                                if (auto* pw = config->opt<Slic3r::ConfigOptionFloat>("prime_tower_width", false)) {
                                    wt_width = pw->value;
                                }
                            } catch (...) {}

                            // Wipe tower bounding box (assuming square-ish shape)
                            Slic3r::BoundingBoxf3 wt_bb(
                                Slic3r::Vec3d(wt_x, wt_y, 0),
                                Slic3r::Vec3d(wt_x + wt_width, wt_y + wt_width, printable_height)
                            );

                            std::cout << "DEBUG: Wipe tower position: [" << wt_x << "," << wt_y << "] width=" << wt_width << std::endl;

                            auto wt_state = build_volume.volume_state_bbox(wt_bb, /*ignore_bottom=*/true);
                            if (wt_state != Slic3r::BuildVolume::ObjectState::Inside) {
                                std::string wt_error = "Wipe/Prime tower is outside print area at position (" +
                                    std::to_string(wt_x) + ", " + std::to_string(wt_y) + ")";
                                std::cout << "DEBUG: " << wt_error << std::endl;
                                validation_errors.push_back(wt_error);
                            }

                            // Check wipe tower collision with objects
                            for (const auto* obj : model->objects) {
                                for (size_t inst_idx = 0; inst_idx < obj->instances.size(); ++inst_idx) {
                                    Slic3r::BoundingBoxf3 inst_bb = obj->instance_bounding_box(inst_idx);
                                    // Check XY overlap (ignore Z for tower collision)
                                    if (wt_bb.min.x() < inst_bb.max.x() && wt_bb.max.x() > inst_bb.min.x() &&
                                        wt_bb.min.y() < inst_bb.max.y() && wt_bb.max.y() > inst_bb.min.y()) {
                                        std::string obj_name = obj->name.empty() ? ("Object_" + std::to_string(inst_idx)) : obj->name;
                                        std::string collision_error = "Wipe tower collides with " + obj_name + " (instance " +
                                            std::to_string(inst_idx) + ")";
                                        std::cout << "DEBUG: " << collision_error << std::endl;
                                        validation_errors.push_back(collision_error);
                                    }
                                }
                            }
                        }

                        // Report validation results
                        std::cout << "DEBUG: ========================================" << std::endl;
                        std::cout << "DEBUG: VALIDATION RESULTS:" << std::endl;
                        std::cout << "DEBUG:   Errors: " << validation_errors.size() << std::endl;
                        std::cout << "DEBUG:   Warnings: " << validation_warnings.size() << std::endl;

                        for (const auto& warn : validation_warnings) {
                            std::cout << "DEBUG: WARNING: " << warn << std::endl;
                        }
                        for (const auto& err : validation_errors) {
                            std::cout << "DEBUG: ERROR: " << err << std::endl;
                        }
                        std::cout << "DEBUG: ========================================" << std::endl;

                        // If there are errors, throw exception to prevent slicing
                        if (!validation_errors.empty()) {
                            std::string error_msg = "Pre-slice validation failed with " + std::to_string(validation_errors.size()) + " error(s):\n";
                            for (const auto& err : validation_errors) {
                                error_msg += "  - " + err + "\n";
                            }
                            throw std::runtime_error(error_msg);
                        }

                        std::cout << "DEBUG: Pre-slice validation PASSED" << std::endl;
                    } else {
                        std::cout << "DEBUG: Skipping validation - no valid printable_area defined" << std::endl;
                    }
                } catch (const std::runtime_error& ve) {
                    // Re-throw validation errors
                    throw;
                } catch (const std::exception& e) {
                    std::cout << "DEBUG: Validation check failed with exception: " << e.what() << std::endl;
                    // Continue with slicing - don't block on validation failures
                }

                std::cout << "DEBUG: ========================================" << std::endl;
                std::cout.flush();

                print->process();

                std::cout << "DEBUG: ========================================" << std::endl;
                std::cout << "DEBUG: print->process() COMPLETED SUCCESSFULLY!" << std::endl;
                std::cout << "DEBUG: ========================================" << std::endl;
                std::cout.flush();

                dbg_log("🔍 [PROCESS] print->process() completed successfully!");
            } catch (const std::exception& e) {
                std::cout << "ERROR: ========================================" << std::endl;
                std::cout << "ERROR: EXCEPTION CAUGHT in print->process()" << std::endl;
                std::cout << "ERROR: Exception message: " << e.what() << std::endl;
                std::cout << "ERROR: Exception type: " << typeid(e).name() << std::endl;
                std::cout << "ERROR: ========================================" << std::endl;

                // Log state after crash
                std::cout << "DEBUG: STATE AFTER EXCEPTION:" << std::endl;
                std::cout << "DEBUG: Print object address: " << print.get() << std::endl;
                try {
                    std::cout << "DEBUG: Print objects count: " << print->objects().size() << std::endl;
                    std::cout << "DEBUG: Print canceled: " << (print->canceled() ? "YES" : "NO") << std::endl;
                } catch (...) {
                    std::cout << "DEBUG: Cannot access Print state (corrupted?)" << std::endl;
                }
                std::cout << "ERROR: ========================================" << std::endl;
                std::cout.flush();
                throw;
            } catch (...) {
                std::cout << "ERROR: ========================================" << std::endl;
                std::cout << "ERROR: UNKNOWN EXCEPTION in print->process()" << std::endl;
                std::cout << "ERROR: ========================================" << std::endl;

                // Log state after crash
                std::cout << "DEBUG: STATE AFTER UNKNOWN EXCEPTION:" << std::endl;
                std::cout << "DEBUG: Print object address: " << print.get() << std::endl;
                try {
                    std::cout << "DEBUG: Print objects count: " << print->objects().size() << std::endl;
                } catch (...) {
                    std::cout << "DEBUG: Cannot access Print state (corrupted?)" << std::endl;
                }
                std::cout << "ERROR: ========================================" << std::endl;
                std::cout.flush();
                throw;
            }

            std::cout << "DEBUG: Print processing completed" << std::endl;

            try {
                const auto &fpc = print->full_print_config();
            } catch (...) {}

            // After process: keep plate_origin semantics consistent
            {
                try {
                    if (center_on_bed) {
                        bool is_bbl = false; try { is_bbl = preset_bundle.is_bbl_vendor(); } catch (...) {}
                        if (!is_bbl) {
                            // Already centered by instance shift; keep plate_origin at zero
                            std::cout << "DEBUG: center_on_bed (NON-BBL, AFTER process) => keeping instances-centered; plate_origin=(0,0)" << std::endl;
                        } else {
                            (void)center_plate_origin_to_bed_center();
                            auto po = print->get_plate_origin();
                            std::cout << "DEBUG: center_on_bed (AFTER process) => plate_origin=(" << po(0) << "," << po(1) << ")" << std::endl;
                        }
                    } else {
                        // Derive bed size from printable area to compute logical stride
                        Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                        if (!bed_pts.empty()) {
                            long minx = std::numeric_limits<long>::max();
                            long maxx = std::numeric_limits<long>::min();
                            long miny = std::numeric_limits<long>::max();
                            long maxy = std::numeric_limits<long>::min();
                            for (const auto &p : bed_pts) { if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y(); }
                            const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
                            const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
                            if (bed_w_mm > 0.0 && bed_d_mm > 0.0) {
                                constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
                                const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                                const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                                const int total = (total_plates_count > 0 ? total_plates_count : 1);
                                const int cols = (int)std::ceil(std::sqrt((double)total));
                                const int idx0 = (plate_id > 0 ? plate_id - 1 : 0);
                                const int row = idx0 / cols;
                                const int col = idx0 % cols;
                                const double origin_x =  (col * stride_x);
                                const double origin_y = -(row * stride_y);
                                print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
                                std::cout << "DEBUG: plate_origin (from plate index, fallback) => origin=(" << origin_x << "," << origin_y
                                          << ") stride=(" << stride_x << "," << stride_y << ") idx=" << idx0 << " cols=" << cols
                                          << " total=" << total << std::endl;
                            }
                        }
                    }
                } catch (const std::exception &e) {
                    std::cout << "WARN: set_plate_origin failed: " << e.what() << std::endl;
                }
            }

            // Final safety validation: block G-code export if any element is outside the printable area
            try {
                const auto &pc_final = print->config();
                Slic3r::BuildVolume build_volume_final(pc_final.printable_area.values, pc_final.printable_height, {}, {});
                if (build_volume_final.valid()) {
                    std::string oob_msg_final;
                    bool outside_final = false;

                    // Convert instance bounding boxes to local bed coords using plate_origin
                    const Slic3r::Vec3d po_fin = print->get_plate_origin();
                    const Slic3r::Vec3d shift_fin(-po_fin(0), -po_fin(1), 0.0);

                    // Check model instances
                    for (const auto *obj : model->objects) {
                        if (!obj) continue;
                        for (const auto *inst : obj->instances) {
                            if (!inst) continue;
                            Slic3r::BoundingBoxf3 bb = obj->instance_bounding_box(*inst);
                            Slic3r::BoundingBoxf3 bb_local(bb.min + shift_fin, bb.max + shift_fin);
                            auto st = build_volume_final.volume_state_bbox(bb_local, /*ignore_bottom=*/true);
                            if (st != Slic3r::BuildVolume::ObjectState::Inside) {
                                std::string name = obj->name.empty() ? std::string("objeto") : obj->name;
                                oob_msg_final = std::string("Elementos fora da área de impressão: '") + name + "' " +
                                                (st == Slic3r::BuildVolume::ObjectState::Colliding ? "parcialmente" : "totalmente") +
                                                " fora da área para o perfil selecionado.";
                                outside_final = true;
                                break;
                            }
                        }
                        if (outside_final) break;
                    }

                    // Check Prime/Wipe Tower
                    if (!outside_final) {
                        bool prime_enabled = false; try { prime_enabled = pc_final.enable_prime_tower.getBool(); } catch (...) {}
                        if (prime_enabled || print->has_wipe_tower()) {
                            float wtx = 0.f, wty = 0.f;
                            try { wtx = pc_final.wipe_tower_x.get_at(0); } catch (...) {}
                            try { wty = pc_final.wipe_tower_y.get_at(0); } catch (...) {}
                            Slic3r::Vec3d pt(double(wtx), double(wty), 0.0);
                            Slic3r::BoundingBoxf3 wtbb(pt, pt);
                            auto st = build_volume_final.volume_state_bbox(wtbb, /*ignore_bottom=*/true);
                            if (st != Slic3r::BuildVolume::ObjectState::Inside) {
                                oob_msg_final = "Elementos fora da área de impressão: Prime Tower fora da área válida para o perfil selecionado.";
                                outside_final = true;
                            }
                        }
                    }

                    if (outside_final) {
                        last_error = std::string("Validação final falhou. ") + oob_msg_final + " O G-code não será gerado para proteger a impressora.";
                        return false;
                    }
                }
            } catch (const std::exception &e) {
                std::cout << "WARN: final build-volume validation failed: " << e.what() << std::endl;
            }

            // Decide export target by output extension
            std::filesystem::path out_path(output_file);
            std::string out_ext = out_path.extension().string();
            std::transform(out_ext.begin(), out_ext.end(), out_ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

            const bool export_3mf = (out_ext == ".3mf");

            if (export_3mf) {
                // WORKAROUND: OrcaSlicer's 3MF export has bugs that cause segfaults
                // For Klipper, we only need the .gcode file anyway
                // So we just export .gcode and rename it to .3mf if requested
                std::cout << "DEBUG: Exporting GCode (3MF export disabled due to OrcaSlicer bugs) to: " << output_file << std::endl;

                // Prepare temp G-code path: use stem (without any extensions) + ".gcode"
                // Example: "/tmp/orca-xxx.gcode.3mf" -> "/tmp/orca-xxx.gcode"
                std::filesystem::path tmp_gcode = out_path.parent_path() / out_path.stem();
                tmp_gcode.replace_extension(".gcode");

                // Remove any existing files (target .3mf and temp .gcode)
                if (std::filesystem::exists(output_file)) std::filesystem::remove(output_file);
                if (std::filesystem::exists(tmp_gcode))   std::filesystem::remove(tmp_gcode);



                // CRITICAL: Synchronize flush_volumes_matrix in full_print_config() BEFORE export
                // This prevents the "Flush volumes matrix do not match to the correct size" error
                std::cout << "DEBUG: [PRE-SYNC] About to synchronize flush_volumes in full_print_config()" << std::endl;
                std::cout.flush();
                try {
                    auto& fpc = const_cast<Slic3r::DynamicPrintConfig&>(print->full_print_config());
                    auto* fil_colour = fpc.opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
                    auto* flush_matrix = fpc.opt<Slic3r::ConfigOptionFloats>("flush_volumes_matrix", false);
                    auto* flush_vector = fpc.opt<Slic3r::ConfigOptionFloats>("flush_volumes_vector", false);
                    auto* flush_multiplier = fpc.opt<Slic3r::ConfigOptionFloats>("flush_multiplier", false);

                    size_t filament_count = fil_colour ? fil_colour->values.size() : 1;
                    size_t heads_count = flush_multiplier ? flush_multiplier->values.size() : 1;
                    size_t expected_matrix_size = filament_count * filament_count * heads_count;

                    std::cout << "DEBUG: [BEFORE EXPORT] Synchronizing flush_volumes_matrix - filament_count=" << filament_count
                              << ", heads_count=" << heads_count
                              << ", expected_matrix_size=" << expected_matrix_size << std::endl;
                    std::cout.flush();

                    if (flush_matrix) {
                        size_t current_size = flush_matrix->values.size();
                        if (current_size != expected_matrix_size) {
                            std::cout << "DEBUG: [BEFORE EXPORT] Resizing flush_volumes_matrix from " << current_size
                                      << " to " << expected_matrix_size << std::endl;
                            // Create a proper matrix: diagonal = 0 (same filament), off-diagonal = 280 (default purge)
                            std::vector<double> new_matrix(expected_matrix_size, 280.0);
                            for (size_t h = 0; h < heads_count; ++h) {
                                for (size_t i = 0; i < filament_count; ++i) {
                                    // Set diagonal elements to 0 (no purge needed for same filament)
                                    size_t idx = h * (filament_count * filament_count) + i * filament_count + i;
                                    new_matrix[idx] = 0.0;
                                }
                            }
                            flush_matrix->values = new_matrix;
                        }
                    } else if (filament_count > 0) {
                        // flush_matrix doesn't exist, create it
                        std::cout << "DEBUG: [BEFORE EXPORT] Creating flush_volumes_matrix with size " << expected_matrix_size << std::endl;
                        std::vector<double> new_matrix(expected_matrix_size, 280.0);
                        for (size_t h = 0; h < heads_count; ++h) {
                            for (size_t i = 0; i < filament_count; ++i) {
                                size_t idx = h * (filament_count * filament_count) + i * filament_count + i;
                                new_matrix[idx] = 0.0;
                            }
                        }
                        fpc.set_key_value("flush_volumes_matrix", new Slic3r::ConfigOptionFloats(new_matrix));
                    }

                    // Also synchronize flush_volumes_vector (2 values per filament)
                    size_t expected_vector_size = 2 * filament_count;
                    if (flush_vector) {
                        size_t current_size = flush_vector->values.size();
                        if (current_size != expected_vector_size) {
                            std::cout << "DEBUG: [BEFORE EXPORT] Resizing flush_volumes_vector from " << current_size
                                      << " to " << expected_vector_size << std::endl;
                            std::vector<double> new_vector(expected_vector_size, 140.0);
                            flush_vector->values = new_vector;
                        }
                    } else if (filament_count > 0) {
                        std::cout << "DEBUG: [BEFORE EXPORT] Creating flush_volumes_vector with size " << expected_vector_size << std::endl;
                        std::vector<double> new_vector(expected_vector_size, 140.0);
                        fpc.set_key_value("flush_volumes_vector", new Slic3r::ConfigOptionFloats(new_vector));
                    }

                    // Ensure flush_multiplier has correct size
                    if (flush_multiplier && flush_multiplier->values.empty()) {
                        flush_multiplier->values.push_back(1.0);
                    } else if (!flush_multiplier) {
                        fpc.set_key_value("flush_multiplier", new Slic3r::ConfigOptionFloats({1.0}));
                    }

                    // FIX: Clear bed_exclude_area to prevent crash in OrcaSlicer GUI when opening the 3MF
                    // The default value of ["0x0"] is malformed (not a multiple of 4 points for rectangles)
                    // This causes a crash in Model::setPrintSpeedTable() when diff() returns an empty polygon list
                    auto* bed_exclude = fpc.opt<Slic3r::ConfigOptionPoints>("bed_exclude_area", false);
                    if (bed_exclude && !bed_exclude->values.empty()) {
                        // Only clear if it's the default malformed value (single point at 0,0)
                        if (bed_exclude->values.size() == 1 &&
                            bed_exclude->values[0].x() == 0.0 && bed_exclude->values[0].y() == 0.0) {
                            std::cout << "DEBUG: Clearing malformed bed_exclude_area (default [0x0]) to prevent OrcaSlicer crash" << std::endl;
                            bed_exclude->values.clear();
                        } else if (bed_exclude->values.size() % 4 != 0) {
                            // If not a multiple of 4 points, clear it (malformed)
                            std::cout << "DEBUG: Clearing malformed bed_exclude_area (size " << bed_exclude->values.size()
                                      << " is not a multiple of 4) to prevent OrcaSlicer crash" << std::endl;
                            bed_exclude->values.clear();
                        }
                    }
                } catch (const std::exception& e) {
                    std::cout << "WARN: Failed to synchronize flush_volumes_matrix: " << e.what() << std::endl;
                }

                // Verify synchronization was successful
                try {
                    const auto& fpc = print->full_print_config();
                    auto* fil_colour = fpc.option<Slic3r::ConfigOptionStrings>("filament_colour");
                    auto* flush_matrix = fpc.option<Slic3r::ConfigOptionFloats>("flush_volumes_matrix");
                    auto* flush_multiplier = fpc.option<Slic3r::ConfigOptionFloats>("flush_multiplier");

                    size_t filament_count = fil_colour ? fil_colour->values.size() : 1;
                    size_t heads_count = flush_multiplier ? flush_multiplier->values.size() : 1;
                    size_t expected_matrix_size = filament_count * filament_count * heads_count;
                    size_t actual_matrix_size = flush_matrix ? flush_matrix->values.size() : 0;

                    std::cout << "DEBUG: [VERIFICATION] filament_count=" << filament_count
                              << ", heads_count=" << heads_count
                              << ", expected_matrix=" << expected_matrix_size
                              << ", actual_matrix=" << actual_matrix_size << std::endl;

                    if (actual_matrix_size != expected_matrix_size && filament_count > 1) {
                        std::cout << "ERROR: flush_volumes_matrix size mismatch! Will cause error in append_full_config" << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "WARN: Failed to verify flush_volumes_matrix: " << e.what() << std::endl;
                }

                // Export raw G-code first
                Slic3r::GCodeProcessorResult proc_result;
                std::cout << "DEBUG: Exporting intermediate G-code to: " << tmp_gcode.string() << std::endl;
                try {
                    auto po = print->get_plate_origin();
                    std::cout << "DEBUG: plate_origin at export => (" << po(0) << "," << po(1) << ")" << std::endl;
                    // Export using current config/model; GUI exporter derives plate-local values itself
                    std::string gcode_path = print->export_gcode(tmp_gcode.string(), &proc_result, nullptr);
                    (void)gcode_path;
                } catch (const std::exception &e) {
                    last_error = std::string("G-code export failed before 3MF packaging: ") + e.what();
                    return false;
                } catch (...) {
                    last_error = "G-code export failed before 3MF packaging (unknown error)";
                    return false;
                }

                if (!std::filesystem::exists(tmp_gcode)) {
                    last_error = "Intermediate G-code not found";
                    return false;
                }

                // Generate proper 3MF file
                // Note: This works for 99% of models, but may segfault on some multi-object models

#if 1  // 3MF EXPORT ENABLED
                // Prepare PlateData for store_bbs_3mf
                Slic3r::PlateData plate;
                // FIX: Always use plate_index = 0 for single-plate export
                // OrcaSlicer uses (plate_index + 1) for file naming, so:
                // - plate_index = 0 → generates plate_1.gcode
                // - plate_index = 1 → generates plate_2.gcode
                // Since we're exporting only ONE plate (the current one), it should always be index 0
                plate.plate_index = 0;
                plate.is_sliced_valid = true;

                // Set gcode_file so it gets embedded in the 3MF
                plate.gcode_file = tmp_gcode.string();

                plate.parse_filament_info(&proc_result);

                // CRITICAL: Populate objects_and_instances to match OrcaSlicer GUI behavior
                // This tells the 3MF which objects belong to this plate
                for (size_t obj_idx = 0; obj_idx < model->objects.size(); ++obj_idx) {
                    const auto* obj = model->objects[obj_idx];
                    if (obj) {
                        for (size_t inst_idx = 0; inst_idx < obj->instances.size(); ++inst_idx) {
                            plate.objects_and_instances.emplace_back(static_cast<int>(obj_idx), static_cast<int>(inst_idx));
                        }
                    }
                }

                // Populate plate.config with print-level overrides so that OrcaSlicer GUI can display them
                // This ensures that when the exported 3MF is opened in OrcaSlicer, the modified parameters
                // are shown in the UI (e.g., seam_position, bottom_surface_pattern, sparse_infill_pattern)
                try {
                    // Copy the override keys and values into plate.config
                    if (!print_overrides_keys.empty()) {
                        for (const auto& key : print_overrides_keys) {
                            if (const Slic3r::ConfigOption* opt = print_cfg_overrides.optptr(key)) {
                                plate.config.set_key_value(key, opt->clone());
                            }
                        }
                        // Set different_settings_to_system field (semicolon-separated list of modified keys)
                        // This is what OrcaSlicer reads to highlight modified parameters in the UI
                        std::string diff_str;
                        for (size_t i = 0; i < print_overrides_keys.size(); ++i) {
                            if (i > 0) diff_str += ";";
                            diff_str += print_overrides_keys[i];
                        }
                        auto* diff_opt = new Slic3r::ConfigOptionStrings();
                        diff_opt->values.push_back(diff_str);
                        plate.config.set_key_value("different_settings_to_system", diff_opt);
                    }

                    // Also include curr_bed_type in plate.config if it's set in the working config
                    // This ensures the bed type is preserved when the 3MF is opened in OrcaSlicer GUI
                    if (config && config->has("curr_bed_type")) {
                        if (const Slic3r::ConfigOption* bed_opt = config->optptr("curr_bed_type")) {
                            plate.config.set_key_value("curr_bed_type", bed_opt->clone());
                        }
                    }

                    // Ensure project_settings.config also gets the override keys list + values
                    // so OrcaSlicer GUI can show them as modified when opening the generated 3MF.
                    if (config) {
                        try {
                            if (!print_overrides_keys.empty()) {
                                // 1) Copy values into main config (DynamicPrintConfig)
                                for (const auto &key : print_overrides_keys) {
                                    if (const Slic3r::ConfigOption* opt = print_cfg_overrides.optptr(key)) {
                                        config->set_key_value(key, opt->clone());
                                    }
                                }
                                // 2) Set different_settings_to_system on main config as well
                                std::string diff_str_cfg;
                                for (size_t i = 0; i < print_overrides_keys.size(); ++i) {
                                    if (i > 0) diff_str_cfg += ";";
                                    diff_str_cfg += print_overrides_keys[i];
                                }
                                auto* diff_opt_cfg = new Slic3r::ConfigOptionStrings();
                                diff_opt_cfg->values.push_back(diff_str_cfg);
                                config->set_key_value("different_settings_to_system", diff_opt_cfg);
                            }
                        } catch (const std::exception& e) {
                            std::cout << "WARN: Failed to copy overrides into main config: " << e.what() << std::endl;
                        } catch (...) {
                            std::cout << "WARN: Failed to copy overrides into main config (unknown error)" << std::endl;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cout << "WARN: Failed to populate plate.config with overrides: " << e.what() << std::endl;
                } catch (...) {
                    std::cout << "WARN: Failed to populate plate.config with overrides (unknown error)" << std::endl;
                }

                // Fill additional plate metadata required by Bambu slice_info.config
                try {
                    // prediction (seconds as string)
                    float pred_secs = 0.0f;
                    pred_secs = proc_result.print_statistics.modes[static_cast<size_t>(Slic3r::PrintEstimatedStatistics::ETimeMode::Normal)].time;
                    plate.gcode_prediction = std::to_string(pred_secs);
                    // Record in impl for API propagation
                    last_estimated_time_sec = static_cast<double>(pred_secs);
                } catch (...) { /* best-effort */ }

                try {
                    // filament info: type, color, filament_id; and total weight
                    double total_g = 0.0;

                    // Get the arrays safely first (these are coStrings, not coString)
                    const Slic3r::ConfigOptionStrings* filament_types = nullptr;
                    const Slic3r::ConfigOptionStrings* filament_colours = nullptr;
                    const Slic3r::ConfigOptionStrings* filament_ids_opt = nullptr;
                    if (config) {
                        filament_types = dynamic_cast<const Slic3r::ConfigOptionStrings*>(config->option("filament_type", false));
                        filament_colours = dynamic_cast<const Slic3r::ConfigOptionStrings*>(config->option("filament_colour", false));
                        filament_ids_opt = dynamic_cast<const Slic3r::ConfigOptionStrings*>(config->option("filament_ids", false));
                    }

                    for (auto &fi : plate.slice_filaments_info) {
                        const size_t e = static_cast<size_t>(fi.id);

                        // Safe access with bounds checking
                        if (filament_types && e < filament_types->values.size()) {
                            const std::string &t = filament_types->values[e];
                            if (!t.empty()) fi.type = t;
                        }
                        if (filament_colours && e < filament_colours->values.size()) {
                            const std::string &c = filament_colours->values[e];
                            if (!c.empty()) fi.color = c;
                        }
                        if (filament_ids_opt && e < filament_ids_opt->values.size()) {
                            const std::string &fid = filament_ids_opt->values[e];
                            if (!fid.empty()) fi.filament_id = fid;
                        }
                        total_g += static_cast<double>(fi.used_g);
                    }
                    plate.gcode_weight = std::to_string(total_g);
                    // Record in impl for API propagation
                    last_filament_used_grams = total_g;
                } catch (...) { /* best-effort */ }

                // Fill printer/nozzle metadata (fallback to hints parsed from project if available)
                try {
                    std::string nozzle_str;
                    if (auto *nozz = dynamic_cast<const Slic3r::ConfigOptionFloats*>(config->option("nozzle_diameter", false)))
                        nozzle_str = nozz->serialize();
                    plate.nozzle_diameters = !nozzle_str.empty() ? nozzle_str : plate_nozzle_variant;
                } catch (...) {}
                try {
                    std::string printer_id = preset_bundle.printers.get_edited_preset().get_printer_type(&preset_bundle);
                    if (printer_id.empty()) printer_id = plate_printer_model_id;
                    plate.printer_model_id = printer_id;
                } catch (...) {
                    plate.printer_model_id = plate_printer_model_id;
                }

                // Build StoreParams
                Slic3r::StoreParams sp;
                sp.path = output_file.c_str();
                sp.model = model.get();
                sp.config = config.get();

                // FIX: Also clear bed_exclude_area in the config that will be serialized to project_settings.config
                // The previous fix only cleared it in full_print_config (fpc), but sp.config is a different object
                try {
                    auto* bed_exclude_cfg = config->opt<Slic3r::ConfigOptionPoints>("bed_exclude_area", false);
                    if (bed_exclude_cfg && !bed_exclude_cfg->values.empty()) {
                        if (bed_exclude_cfg->values.size() == 1 &&
                            bed_exclude_cfg->values[0].x() == 0.0 && bed_exclude_cfg->values[0].y() == 0.0) {
                            std::cout << "DEBUG: Clearing malformed bed_exclude_area in sp.config (default [0x0])" << std::endl;
                            bed_exclude_cfg->values.clear();
                        } else if (bed_exclude_cfg->values.size() % 4 != 0) {
                            std::cout << "DEBUG: Clearing malformed bed_exclude_area in sp.config (size "
                                      << bed_exclude_cfg->values.size() << " is not a multiple of 4)" << std::endl;
                            bed_exclude_cfg->values.clear();
                        }
                    }
                } catch (...) {
                    // best-effort
                }

                // FIX: Ensure printable_area has valid 4 points (rectangle) instead of malformed ['0x0']
                try {
                    auto* printable_area_cfg = config->opt<Slic3r::ConfigOptionPoints>("printable_area", false);
                    if (printable_area_cfg) {
                        bool needs_fix = false;
                        if (printable_area_cfg->values.size() == 1 &&
                            printable_area_cfg->values[0].x() == 0.0 && printable_area_cfg->values[0].y() == 0.0) {
                            needs_fix = true;
                        } else if (printable_area_cfg->values.size() < 4) {
                            needs_fix = true;
                        }
                        if (needs_fix) {
                            std::cout << "DEBUG: Fixing malformed printable_area (was size "
                                      << printable_area_cfg->values.size() << ") to default 256x256" << std::endl;
                            printable_area_cfg->values.clear();
                            printable_area_cfg->values.push_back(Slic3r::Vec2d(0, 0));
                            printable_area_cfg->values.push_back(Slic3r::Vec2d(256, 0));
                            printable_area_cfg->values.push_back(Slic3r::Vec2d(256, 256));
                            printable_area_cfg->values.push_back(Slic3r::Vec2d(0, 256));
                        }
                    }
                } catch (...) {
                    // best-effort
                }

                // FIX: Ensure extruder_offset has valid values instead of malformed ['0x0']
                try {
                    auto* extruder_offset_cfg = config->opt<Slic3r::ConfigOptionPoints>("extruder_offset", false);
                    if (extruder_offset_cfg && !extruder_offset_cfg->values.empty()) {
                        // extruder_offset should have one point per extruder, but a single [0x0] is valid
                        // The issue is when the format is wrong. For now, ensure at least one valid point exists.
                        if (extruder_offset_cfg->values.size() == 1 &&
                            extruder_offset_cfg->values[0].x() == 0.0 && extruder_offset_cfg->values[0].y() == 0.0) {
                            // This is actually valid (no offset for single extruder), leave it
                            std::cout << "DEBUG: extruder_offset is [0x0] - valid for single extruder" << std::endl;
                        }
                    }
                } catch (...) {
                    // best-effort
                }

                // FIX: Propagate preset IDs for display in OrcaSlicer GUI
                // Priority: 1) Custom names from SlicingParams, 2) Project presets from 3MF, 3) Preset bundle
                try {
                    std::string printer_id;
                    std::string print_id;
                    std::vector<std::string> filament_ids;

                    // Determine how many filaments are actually used
                    // Use saved_filament_colours (original 3MF colors) if available, as config may have been trimmed
                    size_t used_filament_count = 1;
                    if (!saved_filament_colours.empty()) {
                        used_filament_count = saved_filament_colours.size();
                        std::cout << "DEBUG: Used filament count from saved_filament_colours: " << used_filament_count << std::endl;
                    } else if (detected_extruders > 0) {
                        used_filament_count = detected_extruders;
                        std::cout << "DEBUG: Used filament count from detected_extruders: " << used_filament_count << std::endl;
                    } else if (auto* fil_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false)) {
                        used_filament_count = std::max(size_t(1), fil_colour->values.size());
                        std::cout << "DEBUG: Used filament count from config filament_colour: " << used_filament_count << std::endl;
                    }
                    std::cout << "DEBUG: Final used filament count for profile IDs: " << used_filament_count << std::endl;

                    // Priority 1: Custom display names from SlicingParams (highest priority)
                    if (!custom_printer_profile_name.empty()) {
                        printer_id = custom_printer_profile_name;
                    }
                    if (!custom_process_profile_name.empty()) {
                        print_id = custom_process_profile_name;
                    }
                    // If custom filament profile name is provided, apply to ALL used filaments
                    if (!custom_filament_profile_name.empty()) {
                        for (size_t i = 0; i < used_filament_count; ++i) {
                            filament_ids.push_back(custom_filament_profile_name);
                        }
                        std::cout << "DEBUG: Applied custom filament profile '" << custom_filament_profile_name
                                  << "' to " << used_filament_count << " filament slots" << std::endl;
                    }

                    // Priority 2: Project presets from loaded 3MF
                    if (printer_id.empty() && !project_printer_preset.empty()) {
                        printer_id = project_printer_preset;
                    }
                    if (print_id.empty() && !project_print_preset.empty()) {
                        print_id = project_print_preset;
                    }
                    // If no custom filament and project has one, apply to all used filaments
                    if (filament_ids.empty() && !project_filament_preset.empty()) {
                        for (size_t i = 0; i < used_filament_count; ++i) {
                            filament_ids.push_back(project_filament_preset);
                        }
                    }

                    // Priority 3: Currently selected presets from preset_bundle (fallback)
                    if (printer_id.empty()) {
                        std::string sel = preset_bundle.printers.get_selected_preset_name();
                        if (!sel.empty() && sel != "- default -" && sel != "Default Printer") {
                            printer_id = sel;
                        }
                    }
                    if (print_id.empty()) {
                        std::string sel = preset_bundle.prints.get_selected_preset_name();
                        if (!sel.empty() && sel != "- default -" && sel != "Default Setting") {
                            print_id = sel;
                        }
                    }
                    if (filament_ids.empty()) {
                        for (const auto& fp : preset_bundle.filament_presets) {
                            if (!fp.empty() && fp != "- default -" && fp != "Default Filament") {
                                filament_ids.push_back(fp);
                            }
                        }
                        // Trim to used_filament_count if we got more from preset_bundle
                        if (filament_ids.size() > used_filament_count) {
                            filament_ids.resize(used_filament_count);
                        }
                        // Or expand if needed (use first filament ID for all)
                        while (filament_ids.size() < used_filament_count && !filament_ids.empty()) {
                            filament_ids.push_back(filament_ids[0]);
                        }
                    }

                    // Set the IDs in config for serialization
                    if (!printer_id.empty()) {
                        config->set_key_value("printer_settings_id", new Slic3r::ConfigOptionString(printer_id));
                        std::cout << "DEBUG: Set printer_settings_id=" << printer_id << std::endl;
                    }
                    if (!print_id.empty()) {
                        config->set_key_value("print_settings_id", new Slic3r::ConfigOptionString(print_id));
                        std::cout << "DEBUG: Set print_settings_id=" << print_id << std::endl;
                    }
                    if (!filament_ids.empty()) {
                        config->set_key_value("filament_settings_id", new Slic3r::ConfigOptionStrings(filament_ids));
                        std::cout << "DEBUG: Set filament_settings_id with " << filament_ids.size() << " entries: ";
                        for (const auto& fid : filament_ids) std::cout << "'" << fid << "' ";
                        std::cout << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "WARN: Failed to propagate preset IDs: " << e.what() << std::endl;
                } catch (...) {
                    // best-effort
                }

                // SIMPLIFIED: Export only the current plate without dummy plates
                // This avoids potential issues with empty PlateData structures
                Slic3r::PlateDataPtrs pd_list;
                pd_list.push_back(&plate);

                sp.plate_data_list = pd_list;
                sp.export_plate_idx = 0; // Always 0 since we only have one plate in the list

                // Strategy: Generate 3MF with G-code only (no 3D model)
                // SkipModel: Skip embedding the 3D model mesh in the 3MF (only include G-code, thumbnails, metadata)
                // WithGcode: Include G-code file in the 3MF
                // Silence: Suppress verbose logging
                // SplitModel: Save objects per file (Production Extension)
                // UseLoadedId: Use loaded IDs for identify_id
                // ShareMesh: Share mesh between objects
                sp.strategy = Slic3r::SaveStrategy::Silence |
                              Slic3r::SaveStrategy::WithGcode |
                              Slic3r::SaveStrategy::SkipModel |
                              Slic3r::SaveStrategy::SplitModel |
                              Slic3r::SaveStrategy::UseLoadedId |
                              Slic3r::SaveStrategy::ShareMesh;

                // Embed project presets (print, filament, printer) for Bambu compatibility
                try {
                    std::vector<Slic3r::Preset*> presets;
                    // print
                    presets.push_back(&preset_bundle.prints.get_edited_preset());
                    // filaments: push all selected if available, otherwise the edited preset
                    if (!preset_bundle.filament_presets.empty()) {
                        for (const auto &name : preset_bundle.filament_presets) {
                            if (auto *p = const_cast<Slic3r::Preset*>(preset_bundle.filaments.find_preset(name, /*first_visible_if_not_found=*/false, /*real=*/false, /*only_from_library=*/false)))
                                presets.push_back(p);
                        }
                    } else {
                        presets.push_back(&preset_bundle.filaments.get_edited_preset());
                    }
                    // printer
                    presets.push_back(&preset_bundle.printers.get_edited_preset());
                    sp.project_presets = std::move(presets);
                } catch (...) {
                    // best-effort
                }

                // Generate headless thumbnail from G-code (plate view)
                try {
                    auto hex_rgba = [](const std::string &hex){ std::array<unsigned char,4> c{200,200,200,255}; if(hex.size()>=7 && hex[0]=='#'){ auto h=[&](char ch){ if(ch>='0'&&ch<='9')return ch-'0'; ch=(char)std::tolower(ch); if(ch>='a'&&ch<='f')return 10+ch-'a'; return 0;}; c[0]=(unsigned char)(h(hex[1])<<4|h(hex[2])); c[1]=(unsigned char)(h(hex[3])<<4|h(hex[4])); c[2]=(unsigned char)(h(hex[5])<<4|h(hex[6])); } return c; };
                    // Build color map per extruder id
                    std::map<int,std::array<unsigned char,4>> id2color; for(const auto &fi: plate.slice_filaments_info){ id2color[fi.id]=hex_rgba(fi.color); }
                    // Parse G-code quickly
                    std::ifstream ifs(tmp_gcode, std::ios::in); if(ifs){
                        double x=0,y=0,z=0,e=0,last_e=0; bool abs_e=true; // default absolute; will switch on M83
                        double minx=1e9,miny=1e9,maxx=-1e9,maxy=-1e9; int tool=plate.slice_filaments_info.empty()?0:plate.slice_filaments_info.front().id;
                        struct Seg{double x1,y1,x2,y2; int t;}; std::vector<Seg> segs; segs.reserve(20000);
                        std::string line; while(std::getline(ifs,line)){
                            if(line.empty()) continue; char c0=line[0]; if(c0==';'||c0=='(') continue; // comments
                            if(line.rfind("M83",0)==0) { abs_e=false; continue; }
                            if(line.rfind("M82",0)==0) { abs_e=true;  continue; }
                            if(line.rfind("T",0)==0 && line.size()>=2){ try{ tool=std::stoi(line.substr(1)); }catch(...){} }
                            if(!(line.rfind("G0",0)==0 || line.rfind("G1",0)==0)) continue;
                            auto findv=[&](char k,double &v){ auto p=line.find(k); if(p!=std::string::npos){ size_t q=p+1; char* end=nullptr; v=strtod(line.c_str()+q,&end); return true;} return false; };
                            double nx=x, ny=y, nz=z, ne=e; bool hasx=findv('X',nx), hasy=findv('Y',ny), hasz=findv('Z',nz), hase=findv('E',ne);
                            double de = abs_e? (ne - e) : ne; bool extrude = hase && de > 0.0005; // tiny threshold
                            if((hasx||hasy)){
                                if(extrude){ segs.push_back({x,y,hasx?nx:x, hasy?ny:y, tool});
                                    double sx=std::min(x,hasx?nx:x), exx=std::max(x,hasx?nx:x); double sy=std::min(y,hasy?ny:y), ey=std::max(y,hasy?ny:y);
                                    minx=std::min(minx,sx); miny=std::min(miny,sy); maxx=std::max(maxx,exx); maxy=std::max(maxy,ey);
                                }
                                x = hasx?nx:x; y = hasy?ny:y;
                            }
                            z=nz; last_e=e; e=ne;
                        }
                        if(!segs.empty()){
                            // Setup canvas
                            const unsigned W=800,H=800; auto *tn=new Slic3r::ThumbnailData(); tn->set(W,H); tn->pixels.assign(W*H*4, 0);
                            // pad
                            double dx=(maxx-minx), dy=(maxy-miny); if(dx<=0||dy<=0){ minx=minx==1e9?0:minx; miny=miny==1e9?0:miny; maxx=std::max(maxx, minx+1.0); maxy=std::max(maxy, miny+1.0); dx=maxx-minx; dy=maxy-miny; }
                            double margin=20.0; double sx=(W-2*margin)/dx, sy=(H-2*margin)/dy; double s=std::min(sx,sy);
                            auto to_px=[&](double vx,double vy){ int px=(int)std::round(margin + (vx-minx)*s); int py=(int)std::round(H-1 - (margin + (vy-miny)*s)); return std::pair<int,int>(px,py); };
                            auto draw_line=[&](int x0,int y0,int x1,int y1, const std::array<unsigned char,4>& col){ int dx=std::abs(x1-x0), sx2=x0<x1?1:-1; int dy=-std::abs(y1-y0), sy2=y0<y1?1:-1; int err=dx+dy; for(;;){ if((unsigned)x0<W && (unsigned)y0<H){ size_t idx=((size_t)y0*W + x0)*4; tn->pixels[idx+0]=col[0]; tn->pixels[idx+1]=col[1]; tn->pixels[idx+2]=col[2]; tn->pixels[idx+3]=255; }
                                    if(x0==x1 && y0==y1) break; int e2=2*err; if(e2>=dy){ err+=dy; x0+=sx2;} if(e2<=dx){ err+=dx; y0+=sy2; } } };
                            for(const auto &sg: segs){ auto p0=to_px(sg.x1, sg.y1); auto p1=to_px(sg.x2, sg.y2); auto it=id2color.find(sg.t); auto col = (it!=id2color.end()? it->second : std::array<unsigned char,4>{255,255,255,255}); draw_line(p0.first,p0.second,p1.first,p1.second,col);}
                            // SIMPLIFIED: Add only the real thumbnail without empty placeholders
                            sp.thumbnail_data.clear();
                            sp.thumbnail_data.push_back(tn);
                        }
                    }
                } catch (...) {
                    // best-effort thumbnail generation
                }

                // Provide project-level metadata (model_id) so exporter writes BBL model tag
                Slic3r::BBLProject project_meta;
                project_meta.project_model_id = plate.printer_model_id;
                sp.project = &project_meta;

                // Provide plate bbox/json data (ids, colors, nozzle), used by AMS UI and previews
#if 1
                try {
                    auto bbox_data = std::make_unique<Slic3r::PlateBBoxData>();

                    // Collect per-object 2D bboxes from world-space exact bounding boxes
                    Slic3r::BoundingBoxf3 all_bb;
                    bool all_bb_init = false;

                    for (size_t oi = 0; oi < model->objects.size(); ++oi) {
                        const Slic3r::ModelObject *obj = model->objects[oi];
                        if (!obj) continue;

                        const auto &bb = obj->bounding_box_exact();

                        if (bb.defined) {
                            if (!all_bb_init) { all_bb = bb; all_bb_init = true; } else { all_bb.merge(bb); }
                            Slic3r::BBoxData jbx;
                            jbx.id = static_cast<int>(oi);
                            jbx.layer_height = (float)(config->has("layer_height") ? config->opt_float("layer_height") : 0.2f);
                            jbx.name = obj->name;
                            // min.xy, max.xy in mm
                            jbx.bbox = { (coordf_t)bb.min.x(), (coordf_t)bb.min.y(), (coordf_t)bb.max.x(), (coordf_t)bb.max.y() };
                            jbx.area = (float)((bb.max.x() - bb.min.x()) * (bb.max.y() - bb.min.y()));
                            bbox_data->bbox_objs.push_back(std::move(jbx));
                        }
                    }
                    if (all_bb_init) {
                        bbox_data->bbox_all = { (coordf_t)all_bb.min.x(), (coordf_t)all_bb.min.y(), (coordf_t)all_bb.max.x(), (coordf_t)all_bb.max.y() };
                    }
                    // Filament ids (extruders used) and colors
                    std::vector<int> used_ids;
                    std::vector<std::string> used_colors;
                    for (const auto &fi : plate.slice_filaments_info) {
                        used_ids.push_back(fi.id);
                        used_colors.push_back(fi.color);
                    }
                    bbox_data->filament_ids = std::move(used_ids);
                    bbox_data->filament_colors = std::move(used_colors);
                    // nozzle diameter
                    try {
                        if (auto *nozz = dynamic_cast<const Slic3r::ConfigOptionFloats*>(config->option("nozzle_diameter", false)))
                            bbox_data->nozzle_diameter = (float)(nozz->get_at(0));
                    } catch (...) {}
                    // first extruder index
                    bbox_data->first_extruder = !plate.slice_filaments_info.empty() ? plate.slice_filaments_info.front().id : 0;
                    // bed type: set a reasonable default expected by Bambu UI
                    bbox_data->bed_type = std::string("hot_plate");
                    // version as integer (2 uses FilamentId view per ThumbnailData.hpp)
                    bbox_data->version = 2;

                    if (!bbox_data->bbox_objs.empty()) {
                        sp.id_bboxes.push_back(bbox_data.release());
                    }
                } catch (...) {
                    // best-effort bbox generation
                }
#endif

                bool ok3mf = false;
                try {
                    ok3mf = Slic3r::store_bbs_3mf(sp);
                } catch (const std::bad_alloc &e) {
                    last_error = "3MF packaging failed: Out of memory (GCode file too large)";
                    ok3mf = false;
                } catch (const std::exception &e) {
                    last_error = std::string("3MF packaging failed: ") + e.what();
                    ok3mf = false;
                }

                // Clean up temp G-code
                try { if (std::filesystem::exists(tmp_gcode)) std::filesystem::remove(tmp_gcode); } catch (...) {}

                if (!ok3mf) {
                    if (last_error.empty()) last_error = "3MF packaging failed";
                    return false;
                }

                // Success
                return true;
#endif  // END OF DISABLED CODE
            } else {
                // Plain G-code export path
                std::cout << "DEBUG: Exporting G-code to: " << output_file << std::endl;

                // Remove any existing output file
                if (std::filesystem::exists(output_file)) {
                    std::filesystem::remove(output_file);
                }

                bool export_successful = false;

                // CRITICAL: Synchronize flush_volumes_matrix in full_print_config() BEFORE export
                // This prevents the "Flush volumes matrix do not match to the correct size" error
                try {
                    auto& fpc = const_cast<Slic3r::DynamicPrintConfig&>(print->full_print_config());
                    auto* fil_colour = fpc.opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
                    auto* flush_matrix = fpc.opt<Slic3r::ConfigOptionFloats>("flush_volumes_matrix", false);
                    auto* flush_vector = fpc.opt<Slic3r::ConfigOptionFloats>("flush_volumes_vector", false);
                    auto* flush_multiplier = fpc.opt<Slic3r::ConfigOptionFloats>("flush_multiplier", false);

                    size_t filament_count = fil_colour ? fil_colour->values.size() : 1;
                    size_t heads_count = flush_multiplier ? flush_multiplier->values.size() : 1;
                    size_t expected_matrix_size = filament_count * filament_count * heads_count;

                    std::cout << "DEBUG: [BEFORE GCODE EXPORT] Synchronizing flush_volumes_matrix - filament_count=" << filament_count
                              << ", heads_count=" << heads_count
                              << ", expected_matrix_size=" << expected_matrix_size << std::endl;

                    if (flush_matrix) {
                        size_t current_size = flush_matrix->values.size();
                        if (current_size != expected_matrix_size) {
                            std::cout << "DEBUG: [BEFORE GCODE EXPORT] Resizing flush_volumes_matrix from " << current_size
                                      << " to " << expected_matrix_size << std::endl;
                            // Create a proper matrix: diagonal = 0 (same filament), off-diagonal = 280 (default purge)
                            std::vector<double> new_matrix(expected_matrix_size, 280.0);
                            for (size_t h = 0; h < heads_count; ++h) {
                                for (size_t i = 0; i < filament_count; ++i) {
                                    // Set diagonal elements to 0 (no purge needed for same filament)
                                    size_t idx = h * (filament_count * filament_count) + i * filament_count + i;
                                    new_matrix[idx] = 0.0;
                                }
                            }
                            flush_matrix->values = new_matrix;
                        }
                    } else if (filament_count > 0) {
                        // flush_matrix doesn't exist, create it
                        std::cout << "DEBUG: [BEFORE GCODE EXPORT] Creating flush_volumes_matrix with size " << expected_matrix_size << std::endl;
                        std::vector<double> new_matrix(expected_matrix_size, 280.0);
                        for (size_t h = 0; h < heads_count; ++h) {
                            for (size_t i = 0; i < filament_count; ++i) {
                                size_t idx = h * (filament_count * filament_count) + i * filament_count + i;
                                new_matrix[idx] = 0.0;
                            }
                        }
                        fpc.set_key_value("flush_volumes_matrix", new Slic3r::ConfigOptionFloats(new_matrix));
                    }

                    // Also synchronize flush_volumes_vector (2 values per filament)
                    size_t expected_vector_size = 2 * filament_count;
                    if (flush_vector) {
                        size_t current_size = flush_vector->values.size();
                        if (current_size != expected_vector_size) {
                            std::cout << "DEBUG: [BEFORE GCODE EXPORT] Resizing flush_volumes_vector from " << current_size
                                      << " to " << expected_vector_size << std::endl;
                            std::vector<double> new_vector(expected_vector_size, 140.0);
                            flush_vector->values = new_vector;
                        }
                    } else if (filament_count > 0) {
                        std::cout << "DEBUG: [BEFORE GCODE EXPORT] Creating flush_volumes_vector with size " << expected_vector_size << std::endl;
                        std::vector<double> new_vector(expected_vector_size, 140.0);
                        fpc.set_key_value("flush_volumes_vector", new Slic3r::ConfigOptionFloats(new_vector));
                    }

                    // Ensure flush_multiplier has correct size
                    if (flush_multiplier && flush_multiplier->values.empty()) {
                        flush_multiplier->values.push_back(1.0);
                    } else if (!flush_multiplier) {
                        fpc.set_key_value("flush_multiplier", new Slic3r::ConfigOptionFloats({1.0}));
                    }

                    // FIX: Clear bed_exclude_area to prevent crash in OrcaSlicer GUI when opening the 3MF
                    // The default value of ["0x0"] is malformed (not a multiple of 4 points for rectangles)
                    // This causes a crash in Model::setPrintSpeedTable() when diff() returns an empty polygon list
                    auto* bed_exclude = fpc.opt<Slic3r::ConfigOptionPoints>("bed_exclude_area", false);
                    if (bed_exclude && !bed_exclude->values.empty()) {
                        // Only clear if it's the default malformed value (single point at 0,0)
                        if (bed_exclude->values.size() == 1 &&
                            bed_exclude->values[0].x() == 0.0 && bed_exclude->values[0].y() == 0.0) {
                            std::cout << "DEBUG: Clearing malformed bed_exclude_area (default [0x0]) to prevent OrcaSlicer crash" << std::endl;
                            bed_exclude->values.clear();
                        } else if (bed_exclude->values.size() % 4 != 0) {
                            // If not a multiple of 4 points, clear it (malformed)
                            std::cout << "DEBUG: Clearing malformed bed_exclude_area (size " << bed_exclude->values.size()
                                      << " is not a multiple of 4) to prevent OrcaSlicer crash" << std::endl;
                            bed_exclude->values.clear();
                        }
                    }
                } catch (const std::exception& e) {
                    std::cout << "WARN: Failed to synchronize flush_volumes_matrix: " << e.what() << std::endl;
                }

                try {


                    std::cout << "DEBUG: Attempting direct G-code export..." << std::endl;
                    // Log current plate_origin that will be applied by GCode
                    {
                        auto po = print->get_plate_origin();
                        std::cout << "DEBUG: plate_origin at export => (" << po(0) << "," << po(1) << ")" << std::endl;
                    }
                    Slic3r::GCodeProcessorResult proc_result; // provide valid result storage to avoid null deref in export path
                    std::string gcode_path = print->export_gcode(output_file, &proc_result, nullptr);
                    std::cout << "DEBUG: Direct G-code export completed successfully" << std::endl;
                    // Capture native statistics from proc_result
                    try {
                        float pred_secs = proc_result.print_statistics.modes[static_cast<size_t>(Slic3r::PrintEstimatedStatistics::ETimeMode::Normal)].time;
                        last_estimated_time_sec = static_cast<double>(pred_secs);
                    } catch (...) { /* best-effort */ }
                    try {
                        // Use PlateData::parse_filament_info to compute per-extruder grams, then sum
                        Slic3r::PlateData tmp_plate;
                        tmp_plate.plate_index = (plate_id > 0 ? plate_id - 1 : 0);
                        tmp_plate.parse_filament_info(&proc_result);
                        double total_g = 0.0;
                        for (auto &fi : tmp_plate.slice_filaments_info) total_g += static_cast<double>(fi.used_g);
                        last_filament_used_grams = total_g;
                    } catch (...) { /* best-effort */ }
                    export_successful = true;
                } catch (const std::exception& e) {
                    std::cout << "DEBUG: Direct export failed with exception: " << e.what() << std::endl;
                    export_successful = false;
                } catch (...) {
                    std::cout << "DEBUG: Direct export failed with unknown exception" << std::endl;
                    export_successful = false;
                }

                // If export failed, do not create any fallback file
                if (!export_successful) {
                    std::cout << "DEBUG: G-code export failed, no fallback file will be created" << std::endl;
                    last_error = "G-code export failed";
                    return false;
                }

                // Check if export was successful
                if (export_successful && std::filesystem::exists(output_file)) {
                    auto file_size = std::filesystem::file_size(output_file);
                    std::cout << "DEBUG: G-code file size: " << file_size << " bytes" << std::endl;

                    if (file_size > 1000) {  // Expect at least 1KB for a real G-code file
                        std::cout << "DEBUG: G-code export successful" << std::endl;
                        return true;
                    } else {
                        std::cout << "DEBUG: G-code file too small (" << file_size << " bytes)" << std::endl;
                        last_error = "G-code file too small (" + std::to_string(file_size) + " bytes)";
                        return false;
                    }
                } else {
                    std::cout << "DEBUG: G-code export failed" << std::endl;
                    last_error = "G-code export failed";
                    return false;
                }
            }
        } catch (const std::exception& e) {
            last_error = std::string("Slicing failed: ") + e.what();
            std::cout << "DEBUG: Exception caught: " << e.what() << std::endl;
            return false;
        }
#else
        // Fallback for when libslic3r is not available
        try {
            std::ofstream output(output_file);
            if (!output.is_open()) {
                last_error = "Failed to open output file: " + output_file;
                return false;
            }

            last_error = "libslic3r not available";
            return false;
            output.close();

            return true;
        } catch (const std::exception& e) {
            last_error = std::string("Slicing failed: ") + e.what();
            return false;
        }
#endif
    }

    AddonCore::ModelInfo getModelInformation() const {
        AddonCore::ModelInfo info;

#if HAVE_LIBSLIC3R
        if (!model || model->objects.empty()) {
            info.is_valid = false;
            info.errors.push_back("No model loaded");
            return info;
        }

        try {
            info.is_valid = true;
            info.object_count = model->objects.size();
            info.volume = 0.0;
            info.triangle_count = 0;

            // Calculate total volume and triangle count
            for (const auto* obj : model->objects) {
                for (const auto* volume : obj->volumes) {
                    if (volume->mesh().its.vertices.size() > 0) {
                        // Cast away const to call volume() method
                        auto& non_const_mesh = const_cast<Slic3r::TriangleMesh&>(volume->mesh());
                        info.volume += non_const_mesh.volume();
                        info.triangle_count += volume->mesh().its.indices.size();
                    }
                }
            }

            // Get bounding box
            if (!model->objects.empty()) {
                auto bbox = model->objects[0]->raw_bounding_box();
                for (size_t i = 1; i < model->objects.size(); ++i) {
                    bbox.merge(model->objects[i]->raw_bounding_box());
                }

                auto size = bbox.size();
                info.bounding_box = "(" + std::to_string(size.x()) + " x " +
                                   std::to_string(size.y()) + " x " +
                                   std::to_string(size.z()) + ")";
            }

        } catch (const std::exception& e) {
            info.is_valid = false;
            info.errors.push_back(std::string("Error getting model info: ") + e.what());
        }
#else
        info.is_valid = false;
        info.errors.push_back("libslic3r not available");
#endif

        return info;
    }
};

// AddonCore implementation

AddonCore::AddonCore() : m_impl(std::make_unique<Impl>()) {
    OrcaSlicerCli::AddonCore::setLoggingSilenced(addon_is_silent());
    std::cout << "========================================" << std::endl;
    std::cout << "🚀 ORCASLICER ADDON LOADED - VERSION WITH MULTI-COLOR FIX" << std::endl;
    std::cout << "🎨 Multi-color support: ENABLED" << std::endl;
    std::cout << "📅 Build date: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "========================================" << std::endl;
}

AddonCore::~AddonCore() = default;

AddonCore::OperationResult AddonCore::initialize(const std::string& resources_path) {
    OrcaSlicerCli::AddonCore::setLoggingSilenced(addon_is_silent());
    std::cout << "🔧 AddonCore::initialize() called with resources_path: " << resources_path << std::endl;

    if (m_impl->initialized) {
        std::cout << "⚠️  Already initialized, skipping" << std::endl;
        return OperationResult(true, "Already initialized");
    }

    std::cout << "🔄 Initializing Slic3r..." << std::endl;
    if (m_impl->initializeSlic3r(resources_path)) {
        m_impl->initialized = true;
        std::cout << "✅ AddonCore initialized successfully" << std::endl;
        return OperationResult(true, "AddonCore initialized successfully");
    } else {
        std::cout << "❌ Initialization failed: " << m_impl->last_error << std::endl;
        return OperationResult(false, "Initialization failed", m_impl->last_error);
    }
}

void AddonCore::shutdown() {
    if (m_impl->initialized) {
        // Perform proper cleanup of libslic3r objects
        m_impl->cleanup();
    #if HAVE_LIBSLIC3R
        try {
            // Reset preset bundle collections to release resources deterministically
            m_impl->preset_bundle.reset(false /* delete_files */);
            // Reset app config to default state
            m_impl->app_config.reset();
        } catch (...) {
            // Best effort cleanup
        }
    #endif
        m_impl->initialized = false;
    }
}

bool AddonCore::isInitialized() const {
    return m_impl->initialized;
}

AddonCore::OperationResult AddonCore::loadModel(const std::string& filename) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

    if (!std::filesystem::exists(filename)) {
        return OperationResult(false, "File not found: " + filename);
    }

    if (m_impl->loadModelFromFile(filename)) {
        return OperationResult(true, "Model loaded successfully: " + filename);
    } else {
        return OperationResult(false, "Failed to load model", m_impl->last_error);
    }
}

AddonCore::ModelInfo AddonCore::getModelInfo() const {
    if (!m_impl->initialized) {
        ModelInfo info;
        info.is_valid = false;
        info.errors.push_back("AddonCore not initialized");
        return info;
    }

    return m_impl->getModelInformation();
}

AddonCore::OperationResult AddonCore::slice(const SlicingParams& params) {
    std::cout << "🎯 AddonCore::slice() CALLED - Multi-color fix version active!" << std::endl;

    if (!m_impl->initialized) {
        std::cout << "❌ AddonCore not initialized!" << std::endl;
        return OperationResult(false, "AddonCore not initialized");
    }

    // Propagate transfer flags into Impl for use in performSlicing()
    m_impl->transfer_printer_customizations  = params.transfer_printer_customizations;
    m_impl->transfer_filament_customizations = params.transfer_filament_customizations;
    m_impl->transfer_process_customizations  = params.transfer_process_customizations;
    m_impl->transfer_project_overrides       = params.transfer_project_overrides;
    // Behavior flags - SEMPRE ATIVOS para garantir que objetos cabem na mesa
    // Ignora parametros e força centralizacao + realinhamento automatico
    m_impl->center_on_bed = true;
    m_impl->auto_realign_if_needed = true;

    std::cout << "DEBUG: Entering slice(): input='" << params.input_file
              << "' plate_index=" << params.plate_index
              << ", profiles(prn/fil/proc)=('" << params.printer_profile_name << "','"
              << params.filament_profile_name << "','" << params.process_profile_name << "')"
              << std::endl;

    // Load model if not already loaded
    if (!params.input_file.empty()) {
    #if HAVE_LIBSLIC3R
        // NOTE: Model::read_from_file -> load_bbs_3mf expects 1-based plate_id.
        // Passing 0 means "all plates". Keep 0 only if caller explicitly sets < 1.
        m_impl->plate_id = (params.plate_index >= 1 ? params.plate_index : 0);
    #endif
        auto load_result = loadModel(params.input_file);
        if (!load_result.success) {
            return load_result;
        }
    #if HAVE_LIBSLIC3R
        // Respect 3MF object/volume overrides even when CLI profiles are provided.
        // Precedence: 3MF parameter overrides > CLI profile overrides > 3MF presets.
        // Therefore, do not clear 3MF overrides here.
    #endif
    }

    // CRITICAL: Reset preset bundle to clean state at the start of each slice
    // This prevents preset state from persisting between requests (e.g., K2 Plus profiles
    // being used when slicing for A1 if profiles are not explicitly passed)
#if HAVE_LIBSLIC3R
    try {
        std::cout << "DEBUG: Resetting preset bundle to clean state before loading new profiles" << std::endl;
        // Reset preset selections to defaults
        m_impl->preset_bundle.printers.select_preset(0);  // Select first (default) printer
        m_impl->preset_bundle.prints.select_preset(0);    // Select first (default) print preset
        m_impl->preset_bundle.filaments.select_preset(0); // Select first (default) filament
        m_impl->preset_bundle.filament_presets.clear();
        // Use selected preset name after select_preset(0)
        m_impl->preset_bundle.filament_presets.push_back(m_impl->preset_bundle.filaments.get_selected_preset_name());
        // Clear project preset names from previous 3MF
        m_impl->project_printer_preset.clear();
        m_impl->project_print_preset.clear();
        m_impl->project_filament_preset.clear();
        m_impl->plate_printer_model_id.clear();
        m_impl->plate_nozzle_variant.clear();
        m_impl->has_project_embedded_presets = false;
        // Capture custom profile display names from SlicingParams
        m_impl->custom_printer_profile_name = params.printer_profile_name;
        m_impl->custom_filament_profile_name = params.filament_profile_name;
        m_impl->custom_process_profile_name = params.process_profile_name;
        if (!params.printer_profile_name.empty() || !params.filament_profile_name.empty() || !params.process_profile_name.empty()) {
            std::cout << "DEBUG: Custom profile display names set: printer='" << params.printer_profile_name
                      << "', filament='" << params.filament_profile_name
                      << "', process='" << params.process_profile_name << "'" << std::endl;
        }
    } catch (const std::exception &e) {
        std::cout << "WARN: Failed to reset preset bundle: " << e.what() << std::endl;
    }
#endif

    // JSON on-the-fly mode: no profile loading, all config via custom_settings

#if HAVE_LIBSLIC3R
    std::cout << "DEBUG: [SLICE] About to update_compatible and apply presets..." << std::endl;
    try {
        // Check if ALL transfer flags are disabled - if so, use pure defaults + custom_settings
        const bool all_transfer_disabled = !m_impl->transfer_printer_customizations &&
                                            !m_impl->transfer_filament_customizations &&
                                            !m_impl->transfer_process_customizations &&
                                            !m_impl->transfer_project_overrides;

        if (all_transfer_disabled) {
            // PURE ON-THE-FLY MODE: Ignore all 3MF configs, use only FullPrintConfig::defaults()
            // This allows slicing with only JSON config without any 3MF contamination
            std::cout << "DEBUG: ALL transfer flags disabled - resetting to pure FullPrintConfig::defaults()" << std::endl;
            m_impl->config->clear();
            Slic3r::FullPrintConfig full_defaults = Slic3r::FullPrintConfig::defaults();
            m_impl->config->apply(full_defaults, true);
            std::cout << "DEBUG: Config reset to pure defaults (all 3MF configs ignored)" << std::endl;

            // CRITICAL: Disable multi-material processing to avoid infinite loops
            // When in pure on-the-fly mode, we force single-extruder mode to prevent
            // the ToolOrdering algorithm from entering infinite loops in
            // reorder_filaments_for_minimum_flush_volume() and update_filament_maps_to_config()
            try {
                std::cout << "DEBUG: PURE ON-THE-FLY MODE - Disabling multi-material processing" << std::endl;
                m_impl->config->set_key_value("single_extruder_multi_material", new Slic3r::ConfigOptionBool(false));
                m_impl->config->set_key_value("enable_prime_tower", new Slic3r::ConfigOptionBool(false));

                // PRESERVE 3MF COLORS: Restore filament colors from the 3MF file
                // This allows objects to keep their original colors even in on-the-fly mode
                if (!m_impl->saved_filament_colours.empty()) {
                    std::cout << "DEBUG: PURE ON-THE-FLY MODE - Restoring 3MF filament colors: ";
                    for (const auto& c : m_impl->saved_filament_colours) std::cout << c << " ";
                    std::cout << std::endl;

                    size_t num_colors = m_impl->saved_filament_colours.size();
                    m_impl->config->set_key_value("filament_colour", new Slic3r::ConfigOptionStrings(m_impl->saved_filament_colours));

                    // Expand filament arrays to match the number of colors
                    std::vector<std::string> filament_types(num_colors, "PLA");
                    std::vector<int> nozzle_temps(num_colors, 220);
                    std::vector<int> bed_temps(num_colors, 55);
                    m_impl->config->set_key_value("filament_type", new Slic3r::ConfigOptionStrings(filament_types));
                    m_impl->config->set_key_value("nozzle_temperature", new Slic3r::ConfigOptionInts(nozzle_temps));
                    m_impl->config->set_key_value("nozzle_temperature_initial_layer", new Slic3r::ConfigOptionInts(nozzle_temps));
                    m_impl->config->set_key_value("bed_temperature", new Slic3r::ConfigOptionInts(bed_temps));
                    m_impl->config->set_key_value("bed_temperature_initial_layer", new Slic3r::ConfigOptionInts(bed_temps));
                    std::cout << "DEBUG: Filament arrays expanded to " << num_colors << " entries to match 3MF colors" << std::endl;
                } else {
                    // No saved colors, use single white extruder
                    m_impl->config->set_key_value("filament_type", new Slic3r::ConfigOptionStrings({"PLA"}));
                    m_impl->config->set_key_value("filament_colour", new Slic3r::ConfigOptionStrings({"#FFFFFF"}));
                    m_impl->config->set_key_value("nozzle_temperature", new Slic3r::ConfigOptionInts({220}));
                    m_impl->config->set_key_value("nozzle_temperature_initial_layer", new Slic3r::ConfigOptionInts({220}));
                    m_impl->config->set_key_value("bed_temperature", new Slic3r::ConfigOptionInts({55}));
                    m_impl->config->set_key_value("bed_temperature_initial_layer", new Slic3r::ConfigOptionInts({55}));
                }

                // PRESERVE 3MF CHANGE_FILAMENT_GCODE: Critical for Bambu AMS multi-color printing
                // This gcode contains M620/M621 macros that control the AMS filament changes
                if (!m_impl->saved_change_filament_gcode.empty()) {
                    std::cout << "DEBUG: PURE ON-THE-FLY MODE - Restoring 3MF change_filament_gcode ("
                              << m_impl->saved_change_filament_gcode.size() << " chars)" << std::endl;
                    m_impl->config->set_key_value("change_filament_gcode",
                        new Slic3r::ConfigOptionString(m_impl->saved_change_filament_gcode));
                }

                std::cout << "DEBUG: Multi-material disabled, extruder colors preserved" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "WARN: Failed to configure on-the-fly mode: " << e.what() << std::endl;
            }
        } else {
            m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);

            // Only apply preset config if we have any loaded presets (not just defaults)
            // This allows slicing with only JSON options when no profiles are loaded
            const bool has_printer = !m_impl->preset_bundle.printers.get_selected_preset_name().empty() &&
                                      m_impl->preset_bundle.printers.get_selected_preset_name() != "Default Printer";
            const bool has_filament = !m_impl->preset_bundle.filament_presets.empty() &&
                                       m_impl->preset_bundle.filament_presets[0] != "Default Filament";
            const bool has_process = !m_impl->preset_bundle.prints.get_selected_preset_name().empty() &&
                                      m_impl->preset_bundle.prints.get_selected_preset_name() != "Default Setting";

            if (has_printer || has_filament || has_process) {
                // Apply preset config on top of existing defaults using safe_build_config
                Slic3r::DynamicPrintConfig preset_config;
                OrcaSlicerCli::util::safe_build_config(m_impl->preset_bundle, preset_config);
                m_impl->config->apply(preset_config, true);
                std::cout << "DEBUG: Applied preset config on top of defaults -> printer='"
                          << m_impl->preset_bundle.printers.get_selected_preset_name()
                          << "', print='" << m_impl->preset_bundle.prints.get_selected_preset_name()
                          << "', filament='" << m_impl->preset_bundle.filaments.get_selected_preset_name()
                          << "'" << std::endl;
            } else {
                // No specific presets loaded - apply generic fallback config
                // This enables on-the-fly slicing with custom_settings from the API
                std::cout << "DEBUG: No specific presets loaded - applying generic fallback config" << std::endl;
                OrcaSlicerCli::config::apply_generic_fallback_config(*m_impl->config, m_impl->resources_path);
            }
        }

        // Dump key values after syncing working config with selected presets
        try { if (const auto* o = m_impl->config->optptr("sparse_infill_density")) std::cout << "DEBUG: synced_config[sparse_infill_density]=" << o->serialize() << std::endl; } catch (...) {}
        try { if (const auto* o = m_impl->config->optptr("top_shell_layers")) std::cout << "DEBUG: synced_config[top_shell_layers]=" << o->serialize() << std::endl; } catch (...) {}

        // CRITICAL: Clear printer-identifying keys when transfer is disabled
        // This must be done AFTER full_config_secure() is applied, because that function
        // re-applies the preset values (which include the 3MF values from load_config_model)
        if (!m_impl->transfer_printer_customizations && !all_transfer_disabled) {
            std::cout << "DEBUG: Clearing printer-identifying keys (transfer_printer_customizations=false)" << std::endl;
            try {
                m_impl->config->set_key_value("printer_model", new Slic3r::ConfigOptionString(""));
                m_impl->config->set_key_value("printer_variant", new Slic3r::ConfigOptionString(""));
                m_impl->config->set_key_value("printer_settings_id", new Slic3r::ConfigOptionString(""));
                // Also clear related fields that may contain printer-specific references
                m_impl->config->set_key_value("print_compatible_printers", new Slic3r::ConfigOptionStrings({}));
            } catch (...) {}
        }
        if (!m_impl->transfer_process_customizations && !all_transfer_disabled) {
            std::cout << "DEBUG: Clearing process-identifying keys (transfer_process_customizations=false)" << std::endl;
            try {
                m_impl->config->set_key_value("print_settings_id", new Slic3r::ConfigOptionString(""));
                m_impl->config->set_key_value("default_print_profile", new Slic3r::ConfigOptionString(""));
            } catch (...) {}
        }
        if (!m_impl->transfer_filament_customizations && !all_transfer_disabled) {
            std::cout << "DEBUG: Clearing filament-identifying keys (transfer_filament_customizations=false)" << std::endl;
            try {
                m_impl->config->set_key_value("filament_settings_id", new Slic3r::ConfigOptionStrings({""}));
                m_impl->config->set_key_value("default_filament_profile", new Slic3r::ConfigOptionStrings({""}));
            } catch (...) {}
        }
    } catch (const std::exception &e) {
        std::cout << "WARN: Failed to refresh working config from selected presets: " << e.what() << std::endl;
    }
#endif

#if HAVE_LIBSLIC3R
    // Re-apply 3MF print-level overrides on top of selected profiles
    std::cout << "TESTE AQUI AGORA >>> About to re-apply print overrides, transfer_process_customizations=" << params.transfer_process_customizations << std::endl;
    if (params.transfer_process_customizations) {
        OrcaSlicerCli::slice::reapply_print_overrides(*m_impl->config, m_impl->print_cfg_overrides, m_impl->print_overrides_keys);
    } else {
        std::cout << "TESTE AQUI AGORA >>> Skipping print overrides because transfer_process_customizations=false" << std::endl;
    }
#endif

    // Load config file if specified
    if (!params.config_file.empty()) {
        auto result = loadConfig(params.config_file);
        if (!result.success) {
            return OperationResult(false, "Failed to load config file: " + params.config_file, result.error_details);
        }
    }

    // Load preset if specified
    if (!params.preset_name.empty()) {
        auto result = loadPreset(params.preset_name);
        if (!result.success) {
            return OperationResult(false, "Failed to load preset: " + params.preset_name, result.error_details);
        }
    }

    // Apply custom settings (these override profile settings)
    // Track which overrides were used vs ignored to report back to callers.
    std::vector<std::string> __used_override_keys;
    std::vector<std::string> __ignored_override_keys;
    if (!params.custom_settings.empty()) {
        OrcaSlicerCli::slice::apply_custom_settings(
            m_impl->config.get(),
            params.custom_settings,
            [&](const std::string& k, const std::string& v){ return setConfigOption(k, v); },
            __used_override_keys,
            __ignored_override_keys);
    }

    // DEBUG: Check printable_area immediately after apply_custom_settings
    try {
        auto* pa_after_custom = m_impl->config->opt<Slic3r::ConfigOptionPoints>("printable_area", false);
        if (pa_after_custom) {
            std::cout << "DEBUG: [AFTER apply_custom_settings] printable_area has " << pa_after_custom->values.size() << " points: ";
            for (const auto& pt : pa_after_custom->values) {
                std::cout << "(" << pt(0) << "," << pt(1) << ") ";
            }
            std::cout << std::endl;
        } else {
            std::cout << "DEBUG: [AFTER apply_custom_settings] printable_area is NULL!" << std::endl;
        }
    } catch (...) {}

    if (params.dry_run) {
        return OperationResult(true, "Dry run completed - no actual slicing performed");
    }

#if HAVE_LIBSLIC3R
    // Re-apply 3MF project parameter overrides with highest priority
    if (params.transfer_project_overrides) {
        {
            std::vector<std::string> keys_to_apply = m_impl->project_overrides_keys;
            if (keys_to_apply.empty()) keys_to_apply = m_impl->project_cfg_after_3mf.keys();
            OrcaSlicerCli::slice::reapply_project_overrides(*m_impl->config, m_impl->project_cfg_after_3mf, keys_to_apply);
        }
    }


    // Ensure 3MF print-level (dirty) overrides take precedence over project-level overrides
    // Some UIs expect per-print edits (shown as orange/dirty) to win. Re-apply them after project overrides.
    std::cout << "TESTE AQUI AGORA >>> Second re-apply of print overrides (after project overrides)" << std::endl;
    if (params.transfer_process_customizations) {
        OrcaSlicerCli::slice::reapply_print_overrides_excluding(
            *m_impl->config,
            m_impl->print_cfg_overrides,
            m_impl->print_overrides_keys,
            __used_override_keys);
    } else {
        std::cout << "TESTE AQUI AGORA >>> Skipping second re-apply because transfer_process_customizations=false" << std::endl;
    }

    // NOTE: Multi-material configuration is now applied AFTER print->apply() in performSlicing()
    // This ensures we use the ACTUAL extruders from the model, not the detected count from config
    std::cout << "🔍 [TRACE 29] Multi-material config will be applied after print->apply() in performSlicing()" << std::endl;

    // Enable single_extruder_multi_material and prime tower if multi-material detected
    // BUT NOT in pure on-the-fly mode (all transfer flags disabled) - this can cause infinite loops
    const bool all_transfer_disabled_for_mm = !m_impl->transfer_printer_customizations &&
                                               !m_impl->transfer_filament_customizations &&
                                               !m_impl->transfer_process_customizations &&
                                               !m_impl->transfer_project_overrides;

    if (m_impl->detected_extruders > 1 && !all_transfer_disabled_for_mm) {
        std::cout << "🔍 Detected multi-material model (" << m_impl->detected_extruders << " colors in 3MF)" << std::endl;
        m_impl->config->set_key_value("single_extruder_multi_material", new Slic3r::ConfigOptionBool(true));
        m_impl->config->set_key_value("enable_prime_tower", new Slic3r::ConfigOptionBool(true));

        // Restore 3MF colors
        auto* fil_colour = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
        if (!m_impl->saved_filament_colours.empty() && fil_colour) {
            std::cout << "🔍 Restoring 3MF colors: ";
            for (const auto& c : m_impl->saved_filament_colours) std::cout << c << " ";
            std::cout << std::endl;
            fil_colour->values = m_impl->saved_filament_colours;
        }

        // Expand filament arrays to match detected_extruders (will be trimmed later in performSlicing)
        auto* fil_diameter = m_impl->config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
        auto* fil_type = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_type", false);

        if (fil_diameter && fil_diameter->values.size() < m_impl->detected_extruders) {
            while (fil_diameter->values.size() < m_impl->detected_extruders) {
                fil_diameter->values.push_back(fil_diameter->values.empty() ? 1.75 : fil_diameter->values.back());
            }
        }
        if (fil_type && fil_type->values.size() < m_impl->detected_extruders) {
            while (fil_type->values.size() < m_impl->detected_extruders) {
                fil_type->values.push_back(fil_type->values.empty() ? "PLA" : fil_type->values.back());
            }
        }

        // Restore change_filament_gcode for Bambu AMS multi-color printing
        // ONLY if not overridden via config JSON (custom_settings)
        {
            bool gcode_overridden = std::find(__used_override_keys.begin(), __used_override_keys.end(),
                                              "change_filament_gcode") != __used_override_keys.end();
            std::cout << "DEBUG: gcode_overridden=" << (gcode_overridden ? "true" : "false")
                      << ", __used_override_keys.size()=" << __used_override_keys.size() << std::endl;
            if (!m_impl->saved_change_filament_gcode.empty() && !gcode_overridden) {
                std::cout << "🔍 Restoring 3MF change_filament_gcode ("
                          << m_impl->saved_change_filament_gcode.size() << " chars)" << std::endl;
                m_impl->config->set_key_value("change_filament_gcode",
                    new Slic3r::ConfigOptionString(m_impl->saved_change_filament_gcode));
            } else if (gcode_overridden) {
                std::cout << "🔍 Keeping config JSON change_filament_gcode (override from custom_settings)" << std::endl;
            }
        }
    } else if (m_impl->detected_extruders > 1 && all_transfer_disabled_for_mm) {
        // PURE ON-THE-FLY MODE with multi-material: Enable SEMM but disable problematic features
        // We need to enable single_extruder_multi_material to respect object extruder assignments
        // but disable features that can cause infinite loops
        std::cout << "🔍 PURE ON-THE-FLY MODE: Enabling multi-material (" << m_impl->detected_extruders
                  << " colors in 3MF) with safe settings" << std::endl;
        m_impl->config->set_key_value("single_extruder_multi_material", new Slic3r::ConfigOptionBool(true));
        m_impl->config->set_key_value("enable_prime_tower", new Slic3r::ConfigOptionBool(false));
        // Disable features that can cause infinite loops in ToolOrdering
        m_impl->config->set_key_value("flush_into_infill", new Slic3r::ConfigOptionBool(false));
        m_impl->config->set_key_value("flush_into_support", new Slic3r::ConfigOptionBool(false));
        m_impl->config->set_key_value("flush_into_objects", new Slic3r::ConfigOptionBool(false));

        // Restore 3MF colors in on-the-fly mode
        auto* fil_colour = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
        if (!m_impl->saved_filament_colours.empty() && fil_colour) {
            std::cout << "🔍 Restoring 3MF colors in on-the-fly mode: ";
            for (const auto& c : m_impl->saved_filament_colours) std::cout << c << " ";
            std::cout << std::endl;
            fil_colour->values = m_impl->saved_filament_colours;
        }

        // Expand filament arrays to match detected_extruders
        auto* fil_diameter = m_impl->config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
        auto* fil_type = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_type", false);
        if (fil_diameter && fil_diameter->values.size() < m_impl->detected_extruders) {
            while (fil_diameter->values.size() < m_impl->detected_extruders) {
                fil_diameter->values.push_back(fil_diameter->values.empty() ? 1.75 : fil_diameter->values.back());
            }
        }
        if (fil_type && fil_type->values.size() < m_impl->detected_extruders) {
            while (fil_type->values.size() < m_impl->detected_extruders) {
                fil_type->values.push_back(fil_type->values.empty() ? "PLA" : fil_type->values.back());
            }
        }

        // Restore change_filament_gcode for Bambu AMS multi-color printing
        // ONLY if not overridden via config JSON (custom_settings)
        {
            bool gcode_overridden = std::find(__used_override_keys.begin(), __used_override_keys.end(),
                                              "change_filament_gcode") != __used_override_keys.end();
            if (!m_impl->saved_change_filament_gcode.empty() && !gcode_overridden) {
                std::cout << "🔍 Restoring 3MF change_filament_gcode in on-the-fly mode ("
                          << m_impl->saved_change_filament_gcode.size() << " chars)" << std::endl;
                m_impl->config->set_key_value("change_filament_gcode",
                    new Slic3r::ConfigOptionString(m_impl->saved_change_filament_gcode));
            } else if (gcode_overridden) {
                std::cout << "🔍 Keeping config JSON change_filament_gcode in on-the-fly mode (override from custom_settings)" << std::endl;
            }
        }
    }

    // CRITICAL: Synchronize flush_volumes_matrix and flush_volumes_vector with actual filament count
    // This prevents the "Flush volumes matrix do not match to the correct size" error
    // which occurs when the matrix size doesn't match (filament_count * filament_count * heads_count)
    {
        auto* fil_colour = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
        auto* flush_matrix = m_impl->config->opt<Slic3r::ConfigOptionFloats>("flush_volumes_matrix", false);
        auto* flush_vector = m_impl->config->opt<Slic3r::ConfigOptionFloats>("flush_volumes_vector", false);
        auto* flush_multiplier = m_impl->config->opt<Slic3r::ConfigOptionFloats>("flush_multiplier", false);

        size_t filament_count = fil_colour ? fil_colour->values.size() : 1;
        size_t heads_count = flush_multiplier ? flush_multiplier->values.size() : 1;
        size_t expected_matrix_size = filament_count * filament_count * heads_count;

        std::cout << "DEBUG: Synchronizing flush_volumes_matrix - filament_count=" << filament_count
                  << ", heads_count=" << heads_count
                  << ", expected_matrix_size=" << expected_matrix_size << std::endl;

        if (flush_matrix) {
            size_t current_size = flush_matrix->values.size();
            if (current_size != expected_matrix_size) {
                std::cout << "DEBUG: Resizing flush_volumes_matrix from " << current_size
                          << " to " << expected_matrix_size << std::endl;
                // Create a proper matrix: diagonal = 0 (same filament), off-diagonal = 280 (default purge)
                std::vector<double> new_matrix(expected_matrix_size, 280.0);
                for (size_t h = 0; h < heads_count; ++h) {
                    for (size_t i = 0; i < filament_count; ++i) {
                        // Set diagonal elements to 0 (no purge needed for same filament)
                        size_t idx = h * (filament_count * filament_count) + i * filament_count + i;
                        new_matrix[idx] = 0.0;
                    }
                }
                flush_matrix->values = new_matrix;
            }
        }

        // Also synchronize flush_volumes_vector (one value per filament per head)
        size_t expected_vector_size = filament_count * heads_count;
        if (flush_vector) {
            size_t current_size = flush_vector->values.size();
            if (current_size != expected_vector_size) {
                std::cout << "DEBUG: Resizing flush_volumes_vector from " << current_size
                          << " to " << expected_vector_size << std::endl;
                std::vector<double> new_vector(expected_vector_size, 140.0);  // Default purge volume
                flush_vector->values = new_vector;
            }
        }
    }

    // DEBUG: Final check of effective config for critical keys before performSlicing()
    try {
        auto dump_one = [&](const char* k){
            if (const Slic3r::ConfigOption* o = m_impl->config->optptr(k))
                dbg_log(std::string("DEBUG: final_config[") + k + "] = " + o->serialize());
            else
                dbg_log(std::string("DEBUG: final_config[") + k + "] not present");
        };
        dump_one("seam_position");
        dump_one("bottom_surface_pattern");
        dump_one("internal_solid_infill_pattern");

        // CRITICAL: Check multi-material settings before slicing
        std::cout << "🔍 [TRACE 30] BEFORE performSlicing() - checking multi-material config:" << std::endl;
        dump_one("single_extruder_multi_material");
        dump_one("enable_prime_tower");
        dump_one("filament_colour");
        dump_one("filament_type");
        dump_one("filament_diameter");
        dump_one("flush_volumes_matrix");
        dump_one("flush_multiplier");
    } catch (...) {}


#if HAVE_LIBSLIC3R
#endif

#endif

    return OrcaSlicerCli::slice::slice_and_package(
        [&](const std::string& out){ return m_impl->performSlicing(out); },
        params.output_file,
        __used_override_keys,
        __ignored_override_keys,
        m_impl->last_estimated_time_sec,
        m_impl->last_filament_used_grams,
        m_impl->last_error
    );
}

std::string AddonCore::getVersion() {
#if HAVE_LIBSLIC3R
    return "OrcaSlicerCli 1.0.0 (based on OrcaSlicer " + std::string(SLIC3R_VERSION) + ")";
#else
    return "OrcaSlicerCli 1.0.0 (libslic3r not linked)";
#endif
}

std::string AddonCore::getBuildInfo() {
    return "Built on " + std::string(__DATE__) + " " + std::string(__TIME__);
}

AddonCore::OperationResult AddonCore::loadConfig(const std::string& config_file) {
    if (!m_impl->initialized) {
        return OperationResult(false, "AddonCore not initialized");
    }

    if (!std::filesystem::exists(config_file)) {
        return OperationResult(false, "Config file not found: " + config_file);
    }

    // TODO: Implement configuration loading when libslic3r is available
    return OperationResult(false, "Configuration loading not implemented");
}

AddonCore::OperationResult AddonCore::loadPreset(const std::string& preset_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

    return OperationResult(false, "Preset loading not implemented");
}

AddonCore::OperationResult AddonCore::setConfigOption(const std::string& key, const std::string& value) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }
#if HAVE_LIBSLIC3R
    try {
        if (!m_impl->config) {
            return OperationResult(false, "No active configuration to modify");
        }
        // Validate that key exists in current DynamicPrintConfig (reject unknown keys explicitly)
        if (m_impl->config->optptr(key.c_str()) == nullptr) {
            return OperationResult(false, std::string("Unknown config key: ") + key);
        }

        // CRITICAL FIX: ConfigOptionPoints::deserialize() expects comma-separated points (e.g., "0x0,180x0,180x180,0x180")
        // but the frontend may send semicolon-separated points (e.g., "0x0;180x0;180x180;0x180")
        // Convert semicolons to commas for printable_area, bed_shape, bed_exclude_area, extruder_offset
        std::string normalized_value = value;
        if (key == "printable_area" || key == "bed_shape" || key == "bed_exclude_area" || key == "extruder_offset") {
            // Replace semicolons with commas
            for (char& c : normalized_value) {
                if (c == ';') c = ',';
            }
            std::cout << "DEBUG: Normalized " << key << " from '" << value << "' to '" << normalized_value << "'" << std::endl;
        }

        // Use set_deserialize to let libslic3r parse and validate the value
        Slic3r::ConfigSubstitutionContext ctx{Slic3r::ForwardCompatibilitySubstitutionRule::Enable};
        m_impl->config->set_deserialize(key, normalized_value, ctx, /*append=*/false);
        std::cout << "DEBUG: Override applied: " << key << "=" << normalized_value << std::endl;
        return OperationResult(true, "Config option set: " + key);
    } catch (const std::exception& e) {
        return OperationResult(false, std::string("Failed to set config option: ") + key, e.what());
    }


#else
    return OperationResult(false, "libslic3r not available");
#endif
}

std::string AddonCore::getConfigOption(const std::string& key) const {
    if (!m_impl->initialized) {
        return "";
    }

    // TODO: Implement configuration getting when libslic3r is available
    return "";  // Return empty for now
}

std::vector<std::string> AddonCore::getAvailablePresets() const {
    return {};
}

std::vector<std::string> AddonCore::getAvailablePrinterProfiles() const {
    if (!m_impl->initialized) return {};
    return OrcaSlicerCli::config::list_printer_profiles(m_impl->resources_path);
}

std::vector<std::string> AddonCore::getAvailableFilamentProfiles() const {
    if (!m_impl->initialized) return {};
    return OrcaSlicerCli::config::list_filament_profiles(m_impl->resources_path);
}

std::vector<std::string> AddonCore::getAvailableProcessProfiles() const {
    if (!m_impl->initialized) return {};
    return OrcaSlicerCli::config::list_process_profiles(m_impl->resources_path);
}

AddonCore::ModelInfo AddonCore::validateModel(const std::string& filename) const {
    ModelInfo info;
    info.filename = filename;

    if (!std::filesystem::exists(filename)) {
        info.is_valid = false;
        info.errors.push_back("File not found");
        return info;
    }

    try {
        // Basic file validation
        std::filesystem::path file_path(filename);
        std::string extension = file_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (extension != ".3mf" && extension != ".stl" && extension != ".obj") {
            info.is_valid = false;
            info.errors.push_back("Unsupported file format: " + extension);
            return info;
        }

        // Try to get file size
        auto file_size = std::filesystem::file_size(filename);
        if (file_size == 0) {
            info.is_valid = false;
            info.errors.push_back("File is empty");
            return info;
        }

        info.is_valid = true;

    } catch (const std::exception& e) {
        info.is_valid = false;
        info.errors.push_back(std::string("Validation error: ") + e.what());
    }

    return info;
}

} // namespace OrcaSlicerCli



