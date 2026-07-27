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
#include <typeinfo>
#include <libslic3r/Arrange.hpp>
#include <libslic3r/ModelArrange.hpp>
// NOTE: Nao incluir Exception.hpp propositalmente.
// liblibslic3r.a e' pre-compilado separadamente, o que causa duplicacao de typeinfo
// para SlicingErrors/SlicingError. Catch por tipo especifico (ou dynamic_cast)
// falha porque a comparacao de typeinfo usa ponteiros que nao coincidem entre
// as duas compilacoes. Em vez disso, identificamos a excecao pelo what() + typeid().

#include <limits>
#include <cstdlib>
#include <cstdint>

#include <array>
#include <map>
#include <unordered_map>

#include "utils/Logger.hpp"
#include <fcntl.h>

#ifdef _WIN32
  #include <io.h>
  #define ADDON_OPEN   ::_open
  #define ADDON_CLOSE  ::_close
  #define ADDON_DUP    ::_dup
  #define ADDON_DUP2   ::_dup2
  #define ADDON_STDOUT_FD _fileno(stdout)
  #define ADDON_STDERR_FD _fileno(stderr)
  #define ADDON_DEVNULL "NUL"
#else
  #include <unistd.h>
  #define ADDON_OPEN   ::open
  #define ADDON_CLOSE  ::close
  #define ADDON_DUP    ::dup
  #define ADDON_DUP2   ::dup2
  #define ADDON_STDOUT_FD STDOUT_FILENO
  #define ADDON_STDERR_FD STDERR_FILENO
  #define ADDON_DEVNULL "/dev/null"
#endif


#if !HAVE_LIBSLIC3R
#error "libslic3r is required. Placeholders are not allowed."
#endif


#if HAVE_LIBSLIC3R

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

    // Lightweight ZIP sanity check for generated .3mf files.
    // Verifies PK signature and End Of Central Directory presence.
    static bool is_valid_3mf_zip(const std::string& file_path, std::string* reason = nullptr)
    {
        std::ifstream in(file_path, std::ios::binary);
        if (!in.is_open()) {
            if (reason) *reason = "cannot open output file";
            return false;
        }

        in.seekg(0, std::ios::end);
        const auto end_pos = in.tellg();
        if (end_pos <= 0) {
            if (reason) *reason = "empty output file";
            return false;
        }

        const std::uint64_t file_size = static_cast<std::uint64_t>(end_pos);
        if (file_size < 22) { // minimum EOCD size
            if (reason) *reason = "file too small to be a ZIP archive";
            return false;
        }

        // ZIP local header signature should start with PK.
        in.seekg(0, std::ios::beg);
        unsigned char sig[4] = {0, 0, 0, 0};
        in.read(reinterpret_cast<char*>(sig), 4);
        if (!in || sig[0] != 0x50 || sig[1] != 0x4B) {
            if (reason) *reason = "missing PK signature at file start";
            return false;
        }

        // EOCD can be located in the last 65,557 bytes (22 + max comment 65,535).
        const std::uint64_t tail_size = std::min<std::uint64_t>(file_size, 65557);
        std::vector<unsigned char> tail(static_cast<size_t>(tail_size));
        in.seekg(static_cast<std::streamoff>(file_size - tail_size), std::ios::beg);
        in.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
        if (!in) {
            if (reason) *reason = "failed to read ZIP tail";
            return false;
        }

        bool found_eocd = false;
        for (std::size_t i = tail.size(); i >= 4; --i) {
            const std::size_t j = i - 4;
            if (tail[j] == 0x50 && tail[j + 1] == 0x4B && tail[j + 2] == 0x05 && tail[j + 3] == 0x06) {
                found_eocd = true;
                break;
            }
            if (j == 0) break;
        }

        if (!found_eocd) {
            if (reason) *reason = "can't find end of central directory";
            return false;
        }

        return true;
    }
}

void OrcaSlicerCli::AddonCore::setLoggingSilenced(bool silent) {
    static std::mutex s_silencer_mutex;
    std::lock_guard<std::mutex> lock(s_silencer_mutex);
    if (silent == s_silenced) return;

    if (silent) {
        s_devnull_fd = ADDON_OPEN(ADDON_DEVNULL, O_WRONLY);
        if (s_devnull_fd < 0) return;
        try {
            if (!s_devnull_stream.is_open()) s_devnull_stream.open(ADDON_DEVNULL);
        } catch (...) {}
        if (!s_orig_cout) s_orig_cout = std::cout.rdbuf();
        if (!s_orig_cerr) s_orig_cerr = std::cerr.rdbuf();
        int saved_out = (s_saved_stdout < 0) ? ADDON_DUP(ADDON_STDOUT_FD) : s_saved_stdout;
        int saved_err = (s_saved_stderr < 0) ? ADDON_DUP(ADDON_STDERR_FD) : s_saved_stderr;
        if (saved_out < 0 || saved_err < 0) {
            if (saved_out >= 0) ADDON_CLOSE(saved_out);
            if (saved_err >= 0) ADDON_CLOSE(saved_err);
            return;
        }
        s_saved_stdout = saved_out;
        s_saved_stderr = saved_err;
        try { std::cout.rdbuf(s_devnull_stream.rdbuf()); } catch (...) {}
        try { std::cerr.rdbuf(s_devnull_stream.rdbuf()); } catch (...) {}
        ADDON_DUP2(s_devnull_fd, ADDON_STDOUT_FD);
        ADDON_DUP2(s_devnull_fd, ADDON_STDERR_FD);
        s_silenced = true;
    } else {
        if (s_orig_cout) { try { std::cout.rdbuf(s_orig_cout); } catch (...) {} }
        if (s_orig_cerr) { try { std::cerr.rdbuf(s_orig_cerr); } catch (...) {} }
        if (s_saved_stdout >= 0) { ADDON_DUP2(s_saved_stdout, ADDON_STDOUT_FD); ADDON_CLOSE(s_saved_stdout); s_saved_stdout = -1; }
        if (s_saved_stderr >= 0) { ADDON_DUP2(s_saved_stderr, ADDON_STDERR_FD); ADDON_CLOSE(s_saved_stderr); s_saved_stderr = -1; }
        if (s_devnull_fd >= 0) { ADDON_CLOSE(s_devnull_fd); s_devnull_fd = -1; }
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

#include "core/plate/PlateCentering.hpp"
#include "core/init/Initialization.hpp"
#include "core/model/ModelIO.hpp"
#include "core/config/ConfigManager.hpp"
#include "core/slice/SliceEngine.hpp"
#include "core/util/Utilities.hpp"
using OrcaSlicerCli::util::dbg_log;
using OrcaSlicerCli::util::bed_temp_key_for;

#endif  // HAVE_LIBSLIC3R

// Helper: Sanitize a DynamicPrintConfig to ensure all options have compatible types.
// This prevents "Comparing incompatible types" errors when print->apply() compares configs.
// For each key, we check if the type matches what's expected by the print_config_def.
// If not, we try to convert by serializing/deserializing, or we remove the problematic key.
static std::vector<std::string> sanitize_config_types(Slic3r::DynamicPrintConfig& cfg) {
    std::vector<std::string> erased_keys;
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
            LOG_DEBUG(std::string("sanitize_config_types: fixing type for '") + key +
                      "' (type " + std::to_string((int)opt->type()) + " -> " + std::to_string((int)opt_def->type) +
                      ") value='" + serialized + "'");

            // Create new option with correct type and deserialize
            std::unique_ptr<Slic3r::ConfigOption> new_opt(opt_def->create_default_option());
            if (new_opt) {
                try {
                    new_opt->deserialize(serialized, Slic3r::ForwardCompatibilitySubstitutionRule::Enable);
                    cfg.set_key_value(key, new_opt.release()); // set_key_value takes ownership
                } catch (...) {
                    // new_opt still held by unique_ptr if deserialize threw; released otherwise
                    erased_keys.push_back(key);
                    cfg.erase(key);
                    LOG_DEBUG("sanitize_config_types: removed key '" + key + "' (conversion failed)");
                }
            }
        } catch (...) {
            erased_keys.push_back(key);
            try { cfg.erase(key); } catch (...) {}
        }
    }

    if (!keys_to_fix.empty()) {
        LOG_DEBUG("sanitize_config_types: fixed " + std::to_string(keys_to_fix.size()) + " type mismatches");
    }
    return erased_keys;
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
    // Keys erased by sanitize_config_types (type mismatch, replaced by defaults)
    std::vector<std::string> sanitize_erased_keys;

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

    bool m_cleaned = false;

    Impl() = default;
    ~Impl() { cleanup(); } // Ensure RAII cleanup even if shutdown() was not called


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
        if (m_cleaned) return;
        m_cleaned = true;
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
        last_error.clear();

        std::error_code ec;
        if (!std::filesystem::exists(filename, ec)) {
            if (ec) {
                LOG_ERROR("Filesystem error checking existence: " + ec.message());
            }
            last_error = "File not found: " + filename;
            return false;
        }

        std::filesystem::path file_path(filename);
        LOG_DEBUG(std::string("loadModelFromFile: '") + filename + "' ext='" + file_path.extension().string() + "' plate_id=" + std::to_string(plate_id));
        std::string extension = file_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

        if (extension != ".3mf" && extension != ".stl" && extension != ".obj") {
            last_error = "Unsupported file format: " + extension;
            return false;
        }

#if HAVE_LIBSLIC3R
        if (!model) { last_error = "Model object not initialized. Call initialize() first."; return false; }

        try {
            print_cfg_overrides.clear();
            project_cfg_after_3mf.clear();
            print_overrides_keys.clear();
            project_overrides_keys.clear();
            plate_data_src.clear();
            saved_filament_colours.clear();
            saved_change_filament_gcode.clear();
            total_plates_count = 0;
            detected_extruders = 0;
            // Clear existing model
            model->clear_objects();

            // Load model based on extension
            if (extension == ".stl") {
                if (!OrcaSlicerCli::model::load_stl(filename, *model, last_error)) {
                    return false;
                }

                detected_extruders = 0;
                saved_filament_colours.clear();
                saved_change_filament_gcode.clear();
                plate_data_src.clear();
                project_cfg_after_3mf.clear();
                project_overrides_keys.clear();
                print_cfg_overrides.clear();
                print_overrides_keys.clear();
                total_plates_count = 0;
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
                        last_error)) {
                    return false;
                }
            }






            // GUI parity: do not normalize instances here. Use only plate_origin for plate-local coordinates.
            // Keep instances in assembly space and apply the offset only during G-code export.
            LOG_DEBUG(std::string("3MF project preset names: printer='") + project_printer_preset +
                      "', print='" + project_print_preset +
                      "', filament='" + project_filament_preset + "'");

            // Ensure model has objects and default instances
            if (!OrcaSlicerCli::model::ensure_default_instances(*model, last_error)) {
                return false;
            }

            return true;
        } catch (const std::exception& e) {
            last_error = std::string("Error loading model: ") + e.what();
            return false;
        }
