#include "AddonCore.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <exception>
#include <cmath>

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
                try {
                    int idx_i = plate_id;
                    if (idx_i < 0) idx_i = 0;
                    int max_i = (int)this->plate_data_src.size() - 1;
                    if (idx_i > max_i) idx_i = max_i;
                    size_t idx = (size_t)idx_i;
                    Slic3r::PlateData* pd = this->plate_data_src[idx];
                    if (pd != nullptr && !pd->config.empty()) {
                        std::cout << "DEBUG: Applying plate config over full_config (GUI parity)" << std::endl;
                        config->apply(pd->config, true);
                        std::cout << "DEBUG: Plate config applied successfully" << std::endl;
                    } else {
                        std::cout << "DEBUG: No plate config to apply (plate_data is empty or null)" << std::endl;
                    }
                } catch (const std::exception& e) {
                    std::cout << "WARN: Failed to apply plate config: " << e.what() << std::endl;
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
                                    (void)center_instances_on_bed_center();
                                    print->set_plate_origin(Slic3r::Vec3d(0.0, 0.0, 0.0));
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
                    apply_config.apply(pd->config, true);
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

            std::cout << "DEBUG: Applying model and config to print (GUI parity: single apply with plate config)..." << std::endl;

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
                Slic3r::BuildVolume build_volume(pc.printable_area.values, pc.printable_height);
                if (build_volume.valid()) {
                    auto check_outside = [&](std::string &msg)->bool{
                        const Slic3r::Vec3d po = print->get_plate_origin();
                        const Slic3r::Vec3d shift_xy(-po(0), -po(1), 0.0);
                        // Check model instances
                        for (const auto *obj : model->objects) {
                            if (!obj) continue;
                            for (const auto *inst : obj->instances) {
                                if (!inst) continue;
                                Slic3r::BoundingBoxf3 bb = obj->instance_bounding_box(*inst);
                                Slic3r::BoundingBoxf3 bb_local(bb.min + shift_xy, bb.max + shift_xy);
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
                    if (check_outside(oob_msg)) {
                        if (auto_realign_if_needed) {
                            bool is_bbl = false; try { is_bbl = preset_bundle.is_bbl_vendor(); } catch (...) {}
                            bool fixed = false;
                            // Use the same arrangement algorithm as the GUI (libnest2d-based)
                            // Build bed shape from current config and run arrange_objects
                            try {
                                // Build selected items (instances) and run full arrange pipeline like GUI
                                Slic3r::ModelInstancePtrs instances;
                                auto selected = Slic3r::get_arrange_polys(*model, instances);

                                Slic3r::arrangement::ArrangeParams params;
                                params.min_obj_distance = 0;
                                params.allow_rotations  = false;
                                params.do_final_align   = true;
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
                                        float conf_wtx = 0.f, conf_wty = 0.f;
                                        bool has_conf_pos = false;
                                        try { if (auto *wx = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_x", false)) { if (!wx->values.empty()) { conf_wtx = wx->values[0]; has_conf_pos = true; } } } catch (...) {}
                                        try { if (auto *wy = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_y", false)) { if (!wy->values.empty()) { conf_wty = wy->values[0]; has_conf_pos = has_conf_pos && true; } } } catch (...) {}

                                        // Check if existing conf position is inside shrunk bed; if so, use it
                                        if (has_conf_pos) {
                                            // existing config is already in local bed coords (mm)
                                            Slic3r::Vec3d pt_local(double(conf_wtx), double(conf_wty), 0.0);
                                            Slic3r::BoundingBoxf3 wtbb_local(pt_local, pt_local);
                                            Slic3r::BuildVolume bv(pc.printable_area.values, double(pc.printable_height));
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
                                size_t dbg_n = std::min<size_t>(selected.size(), 3);
                                for (size_t i = 0; i < dbg_n; ++i) {
                                    Slic3r::BoundingBox bb(selected[i].poly.contour.points);
                                    auto w = bb.size().x(); auto h = bb.size().y();
                                    std::cout << "DEBUG: selected[" << i << "] bb w=" << w << " h=" << h << std::endl;
                                }

                                // Build bed and arrange
                                // Excluded regions already sized (wipe tower includes brim); no inflation on excluded regions here.
                                Slic3r::arrangement::arrange(selected, params.excluded_regions, bed_pts, params);

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

                            } catch (...) {
                                fixed = false;
                            }
                            if (fixed) {
                                oob_msg.clear();
                                if (!check_outside(oob_msg)) {
                                    std::cout << "INFO: Realinhamento automático aplicado com sucesso para caber na mesa." << std::endl;
                                } else {
                                    // Ainda fora: se for Prime Tower, tentar reposicionar automaticamente para o centro da mesa
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
                                            const double cx_local = 0.5 * (minx + maxx);
                                            const double cy_local = 0.5 * (miny + maxy);
                                            const float new_wtx = float(cx_local);
                                            const float new_wty = float(cy_local);

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
                                        const double cx_local = 0.5 * (minx + maxx);
                                        const double cy_local = 0.5 * (miny + maxy);
                                        const float new_wtx = float(cx_local);
                                        const float new_wty = float(cy_local);

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
                                            std::cout << "INFO: Prime Tower reposicionada automaticamente para o centro da mesa (após falha no realinhamento)." << std::endl;
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
                Slic3r::BuildVolume build_volume_final(pc_final.printable_area.values, pc_final.printable_height);
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
                std::cout << "🔍 [3MF-1] Starting 3MF export preparation" << std::endl;

                // Prepare PlateData for store_bbs_3mf
                Slic3r::PlateData plate;
                // FIX: Always use plate_index = 0 for single-plate export
                // OrcaSlicer uses (plate_index + 1) for file naming, so:
                // - plate_index = 0 → generates plate_1.gcode ✅
                // - plate_index = 1 → generates plate_2.gcode ❌
                // Since we're exporting only ONE plate (the current one), it should always be index 0
                plate.plate_index = 0;
                plate.is_sliced_valid = true;

                std::cout << "🔍 [3MF-2] PlateData created, plate_index=" << plate.plate_index << " (input plate_id=" << plate_id << ")" << std::endl;

                // Set gcode_file so it gets embedded in the 3MF
                plate.gcode_file = tmp_gcode.string();
                std::cout << "🔍 [3MF-3] gcode_file set to: " << plate.gcode_file << std::endl;

                plate.parse_filament_info(&proc_result);
                std::cout << "🔍 [3MF-4] parse_filament_info completed, slice_filaments_info.size=" << plate.slice_filaments_info.size() << std::endl;

                // CRITICAL: Populate objects_and_instances to match OrcaSlicer GUI behavior
                // This tells the 3MF which objects belong to this plate
                std::cout << "🔍 [3MF-4a] Populating objects_and_instances for " << model->objects.size() << " objects" << std::endl;
                for (size_t obj_idx = 0; obj_idx < model->objects.size(); ++obj_idx) {
                    const auto* obj = model->objects[obj_idx];
                    if (obj) {
                        for (size_t inst_idx = 0; inst_idx < obj->instances.size(); ++inst_idx) {
                            plate.objects_and_instances.emplace_back(static_cast<int>(obj_idx), static_cast<int>(inst_idx));
                            std::cout << "🔍 [3MF-4b] Added object " << obj_idx << ", instance " << inst_idx << std::endl;
                        }
                    }
                }
                std::cout << "🔍 [3MF-4c] Total objects_and_instances: " << plate.objects_and_instances.size() << std::endl;

                std::cout << "🔍 [3MF-5] Starting plate.config population" << std::endl;

                // Populate plate.config with print-level overrides so that OrcaSlicer GUI can display them
                // This ensures that when the exported 3MF is opened in OrcaSlicer, the modified parameters
                // are shown in the UI (e.g., seam_position, bottom_surface_pattern, sparse_infill_pattern)
                try {
                    // Copy the override keys and values into plate.config
                    if (!print_overrides_keys.empty()) {
                        std::cout << "🔍 [3MF-6] Copying " << print_overrides_keys.size() << " override keys to plate.config" << std::endl;
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
                        std::cout << "DEBUG: Populated plate.config with " << print_overrides_keys.size()
                                  << " override keys for 3MF metadata" << std::endl;
                        std::cout << "DEBUG: different_settings_to_system = " << diff_str << std::endl;
                    }

                    // Also include curr_bed_type in plate.config if it's set in the working config
                    // This ensures the bed type is preserved when the 3MF is opened in OrcaSlicer GUI
                    if (config && config->has("curr_bed_type")) {
                        if (const Slic3r::ConfigOption* bed_opt = config->optptr("curr_bed_type")) {
                            plate.config.set_key_value("curr_bed_type", bed_opt->clone());
                            std::cout << "DEBUG: Included curr_bed_type in plate.config = " << bed_opt->serialize() << std::endl;
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
                                std::cout << "DEBUG: Set different_settings_to_system on main config: " << diff_str_cfg << std::endl;
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

                std::cout << "🔍 [3MF-9] Starting plate metadata population" << std::endl;

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
                    for (auto &fi : plate.slice_filaments_info) {
                        const unsigned int e = static_cast<unsigned int>(fi.id);
                        if (config && config->option("filament_type", false)) {
                            const std::string &t = config->opt_string("filament_type", e);
                            if (!t.empty()) fi.type = t;
                        }
                        if (config && config->option("filament_colour", false)) {
                            const std::string &c = config->opt_string("filament_colour", e);
                            if (!c.empty()) fi.color = c;
                        }
                        if (config && config->option("filament_ids", false)) {
                            const std::string &fid = config->opt_string("filament_ids", e);
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

                std::cout << "🔍 [3MF-10] Plate metadata populated, building StoreParams" << std::endl;

                // Build StoreParams
                Slic3r::StoreParams sp;
                sp.path = output_file.c_str();
                sp.model = model.get();
                sp.config = config.get();

                std::cout << "🔍 [3MF-11] StoreParams created, model objects count=" << model->objects.size() << std::endl;

                // SIMPLIFIED: Export only the current plate without dummy plates
                // This avoids potential issues with empty PlateData structures
                Slic3r::PlateDataPtrs pd_list;
                pd_list.push_back(&plate);

                std::cout << "🔍 [3MF-12] Plate list created, total plates=" << pd_list.size() << std::endl;

                sp.plate_data_list = pd_list;
                sp.export_plate_idx = 0; // Always 0 since we only have one plate in the list

                std::cout << "🔍 [3MF-13] Setting strategy flags" << std::endl;

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

                std::cout << "🔍 [3MF-14] Strategy set to: " << (int)sp.strategy << std::endl;

                std::cout << "🔍 [3MF-15] Embedding project presets" << std::endl;

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
                    std::cout << "🔍 [3MF-16] Project presets embedded, count=" << sp.project_presets.size() << std::endl;
                } catch (...) {
                    std::cout << "🔍 [3MF-16-ERR] Failed to embed project presets" << std::endl;
                }

                std::cout << "🔍 [3MF-17] Starting thumbnail generation from GCode" << std::endl;

                // Generate headless thumbnail from G-code (plate view)
                try {
                    auto hex_rgba = [](const std::string &hex){ std::array<unsigned char,4> c{200,200,200,255}; if(hex.size()>=7 && hex[0]=='#'){ auto h=[&](char ch){ if(ch>='0'&&ch<='9')return ch-'0'; ch=(char)std::tolower(ch); if(ch>='a'&&ch<='f')return 10+ch-'a'; return 0;}; c[0]=(unsigned char)(h(hex[1])<<4|h(hex[2])); c[1]=(unsigned char)(h(hex[3])<<4|h(hex[4])); c[2]=(unsigned char)(h(hex[5])<<4|h(hex[6])); } return c; };
                    // Build color map per extruder id
                    std::map<int,std::array<unsigned char,4>> id2color; for(const auto &fi: plate.slice_filaments_info){ id2color[fi.id]=hex_rgba(fi.color); }
                    std::cout << "🔍 [3MF-18] Parsing GCode for thumbnail, filament colors=" << id2color.size() << std::endl;
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
                            std::cout << "🔍 [3MF-19] Thumbnail generated successfully" << std::endl;
                        }
                    }
                } catch (...) {
                    std::cout << "🔍 [3MF-19-ERR] Thumbnail generation failed" << std::endl;
                }

                std::cout << "🔍 [3MF-20] Setting project metadata" << std::endl;

                // Provide project-level metadata (model_id) so exporter writes BBL model tag
                Slic3r::BBLProject project_meta;
                project_meta.project_model_id = plate.printer_model_id;
                sp.project = &project_meta;

                std::cout << "🔍 [3MF-21] Starting bbox data generation" << std::endl;

                // Provide plate bbox/json data (ids, colors, nozzle), used by AMS UI and previews
#if 1
                try {
                    auto bbox_data = std::make_unique<Slic3r::PlateBBoxData>();
                    std::cout << "🔍 [3MF-21a] BBox data object created" << std::endl;

                    // Collect per-object 2D bboxes from world-space exact bounding boxes
                    Slic3r::BoundingBoxf3 all_bb;
                    bool all_bb_init = false;

                    std::cout << "🔍 [3MF-21b] Starting loop over " << model->objects.size() << " model objects" << std::endl;

                    for (size_t oi = 0; oi < model->objects.size(); ++oi) {
                        std::cout << "🔍 [3MF-21c] Processing object " << oi << "/" << model->objects.size() << std::endl;

                        const Slic3r::ModelObject *obj = model->objects[oi];
                        if (!obj) {
                            std::cout << "🔍 [3MF-21d] Object " << oi << " is NULL, skipping" << std::endl;
                            continue;
                        }

                        std::cout << "🔍 [3MF-21e] Object " << oi << " name='" << obj->name << "', calling bounding_box_exact()..." << std::endl;

                        const auto &bb = obj->bounding_box_exact();

                        std::cout << "🔍 [3MF-21f] Object " << oi << " bounding_box_exact() returned, defined=" << bb.defined << std::endl;

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

                            std::cout << "🔍 [3MF-21g] Object " << oi << " bbox added successfully" << std::endl;
                        }
                    }

                    std::cout << "🔍 [3MF-21h] Finished loop, collected " << bbox_data->bbox_objs.size() << " bboxes" << std::endl;
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
                        size_t obj_count = bbox_data->bbox_objs.size(); // Save before release()
                        sp.id_bboxes.push_back(bbox_data.release());
                        std::cout << "🔍 [3MF-22] BBox data added, objects=" << obj_count << std::endl;
                    } else {
                        std::cout << "🔍 [3MF-22] BBox data empty, skipping" << std::endl;
                    }
                } catch (...) {
                    std::cout << "🔍 [3MF-22-ERR] BBox data generation failed" << std::endl;
                }
#endif

                std::cout << "🔍 [3MF-23] ===== CALLING store_bbs_3mf =====" << std::endl;
                std::cout << "🔍 [3MF-23] StoreParams summary:" << std::endl;
                std::cout << "🔍 [3MF-23]   - path: " << sp.path << std::endl;
                std::cout << "🔍 [3MF-23]   - strategy: " << (int)sp.strategy << std::endl;
                std::cout << "🔍 [3MF-23]   - plate_data_list.size: " << sp.plate_data_list.size() << std::endl;
                std::cout << "🔍 [3MF-23]   - export_plate_idx: " << sp.export_plate_idx << std::endl;
                std::cout << "🔍 [3MF-23]   - model->objects.size: " << (sp.model ? sp.model->objects.size() : 0) << std::endl;
                std::cout << "🔍 [3MF-23]   - project_presets.size: " << sp.project_presets.size() << std::endl;
                std::cout << "🔍 [3MF-23]   - thumbnail_data.size: " << sp.thumbnail_data.size() << std::endl;
                std::cout << "🔍 [3MF-23]   - id_bboxes.size: " << sp.id_bboxes.size() << std::endl;

                // Check GCode file size
                for (size_t i = 0; i < sp.plate_data_list.size(); ++i) {
                    if (sp.plate_data_list[i] && !sp.plate_data_list[i]->gcode_file.empty()) {
                        try {
                            auto gcode_size = std::filesystem::file_size(sp.plate_data_list[i]->gcode_file);
                            std::cout << "🔍 [3MF-23] Plate " << i << " GCode file: " << sp.plate_data_list[i]->gcode_file
                                      << " (size: " << (gcode_size / 1024.0 / 1024.0) << " MB)" << std::endl;
                        } catch (...) {
                            std::cout << "🔍 [3MF-23] Plate " << i << " GCode file: " << sp.plate_data_list[i]->gcode_file
                                      << " (size: UNKNOWN)" << std::endl;
                        }
                    }
                }

                bool ok3mf = false;
                try {
                    std::cout << "🔍 [3MF-24] >>> Entering store_bbs_3mf() <<<" << std::endl;
                    ok3mf = Slic3r::store_bbs_3mf(sp);
                    std::cout << "🔍 [3MF-25] <<< Returned from store_bbs_3mf(), result=" << ok3mf << " >>>" << std::endl;
                } catch (const std::bad_alloc &e) {
                    std::cout << "🔍 [3MF-25-ERR] store_bbs_3mf threw std::bad_alloc (OUT OF MEMORY)" << std::endl;
                    std::cout << "🔍 [3MF-25-ERR] This usually means the GCode file is too large to fit in memory" << std::endl;
                    std::cout << "🔍 [3MF-25-ERR] OrcaSlicer uses streaming/chunked writing for large files" << std::endl;
                    last_error = "3MF packaging failed: Out of memory (GCode file too large)";
                    ok3mf = false;
                } catch (const std::exception &e) {
                    std::cout << "🔍 [3MF-25-ERR] store_bbs_3mf threw exception: " << e.what() << std::endl;
                    last_error = std::string("3MF packaging failed: ") + e.what();
                    ok3mf = false;
                }

                std::cout << "🔍 [3MF-26] Cleaning up temp GCode file" << std::endl;

                // Clean up temp G-code
                try { if (std::filesystem::exists(tmp_gcode)) std::filesystem::remove(tmp_gcode); } catch (...) {}

                if (!ok3mf) {
                    std::cout << "🔍 [3MF-27] ❌ 3MF export FAILED: " << last_error << std::endl;
                    if (last_error.empty()) last_error = "3MF packaging failed";
                    return false;
                }

                std::cout << "🔍 [3MF-28] ✅ 3MF export SUCCESS!" << std::endl;

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
    // Behavior flags
    m_impl->center_on_bed = params.center_on_bed;
    m_impl->auto_realign_if_needed = params.auto_realign_if_needed;

    std::cout << "DEBUG: Entering slice(): input='" << params.input_file
              << "' plate_index=" << params.plate_index
              << ", profiles(prn/fil/proc)=('" << params.printer_profile << "','"
              << params.filament_profile << "','" << params.process_profile << "')"
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

    // Load printer profile if specified
    if (!params.printer_profile.empty()) {
        std::cout << "🎨 [COLOR DEBUG] Loading printer profile: " << params.printer_profile << std::endl;

        // Check if it's a K2 Plus printer
        if (params.printer_profile.find("K2 Plus") != std::string::npos ||
            params.printer_profile.find("K2Plus") != std::string::npos ||
            params.printer_profile.find("k2plus") != std::string::npos) {
            std::cout << "🎨 [COLOR DEBUG] ⚠️  DETECTED K2 PLUS PRINTER - Special handling may be needed!" << std::endl;
        }

        auto result = loadPrinterProfile(params.printer_profile);
        if (!result.success) {
            return OperationResult(false, "Failed to load printer profile: " + params.printer_profile, result.error_details);
        }
    }

    // Load filament profile if specified
    if (!params.filament_profile.empty()) {
        auto result = loadFilamentProfile(params.filament_profile);
        if (!result.success) {
            return OperationResult(false, "Failed to load filament profile: " + params.filament_profile, result.error_details);
        }
    }

    // Load process profile if specified
    if (!params.process_profile.empty()) {
        auto result = loadProcessProfile(params.process_profile);
        if (!result.success) {
            return OperationResult(false, "Failed to load process profile: " + params.process_profile, result.error_details);
        }
    }

#if HAVE_LIBSLIC3R
    // Auto-apply project presets from 3MF delegated to SliceEngine
    OrcaSlicerCli::slice::auto_select_presets_from_3mf(
        params.input_file,
        m_impl->transfer_printer_customizations,
        m_impl->transfer_filament_customizations,
        m_impl->transfer_process_customizations,
        m_impl->has_project_embedded_presets,
        m_impl->project_printer_preset,
        m_impl->project_print_preset,
        m_impl->project_filament_preset,
        m_impl->plate_printer_model_id,
        m_impl->plate_nozzle_variant,
        m_impl->preset_bundle,
        m_impl->app_config,
        *m_impl->config,
        [&](const std::string& name){ return loadPrinterProfile(name); },
        [&](const std::string& name){ return loadFilamentProfile(name); },
        [&](const std::string& name){ return loadProcessProfile(name); },
        params.printer_profile,
        params.filament_profile,
        params.process_profile);
#endif

#if HAVE_LIBSLIC3R
    try {
        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        *m_impl->config = m_impl->preset_bundle.full_config_secure();
        std::cout << "DEBUG: Synchronized working config with selected presets -> printer='"
                  << m_impl->preset_bundle.printers.get_selected_preset_name()
                  << "', print='" << m_impl->preset_bundle.prints.get_selected_preset_name()
                  << "', filament='" << m_impl->preset_bundle.filaments.get_selected_preset_name()
                  << "'" << std::endl;
        // Dump key values after syncing working config with selected presets
        try { if (const auto* o = m_impl->config->optptr("sparse_infill_density")) std::cout << "DEBUG: synced_config[sparse_infill_density]=" << o->serialize() << std::endl; } catch (...) {}
        try { if (const auto* o = m_impl->config->optptr("top_shell_layers")) std::cout << "DEBUG: synced_config[top_shell_layers]=" << o->serialize() << std::endl; } catch (...) {}
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
    if (m_impl->detected_extruders > 1) {
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

AddonCore::OperationResult AddonCore::loadPrinterProfile(const std::string& printer_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }
#if HAVE_LIBSLIC3R
    std::string err;
    bool ok = OrcaSlicerCli::config::load_printer_profile(
        m_impl->resources_path,
        printer_name,
        m_impl->preset_bundle,
        m_impl->app_config,
        *m_impl->config,
        err);
    if (!ok) return OperationResult(false, "Printer profile not found", err);
    return OperationResult(true, "Printer profile loaded successfully: " + printer_name);
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

AddonCore::OperationResult AddonCore::loadFilamentProfile(const std::string& filament_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }
#if HAVE_LIBSLIC3R
    std::string err;
    bool ok = OrcaSlicerCli::config::load_filament_profile(
        filament_name,
        m_impl->preset_bundle,
        *m_impl->config,
        err);
    if (!ok) return OperationResult(false, "Filament profile not found", err);
    return OperationResult(true, "Filament profile loaded successfully: " + filament_name);
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

AddonCore::OperationResult AddonCore::loadProcessProfile(const std::string& process_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }
#if HAVE_LIBSLIC3R
    std::string err;
    bool ok = OrcaSlicerCli::config::load_process_profile(
        process_name,
        m_impl->preset_bundle,
        *m_impl->config,
        err);
    if (!ok) return OperationResult(false, "Process profile not found", err);
    return OperationResult(true, "Process profile loaded successfully: " + process_name);
#else
    return OperationResult(false, "libslic3r not available");
#endif
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
        // Use set_deserialize to let libslic3r parse and validate the value
        Slic3r::ConfigSubstitutionContext ctx{Slic3r::ForwardCompatibilitySubstitutionRule::Enable};
        m_impl->config->set_deserialize(key, value, ctx, /*append=*/false);
        std::cout << "DEBUG: Override applied: " << key << "=" << value << std::endl;
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


AddonCore::OperationResult AddonCore::loadVendor(const std::string& vendor_id) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }
#if HAVE_LIBSLIC3R
    std::string err;
    bool ok = OrcaSlicerCli::config::load_vendor_from_resources(
        m_impl->resources_path,
        vendor_id,
        m_impl->preset_bundle,
        m_impl->app_config,
        m_impl->loaded_vendors,
        err);
    if (!ok) return OperationResult(false, std::string("Error loading vendor: ") + vendor_id, err);
    return OperationResult(true, std::string("Vendor loaded: ") + vendor_id);
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

} // namespace OrcaSlicerCli