#else

        last_error = "libslic3r not available";
        return false;
#endif
    }




    // Helper to reset and configure the Print object (GUI Parity)
    void resetAndConfigurePrint() {
        LOG_DEBUG("Resetting Print object for new slicing operation");
        
        if (print) {
            print.reset();
        }
        print = std::make_unique<Slic3r::Print>();

        // Configure basic flags
        try {
            bool is_bbl = preset_bundle.is_bbl_vendor();
            // Fallback: detect BBL by printer_model when vendor bundle is not loaded
            if (!is_bbl && config) {
                try {
                    std::string printer_model = config->opt_string("printer_model");
                    if (!printer_model.empty() &&
                        (printer_model.find("Bambu Lab") != std::string::npos ||
                         printer_model.find("BBL") != std::string::npos))
                        is_bbl = true;
                } catch (...) {}
            }
            print->is_BBL_printer() = is_bbl;
            LOG_DEBUG(std::string("is_BBL_printer set to ") + (is_bbl ? "true" : "false"));
        } catch (...) {
            LOG_WARNING("Failed to set is_BBL_printer flag");
        }

        // Initialize state
        print->set_plate_origin(Slic3r::Vec3d(0, 0, 0));
        print->set_plate_index(0);
        
        // Set cancel callback to avoid segfaults
        print->set_cancel_callback([](){});
        print->restart(); // Initialize cancel status
    }

    void configurePlateOrigin() {
        if (!print || !model || !config) return;
        // Set basic plate index on Print and Model (GUI parity)
        int idx0 = (plate_id > 0 ? plate_id - 1 : 0);
        model->curr_plate_index = idx0;
        print->set_plate_index(idx0);

        // Derive bed size to compute logical stride
        // This logic mimics how the GUI arranges plates visually
        double stride_x = 250.0; // Default fallback
        double stride_y = 250.0;
        
        try {
            Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
            if (!bed_pts.empty()) {
                long minx = std::numeric_limits<long>::max(), maxx = std::numeric_limits<long>::min();
                long miny = std::numeric_limits<long>::max(), maxy = std::numeric_limits<long>::min();
                for (const auto &p : bed_pts) { 
                    if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); 
                    if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y(); 
                }
                const double bed_w = Slic3r::unscale<double>(maxx - minx);
                const double bed_d = Slic3r::unscale<double>(maxy - miny);
                if (bed_w > 0 && bed_d > 0) {
                    stride_x = bed_w * 1.2;
                    stride_y = bed_d * 1.2;
                }
            }
        } catch (const std::exception& e) {
            LOG_WARNING(std::string("configurePlateOrigin: failed to compute bed shape: ") + e.what());
        } catch (...) {
            LOG_WARNING("configurePlateOrigin: failed to compute bed shape (unknown error)");
        }

        if (center_on_bed) {
            bool is_bbl = false; try { is_bbl = preset_bundle.is_bbl_vendor(); } catch (...) {}
            if (!is_bbl) {
                // Non-BBL: Center instances physically on the bed
                (void)center_instances_on_bed_center();
                print->set_plate_origin(Slic3r::Vec3d(0, 0, 0));
                LOG_DEBUG("Centered instances on bed (Non-BBL mode)");
            } else {
                // BBL: Center plate origin
                (void)center_plate_origin_to_bed_center();
                LOG_DEBUG("Centered plate origin to bed (BBL mode)");
            }
        } else {
            // Default: Try to compute from instances, fallback to grid layout
            if (!compute_and_set_plate_origin_from_model_instances()) {
                const int cols = (int)std::ceil(std::sqrt((double)(total_plates_count > 0 ? total_plates_count : 1)));
                const int row = idx0 / cols;
                const int col = idx0 % cols;
                print->set_plate_origin(Slic3r::Vec3d(col * stride_x, -(row * stride_y), 0));
                LOG_DEBUG("Set plate origin from grid layout fallback");
            } else {
                LOG_DEBUG("Set plate origin from model instances");
            }
        }
    }

    void preparePrintConfig(Slic3r::DynamicPrintConfig& out_config) {
        if (!model) return;

        // 1. Apply plate-specific overrides
        if (!plate_data_src.empty() && plate_id > 0 && plate_id <= (int)plate_data_src.size()) {
            const auto& pd = plate_data_src[plate_id - 1];
            if (pd && !pd->config.empty()) {
                LOG_DEBUG(std::string("Applying plate config overrides from plate ") + std::to_string(plate_id));
                auto plate_keys = pd->config.keys();
                for (const auto& key : plate_keys) {
                    try {
                        std::vector<std::string> single_key = {key};
                        out_config.apply_only(pd->config, single_key, true);
                    } catch (...) {}
                }
            }
        }

        // 2. Trim filament arrays to match used extruders
        // This prevents crashes when arrays are larger than the number of defined extruders
        std::set<int> used_extruders;
        for (const auto* obj : model->objects) {
            for (const auto& volume : obj->volumes) {
                used_extruders.insert(volume->extruder_id());
            }
        }
        size_t max_enc = used_extruders.empty() ? size_t(1) : (static_cast<size_t>(*used_extruders.rbegin()) + 1);
        // For painted models, volume->extruder_id() only reflects the base extruder (e.g. 1),
        // not the paint color slots (which can be up to detected_extruders).
        // Without this guard, a single-volume painted 4-color model gets max_enc=2 and
        // filament_colour is trimmed from 4 to 2, producing a 2-color GCode.
        if (detected_extruders > max_enc) max_enc = detected_extruders;

        // Helper to trim check
        auto check_trim = [&](const char* key) {
             if (auto* opt = out_config.opt<Slic3r::ConfigOptionFloats>(key, false)) {
                 if (opt->values.size() > max_enc) opt->values.resize(max_enc);
             } else if (auto* opt_s = out_config.opt<Slic3r::ConfigOptionStrings>(key, false)) {
                 if (opt_s->values.size() > max_enc) opt_s->values.resize(max_enc);
             }
        };

        check_trim("filament_colour");
        check_trim("filament_diameter");
        check_trim("filament_type");
        check_trim("filament_density");
        check_trim("filament_cost");

        // 3. Sanitize types
        sanitize_erased_keys = sanitize_config_types(out_config);
    }

    void promoteConfigToMetadata() {
        try {
            auto& fpc = const_cast<Slic3r::DynamicPrintConfig&>(print->full_print_config());
            auto promote = [&fpc](const char* key, const Slic3r::ConfigBase& src) {
                if (const Slic3r::ConfigOption* o = src.option(key)) {
                    fpc.set_key_value(key, o->clone());
                }
            };
            promote("seam_position", print->default_object_config());
            promote("bottom_surface_pattern", print->default_region_config());
            promote("sparse_infill_pattern", print->default_region_config());
            promote("top_surface_pattern", print->default_region_config());
            promote("internal_solid_infill_pattern", print->default_region_config());
            LOG_DEBUG("Promoted config values for metadata");
        } catch (...) {
            LOG_WARNING("Failed to promote config values");
        }
    }
    
    // TODO: Implementação correta baseada no arquivo OrcaSlicer src/libslic3r/Model.cpp:674
    // Model::update_print_volume_state() usa BuildVolume::volume_state_bbox da mesma forma.
    // A diferença é que o GUI chama update_print_volume_state em cada ModelInstance (via PVS flags),
    // enquanto aqui fazemos a checagem direto no bounding box — logicamente equivalente.
    // Check if objects are outside printable area
    // Returns true if OUTSIDE (error/warning needed)
    bool checkOutside(std::string &msg) {
        if (!print) return false;
        if (!model) return false;
        LOG_DEBUG("checkOutside: starting printable area validation");

        const auto &pc = print->config();
        Slic3r::BuildVolume build_volume(pc.printable_area.values, pc.printable_height);
        if (!build_volume.valid()) {
            LOG_DEBUG("checkOutside: build_volume invalid, skipping check");
            return false; // Cannot check
        }

        // Log a bit of high-level state for diagnostics
        size_t obj_count = model ? model->objects.size() : 0;
        LOG_DEBUG(std::string("checkOutside: model objects=") + std::to_string(obj_count));

        const Slic3r::Vec3d po = print->get_plate_origin();
        const Slic3r::Vec3d shift_xy(-po(0), -po(1), 0.0);

        // Check instances
        for (const auto *obj : model->objects) {
            if (!obj) continue;
            for (const auto *inst : obj->instances) {
                if (!inst) continue;
                Slic3r::BoundingBoxf3 bb = obj->instance_bounding_box(*inst);
                Slic3r::BoundingBoxf3 bb_local(bb.min + shift_xy, bb.max + shift_xy);
                auto state = build_volume.volume_state_bbox(bb_local, true);
                if (state == Slic3r::BuildVolume::ObjectState::Outside) {
                   msg = "Elements outside printable area: '" + (obj->name.empty() ? "object" : obj->name) + "'";
                   LOG_DEBUG(std::string("checkOutside: object outside printable area -> '") +
                             (obj->name.empty() ? "object" : obj->name) + "'");
                   return true;
                }
            }
        }
        
        // Check Wipe Tower
        // GUI parity (Print.cpp Print::validate): only validate the tower when it will
        // actually be printed — has_wipe_tower() already accounts for enable_prime_tower,
        // spiral mode and filament count. wipe_tower_x/y are per-plate vectors, so index
        // them by the plate being sliced, not plate 0.
        if (print->has_wipe_tower()) {
             const int plate_idx = std::max(0, print->get_plate_index());
             float wtx = 0.f, wty = 0.f;
             try { wtx = pc.wipe_tower_x.get_at(plate_idx); } catch (...) {}
             try { wty = pc.wipe_tower_y.get_at(plate_idx); } catch (...) {}
             Slic3r::Vec3d pt(double(wtx), double(wty), 0.0);
             Slic3r::BoundingBoxf3 wtbb(pt, pt);
                 auto state = build_volume.volume_state_bbox(wtbb, true);
                 if (state == Slic3r::BuildVolume::ObjectState::Outside) {
                     msg = "Prime Tower outside printable area";
                     LOG_DEBUG("checkOutside: prime tower outside printable area");
                     return true;
                 }
        }
        return false;
    }

    // TODO: verificar se podemos remover esse codigo
    // OrcaSlicer GUI usa o sistema de Arrange (src/slic3r/GUI/Plater.cpp via ArrangeJob) para
    // reposicionar objetos — um algoritmo muito mais sofisticado (bin packing / NFP).
    // Esta implementação é uma versão simplificada (row layout) sem equivalente direto no OrcaSlicer.
    // Só é invocada quando auto_realign_if_needed=true, que não existe no GUI (é feature exclusiva do addon).
    bool simpleReposition() {
        if (!model) return false;
        LOG_INFO("Attempting simple repositioning...");
        try {
            Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
            Slic3r::BoundingBox bed_bb(bed_pts);
            double bed_width = Slic3r::unscale<double>(bed_bb.size().x());
            double bed_height = Slic3r::unscale<double>(bed_bb.size().y());
            Slic3r::Point bed_center_pt = bed_bb.center();
            double bed_center_x = Slic3r::unscale<double>(bed_center_pt.x());
            double bed_center_y = Slic3r::unscale<double>(bed_center_pt.y());

            LOG_DEBUG(std::string("simpleReposition: bed_width=") + std::to_string(bed_width) +
                      " bed_height=" + std::to_string(bed_height) +
                      " center=(" + std::to_string(bed_center_x) + "," + std::to_string(bed_center_y) + ")");

            // Collect all instances
            std::vector<Slic3r::ModelInstance*> all_instances;
            for (auto* obj : model->objects) {
                for (auto* inst : obj->instances) all_instances.push_back(inst);
            }
            LOG_DEBUG(std::string("simpleReposition: collected instances=") + std::to_string(all_instances.size()));
            if (all_instances.empty()) return true;

            // Strategy: Place in row centered on bed
             double total_width = 0;
             double spacing = 5.0;
             for (auto* inst : all_instances) {
                 total_width += inst->get_object()->instance_bounding_box(*inst).size().x() + spacing;
             }
             total_width -= spacing;

             bool fits_in_row = (total_width <= bed_width - 10);

             LOG_DEBUG(std::string("simpleReposition: total_width=") + std::to_string(total_width) +
                       " spacing=" + std::to_string(spacing) +
                       " fits_in_row=" + (fits_in_row ? "1" : "0"));
             
             if (fits_in_row) {
                 double current_x = bed_center_x - total_width / 2.0;
                 for (auto* inst : all_instances) {
                      auto* obj = inst->get_object();
                      double w = obj->instance_bounding_box(*inst).size().x();
                      double cx = current_x + w/2.0;
                      double cy = bed_center_y;
                      
                      Slic3r::Vec3d mesh_center = obj->instance_bounding_box(*inst).center();
                      inst->set_offset(Slic3r::Vec3d(cx - mesh_center.x(), cy - mesh_center.y(), inst->get_offset().z()));
                      
                      current_x += w + spacing;
                 }
             } else {
                 // Stick them all in center (fallback)
                 for (auto* inst : all_instances) {
                     Slic3r::Vec3d mesh_center = inst->get_object()->instance_bounding_box(*inst).center();
                     inst->set_offset(Slic3r::Vec3d(bed_center_x - mesh_center.x(), bed_center_y - mesh_center.y(), inst->get_offset().z()));
                 }
             }
             
             // Update bounding boxes
             for (auto* obj : model->objects) obj->invalidate_bounding_box();

             // Reset plate origin to 0 since we centered absolutely on bed
             print->set_plate_origin(Slic3r::Vec3d(0,0,0));

             // Note: We don't call print->apply() here - it will be called by validateAndAutoRealign
             // with proper error handling for config type mismatches
             return true;

        } catch (const std::exception& e) {
            LOG_ERROR(std::string("Reposition failed: ") + e.what());
            return false;
        }
    }

    bool validateAndAutoRealign(std::string &error_msg) {
       LOG_DEBUG("validateAndAutoRealign: begin");

       std::string msg;

       bool outside = false;
       try {
           outside = checkOutside(msg);
       } catch (const std::exception& e) {
           LOG_ERROR(std::string("validateAndAutoRealign: checkOutside threw: ") + e.what());
           error_msg = "validateAndAutoRealign: checkOutside exception";
           return false;
       }

       LOG_DEBUG(std::string("validateAndAutoRealign: checkOutside result=") + (outside ? "OUTSIDE" : "INSIDE") +
                 " auto_realign_if_needed=" + (auto_realign_if_needed ? "1" : "0"));

       if (!outside) return true; // All good

       LOG_WARNING(msg);
       if (auto_realign_if_needed) {
           LOG_DEBUG("validateAndAutoRealign: attempting simpleReposition()");
           if (simpleReposition()) {
        // After repositioning, we need to re-apply the model to the print object
                // to update the print objects with the new positions.
                // IMPORTANT: use preparePrintConfig() (not raw *config) so that plate-level
                // overrides (e.g. print_sequence=by object) are preserved.
                try {
                    Slic3r::DynamicPrintConfig realign_cfg = *config;
                    preparePrintConfig(realign_cfg);
                    print->apply(*model, realign_cfg);
                } catch (const std::exception& e) {
                    // If apply fails due to config type mismatch, try with a minimal config
                    LOG_WARNING(std::string("Re-apply after reposition failed: ") + e.what() + ", trying minimal re-apply");
                    try {
                        // Create a minimal config that only updates geometry, not parameters
                        Slic3r::DynamicPrintConfig minimal_cfg = *config;
                        preparePrintConfig(minimal_cfg);
                        print->apply(*model, minimal_cfg);
                    } catch (const std::exception& e2) {
                        LOG_WARNING(std::string("Minimal re-apply also failed: ") + e2.what() + ", continuing anyway");
                        // Continue anyway - the model positions were updated
                    }
                }

               if (!checkOutside(msg)) {
                   LOG_INFO("Auto-realign successful");
                   return true;
               }
           }
           else {
               LOG_WARNING("validateAndAutoRealign: simpleReposition() returned false");
           }
       }

       error_msg = msg;
       LOG_WARNING("validateAndAutoRealign: objects remain outside printable area after validation/realign");
       return false;
    }

    bool performSlicing(const std::string& output_file) {
#if HAVE_LIBSLIC3R



        try {
            LOG_INFO("Starting slicing process...");

            // reset last-known stats
            last_error.clear();
            last_estimated_time_sec = -1.0;
            last_filament_used_grams = -1.0;
            
            if (!model || model->objects.empty()) {
                last_error = "No model loaded for slicing";
                return false;
            }

            // Log basics
            LOG_DEBUG(std::string("Model objects: ") + std::to_string(model->objects.size()));
            
            // Log selected presets
            LOG_DEBUG(std::string("Printer preset: ") + preset_bundle.printers.get_selected_preset_name());
            LOG_DEBUG(std::string("Print preset:   ") + preset_bundle.prints.get_selected_preset_name());

            // 1. Reset and Configure Print Object
            resetAndConfigurePrint();

            // 2. Configure Plate Origin
            configurePlateOrigin();

            // 2b. Drop floating instances to the bed (min Z > 0). Headless parity com o
            // "place on bed" do GUI: sem isso o slice falha com "empty initial layer".
            // Instâncias afundadas (min Z < 0) são preservadas (corte da base).
            if (OrcaSlicerCli::plate::drop_floating_instances_to_bed(model.get())) {
                LOG_DEBUG("performSlicing: dropped floating instance(s) to bed");
            }

            // 3. Prepare Configuration
            LOG_DEBUG("Preparing print configuration with overrides...");
            Slic3r::DynamicPrintConfig apply_config = *config;
            preparePrintConfig(apply_config);

            // 4. Apply configuration to Print
            LOG_INFO("Applying configuration to print object...");
            try {
                print->apply(*model, apply_config);
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("Print::apply() failed: ") + e.what());
                throw;
            }

            // 5. Promote config to metadata (for G-code headers)
            promoteConfigToMetadata();

            // 6. Validation and Auto-realign
            std::string oob_msg;
            if (!validateAndAutoRealign(oob_msg)) {
                last_error = oob_msg;
                LOG_ERROR(last_error);
                return false;
            }

            // 7. Perform Slicing
            LOG_INFO("Starting print processing (slicing)...");
            try {
                print->process();
                LOG_INFO("Slicing completed.");
            } catch (const std::exception& e) {
                // NOTA: Nao podemos capturar Slic3r::SlicingErrors por tipo porque
                // liblibslic3r.a e' pre-compilado separadamente, causando duplicacao
                // de typeinfo. Nem catch por tipo nem dynamic_cast funcionam.
                //
                // Contorno: usamos reinterpret_cast para ler o vector errors_ em
                // um deslocamento conhecido. Ambas as compilacoes usam o mesmo
                // header Exception.hpp, entao o layout em memoria e' identico.
                //
                // Layout (Itanium ABI + libc++ macOS):
                //   std::exception (base): <empty>
                //   std::runtime_error:   vtable(8) + string(24) = 32
                //   Slic3r::SlicingError: 32 + objectId(8) = 40
                //   Slic3r::SlicingErrors: 32 + vector<SlicingError>(24) = 56
                //
                // Ver webslicer/docs/bugs/2026-06-06-slicing-error-propagation.md

                const std::string what = e.what();

                if (what == "Errors") {
                    // SlicingErrors — extrai o vector via layout conhecido.
                    // Absolutamente FRAGIL, mas necessario: o typeinfo esta
                    // duplicado e nao podemos capturar por tipo. Ambas as
                    // compilacoes usam o mesmo Exception.hpp, entao o layout
                    // em memoria e' identico.
                    //
                    // sizeof(std::runtime_error) varia por plataforma:
                    // - macOS libc++: 32 (vtable 8 + string SSO 24)
                    // - Linux libstdc++: 32 (vtable 8 + string 24)
                    // - Windows MSVC: 28 (vtable 8 + string 20)
                    try {
                        const size_t rtSize = sizeof(std::runtime_error);
                        struct SlicingErrorAccess {
                            char    base[sizeof(std::runtime_error)];
                            size_t  objectId;
                        };
                        struct SlicingErrorsAccess {
                            char    base[sizeof(std::runtime_error)];
                            const SlicingErrorAccess* begin;
                            const SlicingErrorAccess* end;
                            const SlicingErrorAccess* capacity;
                        };
                        static_assert(sizeof(SlicingErrorAccess) == sizeof(std::runtime_error) + sizeof(size_t),
                            "SlicingErrorAccess layout mismatch");
                        static_assert(sizeof(SlicingErrorsAccess) == sizeof(std::runtime_error) + 3 * sizeof(void*),
                            "SlicingErrorsAccess layout mismatch");

                        const auto& data = reinterpret_cast<const SlicingErrorsAccess&>(e);
                        size_t count = static_cast<size_t>(data.end - data.begin);
                        std::string details;
                        for (const auto* p = data.begin; p != data.end; ++p) {
                            if (!details.empty()) details += " | ";
                            const auto& errBase = reinterpret_cast<const std::runtime_error&>(*p);
                            details += std::string(errBase.what())
                                     + " (object " + std::to_string(p->objectId) + ")";
                        }
                        last_error = details.empty()
                            ? std::string("SlicingErrors (") + std::to_string(count) + " errors, what='Errors', type=" + typeid(e).name() + ")"
                            : details;
                    } catch (...) {
                        // Se o reinterpret_cast falhar (e.g. layout diferente),
                        // loga o que sabemos
                        last_error = std::string("SlicingErrors (what='Errors', type=")
                                   + typeid(e).name()
                                   + ") — falha ao extrair detalhes via layout";
                    }
                    LOG_ERROR(std::string("Slicing process failed: ") + last_error);
                    return false;
                }

                // Outras excecoes std::exception — re-lanca para o catch externo
                last_error = std::string("Slicing process failed: ") + what
                           + " (type=" + typeid(e).name() + ")";
                LOG_ERROR(last_error);
                throw;
            } catch (...) {
                last_error = "Slicing process failed with unknown error";
                LOG_ERROR(last_error);
                throw;
            }

            // TODO: FIX para impressão sequencial (ByObject) — problema de arrange_order não inicializado.
            //
            // Contexto: quando print_sequence == ByObject, export_gcode() chama internamente
            // sort_object_instances_by_model_order() (OrcaSlicer/src/libslic3r/GCode.cpp:2060),
            // que ordena as instâncias pelo campo ModelInstance::arrange_order
            // (OrcaSlicer/src/libslic3r/GCode.cpp:1781).
            //
            // O arrange_order é inicializado dentro de sequential_print_clearance_valid()
            // (OrcaSlicer/src/libslic3r/Print.cpp:829), que é chamado apenas dentro de
            // Print::validate() (OrcaSlicer/src/libslic3r/Print.cpp:1078).
            //
            // Nosso addon não chama print->validate() — portanto todos os ModelInstances
            // ficam com arrange_order == 0 (valor padrão em Model.hpp:1250).
            //
            // Quando todas as instâncias têm arrange_order == 0, o lower_bound em
            // GCode.cpp:1793 sempre encontra o mesmo primeiro elemento, e o check
            // `it->first == model_instance` só passa para a primeira instância.
            // Resultado: apenas o primeiro objeto é incluído no G-code gerado.
            //
            // Solução: inicializar arrange_order manualmente antes do export, replicando
            // o comportamento do OrcaSlicer GUI (Print.cpp:829 e ModelArrange.hpp:84).
            {
                const auto* ps_opt = print->config().option<Slic3r::ConfigOptionEnum<Slic3r::PrintSequence>>("print_sequence");
                if (ps_opt && ps_opt->value == Slic3r::PrintSequence::ByObject) {
                    LOG_DEBUG("ByObject mode detected: initializing arrange_order via print->objects() (not model->objects)");
                    // IMPORTANT: print->model() is an internal *copy* of the original model.
                    // sort_object_instances_by_model_order() uses print_instance.model_instance
                    // which points into that copy — so we must set arrange_order through
                    // print->objects() to reach the same ModelInstance* pointers.
                    int seq_order = 1;
                    for (const auto* print_object : print->objects()) {
                        for (const auto& print_instance : print_object->instances()) {
                            const_cast<Slic3r::ModelInstance*>(print_instance.model_instance)->arrange_order = seq_order++;
                        }
                    }
                    LOG_DEBUG(std::string("arrange_order initialized for ") + std::to_string(seq_order - 1) + " instance(s) via print->objects()");
                }
            }

            // Decide export target by output extension
            std::filesystem::path out_path(output_file);
            std::string out_ext = out_path.extension().string();
            std::transform(out_ext.begin(), out_ext.end(), out_ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

            const bool export_3mf = (out_ext == ".3mf");

            if (export_3mf) {
                LOG_DEBUG("Exporting 3MF (ZIP package with embedded G-code) to: " + output_file);

                // Prepare temp G-code path: use stem (without any extensions) + ".gcode"
                // Example: "/tmp/orca-xxx.gcode.3mf" -> "/tmp/orca-xxx.gcode"
                std::filesystem::path tmp_gcode = out_path.parent_path() / out_path.stem();
                tmp_gcode.replace_extension(".gcode");

                // Remove any existing files (target .3mf and temp .gcode)
                std::error_code ec;
                if (std::filesystem::exists(output_file, ec)) std::filesystem::remove(output_file);
                if (std::filesystem::exists(tmp_gcode, ec))   std::filesystem::remove(tmp_gcode);



                // CRITICAL: Synchronize flush_volumes_matrix in full_print_config() BEFORE export
                // This prevents the "Flush volumes matrix do not match to the correct size" error
                LOG_DEBUG("[PRE-SYNC] About to synchronize flush_volumes in full_print_config()");
                try {
                    auto& fpc = const_cast<Slic3r::DynamicPrintConfig&>(print->full_print_config());
                    auto* fil_colour = fpc.opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
                    auto* flush_matrix = fpc.opt<Slic3r::ConfigOptionFloats>("flush_volumes_matrix", false);
                    auto* flush_vector = fpc.opt<Slic3r::ConfigOptionFloats>("flush_volumes_vector", false);
                    auto* flush_multiplier = fpc.opt<Slic3r::ConfigOptionFloats>("flush_multiplier", false);

                    size_t filament_count = fil_colour ? fil_colour->values.size() : 1;
                    size_t heads_count = flush_multiplier ? flush_multiplier->values.size() : 1;
                    size_t expected_matrix_size = filament_count * filament_count * heads_count;

                    LOG_DEBUG(std::string("[BEFORE EXPORT] Synchronizing flush_volumes_matrix - filament_count=") + std::to_string(filament_count) + 
                              ", heads_count=" + std::to_string(heads_count) + 
                              ", expected_matrix_size=" + std::to_string(expected_matrix_size));

                    if (flush_matrix) {
                        size_t current_size = flush_matrix->values.size();
                        if (current_size != expected_matrix_size) {
                            LOG_DEBUG(std::string("[BEFORE EXPORT] Resizing flush_volumes_matrix from ") + std::to_string(current_size) + " to " + std::to_string(expected_matrix_size));
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
                        LOG_DEBUG(std::string("[BEFORE EXPORT] Creating flush_volumes_matrix with size ") + std::to_string(expected_matrix_size));
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
                            LOG_DEBUG(std::string("[BEFORE EXPORT] Resizing flush_volumes_vector from ") + std::to_string(current_size) + " to " + std::to_string(expected_vector_size));
                            std::vector<double> new_vector(expected_vector_size, 140.0);
                            flush_vector->values = new_vector;
                        }
                    } else if (filament_count > 0) {
                        LOG_DEBUG(std::string("[BEFORE EXPORT] Creating flush_volumes_vector with size ") + std::to_string(expected_vector_size));
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
                            LOG_DEBUG("Clearing malformed bed_exclude_area (default [0x0]) to prevent OrcaSlicer crash");
                            bed_exclude->values.clear();
                        } else if (bed_exclude->values.size() % 4 != 0) {
                            // If not a multiple of 4 points, clear it (malformed)
                            LOG_DEBUG(std::string("Clearing malformed bed_exclude_area (size ") + std::to_string(bed_exclude->values.size()) + " is not a multiple of 4) to prevent OrcaSlicer crash");
                            bed_exclude->values.clear();
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING(std::string("Failed to synchronize flush_volumes_matrix: ") + e.what());
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

                    LOG_DEBUG(std::string("[VERIFICATION] filament_count=") + std::to_string(filament_count) +
                              ", heads_count=" + std::to_string(heads_count) +
                              ", expected_matrix=" + std::to_string(expected_matrix_size) +
                              ", actual_matrix=" + std::to_string(actual_matrix_size));

                    if (actual_matrix_size != expected_matrix_size && filament_count > 1) {
                        LOG_ERROR("flush_volumes_matrix size mismatch! Will cause error in append_full_config");
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING(std::string("Failed to verify flush_volumes_matrix: ") + e.what());
                }

                // Export raw G-code first
                Slic3r::GCodeProcessorResult proc_result;
                LOG_DEBUG("Exporting intermediate G-code to: " + tmp_gcode.string());
                try {
                    auto po = print->get_plate_origin();
                    LOG_DEBUG(std::string("plate_origin at export => (") + std::to_string(po(0)) + "," + std::to_string(po(1)) + ")");
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

                if (!std::filesystem::exists(tmp_gcode, ec)) {
                    if (ec) {
                        LOG_ERROR("Filesystem error checking temp G-code: " + ec.message());
                    }
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
                        auto diff_opt = std::make_unique<Slic3r::ConfigOptionStrings>();
                        diff_opt->values.push_back(diff_str);
                        plate.config.set_key_value("different_settings_to_system", diff_opt.release());
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
                                auto diff_opt_cfg = std::make_unique<Slic3r::ConfigOptionStrings>();
                                diff_opt_cfg->values.push_back(diff_str_cfg);
                                config->set_key_value("different_settings_to_system", diff_opt_cfg.release());
                            }
                        } catch (const std::exception& e) {
                            LOG_WARNING(std::string("Failed to copy overrides into main config: ") + e.what());
                        } catch (...) {
                            LOG_WARNING("Failed to copy overrides into main config (unknown error)");
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING(std::string("Failed to populate plate.config with overrides: ") + e.what());
                } catch (...) {
                    LOG_WARNING("Failed to populate plate.config with overrides (unknown error)");
                }

                // Fill additional plate metadata required by Bambu slice_info.config
                try {
                    // prediction (seconds as string)
                    float pred_secs = 0.0f;
                    if (!proc_result.print_statistics.modes.empty()) {
                        pred_secs = proc_result.print_statistics.modes[static_cast<size_t>(Slic3r::PrintEstimatedStatistics::ETimeMode::Normal)].time;
                    }
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
                            LOG_DEBUG("Clearing malformed bed_exclude_area in sp.config (default [0x0])");
                            bed_exclude_cfg->values.clear();
                        } else if (bed_exclude_cfg->values.size() % 4 != 0) {
                            LOG_DEBUG(std::string("Clearing malformed bed_exclude_area in sp.config (size ") + std::to_string(bed_exclude_cfg->values.size()) + " is not a multiple of 4)");
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
                            LOG_DEBUG(std::string("Fixing malformed printable_area (was size ") + std::to_string(printable_area_cfg->values.size()) + ") to default 256x256");
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
                            LOG_DEBUG("extruder_offset is [0x0] - valid for single extruder");
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
                        LOG_DEBUG(std::string("Used filament count from saved_filament_colours: ") + std::to_string(used_filament_count));
                    } else if (detected_extruders > 0) {
                        used_filament_count = detected_extruders;
                        LOG_DEBUG(std::string("Used filament count from detected_extruders: ") + std::to_string(used_filament_count));
                    } else if (auto* fil_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false)) {
                        used_filament_count = std::max(size_t(1), fil_colour->values.size());
                        LOG_DEBUG(std::string("Used filament count from config filament_colour: ") + std::to_string(used_filament_count));
                    }
                    LOG_DEBUG(std::string("Final used filament count for profile IDs: ") + std::to_string(used_filament_count));

                    // Priority 1: Project presets from loaded 3MF
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
                        LOG_DEBUG("Set printer_settings_id=" + printer_id);
                    }
                    if (!print_id.empty()) {
                        config->set_key_value("print_settings_id", new Slic3r::ConfigOptionString(print_id));
                        LOG_DEBUG("Set print_settings_id=" + print_id);
                    }
                    if (!filament_ids.empty()) {
                        auto fopt = std::make_unique<Slic3r::ConfigOptionStrings>(filament_ids);
                        config->set_key_value("filament_settings_id", fopt.release());
                        std::string ids_str;
                        for (const auto& fid : filament_ids) ids_str += "'" + fid + "' ";
                        LOG_DEBUG("Set filament_settings_id with " + std::to_string(filament_ids.size()) + " entries: " + ids_str);
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING(std::string("Failed to propagate preset IDs: ") + e.what());
                } catch (...) {
                    // best-effort
                }

                // SIMPLIFIED: Export only the current plate without dummy plates
                // This avoids potential issues with empty PlateData structures
                Slic3r::PlateDataPtrs pd_list;
                pd_list.push_back(&plate);

                sp.plate_data_list = pd_list;
                sp.export_plate_idx = 0; // Always 0 since we only have one plate in the list

                // Strategy: Generate 3MF with embedded G-code only (no original 3D model)
                // WithGcode: Include G-code file in the 3MF
                // SkipModel: Do not embed original 3D meshes/objects in resulting package
                // Silence: Suppress verbose logging
                // SplitModel: Save objects per file (Production Extension)
                // UseLoadedId: Use loaded IDs for identify_id
                // ShareMesh: Share mesh between objects
                // WithSliceInfo: Include slice statistics metadata
                sp.strategy = Slic3r::SaveStrategy::Silence |
                              Slic3r::SaveStrategy::WithGcode |
                              Slic3r::SaveStrategy::SkipModel |
                              Slic3r::SaveStrategy::SplitModel |
                              Slic3r::SaveStrategy::UseLoadedId |
                              Slic3r::SaveStrategy::ShareMesh |
                              Slic3r::SaveStrategy::WithSliceInfo;

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
                            const unsigned W=800,H=800; auto tn = std::make_unique<Slic3r::ThumbnailData>(); tn->set(W,H); tn->pixels.assign(W*H*4, 0);
                            // pad
                            double dx=(maxx-minx), dy=(maxy-miny); if(dx<=0||dy<=0){ minx=minx==1e9?0:minx; miny=miny==1e9?0:miny; maxx=std::max(maxx, minx+1.0); maxy=std::max(maxy, miny+1.0); dx=maxx-minx; dy=maxy-miny; }
                            double margin=20.0; double sx=(W-2*margin)/dx, sy=(H-2*margin)/dy; double s=std::min(sx,sy);
                            auto to_px=[&](double vx,double vy){ int px=(int)std::round(margin + (vx-minx)*s); int py=(int)std::round(H-1 - (margin + (vy-miny)*s)); return std::pair<int,int>(px,py); };
                            auto draw_line=[&](int x0,int y0,int x1,int y1, const std::array<unsigned char,4>& col){ int dx=std::abs(x1-x0), sx2=x0<x1?1:-1; int dy=-std::abs(y1-y0), sy2=y0<y1?1:-1; int err=dx+dy; for(;;){ if((unsigned)x0<W && (unsigned)y0<H){ size_t idx=((size_t)y0*W + x0)*4; tn->pixels[idx+0]=col[0]; tn->pixels[idx+1]=col[1]; tn->pixels[idx+2]=col[2]; tn->pixels[idx+3]=255; }
                                    if(x0==x1 && y0==y1) break; int e2=2*err; if(e2>=dy){ err+=dy; x0+=sx2;} if(e2<=dx){ err+=dx; y0+=sy2; } } };
                            for(const auto &sg: segs){ auto p0=to_px(sg.x1, sg.y1); auto p1=to_px(sg.x2, sg.y2); auto it=id2color.find(sg.t); auto col = (it!=id2color.end()? it->second : std::array<unsigned char,4>{255,255,255,255}); draw_line(p0.first,p0.second,p1.first,p1.second,col);}
                            // SIMPLIFIED: Add only the real thumbnail without empty placeholders
                            sp.thumbnail_data.clear();
                            {
                                auto *raw_tn = tn.release();
                                try {
                                    sp.thumbnail_data.push_back(raw_tn);
                                } catch (...) {
                                    delete raw_tn;
                                    throw;
                                }
                            }
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
                        {
                            auto *raw_bbox = bbox_data.release();
                            try {
                                sp.id_bboxes.push_back(raw_bbox);
                            } catch (...) {
                                delete raw_bbox;
                                throw;
                            }
                        }
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

                if (ok3mf) {
                    std::string zip_reason;
                    if (!is_valid_3mf_zip(output_file, &zip_reason)) {
                        last_error = std::string("3MF packaging produced invalid ZIP: ") + zip_reason;
                        ok3mf = false;
                    }
                }

                // Clean up temp G-code
                try { std::error_code ec; if (std::filesystem::exists(tmp_gcode, ec)) std::filesystem::remove(tmp_gcode); } catch (...) {}

                if (!ok3mf) {
                    if (last_error.empty()) last_error = "3MF packaging failed";
                    return false;
                }

                // Success
                return true;
#endif  // END OF DISABLED CODE
            } else {
                // Plain G-code export path
                LOG_DEBUG("Exporting G-code to: " + output_file);

                // Remove any existing output file
                std::error_code ec;
                if (std::filesystem::exists(output_file, ec)) {
                    std::filesystem::remove(output_file);
                }

                bool export_successful = false;
                std::string export_error;

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

                    LOG_DEBUG(std::string("[BEFORE GCODE EXPORT] Synchronizing flush_volumes_matrix: filament_count=") + std::to_string(filament_count) +
                              ", heads_count=" + std::to_string(heads_count) +
                              ", expected_matrix_size=" + std::to_string(expected_matrix_size));

                    if (flush_matrix) {
                        size_t current_size = flush_matrix->values.size();
                        if (current_size != expected_matrix_size) {
                            LOG_DEBUG(std::string("[BEFORE GCODE EXPORT] Resizing flush_volumes_matrix from ") + std::to_string(current_size) + " to " + std::to_string(expected_matrix_size));
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
                        LOG_DEBUG(std::string("[BEFORE GCODE EXPORT] Creating flush_volumes_matrix with size ") + std::to_string(expected_matrix_size));
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
                            LOG_DEBUG(std::string("[BEFORE GCODE EXPORT] Resizing flush_volumes_vector from ") + std::to_string(current_size) + " to " + std::to_string(expected_vector_size));
                            std::vector<double> new_vector(expected_vector_size, 140.0);
                            flush_vector->values = new_vector;
                        }
                    } else if (filament_count > 0) {
                        LOG_DEBUG(std::string("[BEFORE GCODE EXPORT] Creating flush_volumes_vector with size ") + std::to_string(expected_vector_size));
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
                            LOG_DEBUG("Clearing malformed bed_exclude_area (default [0x0]) to prevent OrcaSlicer crash");
                            bed_exclude->values.clear();
                        } else if (bed_exclude->values.size() % 4 != 0) {
                            // If not a multiple of 4 points, clear it (malformed)
                            LOG_DEBUG(std::string("Clearing malformed bed_exclude_area (size ") + std::to_string(bed_exclude->values.size()) + " is not a multiple of 4) to prevent OrcaSlicer crash");
                            bed_exclude->values.clear();
                        }
                    }
                } catch (const std::exception& e) {
                    LOG_WARNING(std::string("Failed to synchronize flush_volumes_matrix: ") + e.what());
                }

                try {


                    LOG_DEBUG("Attempting direct G-code export...");
                    // Log current plate_origin that will be applied by GCode
                    {
                        auto po = print->get_plate_origin();
                        LOG_DEBUG(std::string("plate_origin at export => (") + std::to_string(po(0)) + "," + std::to_string(po(1)) + ")");
                    }
                    Slic3r::GCodeProcessorResult proc_result; // provide valid result storage to avoid null deref in export path
                    std::string gcode_path = print->export_gcode(output_file, &proc_result, nullptr);
                    LOG_DEBUG("Direct G-code export completed successfully");
                    // Capture native statistics from proc_result
                    try {
                        float pred_secs = 0.0f;
                        if (!proc_result.print_statistics.modes.empty()) {
                            pred_secs = proc_result.print_statistics.modes[static_cast<size_t>(Slic3r::PrintEstimatedStatistics::ETimeMode::Normal)].time;
                        }
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
                    LOG_DEBUG(std::string("Direct export failed with exception: ") + e.what());
                    export_error = e.what();
                    export_successful = false;
                } catch (...) {
                    LOG_DEBUG("Direct export failed with unknown exception");
                    export_error = "unknown error";
                    export_successful = false;
                }

                // If export failed, do not create any fallback file
                if (!export_successful) {
                    LOG_DEBUG("G-code export failed, no fallback file will be created");
                    last_error = std::string("G-code export failed: ") + export_error;
                    return false;
                }

                // Check if export was successful
                if (export_successful && std::filesystem::exists(output_file, ec)) {
                    auto file_size = std::filesystem::file_size(output_file);
                    LOG_DEBUG(std::string("G-code file size: ") + std::to_string(file_size) + " bytes");

                    if (file_size > 1000) {  // Expect at least 1KB for a real G-code file
                        LOG_DEBUG("G-code export successful");
                        return true;
                    } else {
                        LOG_DEBUG(std::string("G-code file too small (") + std::to_string(file_size) + " bytes)");
                        last_error = "G-code file too small (" + std::to_string(file_size) + " bytes)";
                        return false;
                    }
                } else {
                    if (ec) {
                        LOG_ERROR("Filesystem error checking output file: " + ec.message());
                    }
                    LOG_DEBUG("G-code export failed");
                    last_error = std::string("G-code export failed") + (ec ? (": " + ec.message()) : "");
                    return false;
                }
            }
        } catch (const std::exception& e) {
            last_error = std::string("Slicing failed: ") + e.what();
            LOG_DEBUG(std::string("Exception caught: ") + e.what());
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
}

AddonCore::~AddonCore() = default;

AddonCore::OperationResult AddonCore::initialize(const std::string& resources_path) {
    LOG_DEBUG(std::string("AddonCore::initialize() resources_path='") + resources_path + "'");

    if (m_impl->initialized) {
        return OperationResult(true, "Already initialized");
    }

    if (m_impl->initializeSlic3r(resources_path)) {
        m_impl->initialized = true;
        LOG_DEBUG("AddonCore initialized successfully");
        return OperationResult(true, "AddonCore initialized successfully");
    } else {
        LOG_ERROR(std::string("Initialization failed: ") + m_impl->last_error);
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

    std::error_code ec;
    if (!std::filesystem::exists(filename, ec)) {
        if (ec) {
            LOG_ERROR("Filesystem error checking existence: " + ec.message());
        }
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

AddonCore::OperationResult AddonCore::loadVendor(const std::string& vendor_id) {
    if (!m_impl->initialized) return OperationResult(false, "AddonCore not initialized");
#if HAVE_LIBSLIC3R
    if (OrcaSlicerCli::config::load_vendor_from_resources(
            m_impl->resources_path, vendor_id,
            m_impl->preset_bundle, m_impl->app_config,
            m_impl->loaded_vendors, m_impl->last_error)) {
        return OperationResult(true, "Vendor loaded: " + vendor_id);
    }
    return OperationResult(false, "Failed to load vendor: " + vendor_id, m_impl->last_error);
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

AddonCore::OperationResult AddonCore::slice(const SlicingParams& params) {
    if (!m_impl->initialized) {
        return OperationResult(false, "AddonCore not initialized");
    }

    // Behavior flags: respect caller params
    m_impl->center_on_bed = params.center_on_bed;
    m_impl->auto_realign_if_needed = params.auto_realign_if_needed;

    LOG_DEBUG(std::string("slice(): input='") + params.input_file +
              "' plate_index=" + std::to_string(params.plate_index));

#if HAVE_LIBSLIC3R
    try {
        // Reset preset selections to defaults before each slice
        m_impl->preset_bundle.printers.select_preset(0);
        m_impl->preset_bundle.prints.select_preset(0);
        m_impl->preset_bundle.filaments.select_preset(0);
        m_impl->preset_bundle.filament_presets.clear();
        m_impl->preset_bundle.filament_presets.push_back(m_impl->preset_bundle.filaments.get_selected_preset_name());
        // Clear project preset names from previous slice
        m_impl->project_printer_preset.clear();
        m_impl->project_print_preset.clear();
        m_impl->project_filament_preset.clear();
        m_impl->plate_printer_model_id.clear();
        m_impl->plate_nozzle_variant.clear();
        m_impl->has_project_embedded_presets = false;
        m_impl->print_cfg_overrides.clear();
        m_impl->project_cfg_after_3mf.clear();
        m_impl->print_overrides_keys.clear();
        m_impl->project_overrides_keys.clear();
        m_impl->plate_data_src.clear();
        m_impl->saved_filament_colours.clear();
        m_impl->saved_change_filament_gcode.clear();
        m_impl->total_plates_count = 0;
        m_impl->detected_extruders = 0;
        m_impl->sanitize_erased_keys.clear();
    } catch (const std::exception &e) {
        LOG_WARNING(std::string("Failed to reset preset bundle: ") + e.what());
    }

    try {
        m_impl->config->apply(Slic3r::FullPrintConfig::defaults(), true);
        m_impl->config->erase("different_settings_to_system");
        m_impl->config->erase("printer_settings_id");
        m_impl->config->erase("print_settings_id");
        for (const auto& key : m_impl->config->keys()) {
            if (key.rfind("filament_settings_id", 0) == 0) {
                m_impl->config->erase(key);
            }
        }
    } catch (const std::exception &e) {
        LOG_WARNING(std::string("Failed to reset config to defaults: ") + e.what());
    }
#endif

    // Load model — 3MF customizations are stored and re-applied after the profile baseline.
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
    }

#if HAVE_LIBSLIC3R
    LOG_DEBUG("[SLICE] About to update_compatible and apply presets...");
    try {
        // Check if ALL transfer flags are disabled - if so, use pure defaults + custom_settings
        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);

        // Only apply preset config if we have any loaded presets (not just defaults)
        const bool has_printer = !m_impl->preset_bundle.printers.get_selected_preset_name().empty() &&
                                  m_impl->preset_bundle.printers.get_selected_preset_name() != "Default Printer";
        const bool has_filament = !m_impl->preset_bundle.filament_presets.empty() &&
                                   m_impl->preset_bundle.filament_presets[0] != "Default Filament";
        const bool has_process = !m_impl->preset_bundle.prints.get_selected_preset_name().empty() &&
                                  m_impl->preset_bundle.prints.get_selected_preset_name() != "Default Setting";

        if (has_printer || has_filament || has_process) {
            Slic3r::DynamicPrintConfig preset_config;
            OrcaSlicerCli::util::safe_build_config(m_impl->preset_bundle, preset_config);
            m_impl->config->apply(preset_config, true);
            LOG_DEBUG(std::string("Applied preset config -> printer='") +
                      m_impl->preset_bundle.printers.get_selected_preset_name() +
                      "', print='" + m_impl->preset_bundle.prints.get_selected_preset_name() +
                      "', filament='" + m_impl->preset_bundle.filaments.get_selected_preset_name() + "'");
        } else {
            LOG_DEBUG("No specific presets loaded - applying generic fallback config");
            OrcaSlicerCli::config::apply_generic_fallback_config(*m_impl->config, m_impl->resources_path);
        }

        // Dump key values after syncing working config with selected presets
        try { if (const auto* o = m_impl->config->optptr("sparse_infill_density")) LOG_DEBUG(std::string("synced_config[sparse_infill_density]=") + o->serialize()); } catch (...) {}
        try { if (const auto* o = m_impl->config->optptr("top_shell_layers")) LOG_DEBUG(std::string("synced_config[top_shell_layers]=") + o->serialize()); } catch (...) {}

    } catch (const std::exception &e) {
        LOG_WARNING(std::string("Failed to refresh working config from selected presets: ") + e.what());
    }

    // Apply on-the-fly profile settings on top of fallback/presets.
    // Priority: fallback < profile_settings < 3MF < custom_settings (options).
    if (!params.profile_settings.empty()) {
        LOG_DEBUG(std::string("Applying profile_settings (") + std::to_string(params.profile_settings.size()) + " keys) as on-the-fly profile baseline");
        for (const auto& kv : params.profile_settings) {
            try {
                setConfigOption(kv.first, kv.second);
            } catch (const std::exception& e) {
                LOG_DEBUG(std::string("profile_settings: skipping '") + kv.first + "': " + e.what());
            }
        }
    }
#endif

#if HAVE_LIBSLIC3R
    // Re-apply 3MF print-level overrides on top of profile (3MF wins over profile)
    OrcaSlicerCli::slice::reapply_print_overrides(*m_impl->config, m_impl->print_cfg_overrides, m_impl->print_overrides_keys);
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
    // Re-apply 3MF project parameter overrides (but NOT for keys set by API custom_settings)
    // Priority order: Profiles -> 3MF customizations -> API custom_settings (highest)
    {
        std::vector<std::string> keys_to_apply = m_impl->project_overrides_keys;
        if (keys_to_apply.empty()) keys_to_apply = m_impl->project_cfg_after_3mf.keys();
        OrcaSlicerCli::slice::reapply_project_overrides_excluding(
            *m_impl->config,
            m_impl->project_cfg_after_3mf,
            keys_to_apply,
            __used_override_keys);
    }

    // Ensure 3MF print-level (dirty) overrides take precedence over project-level overrides
    OrcaSlicerCli::slice::reapply_print_overrides_excluding(
        *m_impl->config,
        m_impl->print_cfg_overrides,
        m_impl->print_overrides_keys,
        __used_override_keys);

    // Enable single_extruder_multi_material and prime tower if multi-material detected
    if (m_impl->detected_extruders > 1) {
        LOG_DEBUG(std::string("Detected multi-material model (") + std::to_string(m_impl->detected_extruders) + " colors in 3MF)");
        if (std::find(__used_override_keys.begin(), __used_override_keys.end(), "single_extruder_multi_material") == __used_override_keys.end()) {
            m_impl->config->set_key_value("single_extruder_multi_material", new Slic3r::ConfigOptionBool(true));
        }

        // Only force-enable the prime tower when no layer explicitly chose a value.
        // A 3MF saved with the tower disabled — or an API profile/override disabling it —
        // must be respected; the effective config value at this point already reflects
        // the priority order profile_settings < 3MF < custom_settings. Multicolor
        // without a prime tower is valid (flush happens via filament-change G-code).
        const bool prime_tower_explicit =
            params.profile_settings.count("enable_prime_tower") > 0 ||
            params.custom_settings.count("enable_prime_tower") > 0 ||
            std::find(m_impl->print_overrides_keys.begin(), m_impl->print_overrides_keys.end(),
                      "enable_prime_tower") != m_impl->print_overrides_keys.end() ||
            m_impl->project_cfg_after_3mf.has("enable_prime_tower");
        if (!prime_tower_explicit) {
            m_impl->config->set_key_value("enable_prime_tower", new Slic3r::ConfigOptionBool(true));
        } else {
            bool eff = false;
            try { eff = m_impl->config->opt_bool("enable_prime_tower"); } catch (...) {}
            LOG_DEBUG(std::string("enable_prime_tower explicitly set by 3MF/profile/override, keeping value=") + (eff ? "1" : "0"));
        }

        // Restore 3MF colors (unless user explicitly overrode)
        auto* fil_colour = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
        if (!m_impl->saved_filament_colours.empty() && fil_colour &&
            std::find(__used_override_keys.begin(), __used_override_keys.end(), "filament_colour") == __used_override_keys.end()) {
            LOG_DEBUG("Restoring 3MF filament colors");
            fil_colour->values = m_impl->saved_filament_colours;
        }

        // Expand filament arrays to match detected_extruders (will be trimmed later in performSlicing)
        auto* fil_diameter = m_impl->config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
        auto* fil_type = m_impl->config->opt<Slic3r::ConfigOptionStrings>("filament_type", false);
        if (fil_diameter && fil_diameter->values.size() < m_impl->detected_extruders) {
            while (fil_diameter->values.size() < m_impl->detected_extruders)
                fil_diameter->values.push_back(fil_diameter->values.empty() ? 1.75 : fil_diameter->values.back());
        }
        if (fil_type && fil_type->values.size() < m_impl->detected_extruders) {
            while (fil_type->values.size() < m_impl->detected_extruders)
                fil_type->values.push_back(fil_type->values.empty() ? "PLA" : fil_type->values.back());
        }

        // Restore change_filament_gcode for Bambu AMS multi-color printing
        // ONLY if not overridden via config JSON (custom_settings)
        bool gcode_overridden = std::find(__used_override_keys.begin(), __used_override_keys.end(),
                                          "change_filament_gcode") != __used_override_keys.end();
        if (!m_impl->saved_change_filament_gcode.empty() && !gcode_overridden) {
            LOG_DEBUG(std::string("Restoring 3MF change_filament_gcode (") + std::to_string(m_impl->saved_change_filament_gcode.size()) + " chars)");
            m_impl->config->set_key_value("change_filament_gcode",
                new Slic3r::ConfigOptionString(m_impl->saved_change_filament_gcode));
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

        LOG_DEBUG(std::string("Synchronizing flush_volumes_matrix: filament_count=") + std::to_string(filament_count) +
                  ", heads_count=" + std::to_string(heads_count) +
                  ", expected_matrix_size=" + std::to_string(expected_matrix_size));

        if (flush_matrix) {
            size_t current_size = flush_matrix->values.size();
            if (current_size != expected_matrix_size) {
                LOG_DEBUG(std::string("Resizing flush_volumes_matrix from ") + std::to_string(current_size) + " to " + std::to_string(expected_matrix_size));
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
        size_t expected_vector_size = 2 * filament_count;
        if (flush_vector) {
            size_t current_size = flush_vector->values.size();
            if (current_size != expected_vector_size) {
                LOG_DEBUG(std::string("Resizing flush_volumes_vector from ") + std::to_string(current_size) + " to " + std::to_string(expected_vector_size));
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

        // Check multi-material settings before slicing
        LOG_DEBUG("[TRACE 30] BEFORE performSlicing() - checking multi-material config:");
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

    // Merge sanitize-erased keys into the ignored list for caller visibility
    for (const auto& key : m_impl->sanitize_erased_keys) {
        __ignored_override_keys.push_back(key + " (type mismatch, using default)");
    }

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

    std::error_code ec;
    if (!std::filesystem::exists(config_file, ec)) {
        if (ec) {
            LOG_ERROR("Filesystem error checking existence: " + ec.message());
        }
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
            LOG_DEBUG(std::string("Normalized ") + key + " from '" + value + "' to '" + normalized_value + "'");
        }

        // Use set_deserialize to let libslic3r parse and validate the value
        Slic3r::ConfigSubstitutionContext ctx{Slic3r::ForwardCompatibilitySubstitutionRule::Enable};
        m_impl->config->set_deserialize(key, normalized_value, ctx, /*append=*/false);

        static const std::unordered_map<std::string, std::pair<double, double>> SAFETY_BOUNDS = {
            {"machine_max_acceleration_x", {1.0, 50000.0}},
            {"machine_max_acceleration_y", {1.0, 50000.0}},
            {"machine_max_acceleration_z", {1.0, 50000.0}},
            {"machine_max_acceleration_extruding", {1.0, 50000.0}},
            {"machine_max_acceleration_retracting", {1.0, 50000.0}},
            {"default_acceleration", {1.0, 50000.0}},
            {"machine_max_feedrate_x", {1.0, 1000.0}},
            {"machine_max_feedrate_y", {1.0, 1000.0}},
            {"machine_max_feedrate_z", {1.0, 1000.0}},
            {"machine_max_jerk_x", {0.0, 60.0}},
            {"machine_max_jerk_y", {0.0, 60.0}},
            {"machine_max_jerk_z", {0.0, 60.0}},
            {"travel_speed", {1.0, 1000.0}},
        };

        auto bounds_it = SAFETY_BOUNDS.find(key);
        if (bounds_it != SAFETY_BOUNDS.end()) {
            auto* opt = m_impl->config->option<Slic3r::ConfigOptionFloat>(key);
            if (opt && (opt->value < bounds_it->second.first || opt->value > bounds_it->second.second)) {
                m_impl->config->erase(key);
                return OperationResult(false, "Value out of safety bounds for " + key);
            }
        }

        LOG_DEBUG(std::string("Override applied: ") + key + "=" + normalized_value);
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

    std::error_code ec;
    if (!std::filesystem::exists(filename, ec)) {
        if (ec) {
            LOG_ERROR("Filesystem error checking existence: " + ec.message());
        }
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



