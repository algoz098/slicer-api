#include "CliCore.hpp"

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


#if !HAVE_LIBSLIC3R
#error "libslic3r is required. Placeholders are not allowed."
#endif


#if HAVE_LIBSLIC3R
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

#include "libslic3r/Preset.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/ProjectTask.hpp"

#endif

#if HAVE_LIBSLIC3R
namespace {
    static std::string bed_temp_key_for(Slic3r::BedType type, bool first_layer) {
        if (first_layer) {
            switch (type) {
                case Slic3r::btSuperTack: return "supertack_plate_temp_initial_layer";
                case Slic3r::btPC:        return "cool_plate_temp_initial_layer";
                case Slic3r::btPCT:       return "textured_cool_plate_temp_initial_layer";
                case Slic3r::btEP:        return "eng_plate_temp_initial_layer";
                case Slic3r::btPEI:       return "hot_plate_temp_initial_layer";
                case Slic3r::btPTE:       return "textured_plate_temp_initial_layer";
                default: return std::string();
            }
        } else {
            switch (type) {
                case Slic3r::btSuperTack: return "supertack_plate_temp";
                case Slic3r::btPC:        return "cool_plate_temp";
                case Slic3r::btPCT:       return "textured_cool_plate_temp";
                case Slic3r::btEP:        return "eng_plate_temp";
                case Slic3r::btPEI:       return "hot_plate_temp";
                case Slic3r::btPTE:       return "textured_plate_temp";
                default: return std::string();
            }
        }
    }
}
#endif


// Debug logging helper: duplicates to stdout and to file if ORCACLI_DEBUG_LOG_PATH is set
namespace {
    static void dbg_log(const std::string& s) {
        static std::ofstream __orcacli_dbg_file;
        static bool __orcacli_dbg_inited = false;
        if (!__orcacli_dbg_inited) {
            const char* p = std::getenv("ORCACLI_DEBUG_LOG_PATH");
            if (p && *p) {
                __orcacli_dbg_file.open(p, std::ios::app);
            }
            __orcacli_dbg_inited = true;
        }
        std::cout << s << std::endl;
        std::cout.flush();
        if (__orcacli_dbg_file.is_open()) {
            __orcacli_dbg_file << s << std::endl;
            __orcacli_dbg_file.flush();
        }
    }
}


namespace OrcaSlicerCli {



/**
 * @brief Private implementation class for CliCore
 */
class CliCore::Impl {
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
    ~Impl() = default; // Cleanup is performed explicitly via CliCore::shutdown()


    #if HAVE_LIBSLIC3R
        // Compute and set plate_origin from model instances (assembly offsets) so that G-code is plate-local.
        bool compute_and_set_plate_origin_from_model_instances()
        {
            if (!model || !print) return false;
            try {
                Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                if (bed_pts.empty()) return false;
                long minx = std::numeric_limits<long>::max();
                long maxx = std::numeric_limits<long>::min();
                long miny = std::numeric_limits<long>::max();
                long maxy = std::numeric_limits<long>::min();
                for (const auto &p : bed_pts) { if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y(); }
                const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
                const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
                if (!(bed_w_mm > 0.0 && bed_d_mm > 0.0)) return false;
                constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
                const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);

                bool origin_found = false;
                double origin_x = 0.0, origin_y = 0.0;
                for (auto *obj : model->objects) {
                    for (auto *inst : obj->instances) {
                        Slic3r::Vec3d aoff = inst->get_offset_to_assembly();
                        const double col = std::round(aoff(0) / stride_x);
                        const double row = std::round(-aoff(1) / stride_y);
                        origin_x = col * stride_x;
                        origin_y = -row * stride_y; // GUI uses negative Y per row
                        origin_found = true;
                        break;
                    }
                    if (origin_found) break;
                }
                if (!origin_found) return false;

                print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
                std::cout << "DEBUG: plate_origin (from instance assembly offsets) => origin=(" << origin_x << "," << origin_y
                          << ") stride=(" << stride_x << "," << stride_y << ")" << std::endl;
                return true;
            } catch (const std::exception &e) {
                std::cout << "WARN: compute_and_set_plate_origin_from_model_instances failed: " << e.what() << std::endl;
                return false;
            }
        }

        // Center currently loaded plate content onto the bed center by adjusting plate_origin
        bool center_plate_origin_to_bed_center()
        {
            if (!model || !print || !config) return false;
            try {
                // Determine bed center (in mm) from printable area
                Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                if (bed_pts.empty()) return false;
                long minx = std::numeric_limits<long>::max();
                long maxx = std::numeric_limits<long>::min();
                long miny = std::numeric_limits<long>::max();
                long maxy = std::numeric_limits<long>::min();
                for (const auto &p : bed_pts) {
                    if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x();
                    if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y();
                }
                const double bed_cx_mm = Slic3r::unscale<double>(minx + (maxx - minx) / 2);
                const double bed_cy_mm = Slic3r::unscale<double>(miny + (maxy - miny) / 2);

                // Compute world-space exact bounding box of all objects
                Slic3r::BoundingBoxf3 all_bb;
                bool all_bb_init = false;
                for (size_t oi = 0; oi < model->objects.size(); ++oi) {
                    const Slic3r::ModelObject *obj = model->objects[oi];
                    if (!obj) continue;
                    const auto &bb = obj->bounding_box_exact();
                    if (bb.defined) {
                        if (!all_bb_init) { all_bb = bb; all_bb_init = true; }
                        else { all_bb.merge(bb); }
                    }
                }
                if (!all_bb_init) return false;
                const double cx = 0.5 * (all_bb.min.x() + all_bb.max.x());
                const double cy = 0.5 * (all_bb.min.y() + all_bb.max.y());

                const double origin_x = cx - bed_cx_mm;
                const double origin_y = cy - bed_cy_mm;
                print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
                std::cout << "DEBUG: center_on_bed => plate_origin set to (" << origin_x << "," << origin_y << ") using model center=(" << cx << "," << cy << ") and bed center=(" << bed_cx_mm << "," << bed_cy_mm << ")" << std::endl;
                return true;
            } catch (const std::exception &e) {
                std::cout << "WARN: center_plate_origin_to_bed_center failed: " << e.what() << std::endl;
                return false;
            }
        }

        // Shift instances so their center aligns with bed center (used for non-BBL vendors when center_on_bed=true)
        bool center_instances_on_bed_center()
        {
            if (!model || !config) return false;
            try {
                // Determine bed center (in mm) from printable area
                Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                if (bed_pts.empty()) return false;
                long minx = std::numeric_limits<long>::max();
                long maxx = std::numeric_limits<long>::min();
                long miny = std::numeric_limits<long>::max();
                long maxy = std::numeric_limits<long>::min();
                for (const auto &p : bed_pts) {
                    if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x();
                    if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y();
                }
                const double bed_cx_mm = Slic3r::unscale<double>(minx + (maxx - minx) / 2);
                const double bed_cy_mm = Slic3r::unscale<double>(miny + (maxy - miny) / 2);

                // Compute world-space exact bounding box of all objects
                Slic3r::BoundingBoxf3 all_bb;
                bool all_bb_init = false;
                for (size_t oi = 0; oi < model->objects.size(); ++oi) {
                    const Slic3r::ModelObject *obj = model->objects[oi];
                    if (!obj) continue;
                    const auto &bb = obj->bounding_box_exact();
                    if (bb.defined) {
                        if (!all_bb_init) { all_bb = bb; all_bb_init = true; }
                        else { all_bb.merge(bb); }
                    }
                }
                if (!all_bb_init) return false;
                const double cx = 0.5 * (all_bb.min.x() + all_bb.max.x());
                const double cy = 0.5 * (all_bb.min.y() + all_bb.max.y());

                const double dx = bed_cx_mm - cx;
                const double dy = bed_cy_mm - cy;

                size_t adjusted = 0;
                for (auto *obj : model->objects) {
                    if (!obj) continue;
                    for (auto *inst : obj->instances) {
                        auto tf = inst->get_transformation();
                        Slic3r::Vec3d toff = tf.get_offset();
                        toff(0) += dx; toff(1) += dy;
                        tf.set_offset(toff);
                        inst->set_transformation(tf);
                        ++adjusted;
                    }
                }

                std::cout << "DEBUG: center_on_bed (instances) => shifted " << adjusted << " instances by (" << dx << "," << dy << ") bed_center=(" << bed_cx_mm << "," << bed_cy_mm << ") model_center=(" << cx << "," << cy << ")" << std::endl;
                return adjusted > 0;
            } catch (const std::exception &e) {
                std::cout << "WARN: center_instances_on_bed_center failed: " << e.what() << std::endl;
                return false;
            }
        }


        // Normalize model instances into plate-local coordinates by removing the logical grid stride.
        bool normalize_model_instances_to_plate_local()
        {
            if (!model) return false;
            try {
                Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
                if (bed_pts.empty()) return false;
                long minx = std::numeric_limits<long>::max();
                long maxx = std::numeric_limits<long>::min();
                long miny = std::numeric_limits<long>::max();
                long maxy = std::numeric_limits<long>::min();
                for (const auto &p : bed_pts) { if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y(); }
                const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
                const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
                if (!(bed_w_mm > 0.0 && bed_d_mm > 0.0)) return false;
                constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
                const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
                const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);

                bool origin_found = false;
                double asm_origin_x = 0.0, asm_origin_y = 0.0;
                for (auto *obj : model->objects) {
                    for (auto *inst : obj->instances) {
                        Slic3r::Vec3d aoff = inst->get_offset_to_assembly();
                        const double col = std::round(aoff(0) / stride_x);
                        const double row = std::round(-aoff(1) / stride_y);
                        asm_origin_x = col * stride_x;
                        asm_origin_y = -row * stride_y; // GUI uses negative Y per row
                        origin_found = true;
                        break;
                    }
                    if (origin_found) break;
                }
                if (!origin_found) return false;

                size_t adjusted = 0;
                for (auto *obj : model->objects) {
                    for (auto *inst : obj->instances) {
                        Slic3r::Vec3d toff = inst->get_transformation().get_offset();
                        toff(0) -= asm_origin_x; toff(1) -= asm_origin_y;
                        inst->set_offset(toff);
                        Slic3r::Vec3d aoff = inst->get_offset_to_assembly();
                        aoff(0) -= asm_origin_x; aoff(1) -= asm_origin_y;
                        inst->set_offset_to_assembly(aoff);
                        ++adjusted;
                    }
                }
                std::cout << "DEBUG: normalized instances to plate-local FROM assembly: asm_origin=(" << asm_origin_x << "," << asm_origin_y
                          << ") stride=(" << stride_x << "," << stride_y << ") adjusted_instances=" << adjusted << std::endl;
                return adjusted > 0;
            } catch (...) { return false; }
        }
    #endif

    void cleanup() {
#if HAVE_LIBSLIC3R
        try {
            // Destroy in safe order to avoid segfaults due to dangling references in libslic3r
            // 1) Ensure Print is destroyed before Model
            if (print) {
                print.reset();
            }
            // 2) Clear model objects and destroy Model
            if (model) {
                model->clear_objects();
                model.reset();
            }
            // 3) Release configuration last
            if (config) {
                config.reset();
            }
        } catch (const std::exception& e) {
            // Log error but don't throw in destructor
            std::cerr << "Warning: Error during cleanup: " << e.what() << std::endl;
        }
#endif
    }

    // Helper function to load JSON configuration file
    bool loadJsonConfig(const std::string& file_path, Slic3r::DynamicPrintConfig& config) {
#if HAVE_LIBSLIC3R
        try {
            if (!std::filesystem::exists(file_path)) {
                last_error = "Profile file not found: " + file_path;
                return false;
            }

            // Load configuration from JSON file
            Slic3r::ConfigSubstitutions substitutions = config.load(file_path, Slic3r::ForwardCompatibilitySubstitutionRule::Enable);

            std::cout << "DEBUG: Loaded profile from " << file_path << " with " << substitutions.size() << " substitutions" << std::endl;
            return true;
        } catch (const std::exception& e) {
            last_error = "Failed to load profile from " + file_path + ": " + e.what();
            return false;
        }
#else
        last_error = "libslic3r not available";
        return false;
#endif
    }

    // Helper function to find profile file by name
    std::string findProfileFile(const std::string& profile_name, const std::string& profile_type) {
        std::string profiles_dir = resources_path + "/profiles/BBL/" + profile_type;

        // Try exact match first


        std::string exact_path = profiles_dir + "/" + profile_name + ".json";
        if (std::filesystem::exists(exact_path)) {
            return exact_path;
        }

        // Try to find by searching in directory
        try {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(profiles_dir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    std::string filename = entry.path().stem().string();
                    if (filename == profile_name) {
                        return entry.path().string();
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error searching for profile: " << e.what() << std::endl;
        }

        return "";
    }

    bool initializeSlic3r(const std::string& resources_path) {
        try {
            this->resources_path = resources_path;

            // Debug: validate resources path visibility
            try {
                namespace fs = std::filesystem;
                bool root_ok = fs::exists(fs::path(resources_path));
                bool bbl_ok  = fs::exists(fs::path(resources_path) / "profiles" / "BBL.json");
                std::cout << "DEBUG: initializeSlic3r: resources_path='" << resources_path
                          << "' root_exists=" << (root_ok?1:0)
                          << " BBL.json_exists=" << (bbl_ok?1:0) << std::endl;
            } catch (...) {}
            // Extra DEBUG: dump env relevant to strict/eager loading
            try {
                auto getenv_or = [](const char* n){ const char* v = std::getenv(n); return v? v : "(unset)"; };
                std::cout << "DEBUG: initializeSlic3r env: ORCACLI_STRICT_VENDORS_ONLY=" << getenv_or("ORCACLI_STRICT_VENDORS_ONLY") << std::endl;
                std::cout << "DEBUG: initializeSlic3r env: ORCACLI_DISABLE_AUTOLOAD=" << getenv_or("ORCACLI_DISABLE_AUTOLOAD") << std::endl;
                std::cout << "DEBUG: initializeSlic3r env: ORCACLI_EAGER_LOAD_PRESETS=" << getenv_or("ORCACLI_EAGER_LOAD_PRESETS") << std::endl;
                std::cout << "DEBUG: initializeSlic3r env: ORCACLI_VENDORS=" << getenv_or("ORCACLI_VENDORS") << std::endl;
            } catch (...) {}


#if HAVE_LIBSLIC3R
            // Force deterministic numeric formatting independent of OS locale
            // This ensures CONFIG/HEADER numbers (e.g., 4 vs 4,0) use C locale
            try { std::setlocale(LC_NUMERIC, "C"); } catch (...) {}

            // Initialize libslic3r search paths first
            Slic3r::set_resources_dir(resources_path);
            namespace fs = std::filesystem;
            fs::path cwd = fs::current_path();
            fs::path data_dir = cwd / ".orcaslicercli";
            if (!fs::exists(data_dir)) fs::create_directories(data_dir);
            Slic3r::set_data_dir(data_dir.string());
            std::cout << "DEBUG: Set data_dir to '" << data_dir.string() << "'" << std::endl; // TEST TRACE
            // Ensure a writable temporary directory for libslic3r (needed by 3MF loader backup/extract paths)
            try {
                fs::path tmp_dir = data_dir / "tmp";
                if (!fs::exists(tmp_dir)) fs::create_directories(tmp_dir);
                Slic3r::set_temporary_dir(tmp_dir.string());
                std::cout << "DEBUG: Set temporary_dir to '" << tmp_dir.string() << "'" << std::endl;
            } catch (const std::exception &e) {



                std::cerr << "WARN: Failed to prepare temporary_dir under data_dir: " << e.what() << std::endl;
            }
            // var/local/sys_shapes/custom_gcodes are runtime/read paths that default off data/resources
            // Keep them unset to let libslic3r resolve internally unless the directories exist.
            if (fs::exists(fs::path(resources_path) / "i18n"))
                Slic3r::set_local_dir((fs::path(resources_path) / "i18n").string());
            if (fs::exists(fs::path(resources_path) / "shapes"))
                Slic3r::set_sys_shapes_dir((fs::path(resources_path) / "shapes").string());
            if (fs::exists(fs::path(resources_path) / "custom_gcodes"))
                Slic3r::set_custom_gcodes_dir((fs::path(resources_path) / "custom_gcodes").string());

            {
                unsigned int lvl = 4;
                if (const char* lv = std::getenv("ORCACLI_LOGLEVEL")) {
                    try { lvl = (unsigned int)std::stoi(lv); } catch (...) {}
                }
                Slic3r::set_logging_level(lvl);
            }


            // Strict mode to disable any seeding or env-driven autoloads
            bool strict_no_autoload = false;
            if (const char* s = std::getenv("ORCACLI_STRICT_VENDORS_ONLY")) {
                if (s[0]=='1' || s[0]=='T' || s[0]=='t' || s[0]=='Y' || s[0]=='y') strict_no_autoload = true;
            }
            if (const char* s2 = std::getenv("ORCACLI_DISABLE_AUTOLOAD")) {
                if (s2[0]=='1' || s2[0]=='T' || s2[0]=='t' || s2[0]=='Y' || s2[0]=='y') strict_no_autoload = true;
            }
            std::cout << "DEBUG: [TEST TRACE] strict_no_autoload=" << (strict_no_autoload?1:0) << std::endl;

                // Enforce API-only control: disable any env-driven autoloads unconditionally
                strict_no_autoload = true;
                std::cout << "DEBUG: [TEST TRACE] overriding strict_no_autoload=1 (API-only control)" << std::endl;


            if (strict_no_autoload) {
                std::cout << "DEBUG: Strict no-autoload mode enabled: skipping seeding and env-driven vendor/preset loads" << std::endl;
                try {
                    namespace fs = std::filesystem;
                    fs::path sys_dir = fs::path(Slic3r::data_dir()) / "system";
                    if (fs::exists(sys_dir)) {
                        std::cout << "DEBUG: Strict mode: clearing system presets directory '" << sys_dir.string() << "'" << std::endl;
                        std::error_code ec;
                        fs::remove_all(sys_dir, ec);
                        (void)ec;
                    }
                    fs::create_directories(sys_dir);
                } catch (...) { /* best-effort cleanup */ }
            }

            // Seed PresetBundle system directory from resources if explicitly enabled (truthy: 1/true/yes)
            if (!strict_no_autoload) {
                if (const char* seed = std::getenv("ORCACLI_SEED_ALL"); seed && (seed[0]=='1' || seed[0]=='T' || seed[0]=='t' || seed[0]=='Y' || seed[0]=='y')) { try {
                    preset_bundle.setup_directories();
                    namespace fs = std::filesystem;
                    fs::path profiles_dir = fs::path(resources_path) / "profiles";
                    fs::path sys_dir      = fs::path(Slic3r::data_dir()) / "system";
                    if (!fs::exists(sys_dir)) fs::create_directories(sys_dir);
                    // Copy vendor list JSONs (e.g., BBL.json, Prusa.json, etc.) into data_dir/system where libslic3r expects them,
                    // AND also copy the corresponding vendor subdirectories (machine/process/filament) so LoadSystem can paste presets.
                    size_t copied_jsons = 0;
                    size_t copied_dirs  = 0;
                    if (fs::exists(profiles_dir) && fs::is_directory(profiles_dir)) {
                        // 1) Copy root vendor index JSONs
                        for (const auto &entry : fs::directory_iterator(profiles_dir)) {
                            if (!entry.is_regular_file()) continue;
                            if (entry.path().extension() != ".json") continue;
                            const std::string fname = entry.path().filename().string();
                            if (fname == "OrcaFilamentLibrary.json") continue; // avoid known ASan issue in this environment
                            try {
                                fs::path dst = sys_dir / fname;
                                if (!fs::exists(dst)) {
                                    fs::copy_file(entry.path(), dst, fs::copy_options::overwrite_existing);
                                    ++copied_jsons;
                                }
                            } catch (...) { /* ignore individual copy errors */ }
                        }
                        // 2) Copy vendor folders recursively (BBL/, Prusa/, etc.) so subfiles referenced by the index JSONs exist under data_dir/system
                        for (const auto &entry : fs::directory_iterator(profiles_dir)) {
                            if (!entry.is_directory()) continue;
                            const std::string dname = entry.path().filename().string();
                            if (dname == "OrcaFilamentLibrary") continue; // skip library; handled specially by libslic3r
                            try {
                                fs::path dst_dir = sys_dir / dname;
                                // Copy recursively and overwrite to keep in sync with resources
                                fs::copy(entry.path(), dst_dir, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
                                ++copied_dirs;
                            } catch (...) { /* ignore individual copy errors */ }
                        }
                    }
                    std::cout << "DEBUG: Seeded vendor profiles into '" << sys_dir.string() << "' (jsons=" << copied_jsons << ", dirs=" << copied_dirs << ")" << std::endl;
                    // List root vendor JSONs to verify presence (e.g., BBL.json)
                    try {
                        std::vector<std::string> root_jsons;
                        for (const auto &e : fs::directory_iterator(sys_dir)) {
                            if (e.is_regular_file() && e.path().extension() == ".json") {
                                root_jsons.push_back(e.path().filename().string());
                            }
                        }
                        std::sort(root_jsons.begin(), root_jsons.end());
                        bool has_bbl = std::find(root_jsons.begin(), root_jsons.end(), std::string("BBL.json")) != root_jsons.end();
                        std::cout << "DEBUG: system root JSONs (" << root_jsons.size() << ") has BBL.json=" << (has_bbl?"yes":"no") << std::endl;
                        size_t show = std::min<size_t>(root_jsons.size(), 10);
                        for (size_t i=0;i<show;i++) std::cout << "  - " << root_jsons[i] << std::endl;
                    } catch (...) {}

                    // Optional: validation mode to focus vendor loading diagnostics
                    if (const char* v = std::getenv("ORCACLI_VALIDATE_VENDOR")) {
                        try {
                            preset_bundle.set_is_validation_mode(true);
                            preset_bundle.set_vendor_to_validate(std::string(v));
                            std::cout << "DEBUG: Validation mode enabled for vendor '" << v << "'" << std::endl;
                        } catch (...) {}
                    }
                    // Intentionally do NOT auto-load any vendors here.
                    // We only stage files into the system dir; actual vendor loading must be explicit
                    // (via ORCACLI_VENDORS, ORCACLI_EAGER_LOAD_PRESETS, or API calls like loadVendor()).
                    (void)sys_dir; // suppress unused warning if compiled with reduced logs
                } catch (...) { /* ignore seeding errors */ }
                }
            }

            // Initialize AppConfig and load defaults (and existing file if any)
            app_config.reset();


            // Lazy load only vendors requested via env ORCACLI_VENDORS (comma-separated)
            std::cout << "DEBUG: [TEST TRACE] entering env-driven vendor autoload section, strict_no_autoload=" << (strict_no_autoload?1:0) << std::endl;

            if (!strict_no_autoload) {
                if (const char* ev = std::getenv("ORCACLI_VENDORS")) {
                    try {
                        std::string s(ev);
                        std::vector<std::string> vendors; vendors.reserve(4);
                        std::string cur;
                        for (char c : s) {
                            if (c == ',') { if (!cur.empty()) { vendors.push_back(cur); cur.clear(); } }
                            else if (!std::isspace((unsigned char)c)) { cur.push_back(c); }
                        }
                        if (!cur.empty()) vendors.push_back(cur);
                        namespace fs = std::filesystem;
                        fs::path res_profiles = fs::path(resources_path) / "profiles";
                        for (const auto &v : vendors) {
                            try {
                                preset_bundle.load_vendor_configs_from_json(res_profiles.string(), v, Slic3r::PresetBundle::LoadSystem, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                                loaded_vendors.insert(v);
                            } catch (...) {}
                        }
                        // Materialize installed printers for selected vendors
                        try { preset_bundle.load_installed_printers(app_config); } catch (...) {}
                    } catch (...) {}
                }
            }

            // Load system and user presets using PresetBundle's official API (handles vendor order and merges internally).
            std::cout << "DEBUG: [TEST TRACE] considering eager load: strict_no_autoload=" << (strict_no_autoload?1:0) << std::endl;
            if (!strict_no_autoload) {
                if (const char* eager = std::getenv("ORCACLI_EAGER_LOAD_PRESETS")) {
                    std::cout << "DEBUG: [TEST TRACE] ORCACLI_EAGER_LOAD_PRESETS='" << eager << "'" << std::endl;
                    if (eager[0]=='1' || eager[0]=='T' || eager[0]=='t' || eager[0]=='Y' || eager[0]=='y') {
                        std::cout << "DEBUG: [TEST TRACE] calling preset_bundle.load_presets(...)" << std::endl;
                        preset_bundle.load_presets(app_config, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                    }
                } else {
                    std::cout << "DEBUG: [TEST TRACE] ORCACLI_EAGER_LOAD_PRESETS not set" << std::endl;
                }
            } else {
                std::cout << "DEBUG: [TEST TRACE] Skipping eager load due to strict_no_autoload=1" << std::endl;
            }
            try {
                size_t total = preset_bundle.printers.size();
                size_t visible = 0; for (const auto &p : preset_bundle.printers) if (p.is_visible) ++visible;
                std::cout << "DEBUG: After load_presets: printers total=" << total << " visible=" << visible << std::endl;
            } catch (...) {}

                // Defer model/printer materialization until vendors are explicitly loaded or EAGER preset load is requested
                if (!loaded_vendors.empty()) {
                    try {
                        preset_bundle.load_installed_printers(app_config);
                        size_t totalp = preset_bundle.printers.size();
                        size_t visiblep = 0; for (const auto &p : preset_bundle.printers) if (p.is_visible) ++visiblep;
                        std::cout << "DEBUG: After guarded load_installed_printers: printers total=" << totalp << " visible=" << visiblep << std::endl;
                    } catch (...) {}
                }



            // Note: We rely on load_presets() above, which internally calls
            // load_system_presets_from_json(LoadSystem) and populates printers as well.
            // The previous call to load_system_models_from_json() used LoadVendorOnly
            // under the hood and did not paste printer configs; removing it avoids
            // confusion and duplicate state.

            // Skip implicit materialization; do it only after explicit vendor/profile loads.
            if (!loaded_vendors.empty()) {
                try {
                    preset_bundle.load_installed_printers(app_config);
                    size_t total = preset_bundle.printers.size();
                    size_t visible = 0; for (const auto &p : preset_bundle.printers) if (p.is_visible) ++visible;
                    std::cout << "DEBUG: After guarded load_installed_printers (2): printers total=" << total << " visible=" << visible << std::endl;
                } catch (...) {}
            }

            // Always ensure base objects exist; fill config from presets only when vendors are loaded
            config = std::make_unique<Slic3r::DynamicPrintConfig>();
            model = std::make_unique<Slic3r::Model>();
            // NOTE: Print object will be created FRESH for each slicing operation (GUI parity)
            // The GUI creates a new Print for each plate, we'll create a new one for each slicing
            // This prevents dangling m_shared_object pointers when print->apply() deletes old PrintObjects
            print = nullptr;
            std::cout << "DEBUG: Print object will be created fresh for each slicing operation (GUI parity)" << std::endl;
            if (!loaded_vendors.empty()) {
                *config = preset_bundle.full_config_secure();
                std::cout << "DEBUG: Materialized config/model with persistent Print" << std::endl;
            } else {
                std::cout << "DEBUG: Created empty config and base model (no vendors loaded yet)" << std::endl;
            }
#endif

            return true;
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
                // Use TriangleMesh approach for more robust loading
                Slic3r::TriangleMesh mesh;
                if (!mesh.ReadSTLFile(filename.c_str(), true)) {
                    last_error = "Failed to read STL file: " + filename;
                    return false;
                }

                if (mesh.empty()) {
                    last_error = "STL file is empty or invalid: " + filename;
                    return false;
                }

                // Extract filename for object name (keep extension to match reference G-code)
                std::filesystem::path file_path(filename);
                std::string object_name = file_path.filename().string();

                // Add object to model
                model->add_object(object_name.c_str(), filename.c_str(), std::move(mesh));
            } else if (extension == ".3mf") {
                // Load .3mf project and select the requested plate (0-based id in Impl)
                Slic3r::ConfigSubstitutionContext config_substitutions{Slic3r::ForwardCompatibilitySubstitutionRule::Enable};
                std::vector<Slic3r::Preset*> project_presets;
                bool is_bbl_3mf = false;
                Slic3r::Semver file_version;

                // Read model and project config from 3mf (includes per-plate content)
                Slic3r::Model loaded = Slic3r::Model::read_from_file(
                    filename,
                    config.get(),
                    &config_substitutions,
                    Slic3r::LoadStrategy::LoadModel | Slic3r::LoadStrategy::LoadConfig,
                    &plate_data_src,  // Use member variable
                    &project_presets,
                    &is_bbl_3mf,
                    &file_version,
                    nullptr,
                    nullptr,
                    nullptr,
                    plate_id // 0-based
                );
                std::cout << "DEBUG: read_from_file: project_presets=" << project_presets.size()
                          << ", is_bbl_3mf=" << (is_bbl_3mf ? 1 : 0)
                          << ", file_version=" << file_version.to_string() << std::endl;

                // Capture project-embedded preset names (prefer these over config IDs) BEFORE moving the model
                project_printer_preset.clear();
                project_print_preset.clear();
                project_filament_preset.clear();
                for (const auto *pp : project_presets) {
                    if (pp == nullptr) continue;
                    switch (pp->type) {
                        case Slic3r::Preset::TYPE_PRINTER:
                            if (project_printer_preset.empty()) project_printer_preset = pp->name;
                            break;
                        case Slic3r::Preset::TYPE_PRINT:
                            if (project_print_preset.empty()) project_print_preset = pp->name;
                            break;
                        case Slic3r::Preset::TYPE_FILAMENT:
                            if (project_filament_preset.empty()) project_filament_preset = pp->name;
                            break;
                        default: break;
                    }
                }
                // Flag presence of embedded presets for later selection logic
                has_project_embedded_presets = !project_presets.empty();

                // Derive plate-level printer hints from BBL 3MF metadata (printer_model_id, nozzle_diameters) BEFORE moving the model
                if (!plate_data_src.empty()) {
                    int idx_i = plate_id;
                    if (idx_i < 0) idx_i = 0;
                    int max_i = (int)plate_data_src.size() - 1;
                    if (idx_i > max_i) idx_i = max_i;
                    size_t idx = (size_t)idx_i;
                    Slic3r::PlateData* pd = plate_data_src[idx];
                    if (pd != nullptr) {
                        plate_printer_model_id = pd->printer_model_id;
                        std::string nd = pd->nozzle_diameters;
                        // pick first diameter if multi-extruder (comma separated)
                        auto comma = nd.find(',');
                        std::string first = (comma == std::string::npos) ? nd : nd.substr(0, comma);
                        // trim spaces
                        auto ltrim = [](std::string &s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); })); };
                        auto rtrim = [](std::string &s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end()); };
                        ltrim(first); rtrim(first);
                        if (!first.empty()) plate_nozzle_variant = first;
                        std::cout << "DEBUG: Plate hints -> printer_model_id='" << plate_printer_model_id
                                  << "', nozzle_variant='" << plate_nozzle_variant << "'" << std::endl;
                    }
                    // Record total plate count for origin computation (GUI parity)
                    total_plates_count = static_cast<int>(plate_data_src.size());
                }


                // CRITICAL FIX: Detect number of colors/extruders from the loaded 3MF model
                // This ensures multi-color models are sliced with the correct number of extruders
                // MUST be declared OUTSIDE the try block so it's accessible later
                size_t detected_extruders = 0;

                std::cout << "🔍 [TRACE 1] Starting 3MF project configuration import" << std::endl;

                // Import the 3MF project configuration into the PresetBundle (mirror GUI behavior)
                try {
                    std::cout << "🔍 [TRACE 2] Inside main try block" << std::endl;

                    // Preserve wipe tower positions from the 3MF before PresetBundle manipulations (GUI parity)
                    std::optional<Slic3r::ConfigOptionFloats> file_wipe_tower_x;
                    std::optional<Slic3r::ConfigOptionFloats> file_wipe_tower_y;
                    if (auto *wt_x = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_x")) file_wipe_tower_x = *wt_x;
                    if (auto *wt_y = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_y")) file_wipe_tower_y = *wt_y;
                    // CRITICAL FIX: Use filament_colour->size() from 3MF config (GUI parity)
                    // The GUI does NOT detect extruders from the model, it uses the config!
                    // See: OrcaSlicer/src/libslic3r/PresetBundle.cpp line 2424:
                    //   size_t num_filaments = config.option<ConfigOptionStrings>("filament_colour")->size();
                    std::cout << "🔍 [TRACE 3] Detecting number of filaments from 3MF config (GUI parity)" << std::endl;
                    try {
                        if (auto* filament_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour")) {
                            detected_extruders = filament_colour->size();
                            std::cout << "DEBUG: Detected " << detected_extruders << " filaments from filament_colour config" << std::endl;
                        } else {
                            std::cout << "WARN: No filament_colour in config, defaulting to 0" << std::endl;
                            detected_extruders = 0;
                        }
                    } catch (const std::exception& e) {
                        std::cout << "WARN: Failed to get filament_colour from config: " << e.what() << std::endl;
                        detected_extruders = 0;
                    }

                    // Save detected_extruders to Impl so it's accessible later
                    this->detected_extruders = detected_extruders;
                    std::cout << "DEBUG: detected_extruders (from config) = " << detected_extruders << std::endl;
                    std::cout << "🔍 [TRACE 4] About to capture config before loading 3MF" << std::endl;

                    // Capture config before loading 3MF to detect project-level parameter overrides
                    Slic3r::DynamicPrintConfig _cfg_before(*config);

                    std::cout << "🔍 [TRACE 5] About to call preset_bundle.load_config_model()" << std::endl;
                    preset_bundle.load_config_model(filename, *config, file_version);
                    std::cout << "🔍 [TRACE 6] preset_bundle.load_config_model() completed successfully" << std::endl;

                    // After loading 3MF, the GUI stores project-level overrides into preset_bundle.project_config.
                    // For CLI parity, snapshot those overrides and re-apply onto the working config later.
                    project_cfg_after_3mf = Slic3r::DynamicPrintConfig();

                    // TRACE: Check if different_settings_to_system exists in config
                    std::cout << "TESTE AQUI AGORA >>> Checking for different_settings_to_system in loaded 3MF config" << std::endl;

                    // Capture print-level overrides: keys where edited preset differs from selected base preset
                    try {
                        // FIRST: Try to get override keys from different_settings_to_system field in the 3MF project config
                        // This field contains the actual keys that were modified by the user in the original project
                        bool found_different_settings = false;
                        try {
                            if (auto* diff_settings_opt = config->opt<Slic3r::ConfigOptionStrings>("different_settings_to_system")) {
                                const auto& diff_settings_vec = diff_settings_opt->values;
                                std::cout << " >>> Found different_settings_to_system with " << diff_settings_vec.size() << " entries" << std::endl;

                                // The first entry (index 0) contains print/process settings
                                if (!diff_settings_vec.empty() && !diff_settings_vec[0].empty()) {
                                    std::string diff_str = diff_settings_vec[0];

                                    // Parse semicolon-separated keys
                                    std::istringstream ss(diff_str);
                                    std::string key;
                                    while (std::getline(ss, key, ';')) {
                                        if (!key.empty()) {
                                            print_overrides_keys.push_back(key);
                                        }
                                    }
                                    found_different_settings = true;
                                }
                            }
                        } catch (const std::exception& e) {
                            std::cout << "Exception reading different_settings_to_system: " << e.what() << std::endl;
                        }

                        // FALLBACK: If different_settings_to_system is not available or empty, use parent comparison
                        if (!found_different_settings) {
                            // Compare edited preset against its parent (base) to capture true "dirty" keys embedded in the 3MF,
                            // independent of what is currently selected in the bundle.
                            auto dirty = preset_bundle.prints.current_different_from_parent_options(true /*deep_compare*/);
                            print_overrides_keys.assign(dirty.begin(), dirty.end());
                        }

                        // DEBUG: dump print_overrides_keys in JSON-like format (similar to console.log(JSON.stringify(..., null, 2)))
                        try {
                            std::string json = "[";
                            for (size_t i = 0; i < print_overrides_keys.size(); ++i) {
                                json += (i == 0 ? "\n  \"" : "  \"");
                                json += print_overrides_keys[i];
                                json += "\"";
                                if (i + 1 < print_overrides_keys.size()) json += ",\n";
                                else json += "\n";
                            }
                            json += "]";
                            dbg_log(std::string("DEBUG: print_overrides_keys = ") + json);
                        } catch (...) {}

                        // Now extract the actual values for these keys from the loaded config
                        print_cfg_overrides = Slic3r::DynamicPrintConfig();
                        for (const auto& key : print_overrides_keys) {
                            try {
                                if (const Slic3r::ConfigOption* opt = config->optptr(key)) {
                                    print_cfg_overrides.set_key_value(key, opt->clone());
                                } else {
                                }
                            } catch (const std::exception& e) {
                                std::cout << "ERROR extracting key '" << key << "': " << e.what() << std::endl;
                            }
                        }
                        dbg_log(std::string("DEBUG: Detected ") + std::to_string(print_overrides_keys.size()) + " print override key(s) from 3MF");
                    } catch (...) {
                    }

                    // Snapshot project-level overrides robustly: some vectors in project_config may be empty and throw on bulk apply.
                    try {
                        project_cfg_after_3mf.apply(preset_bundle.project_config, /*ignore_nonexistent=*/true);
                    } catch (const std::exception &e_bulk) {
                        // DEBUG: dump project_overrides_keys in JSON-like format
                        try {
                            std::string json = "[";
                            for (size_t i = 0; i < project_overrides_keys.size(); ++i) {
                                json += (i == 0 ? "\n  \"" : "  \"");
                                json += project_overrides_keys[i];
                                json += "\"";
                                if (i + 1 < project_overrides_keys.size()) json += ",\n";
                                else json += "\n";
                            }
                            json += "]";
                            dbg_log(std::string("DEBUG: project_overrides_keys = ") + json);
                            auto dump_val = [&](const char* k){ if (const auto* o = project_cfg_after_3mf.optptr(k)) dbg_log(std::string("DEBUG: project_cfg_after_3mf[") + k + "] = " + o->serialize()); };
                            dump_val("seam_position");
                            dump_val("bottom_surface_pattern");
                            dump_val("internal_solid_infill_pattern");
                        } catch (...) {}

                        std::cout << "WARN: Bulk apply of project_config failed (will retry per-key): " << e_bulk.what() << std::endl;
                        try {
                            auto __keys = preset_bundle.project_config.keys();
                            for (const auto &__k : __keys) {
                                try {
                                    if (const Slic3r::ConfigOption *opt = preset_bundle.project_config.optptr(__k))
                                        project_cfg_after_3mf.set_key_value(__k, opt->clone());
                                } catch (const std::exception &e_k) {
                                    std::cout << "DEBUG: Skipped project_config key due to error: '" << __k << "' -> " << e_k.what() << std::endl;
                                }
                            }
                        } catch (...) {}
                    }
                        // DEBUG: list some critical dirty keys if present
                        try {
                            auto log_key = [&](const char* k){
                                if (std::find(print_overrides_keys.begin(), print_overrides_keys.end(), std::string(k)) != print_overrides_keys.end()) {
                                    if (const Slic3r::ConfigOption* o = print_cfg_overrides.optptr(k))
                                        dbg_log(std::string("DEBUG: print_dirty[") + k + "] = " + o->serialize());
                                    else
                                        dbg_log(std::string("DEBUG: print_dirty[") + k + "] present but not found in print_cfg_overrides");
                                }
                            };
                            log_key("seam_position");
                            log_key("bottom_surface_pattern");
                            log_key("internal_solid_infill_pattern");
                        } catch (...) {}

                    project_overrides_keys = project_cfg_after_3mf.keys();

                    // Restore wipe tower positions into the project config (GUI does this explicitly)
                    try {
                        Slic3r::DynamicConfig &proj_cfg = preset_bundle.project_config;
                        if (file_wipe_tower_x) {
                            if (auto *opt = proj_cfg.opt<Slic3r::ConfigOptionFloats>("wipe_tower_x"))
                                *opt = *file_wipe_tower_x;
                        }
                        if (file_wipe_tower_y) {
                            if (auto *opt = proj_cfg.opt<Slic3r::ConfigOptionFloats>("wipe_tower_y"))
                                *opt = *file_wipe_tower_y;
                        }
                    } catch (...) {}

                    // If the loaded model carries wipe tower positions, prefer them as source of truth (GUI parity)
                    try {
                        if (!loaded.wipe_tower.positions.empty()) {
                            Slic3r::ConfigOptionFloats wtx, wty;
                            wtx.values.resize(loaded.wipe_tower.positions.size());
                            wty.values.resize(loaded.wipe_tower.positions.size());
                            for (size_t i = 0; i < loaded.wipe_tower.positions.size(); ++i) {
                                wtx.values[i] = static_cast<float>(loaded.wipe_tower.positions[i].x());
                                wty.values[i] = static_cast<float>(loaded.wipe_tower.positions[i].y());
                            }
                            // Apply to project_config immediately
                            preset_bundle.project_config.set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx));
                            preset_bundle.project_config.set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty));
                            // Keep project overrides snapshot in sync so later re-apply uses the same values
                            project_cfg_after_3mf.set_key_value("wipe_tower_x", new Slic3r::ConfigOptionFloats(wtx));
                        // DEBUG: snapshot project-level override keys presence for critical ones
                        try {
                            auto has_proj = [&](const char* k){
                                bool present = std::find(project_overrides_keys.begin(), project_overrides_keys.end(), std::string(k)) != project_overrides_keys.end();
                                dbg_log(std::string("DEBUG: project_override_has[") + k + "] = " + (present?"1":"0"));
                            };
                            has_proj("seam_position");
                            has_proj("bottom_surface_pattern");
                            has_proj("internal_solid_infill_pattern");
                        } catch (...) {}

                            project_cfg_after_3mf.set_key_value("wipe_tower_y", new Slic3r::ConfigOptionFloats(wty));

                        }
                    } catch (...) {}

                    // Debug specific: check whether wipe_tower_x is overridden by the 3MF
                    if (std::find(project_overrides_keys.begin(), project_overrides_keys.end(), std::string("wipe_tower_x")) != project_overrides_keys.end()) {
                        try {
                            auto *opt = project_cfg_after_3mf.optptr("wipe_tower_x");
                            if (opt) std::cout << "DEBUG: 3MF overrides wipe_tower_x = " << opt->serialize() << std::endl;
                        } catch (...) {}
                    }


                        // Use the sanitized snapshot as the authoritative project_config to avoid vector-empties.
                        preset_bundle.project_config = project_cfg_after_3mf;

                    // Refresh working config from bundle selections (robust)
                    try {
                        *config = preset_bundle.full_config_secure();
                    } catch (const std::exception &e_fc) {
                        std::cout << "WARN: full_config_secure failed (assembling config robustly): " << e_fc.what() << std::endl;
                        Slic3r::DynamicPrintConfig out;
                        try { out.apply(preset_bundle.prints.get_edited_preset().config); } catch (...) {}
                        try { out.apply(preset_bundle.filaments.default_preset().config); } catch (...) {}
                        try { out.apply(preset_bundle.printers.get_edited_preset().config); } catch (...) {}
                        try { out.apply(project_cfg_after_3mf, /*ignore_nonexistent=*/true); } catch (...) {}
                        *config = out;
                    }

                    std::cout << "DEBUG: Loaded 3MF project config into PresetBundle -> printer='"
                              << preset_bundle.printers.get_selected_preset_name()
                              << "', print='" << preset_bundle.prints.get_selected_preset_name()
                              << "', filament='" << (preset_bundle.filament_presets.empty()?std::string():preset_bundle.filament_presets.front())
                              << "' (project overrides keys: " << project_overrides_keys.size() << ")" << std::endl;
                    std::cout << "🔍 [TRACE 7] End of try block - SUCCESS" << std::endl;
                } catch (const std::exception &e) {
                    std::cout << "🔍 [TRACE 8] EXCEPTION caught in main try block: " << e.what() << std::endl;
                    std::cout << "WARN: Failed to load 3MF project config into PresetBundle: " << e.what() << std::endl;
                }

                std::cout << "🔍 [TRACE 9] After main try-catch block, detected_extruders=" << detected_extruders << std::endl;

                // CRITICAL FIX: Enable single_extruder_multi_material for AMS/CFS systems
                // This MUST be done AFTER the try-catch block to ensure it executes even if load_config_model throws
                std::cout << "DEBUG: Checking if need to enable single_extruder_multi_material (detected_extruders=" << detected_extruders << ")" << std::endl;
                if (detected_extruders > 1) {
                    std::cout << "🔍 [TRACE 10] Enabling multi-material settings for " << detected_extruders << " extruders" << std::endl;
                    std::cout << "DEBUG: Enabling single_extruder_multi_material for multi-color printing" << std::endl;
                    config->set_key_value("single_extruder_multi_material", new Slic3r::ConfigOptionBool(true));
                    std::cout << "DEBUG: single_extruder_multi_material = " << config->opt_bool("single_extruder_multi_material") << std::endl;

                    // CRITICAL: Also enable prime tower (essential for multi-material printing)
                    std::cout << "DEBUG: Enabling enable_prime_tower for multi-color printing" << std::endl;
                    config->set_key_value("enable_prime_tower", new Slic3r::ConfigOptionBool(true));
                    std::cout << "DEBUG: enable_prime_tower = " << config->opt_bool("enable_prime_tower") << std::endl;
                } else {
                    std::cout << "🔍 [TRACE 11] NOT enabling single_extruder_multi_material (detected_extruders=" << detected_extruders << ")" << std::endl;
                    std::cout << "DEBUG: NOT enabling single_extruder_multi_material (only " << detected_extruders << " extruder(s))" << std::endl;
                }

                std::cout << "🔍 [TRACE 12] About to expand filament arrays (detected_extruders=" << detected_extruders << ")" << std::endl;

                // CRITICAL FIX: Ensure filament arrays match detected extruder count
                // This is essential because extruders_count is determined by filament_diameter.size()
                // MUST be done AFTER the try-catch block to ensure it executes
                if (detected_extruders > 0) {
                    std::cout << "🔍 [TRACE 13] Inside filament expansion block" << std::endl;
                    try {
                        std::cout << "🔍 [TRACE 14] Getting filament config options" << std::endl;
                        // Get current filament array sizes
                        auto* fil_diameter = config->opt<Slic3r::ConfigOptionFloats>("filament_diameter", false);
                        auto* fil_density = config->opt<Slic3r::ConfigOptionFloats>("filament_density", false);
                        auto* fil_colour = config->opt<Slic3r::ConfigOptionStrings>("filament_colour", false);
                        auto* fil_type = config->opt<Slic3r::ConfigOptionStrings>("filament_type", false);
                        auto* fil_ids = config->opt<Slic3r::ConfigOptionStrings>("filament_ids", false);

                        size_t diameter_count = fil_diameter ? fil_diameter->values.size() : 0;
                        size_t colour_count = fil_colour ? fil_colour->values.size() : 0;
                        size_t type_count = fil_type ? fil_type->values.size() : 0;
                        size_t density_count = fil_density ? fil_density->values.size() : 0;
                        size_t ids_count = fil_ids ? fil_ids->values.size() : 0;

                        std::cout << "🔍 [TRACE 15] Array sizes - diameter:" << diameter_count
                                  << " colour:" << colour_count
                                  << " type:" << type_count
                                  << " density:" << density_count
                                  << " ids:" << ids_count
                                  << " detected_extruders:" << detected_extruders << std::endl;

                        // Debug: print current filament colors
                        if (fil_colour) {
                            std::cout << "DEBUG: Current filament_colour values (" << fil_colour->values.size() << "): ";
                            for (const auto& c : fil_colour->values) std::cout << c << " ";
                            std::cout << std::endl;
                        }

                        // Debug: print current filament types
                        if (fil_type) {
                            std::cout << "DEBUG: Current filament_type values (" << fil_type->values.size() << "): ";
                            for (const auto& t : fil_type->values) std::cout << t << " ";
                            std::cout << std::endl;
                        }

                        // CRITICAL: Expand EACH array individually if it's smaller than detected_extruders
                        // This ensures all arrays match the extruder count

                        // Expand filament_diameter (this determines extruders_count!)
                        if (fil_diameter && fil_diameter->values.size() < detected_extruders) {
                            std::cout << "🔍 [TRACE 16] Expanding filament_diameter from " << fil_diameter->values.size() << " to " << detected_extruders << std::endl;
                            while (fil_diameter->values.size() < detected_extruders) {
                                double last_diameter = fil_diameter->values.empty() ? 1.75 : fil_diameter->values.back();
                                fil_diameter->values.push_back(last_diameter);
                            }
                            std::cout << "DEBUG: Expanded filament_diameter to " << fil_diameter->values.size() << std::endl;
                        }

                        // Expand filament_density
                        if (fil_density && fil_density->values.size() < detected_extruders) {
                            std::cout << "🔍 [TRACE 17] Expanding filament_density from " << fil_density->values.size() << " to " << detected_extruders << std::endl;
                            while (fil_density->values.size() < detected_extruders) {
                                double last_density = fil_density->values.empty() ? 1.26 : fil_density->values.back();
                                fil_density->values.push_back(last_density);
                            }
                        }

                        // Expand filament_colour
                        if (fil_colour && fil_colour->values.size() < detected_extruders) {
                            std::cout << "🔍 [TRACE 18] Expanding filament_colour from " << fil_colour->values.size() << " to " << detected_extruders << std::endl;
                            while (fil_colour->values.size() < detected_extruders) {
                                std::string last_color = fil_colour->values.empty() ? "#FFFFFF" : fil_colour->values.back();
                                fil_colour->values.push_back(last_color);
                            }
                            std::cout << "DEBUG: Expanded filament_colour to " << fil_colour->values.size() << std::endl;
                        }

                        // CRITICAL: Save the 3MF colors to restore later (presets will overwrite them)
                        if (fil_colour && fil_colour->values.size() >= detected_extruders) {
                            this->saved_filament_colours = fil_colour->values;
                            std::cout << "🔍 [TRACE 18.5] SAVED 3MF colors: ";
                            for (const auto& c : this->saved_filament_colours) std::cout << c << " ";
                            std::cout << std::endl;
                        }

                        // Expand filament_type
                        if (fil_type && fil_type->values.size() < detected_extruders) {
                            std::cout << "🔍 [TRACE 19] Expanding filament_type from " << fil_type->values.size() << " to " << detected_extruders << std::endl;
                            while (fil_type->values.size() < detected_extruders) {
                                std::string last_type = fil_type->values.empty() ? "PLA" : fil_type->values.back();
                                fil_type->values.push_back(last_type);
                            }
                        }

                        // Expand filament_ids
                        if (fil_ids && fil_ids->values.size() < detected_extruders) {
                            std::cout << "🔍 [TRACE 20] Expanding filament_ids from " << fil_ids->values.size() << " to " << detected_extruders << std::endl;
                            while (fil_ids->values.size() < detected_extruders) {
                                std::string last_id = fil_ids->values.empty() ? "GFL99" : fil_ids->values.back();
                                fil_ids->values.push_back(last_id);
                            }
                        }

                        std::cout << "🔍 [TRACE 21] Expansion complete - final sizes: diameter:"
                                  << (fil_diameter ? fil_diameter->values.size() : 0)
                                  << " colour:" << (fil_colour ? fil_colour->values.size() : 0)
                                  << " type:" << (fil_type ? fil_type->values.size() : 0) << std::endl;
                    } catch (const std::exception& e) {
                        std::cout << "🔍 [TRACE 22] EXCEPTION in filament expansion: " << e.what() << std::endl;
                        std::cout << "WARN: Failed to expand filament arrays: " << e.what() << std::endl;
                    }
                } else {
                    std::cout << "🔍 [TRACE 23] Skipping filament expansion (detected_extruders=" << detected_extruders << ")" << std::endl;
                }

                // Load and activate project-embedded presets via PresetBundle official API
                try {
                    // Filter project presets by transfer_* flags to control selection granularly
                    if (!transfer_printer_customizations || !transfer_filament_customizations || !transfer_process_customizations) {
                        std::vector<Slic3r::Preset*> __filtered;
                        __filtered.reserve(project_presets.size());
                        for (Slic3r::Preset* pp : project_presets) {
                            if (!pp) continue;
                            if (pp->type == Slic3r::Preset::TYPE_PRINTER  && !transfer_printer_customizations)  continue;
                            if (pp->type == Slic3r::Preset::TYPE_FILAMENT && !transfer_filament_customizations) continue;
                            if (pp->type == Slic3r::Preset::TYPE_PRINT    && !transfer_process_customizations)  continue;
                            __filtered.push_back(pp);
                        }
                        project_presets.swap(__filtered);
                        // After filtering, reflect whether project still embeds presets we will honor

                        // Fallback: if earlier dirty detection/logging was skipped due to exception,
                        // attempt to detect and log print_overrides_keys here as well.
                        if (print_overrides_keys.empty()) {

                            // FIRST: Try to get override keys from different_settings_to_system field
                            bool found_different_settings_fallback = false;
                            try {
                                if (auto* diff_settings_opt = config->opt<Slic3r::ConfigOptionStrings>("different_settings_to_system")) {
                                    const auto& diff_settings_vec = diff_settings_opt->values;

                                    // The first entry (index 0) contains print/process settings
                                    if (!diff_settings_vec.empty() && !diff_settings_vec[0].empty()) {
                                        std::string diff_str = diff_settings_vec[0];

                                        // Parse semicolon-separated keys
                                        std::istringstream ss(diff_str);
                                        std::string key;
                                        while (std::getline(ss, key, ';')) {
                                            if (!key.empty()) {
                                                print_overrides_keys.push_back(key);
                                            }
                                        }
                                        found_different_settings_fallback = true;
                                    }
                                }
                            } catch (const std::exception& e) {
                            }

                            // SECOND FALLBACK: If different_settings_to_system is not available, use dirty detection
                            if (!found_different_settings_fallback) {
                                try {
                                    auto dirty2 = preset_bundle.prints.current_dirty_options(true /*deep_compare*/);
                                    print_overrides_keys.assign(dirty2.begin(), dirty2.end());
                                } catch (const std::exception &e) {
                                    dbg_log(std::string("DEBUG: (fallback) current_dirty_options threw: ") + e.what());
                                }
                            }

                            // JSON-like dump
                            try {
                                std::string json2 = "[";
                                for (size_t i = 0; i < print_overrides_keys.size(); ++i) {
                                    json2 += (i == 0 ? "\n  \"" : "  \"");
                                    json2 += print_overrides_keys[i];
                                    json2 += "\"";
                                    if (i + 1 < print_overrides_keys.size()) json2 += ",\n";
                                    else json2 += "\n";
                                }
                                json2 += "]";
                                dbg_log(std::string("DEBUG: (fallback) print_overrides_keys = ") + json2);
                            } catch (...) {}

                            // Rebuild print_cfg_overrides to match the detected keys
                            print_cfg_overrides = Slic3r::DynamicPrintConfig();
                            for (const auto& key : print_overrides_keys) {
                                try {
                                    if (const Slic3r::ConfigOption* opt = config->optptr(key)) {
                                        print_cfg_overrides.set_key_value(key, opt->clone());
                                    } else {
                                    }
                                } catch (const std::exception& e) {
                                }
                            }
                        } else {
                        }

                        has_project_embedded_presets = !project_presets.empty();
                    }
                    auto subs = preset_bundle.load_project_embedded_presets(project_presets, Slic3r::ForwardCompatibilitySubstitutionRule::Enable);
                    (void)subs; // substitutions may be logged/used later if needed
                    // Refresh working config from full resolved config after selections
                    *config = preset_bundle.full_config_secure();
                    // Ensure working config mirrors project wipe tower positions
                    try {
                        if (const Slic3r::ConfigOption *opt = preset_bundle.project_config.optptr("wipe_tower_x"))
                            config->set_key_value("wipe_tower_x", opt->clone());
                        if (const Slic3r::ConfigOption *opt = preset_bundle.project_config.optptr("wipe_tower_y"))
                            config->set_key_value("wipe_tower_y", opt->clone());
                    } catch (...) {}

                    // DEBUG dump selected override keys for a few expected params
                    try {
                        auto dump_opt = [&](const char* label, const Slic3r::DynamicConfig& cfg){
                            auto dump_one = [&](const char* k){
                                if (const Slic3r::ConfigOption* o = cfg.optptr(k))
                                    std::cout << "DEBUG: " << label << "[" << k << "] = " << o->serialize() << std::endl;
                            };
                            dump_one("sparse_infill_density");
                            dump_one("top_shell_layers");
                        };
                        dump_opt("project_cfg_after_3mf", project_cfg_after_3mf);
                        dump_opt("working_config_before_override", *config);
                    } catch (...) {}



                    // Re-apply project-level overrides from the 3MF onto the working config to honor project settings
                    if (!project_overrides_keys.empty()) {

                            // DEBUG dump values after we apply each key
                            try {
                                for (const auto &k : project_overrides_keys) {
                                    if (k == std::string("sparse_infill_density") || k == std::string("top_shell_layers")) {
                                        if (const Slic3r::ConfigOption* o = config->optptr(k.c_str()))
                                            std::cout << "DEBUG: working_config_after_override[" << k << "] = " << o->serialize() << std::endl;
                                    }
                                }
                            } catch (...) {}

                        for (const auto &k : project_overrides_keys) {
                            try {
                                const Slic3r::ConfigOption *opt = project_cfg_after_3mf.optptr(k);
                                if (opt) {
                                    // Take ownership of the cloned option on the working config
                                    config->set_key_value(k, opt->clone());
                                }
                            } catch (...) {
                                // ignore per-key issues; continue applying others
                            }
                        }
                    }




                    std::cout << "DEBUG: Applied project-embedded presets -> printer='"
                              << preset_bundle.printers.get_selected_preset_name()
                              << "', print='" << preset_bundle.prints.get_selected_preset_name()
                              << "', filament='" << preset_bundle.filaments.get_selected_preset_name()
                              << "'" << std::endl;
                } catch (const std::exception &e) {
                    std::cout << "WARN: Failed to apply project-embedded presets via PresetBundle: " << e.what() << std::endl;
                }

                // No strict failure here; we will enforce policy later in slice() based on CLI vs 3MF data presence.
                (void)project_presets;

                // Replace current model with the loaded one AFTER consuming project_presets pointers
                // to avoid dangling references during PresetBundle operations.
                *model = std::move(loaded);

                }






            // GUI parity: do not normalize instances here. Use only plate_origin for plate-local coordinates.
            // Keep instances in assembly space and apply the offset only during G-code export.
            std::cout << "DEBUG: 3MF project preset names captured: printer='" << project_printer_preset
                      << "', print='" << project_print_preset
                      << "', filament='" << project_filament_preset << "'" << std::endl;

            // Ensure model has objects
            if (model->objects.empty()) {
                last_error = "No objects found in model file";
                return false;
            }

            // Add default instance if none exists
            for (auto* obj : model->objects) {
                if (obj->instances.empty()) {
                    obj->add_instance();
                }

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
            // Precedence is centralized in CliCore::slice(): options > print-dirty (3MF) > project-overrides (3MF) > presets.
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

            std::cout << "Used extruders: ";
            for (int e : used_extruders) std::cout << e << " ";
            std::cout << std::endl;
            std::cout << "Max extruder needed: " << max_extruder_needed << std::endl;

            if (fil_colour_pre) {
                std::cout << "filament_colour BEFORE: " << fil_colour_pre->values.size() << std::endl;
                if (fil_colour_pre->values.size() > max_extruder_needed) {
                    std::cout << "TRIMMING filament_colour: " << fil_colour_pre->values.size() << " -> " << max_extruder_needed << std::endl;
                    fil_colour_pre->values.resize(max_extruder_needed);
                }
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
                plate.plate_index = (plate_id > 0 ? plate_id - 1 : 0); // zero-based
                plate.is_sliced_valid = true;

                std::cout << "🔍 [3MF-2] PlateData created, plate_index=" << plate.plate_index << std::endl;

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

                // Strategy: match OrcaSlicer's export_project strategy exactly
                // OrcaSlicer uses: Silence|WithGcode|SplitModel|UseLoadedId|ShareMesh
                sp.strategy = Slic3r::SaveStrategy::Silence |
                              Slic3r::SaveStrategy::WithGcode |
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

    CliCore::ModelInfo getModelInformation() const {
        CliCore::ModelInfo info;

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

// CliCore implementation

CliCore::CliCore() : m_impl(std::make_unique<Impl>()) {
    std::cout << "========================================" << std::endl;
    std::cout << "🚀 ORCASLICER ADDON LOADED - VERSION WITH MULTI-COLOR FIX" << std::endl;
    std::cout << "🎨 Multi-color support: ENABLED" << std::endl;
    std::cout << "📅 Build date: " << __DATE__ << " " << __TIME__ << std::endl;
    std::cout << "========================================" << std::endl;
}

CliCore::~CliCore() = default;

CliCore::OperationResult CliCore::initialize(const std::string& resources_path) {
    std::cout << "🔧 CliCore::initialize() called with resources_path: " << resources_path << std::endl;

    if (m_impl->initialized) {
        std::cout << "⚠️  Already initialized, skipping" << std::endl;
        return OperationResult(true, "Already initialized");
    }

    std::cout << "🔄 Initializing Slic3r..." << std::endl;
    if (m_impl->initializeSlic3r(resources_path)) {
        m_impl->initialized = true;
        std::cout << "✅ CLI Core initialized successfully" << std::endl;
        return OperationResult(true, "CLI Core initialized successfully");
    } else {
        std::cout << "❌ Initialization failed: " << m_impl->last_error << std::endl;
        return OperationResult(false, "Initialization failed", m_impl->last_error);
    }
}

void CliCore::shutdown() {
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

bool CliCore::isInitialized() const {
    return m_impl->initialized;
}

CliCore::OperationResult CliCore::loadModel(const std::string& filename) {
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

CliCore::ModelInfo CliCore::getModelInfo() const {
    if (!m_impl->initialized) {
        ModelInfo info;
        info.is_valid = false;
        info.errors.push_back("CLI Core not initialized");
        return info;
    }

    return m_impl->getModelInformation();
}

CliCore::OperationResult CliCore::slice(const SlicingParams& params) {
    std::cout << "🎯 CliCore::slice() CALLED - Multi-color fix version active!" << std::endl;

    if (!m_impl->initialized) {
        std::cout << "❌ CLI Core not initialized!" << std::endl;
        return OperationResult(false, "CLI Core not initialized");
    }

    // Propagate transfer flags into Impl for use in performSlicing()
    m_impl->transfer_printer_customizations  = params.transfer_printer_customizations;
    m_impl->transfer_filament_customizations = params.transfer_filament_customizations;
    m_impl->transfer_process_customizations  = params.transfer_process_customizations;
    m_impl->transfer_project_overrides       = params.transfer_project_overrides;
    // Behavior flags
    m_impl->center_on_bed = params.center_on_bed;

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
    // Auto-apply project presets from 3MF. We always parse 3MF to capture project hints;
    // if user provided explicit profiles, we will not override them during selection.
    {
        std::filesystem::path _p(params.input_file);
        std::string _ext = _p.extension().string();
        std::transform(_ext.begin(), _ext.end(), _ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (_ext == ".3mf") {
            try {
                // Prefer exact names captured from project presets over config IDs (more reliable)
                std::string _printer = m_impl->project_printer_preset;
                std::string _process = m_impl->project_print_preset;
                std::string _filament = m_impl->project_filament_preset;
                if (_printer.empty() && _process.empty() && _filament.empty()) {
                    if (auto *op = m_impl->config->option<Slic3r::ConfigOptionString>("printer_settings_id", false)) _printer = op->value;
                    if (auto *op2 = m_impl->config->option<Slic3r::ConfigOptionString>("print_settings_id", false)) _process = op2->value;
                    if (auto *opf = m_impl->config->option<Slic3r::ConfigOptionStrings>("filament_settings_id", false)) {
                        if (!opf->values.empty()) _filament = opf->values.front();
                    }
                    // New: consider default_* profile names embedded by Orca GUI
                    if (_process.empty() && m_impl->config->has("default_print_profile"))
                        _process = m_impl->config->opt_string("default_print_profile");
                    if (_filament.empty() && m_impl->config->has("default_filament_profile"))
                        _filament = m_impl->config->opt_string("default_filament_profile");
                }

                // Respect explicit CLI profiles: do not override user intent
                const bool user_prn = !params.printer_profile.empty();
                const bool user_proc = !params.process_profile.empty();
                const bool user_fil = !params.filament_profile.empty();
                if (user_prn) _printer.clear();
                if (user_proc) _process.clear();
                if (user_fil) _filament.clear();

                // If printer not specified or is generic default, derive from model/variant in project config
                std::string cfg_model   = m_impl->config->has("printer_model")   ? m_impl->config->opt_string("printer_model")   : std::string();
                std::string cfg_variant = m_impl->config->has("printer_variant") ? m_impl->config->opt_string("printer_variant") : std::string();

                // Enforce priority: CLI > 3MF names > hard fail (no synthetic fallbacks)
                const bool any_cli = user_prn || user_proc || user_fil;
                if (!any_cli) {
                    // STRICT only if the 3MF embeds explicit project preset names (not defaults/config fallbacks)
                    const bool any_project_named = ((!m_impl->project_printer_preset.empty() && m_impl->project_printer_preset != "Default Printer") ||
                                                    (!m_impl->project_print_preset.empty()   && m_impl->project_print_preset   != "Default Setting") ||
                                                    (!m_impl->project_filament_preset.empty()&& m_impl->project_filament_preset!= "Default Filament"));
                    if (!any_project_named) {
                        // No explicit project preset names: keep going with 3MF-embedded fields (model/variant, default_*),
                        // without inventing external fallbacks.
                    } else {
                        // Apply only the explicit names from the 3MF (strict, no heuristics) honoring granular transfer_* flags
                        bool all_ok = true;
                        if (m_impl->transfer_printer_customizations && !m_impl->project_printer_preset.empty() && m_impl->project_printer_preset != "Default Printer") {
                            auto r = loadPrinterProfile(m_impl->project_printer_preset);
                            all_ok = all_ok && r.success;
                        }
                        if (m_impl->transfer_process_customizations && !m_impl->project_print_preset.empty() && m_impl->project_print_preset != "Default Setting") {
                            auto r = loadProcessProfile(m_impl->project_print_preset);
                            all_ok = all_ok && r.success;
                        }
                        if (m_impl->transfer_filament_customizations && !m_impl->project_filament_preset.empty() && m_impl->project_filament_preset != "Default Filament") {
                            auto r = loadFilamentProfile(m_impl->project_filament_preset);
                            all_ok = all_ok && r.success;
                        }
                        if (!all_ok) {
                            return OperationResult(false, "Failed to apply 3MF embedded preset names strictly");
                        }
                        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                        *m_impl->config = m_impl->preset_bundle.full_config_secure();
                        std::cout << "DEBUG: Strict 3MF preset names applied -> printer='"
                                  << m_impl->preset_bundle.printers.get_selected_preset_name()
                                  << "', process='" << m_impl->preset_bundle.prints.get_selected_preset_name()
                                  << "', filament='" << (m_impl->preset_bundle.filament_presets.empty()?std::string():m_impl->preset_bundle.filament_presets.front())
                                  << "'" << std::endl;
                        // Neuter any remaining heuristic paths by clearing hint sources and names
                        m_impl->plate_printer_model_id.clear();
                        m_impl->plate_nozzle_variant.clear();
                        cfg_model.clear();
                        cfg_variant.clear();
                        _printer.clear();
                        _process.clear();
                        _filament.clear();

                        // After strict application, skip any further heuristic selection below by returning to outer scope
                        // We do this by short-circuiting the .3mf branch early; slicing continues after this block.
                    }
                }

                // If 3MF doesn't expose printer_model/printer_variant, try to infer model from default_print_profile suffix "@BBL <model>"
                if (cfg_model.empty() && m_impl->config->has("default_print_profile")) {
                    std::string dp = m_impl->config->opt_string("default_print_profile");
                    auto pos = dp.find("@BBL ");
                    if (pos != std::string::npos) {
                        std::string suffix = dp.substr(pos + 5); // text after "@BBL "
                        if (!suffix.empty()) cfg_model = std::string("Bambu Lab ") + suffix;
                    }
                }
                // Try plate-derived hints first (from BBL 3MF metadata)



                std::string derived_printer;
                if ((_printer.empty() || _printer == "Default Printer") && !cfg_model.empty() && !cfg_variant.empty()) {
                    // e.g. "Bambu Lab A1" + "0.4" -> "Bambu Lab A1 0.4 nozzle"
                    derived_printer = cfg_model + " " + cfg_variant + " nozzle";
                    std::cout << "DEBUG: Derived printer from project config: '" << derived_printer << "'" << std::endl;
                }

                // Ensure BBL vendor and the exact (model, variant) are enabled so the preset becomes visible for selection
                if (!cfg_model.empty() && !cfg_variant.empty()) {
                    try {
                        m_impl->app_config.set_variant("BBL", cfg_model, cfg_variant, true);
                        m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                        std::cout << "DEBUG: Enabled variant in AppConfig and reloaded installed printers for model='"
                                  << cfg_model << "' variant='" << cfg_variant << "'" << std::endl;
                    } catch (...) {
                        std::cout << "WARN: Failed to enable model/variant in AppConfig (continuing)" << std::endl;
                    }
                }

                std::cout << "DEBUG: 3MF auto-apply candidates -> printer='" << (_printer.empty() ? derived_printer : _printer)
                          << "', process='" << _process
                          << "', filament='" << _filament << "'" << std::endl;

                // When the 3MF embeds presets, prefer those and do NOT reselect from system by name.
                const bool project_has_embedded = m_impl->has_project_embedded_presets;

                // 1) Select printer preset
                std::string selected_printer_name;
                if (!project_has_embedded && !user_prn) {
                    // Try plate-derived hints first (from BBL 3MF metadata)
                    if (selected_printer_name.empty() && !m_impl->plate_printer_model_id.empty() && !m_impl->plate_nozzle_variant.empty()) {
                        const Slic3r::Preset *sys = m_impl->preset_bundle.printers.find_system_preset_by_model_and_variant(m_impl->plate_printer_model_id, m_impl->plate_nozzle_variant);
                        if (sys != nullptr) {
                            if (m_impl->preset_bundle.printers.select_preset_by_name(sys->name, /*force=*/true)) {
                                selected_printer_name = sys->name;
                                m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                *m_impl->config = m_impl->preset_bundle.full_config_secure();
                                std::cout << "DEBUG: Selected printer from plate hints: '" << selected_printer_name << "'" << std::endl;
                            }
                        }
                    }
                    if (!derived_printer.empty()) {
                        auto r = loadPrinterProfile(derived_printer);
                        if (r.success) selected_printer_name = derived_printer;
                    }
                    if (selected_printer_name.empty() && !_printer.empty() && _printer != "Default Printer") {
                        auto r = loadPrinterProfile(_printer);
                        if (r.success) selected_printer_name = _printer;
                    }
                    // New: If still no printer, and we have a process preset name, use its compatibility metadata to pick a printer
                    if (selected_printer_name.empty() && !_process.empty() && _process != "Default Setting") {
                        const Slic3r::Preset *proc = m_impl->preset_bundle.prints.find_preset(_process, /*first_visible_if_not_found=*/false, /*real=*/false, /*only_from_library=*/false);
                        if (proc != nullptr) {
                            std::string compat_list;
                            if (proc->config.has("print_compatible_printers"))
                                compat_list = proc->config.opt_string("print_compatible_printers");
                            if (!compat_list.empty()) {
                                // compat_list is a textual list; select the first available printer
                                // Try simple split on ';' and '\n'
                                std::vector<std::string> candidates; candidates.reserve(8);
                                std::string token; token.reserve(64);
                                for (char c : compat_list) {
                                    if (c == '\n' || c == ';') { if (!token.empty()) { candidates.push_back(token); token.clear(); } }
                                    else token.push_back(c);
                                }
                                if (!token.empty()) candidates.push_back(token);
                                for (auto &cand : candidates) {
                                    // trim spaces
                                    while (!cand.empty() && (cand.front()==' '||cand.front()=='\t')) cand.erase(cand.begin());
                                    while (!cand.empty() && (cand.back()==' '||cand.back()=='\t')) cand.pop_back();
                                    if (cand.empty()) continue;
                                    auto r = loadPrinterProfile(cand);
                                    if (r.success) { selected_printer_name = cand; break; }
                                }
                            }
                        }
                    }
                    // Fallback: scan printers for matching model when variant is unknown
                    if (selected_printer_name.empty() && !cfg_model.empty() && cfg_variant.empty()) {
                        for (const auto &p : m_impl->preset_bundle.printers) {
                            try {
                                std::string m = p.config.has("printer_model") ? p.config.opt_string("printer_model") : std::string();
                                if (m == cfg_model) {
                                    if (m_impl->preset_bundle.printers.select_preset_by_name(p.name, /*force=*/true)) {
                                        selected_printer_name = p.name;
                                        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                        *m_impl->config = m_impl->preset_bundle.full_config_secure();
                                        // Enable visibility for this specific preset
                                        std::string v = p.config.has("printer_variant") ? p.config.opt_string("printer_variant") : std::string();
                                        if (!v.empty()) {
                                            try { m_impl->app_config.set_variant("BBL", m, v, true); m_impl->preset_bundle.load_installed_printers(m_impl->app_config); } catch (...) {}
                                        }
                                        break;
                                    }
                                }
                            } catch (...) {}
                        }
                    }
                }

                // 2) Select filament preset (prefer BBL PLA Basic for the printer model when unspecified)
                std::string selected_filament_name;
                if (!project_has_embedded && !user_fil) {
                    if (!_filament.empty() && _filament != "Default Filament") {
                        auto r = loadFilamentProfile(_filament);
                        if (r.success) selected_filament_name = _filament;
                    }
                    if (selected_filament_name.empty()) {
                        // Try project-embedded filament preset first
                        if (!m_impl->project_filament_preset.empty()) {
                            auto r = loadFilamentProfile(m_impl->project_filament_preset);
                            if (r.success) selected_filament_name = m_impl->project_filament_preset;
                        }
                    }
                    if (selected_filament_name.empty() && !cfg_model.empty()) {
                        // Extract suffix like "A1" from model "Bambu Lab A1"
                        std::string model_suffix = cfg_model;
                        size_t pos = model_suffix.rfind(' ');
                        if (pos != std::string::npos) model_suffix = model_suffix.substr(pos + 1);
                        const std::vector<std::string> filament_candidates = {
                            std::string("Bambu PLA Basic @BBL ") + model_suffix,
                            std::string("Bambu PLA Basic")
                        };
                        for (const auto &cand : filament_candidates) {
                            auto r = loadFilamentProfile(cand);
                            if (r.success) { selected_filament_name = cand; break; }
                        }
                    }
                }

                // 3) Select process preset (prefer Standard for this printer/model)
                std::string selected_process_name;
                if (!project_has_embedded && !user_proc) {
                    if (!_process.empty() && _process != "Default Setting") {
                        // If no real printer is selected yet, select process directly by name (do not require compatibility yet)
                        const std::string curr_pr = m_impl->preset_bundle.printers.get_selected_preset_name();
                        if (curr_pr.empty() || curr_pr == "Default Printer") {
                            if (m_impl->preset_bundle.prints.select_preset_by_name(_process, /*force=*/true))
                                selected_process_name = _process;
                        } else {
                            auto r = loadProcessProfile(_process);
                            if (r.success) selected_process_name = _process;
                        }
                    }
                    if (selected_process_name.empty() && !selected_printer_name.empty()) {
                        // Find a print preset compatible with selected printer, preferring 0.20mm Standard for model
                        std::string model_suffix;
                        if (!cfg_model.empty()) {
                            model_suffix = cfg_model.substr(cfg_model.rfind(' ') == std::string::npos ? 0 : cfg_model.rfind(' ') + 1);
                        }
                        auto prefers = [&](const std::string &name){
                            bool for_model = model_suffix.empty() ? true : (name.find("@BBL "+model_suffix) != std::string::npos);
                            bool std20 = (name.find("0.20mm Standard") != std::string::npos);
                            return for_model && std20;
                        };
                        const std::string &spn = m_impl->preset_bundle.printers.get_selected_preset().name;
                        // Iterate and choose best match
                        std::string fallback_name;
                        for (const auto &pr : m_impl->preset_bundle.prints) {
                            // Check compatibility by metadata when available (guard missing key)
                            bool is_compat = true;
                            if (pr.config.has("print_compatible_printers")) {
                                const std::string &compat_ref = pr.config.opt_string("print_compatible_printers");
                                is_compat = compat_ref.empty() || (compat_ref.find(spn) != std::string::npos);
                            }
                            if (!is_compat) continue;
                            if (prefers(pr.name)) {
                                if (m_impl->preset_bundle.prints.select_preset_by_name(pr.name, /*force=*/true)) {
                                    selected_process_name = pr.name;
                                    break;
                                }
                            }
                            if (fallback_name.empty() && pr.name.find("Standard") != std::string::npos)
                                fallback_name = pr.name;
                        }
                        if (selected_process_name.empty() && !fallback_name.empty()) {
                            if (m_impl->preset_bundle.prints.select_preset_by_name(fallback_name, /*force=*/true))
                                selected_process_name = fallback_name;
                        }
                        if (!selected_process_name.empty()) {
                            m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            *m_impl->config = m_impl->preset_bundle.full_config_secure();
                        }
                    }
                }

                // 3.1) If a process is selected but printer is still Default, derive printer from process compatibility list
                {
                    const std::string curr_pr = m_impl->preset_bundle.printers.get_selected_preset_name();
                    std::string proc_for_compat = !selected_process_name.empty() ? selected_process_name : m_impl->preset_bundle.prints.get_selected_preset_name();
                    if ((curr_pr.empty() || curr_pr == "Default Printer") && !proc_for_compat.empty() && proc_for_compat != "Default Setting") {
                        const Slic3r::Preset *proc = m_impl->preset_bundle.prints.find_preset(proc_for_compat, /*first_visible_if_not_found=*/false, /*real=*/false, /*only_from_library=*/false);
                        if (proc != nullptr && proc->config.has("print_compatible_printers")) {
                            std::string compat_list = proc->config.opt_string("print_compatible_printers");
                            if (!compat_list.empty()) {
                                std::vector<std::string> cands; cands.reserve(8);
                                std::string tok;
                                for (char c : compat_list) { if (c=='\n' || c==';') { if (!tok.empty()) { cands.push_back(tok); tok.clear(); } } else tok.push_back(c); }
                                if (!tok.empty()) cands.push_back(tok);
                                for (auto &cand : cands) {
                                    while (!cand.empty() && (cand.front()==' '||cand.front()=='\t')) cand.erase(cand.begin());
                                    while (!cand.empty() && (cand.back()==' '||cand.back()=='\t')) cand.pop_back();
                                    if (cand.empty()) continue;
                                    auto rr = loadPrinterProfile(cand);
                                    if (rr.success) { selected_printer_name = cand; break; }
                                }
                                if (!selected_printer_name.empty()) {
                                    m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                    *m_impl->config = m_impl->preset_bundle.full_config_secure();
                                    // Re-select the process to keep it after compatibility update
                                    const std::string reproc = !selected_process_name.empty() ? selected_process_name : proc_for_compat;
                                    if (!reproc.empty()) {
                                        m_impl->preset_bundle.prints.select_preset_by_name(reproc, /*force=*/true);
                                        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                        *m_impl->config = m_impl->preset_bundle.full_config_secure();
                                    }
                                }
                            }
                        }
                    }
                }

                std::cout << "DEBUG: After applying 3MF presets -> selected printer='"
                          << m_impl->preset_bundle.printers.get_selected_preset_name()
                          << "', print='" << m_impl->preset_bundle.prints.get_selected_preset_name()
                          << "', filament='" << m_impl->preset_bundle.filaments.get_selected_preset_name()
                          << "'" << std::endl;

                // Final guard: if still on Default Printer and project presets exist, select them (honor granular transfer_* flags)
                {
                    const std::string curr_pr = m_impl->preset_bundle.printers.get_selected_preset_name();
                    if (m_impl->transfer_printer_customizations && (curr_pr.empty() || curr_pr == "Default Printer") && !m_impl->project_printer_preset.empty()) {
                        auto rr = loadPrinterProfile(m_impl->project_printer_preset);
                        if (rr.success) {
                            m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            *m_impl->config = m_impl->preset_bundle.full_config_secure();
                            std::cout << "DEBUG: Final-guard selected printer from project preset: '"
                                      << m_impl->project_printer_preset << "'" << std::endl;
                        }
                    }
                    // Ensure project print and filament presets are selected if provided by 3MF
                    if (m_impl->transfer_process_customizations && !m_impl->project_print_preset.empty()) {
                        if (m_impl->preset_bundle.prints.select_preset_by_name(m_impl->project_print_preset, /*force=*/true)) {
                            std::cout << "DEBUG: Final-guard selected process from project preset: '"
                                      << m_impl->project_print_preset << "'" << std::endl;
                        }
                    }
                    if (m_impl->transfer_filament_customizations && !m_impl->project_filament_preset.empty()) {
                        if (m_impl->preset_bundle.filaments.select_preset_by_name(m_impl->project_filament_preset, /*force=*/true)) {
                            std::cout << "DEBUG: Final-guard selected filament from project preset: '"
                                      << m_impl->project_filament_preset << "'" << std::endl;
                        }
                    }
                    m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                    *m_impl->config = m_impl->preset_bundle.full_config_secure();
                }
            } catch (const std::exception &e) {
                std::cout << "WARN: Failed to apply project presets from 3MF: " << e.what() << std::endl;
            }
        }
    }
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
    // Re-apply 3MF print-level overrides (e.g., sparse_infill_density, top_shell_layers) on top of selected profiles
    std::cout << "TESTE AQUI AGORA >>> About to re-apply print overrides, transfer_process_customizations=" << params.transfer_process_customizations << std::endl;
    if (params.transfer_process_customizations) {
        try {
            if (!m_impl->print_overrides_keys.empty()) {
                for (const auto& key : m_impl->print_overrides_keys) {
                    if (const auto* opt = m_impl->print_cfg_overrides.optptr(key)) {
                    }
                }

                m_impl->config->apply_only(m_impl->print_cfg_overrides, m_impl->print_overrides_keys, /*ignore_nonexistent=*/true);
                std::cout << "DEBUG: Re-applied " << m_impl->print_overrides_keys.size() << " 3MF print override(s) on top of selected profiles" << std::endl;

                // Dump key values after re-apply
                if (const auto* o = m_impl->config->optptr("sparse_infill_density")) std::cout << "DEBUG: synced_after_overrides[sparse_infill_density]=" << o->serialize() << std::endl;
                if (const auto* o2 = m_impl->config->optptr("top_shell_layers")) std::cout << "DEBUG: synced_after_overrides[top_shell_layers]=" << o2->serialize() << std::endl;
                if (const auto* o3 = m_impl->config->optptr("seam_position")) std::cout << "TESTE AQUI AGORA >>> synced_after_overrides[seam_position]=" << o3->serialize() << std::endl;
                if (const auto* o4 = m_impl->config->optptr("bottom_surface_pattern")) std::cout << "TESTE AQUI AGORA >>> synced_after_overrides[bottom_surface_pattern]=" << o4->serialize() << std::endl;
                if (const auto* o5 = m_impl->config->optptr("sparse_infill_pattern")) std::cout << "TESTE AQUI AGORA >>> synced_after_overrides[sparse_infill_pattern]=" << o5->serialize() << std::endl;
            } else {
            }
        } catch (const std::exception &e) {
            std::cout << "WARN: Failed to re-apply 3MF print overrides: " << e.what() << std::endl;
        }
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
    // Handle bed temperature aliases correctly for current bed type.
    if (!params.custom_settings.empty()) {
        // 1) Apply curr_bed_type first if provided, so alias resolution uses the right type.
        auto it_bed = params.custom_settings.find("curr_bed_type");
        if (it_bed != params.custom_settings.end()) {
            auto r = setConfigOption(it_bed->first, it_bed->second);
            if (r.success) {
                __used_override_keys.push_back("curr_bed_type");
            } else {
                std::cout << "DEBUG: Ignoring invalid override key/value: " << it_bed->first << " (" << r.error_details << ")" << std::endl;
                __ignored_override_keys.push_back("curr_bed_type");
            }
        }
        // 2) Apply the rest, resolving known aliases.
        for (const auto &kv : params.custom_settings) {
            const std::string &key = kv.first;
            const std::string &val = kv.second;
            if (key == "curr_bed_type") continue; // already handled
        #if HAVE_LIBSLIC3R
            // Resolve first_layer_bed_temperature and bed_temperature aliases to the per-bed-type keys used by libslic3r.
            if (key == "first_layer_bed_temperature" || key == "bed_temperature") {
                // Determine active bed type from current config.
                int bed_type_int = int(Slic3r::btPEI);
                if (m_impl->config && m_impl->config->has("curr_bed_type")) {
                    bed_type_int = m_impl->config->option("curr_bed_type")->getInt();
                }
                Slic3r::BedType bed_type = static_cast<Slic3r::BedType>(bed_type_int);
                std::string actual_key = bed_temp_key_for(bed_type, key == "first_layer_bed_temperature");
                if (actual_key.empty()) {
                    std::cout << "DEBUG: Ignoring alias override '" << key << "' for current bed type (no mapping available)" << std::endl;
                    __ignored_override_keys.push_back(key);
                    continue;
                }
                auto rr = setConfigOption(actual_key, val);
                if (rr.success) {
                    __used_override_keys.push_back(key);
                } else {
                    std::cout << "DEBUG: Ignoring invalid alias override: " << actual_key << " (" << rr.error_details << ")" << std::endl;
                    __ignored_override_keys.push_back(key);
                }
                continue;
            }
        #endif
            // Compatibility layer: map common legacy/PrusaSlicer keys to OrcaSlicer equivalents.
            std::string mapped_key = key;
            std::string mapped_val = val;
            if (key == "perimeters") {
                mapped_key = "wall_loops";
            } else if (key == "top_solid_layers") {
                mapped_key = "top_shell_layers";
            } else if (key == "bottom_solid_layers") {
                mapped_key = "bottom_shell_layers";
            } else if (key == "infill_pattern") {
                mapped_key = "sparse_infill_pattern";
            } else if (key == "fill_angle") {
                // Map to sparse infill direction (degrees)
                mapped_key = "infill_direction";
            } else if (key == "external_perimeters_first") {
                // Map boolean to wall sequence enum
                mapped_key = "wall_sequence";
                const std::string v = val;
                const bool truthy = (v == "1" || v == "true" || v == "True" || v == "TRUE");
                mapped_val = truthy ? "outer wall/inner wall" : "inner wall/outer wall";
            } else if (key == "skirts") {
                mapped_key = "skirt_loops";
            } else if (key == "fan_speed") {
                // Best effort: map to overhang/bridges fan speed. Accept a single integer.
                mapped_key = "overhang_fan_speed";
            } else if (key == "fan_always_on") {
                // Map to Orca's setting that keeps fan from stopping completely.
                mapped_key = "reduce_fan_stop_start_freq";
            }

        #if HAVE_LIBSLIC3R
            if (m_impl->config && !m_impl->config->has(mapped_key)) {
                std::cout << "DEBUG: Ignoring unknown override key: " << mapped_key << std::endl;
                __ignored_override_keys.push_back(key);
                continue;
            }
        #endif
            auto result = setConfigOption(mapped_key, mapped_val);
            if (result.success) {
                __used_override_keys.push_back(key);
            } else {
                std::cout << "DEBUG: Ignoring invalid override key/value: " << mapped_key << " (" << result.error_details << ")" << std::endl;
                __ignored_override_keys.push_back(key);

            }
        }
    }

    if (params.dry_run) {
        return OperationResult(true, "Dry run completed - no actual slicing performed");
    }

#if HAVE_LIBSLIC3R
    // Re-apply 3MF project parameter overrides with highest priority
    if (params.transfer_project_overrides) {
        try {
            // Prefer the explicit project_overrides_keys; if empty, fall back to all keys present in project_cfg_after_3mf
            std::vector<std::string> keys_to_apply = m_impl->project_overrides_keys;
            if (keys_to_apply.empty())
                keys_to_apply = m_impl->project_cfg_after_3mf.keys();
            if (!keys_to_apply.empty()) {
                m_impl->config->apply_only(m_impl->project_cfg_after_3mf, keys_to_apply, /*ignore_nonexistent=*/true);
                std::cout << "DEBUG: Re-applied " << keys_to_apply.size() << " 3MF project override(s) on top of selected profiles" << std::endl;
                // Log specific forced keys if present
                auto logk = [&](const char* k){ if (const auto* o = m_impl->config->optptr(k)) dbg_log(std::string("DEBUG: after_project_apply[") + k + "] = " + o->serialize()); };
                logk("seam_position");
                logk("bottom_surface_pattern");
                logk("internal_solid_infill_pattern");
                logk("sparse_infill_pattern");
                logk("top_surface_pattern");
            } else {
                dbg_log("DEBUG: No project override keys to apply (both explicit and fallback empty)");
            }
        } catch (const std::exception &e) {
            std::cout << "WARN: Failed to re-apply 3MF overrides: " << e.what() << std::endl;
        }
    }


    // Ensure 3MF print-level (dirty) overrides take precedence over project-level overrides
    // Some UIs expect per-print edits (shown as orange/dirty) to win. Re-apply them after project overrides.
    std::cout << "TESTE AQUI AGORA >>> Second re-apply of print overrides (after project overrides)" << std::endl;
    if (params.transfer_process_customizations) {
        try {
            if (!m_impl->print_overrides_keys.empty()) {
                std::vector<std::string> __keys = m_impl->print_overrides_keys;
                if (!__used_override_keys.empty()) {
                    __keys.erase(std::remove_if(__keys.begin(), __keys.end(), [&](const std::string& k){
                        return std::find(__used_override_keys.begin(), __used_override_keys.end(), k) != __used_override_keys.end();
                    }), __keys.end());
                }
                if (!__keys.empty()) {
                    for (const auto& key : __keys) {
                        if (const auto* opt = m_impl->print_cfg_overrides.optptr(key)) {
                        }
                    }
                    m_impl->config->apply_only(m_impl->print_cfg_overrides, __keys, /*ignore_nonexistent=*/true);
                    dbg_log(std::string("DEBUG: Re-applied ") + std::to_string(__keys.size()) + " print override(s) after project overrides to ensure precedence");

                    // Log final values
                    if (const auto* o = m_impl->config->optptr("seam_position")) std::cout << "TESTE AQUI AGORA >>> final[seam_position]=" << o->serialize() << std::endl;
                    if (const auto* o2 = m_impl->config->optptr("bottom_surface_pattern")) std::cout << "TESTE AQUI AGORA >>> final[bottom_surface_pattern]=" << o2->serialize() << std::endl;
                    if (const auto* o3 = m_impl->config->optptr("sparse_infill_pattern")) std::cout << "TESTE AQUI AGORA >>> final[sparse_infill_pattern]=" << o3->serialize() << std::endl;
                } else {
                    dbg_log("DEBUG: Skipped re-applying print overrides because options already set those keys");
                }
            } else {
            }
        } catch (const std::exception &e) {
            std::cout << "WARN: Failed to re-apply print overrides after project overrides: " << e.what() << std::endl;
        }
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

    std::cout << "🔍 [TRACE 31] About to call performSlicing()" << std::endl;
    if (m_impl->performSlicing(params.output_file)) {
        // Build compact JSON with which overrides were used vs ignored. Consumers (Node addon) may parse this.
        std::string __json = "{\"used\":[";
        for (size_t i = 0; i < __used_override_keys.size(); ++i) {
            if (i) __json += ",";
            __json += "\"" + __used_override_keys[i] + "\"";
        }
        __json += "],\"ignored\":[";
        for (size_t i = 0; i < __ignored_override_keys.size(); ++i) {
            if (i) __json += ",";
            __json += "\"" + __ignored_override_keys[i] + "\"";
        }
        __json += "]}";
        OperationResult out(true, __json);
        out.estimated_time_sec = m_impl->last_estimated_time_sec;
        out.filament_used_grams = m_impl->last_filament_used_grams;
        return out;
    } else {
        return OperationResult(false, "Slicing failed", m_impl->last_error);
    }
}

std::string CliCore::getVersion() {
#if HAVE_LIBSLIC3R
    return "OrcaSlicerCli 1.0.0 (based on OrcaSlicer " + std::string(SLIC3R_VERSION) + ")";
#else
    return "OrcaSlicerCli 1.0.0 (libslic3r not linked)";
#endif
}

std::string CliCore::getBuildInfo() {
    return "Built on " + std::string(__DATE__) + " " + std::string(__TIME__);
}

CliCore::OperationResult CliCore::loadConfig(const std::string& config_file) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

    if (!std::filesystem::exists(config_file)) {
        return OperationResult(false, "Config file not found: " + config_file);
    }

    // TODO: Implement configuration loading when libslic3r is available
    return OperationResult(false, "Configuration loading not implemented");
}

CliCore::OperationResult CliCore::loadPreset(const std::string& preset_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

    return OperationResult(false, "Preset loading not implemented");
}

CliCore::OperationResult CliCore::loadPrinterProfile(const std::string& printer_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

#if HAVE_LIBSLIC3R
    try {
        // Proactively enable BBL (model, variant) visibility in AppConfig when the printer name follows
        // the common pattern "<Model Name> <diameter> nozzle". This materializes the system preset.
        {
            const std::string suffix = " nozzle";
            if (printer_name.size() > suffix.size() && printer_name.rfind(suffix) == printer_name.size() - suffix.size()) {
                std::string tmp = printer_name.substr(0, printer_name.size() - suffix.size());
                // Extract last token as variant if it looks numeric (e.g., "0.4")
                auto sp = tmp.find_last_of(' ');
                if (sp != std::string::npos) {
                    std::string maybe_variant = tmp.substr(sp + 1);
                    auto is_numeric = [](const std::string &s){ return !s.empty() && (std::isdigit((unsigned char)s[0]) || s[0] == '.'); };
                    if (is_numeric(maybe_variant)) {
                        std::string model_name = tmp.substr(0, sp);
                        try {
                            m_impl->app_config.set_variant("BBL", model_name, maybe_variant, true);
                            m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                        } catch (...) { /* non-fatal */ }
                    }
                }
            }
        }

        // If the argument looks like a JSON file path, import it directly as a preset.
        // Accept absolute or relative paths; for relative, also try under resources/profiles/BBL/machine.
        Slic3r::Preset* preset = nullptr;
        try {
            namespace fs = std::filesystem;
            auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
            bool looks_path = printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json");
            if (looks_path) {
                std::vector<fs::path> candidates;
                fs::path inp = fs::path(printer_name);
                candidates.push_back(inp);
                if (!inp.is_absolute()) {
                    candidates.push_back(fs::path(m_impl->resources_path) / inp);
                    if (inp.parent_path().empty())
                        candidates.push_back(fs::path(m_impl->resources_path) / "profiles" / "BBL" / "machine" / inp);
                }
                for (const auto &cand : candidates) {
                    try {
                        bool ex = fs::exists(cand);
                        std::cout << "DEBUG: loadPrinterProfile path-mode: candidate='" << cand.string() << "' exists=" << (ex?1:0) << std::endl;
                        if (!ex) continue;
                        // Derive expected preset name from filename stem
                        std::string stem = cand.stem().string();
                        // Try to find by name before importing (might already be present)
                        preset = m_impl->preset_bundle.printers.find_preset(stem, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                        if (!preset) {
                            Slic3r::PresetsConfigSubstitutions subs; std::string file = cand.string(); int overwrite=1; std::vector<std::string> out;
                            auto override_confirm = [](std::string const &){ return 1; };
                            bool ok = m_impl->preset_bundle.import_json_presets(subs, file, override_confirm, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent, overwrite, out);
                            std::cout << "DEBUG: loadPrinterProfile path-mode: import_json_presets ok=" << (ok?1:0) << std::endl;
                            if (ok) {
                                m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                preset = m_impl->preset_bundle.printers.find_preset(stem, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                            }
                        }
                        if (preset) break;
                    } catch (...) { /* ignore candidate error */ }
                }
                if (preset == nullptr) {
                    std::cout << "DEBUG: loadPrinterProfile path-mode: import failed, will try system models fallback" << std::endl;
                }
            }
        } catch (...) { /* ignore path-mode errors; fall back to name lookup */ }

        // Find the preset by name regardless of its current visibility (if not already resolved by path)
        if (!preset)
            preset = m_impl->preset_bundle.printers.find_preset(printer_name, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);

        // If still not found, attempt to load only vendor MODELS (no filaments) from resources and retry
        if (!preset) {
            try {
                std::cout << "DEBUG: loadPrinterProfile: attempting load_system_models_from_json()" << std::endl;
                m_impl->preset_bundle.load_system_models_from_json(Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                // Retry by the original string
                preset = m_impl->preset_bundle.printers.find_preset(printer_name, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                // If the original string was a path or a .json, also try by filename stem
                if (!preset) {
                    namespace fs = std::filesystem;
                    auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                    if (printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json")) {
                        std::string stem = fs::path(printer_name).stem().string();
                        preset = m_impl->preset_bundle.printers.find_preset(stem, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                        if (preset) std::cout << "DEBUG: loadPrinterProfile: resolved by stem after load_system_models_from_json ('" << stem << "')" << std::endl;
                    }
                }
                if (preset)
                    std::cout << "DEBUG: loadPrinterProfile: resolved after load_system_models_from_json" << std::endl;
            } catch (...) {
                std::cout << "DEBUG: loadPrinterProfile: load_system_models_from_json threw" << std::endl;
            }
        }

        if (preset == nullptr) {
            // Attempt a compatibility fallback: many G-code headers encode printer as "<model> <nozzle> nozzle",
            // while installed presets may be named just by model (e.g., "Bambu Lab X1 Carbon").
            std::string base_try;
            {
                std::string name_for_parse = printer_name;
                auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                if (printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json")) {
                    try { name_for_parse = std::filesystem::path(printer_name).stem().string(); } catch (...) {}
                }
                const std::string &s = name_for_parse;
                const std::string suffix = " nozzle";
                auto pos = s.rfind(suffix);
                if (pos != std::string::npos) {
                    // Drop the " nozzle" suffix
                    std::string tmp = s.substr(0, pos);
                    // If the remaining ends with a diameter token like " 0.4" or " 0.2", strip that token as well
                    auto sp = tmp.find_last_of(' ');
                    if (sp != std::string::npos) {
                        std::string last = tmp.substr(sp + 1);
                        bool looks_diameter = !last.empty() && (std::isdigit(last[0]) || last[0] == '.');
                        if (looks_diameter) {
                            base_try = tmp.substr(0, sp);
                        }
                    }
                }
            }

            if (!base_try.empty()) {
                std::cout << "DEBUG: Printer preset not found by name: '" << printer_name
                          << "'. Trying base model fallback: '" << base_try << "'" << std::endl;
                preset = m_impl->preset_bundle.printers.find_preset(base_try, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                if (preset) {
                    // Use the base model name
                    std::cout << "DEBUG: Fallback matched base printer preset: '" << base_try << "'" << std::endl;
                }
            }

            // If still not found, try enabling the BBL model/variant in AppConfig to materialize system presets, then retry
            if (!preset) {
                try {
                    // Extract nozzle variant (e.g., "0.4") from the original name if present
                    std::string variant;
                    {
                        std::string name_for_parse = printer_name;
                        auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                        if (printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json")) {
                            try { name_for_parse = std::filesystem::path(printer_name).stem().string(); } catch (...) {}
                        }
                        const std::string &s = name_for_parse;
                        const std::string suffix = " nozzle";
                        auto pos = s.rfind(suffix);
                        if (pos != std::string::npos) {
                            std::string tmp = s.substr(0, pos);
                            auto sp = tmp.find_last_of(' ');
                            if (sp != std::string::npos) {
                                std::string last = tmp.substr(sp + 1);
                                bool looks = !last.empty() && (std::isdigit(last[0]) || last[0] == '.');
                                if (looks) variant = last;
                            }
                        }
                    }
                    if (!base_try.empty() && !variant.empty()) {
                        std::cout << "DEBUG: Enabling AppConfig vendor variant: vendor=BBL, model='" << base_try << "', variant='" << variant << "'" << std::endl;
                        m_impl->app_config.set_variant("BBL", base_try, variant, true);
                        m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                        // Retry lookup by full name first, then base model
                        preset = m_impl->preset_bundle.printers.find_preset(printer_name, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                        if (!preset)
                            preset = m_impl->preset_bundle.printers.find_preset(base_try, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                        // As a final attempt, resolve BBL model_id from resources by matching machine name, then match by (model_id, variant)
                        if (!preset) {
                            try {
                                namespace fs = std::filesystem;
                                std::string model_id;
                                fs::path machines_dir = fs::path(m_impl->resources_path) / "profiles" / "BBL" / "machine";
                                if (fs::exists(machines_dir) && fs::is_directory(machines_dir)) {
                                    for (const auto &entry : fs::directory_iterator(machines_dir)) {
                                        if (!entry.is_regular_file()) continue;
                                        if (entry.path().extension() != ".json") continue;
                                        try {
                                            std::ifstream ifs(entry.path());
                                            nlohmann::json j; ifs >> j;
                                            if (j.contains("name") && j["name"].is_string() && j["name"].get<std::string>() == base_try) {
                                                if (j.contains("model_id") && j["model_id"].is_string()) {
                                                    model_id = j["model_id"].get<std::string>();
                                                    break;
                                                }
                                            }
                                        } catch (...) { /* ignore malformed json */ }
                                    }
                                }
                                if (!model_id.empty()) {
                                    // Enable the exact (vendor_id, model_id, variant) in AppConfig to materialize visibility, then refresh installed printers.
                                    try {
                                        m_impl->app_config.set_variant("BBL", model_id, variant, true);
                                        m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                                    } catch (...) { /* ignore */ }
                                    if (const Slic3r::Preset* sys = m_impl->preset_bundle.printers.find_system_preset_by_model_and_variant(model_id, variant)) {
                                        std::cout << "DEBUG: Matched system preset by model_id+variant: model_id='" << model_id << "', variant='" << variant << "' -> name='" << sys->name << "'" << std::endl;
                                        preset = const_cast<Slic3r::Preset*>(sys);
                                    }
                                }
                                // As a robust fallback, scan visible printers for matching (printer_model, printer_variant)
                                if (!preset) {
                                    for (const auto &p : m_impl->preset_bundle.printers) {
                                        try {
                                            std::string m = p.config.has("printer_model")   ? p.config.opt_string("printer_model")   : std::string();
                                            std::string v = p.config.has("printer_variant") ? p.config.opt_string("printer_variant") : std::string();
                                            if (m == base_try && (v == variant || v == (variant + ".0"))) {
                                                std::cout << "DEBUG: Found matching preset by (printer_model,printer_variant): '" << p.name << "'" << std::endl;
                                                preset = const_cast<Slic3r::Preset*>(&p);
                                                break;
                                            }
                                        } catch (...) {}
                                    }
                                }
                            } catch (...) { /* ignore */ }
                        }
                    }
                } catch (...) {
                    // ignore
                }
            }

            // Targeted fallback: fully load only BBL vendor system presets, then retry resolution
            if (!preset) {
                try {
                    namespace fs = std::filesystem;
                    fs::path res_profiles = fs::path(m_impl->resources_path) / "profiles";
                    std::cout << "DEBUG: loadPrinterProfile: loading vendor 'BBL' system presets from '" << res_profiles.string() << "'" << std::endl;
                    m_impl->preset_bundle.load_vendor_configs_from_json(
                        res_profiles.string(),
                        std::string("BBL"),
                        Slic3r::PresetBundle::LoadSystem,
                        Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent
                    );
                    m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                    // Retry by original string
                    preset = m_impl->preset_bundle.printers.find_preset(printer_name, false, true, false);
                    // Retry by stem if input was a path/json
                    if (!preset) {
                        auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                        if (printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json")) {
                            std::string stem = fs::path(printer_name).stem().string();
                            preset = m_impl->preset_bundle.printers.find_preset(stem, false, true, false);
                        }
                    }
                    // Retry by base model
                    // base_try is computed above from the name (e.g., "Bambu Lab A1" from "Bambu Lab A1 0.4 nozzle")
                    if (!preset && !base_try.empty()) {
                        preset = m_impl->preset_bundle.printers.find_preset(base_try, false, true, false);
                    }
                } catch (...) {
                    std::cout << "DEBUG: loadPrinterProfile: BBL vendor system load threw" << std::endl;
                }
            }

            // If BBL LoadSystem failed or preset still not found, try a machines-only sandbox for BBL
            if (!preset) {
                try {
                    namespace fs = std::filesystem;
                    fs::path res_profiles = fs::path(m_impl->resources_path) / "profiles";
                    fs::path res_bbl_json = res_profiles / "BBL.json";
                    fs::path res_machines = res_profiles / "BBL" / "machine";
                    fs::path sandbox_root = fs::path(Slic3r::data_dir()) / "sandbox_vendor_bbl";
                    std::error_code ec_rm;
                    fs::remove_all(sandbox_root, ec_rm);
                    (void)ec_rm;
                    fs::create_directories(sandbox_root / "BBL" / "machine");
                    // Read original vendor root to get version and model list
                    nlohmann::json jroot;
                    try {
                        std::ifstream ifs(res_bbl_json);
                        if (ifs.good()) ifs >> jroot;
                    } catch (...) {}
                    nlohmann::json jout = nlohmann::json::object();
                    if (jroot.contains("version")) jout["version"] = jroot["version"];
                    if (jroot.contains("name")) jout["name"] = jroot["name"];
                    // Filter machine_model_list to only include the target base model (e.g. "Bambu Lab A1")
                    {
                        nlohmann::json machine_models = nlohmann::json::array();
                        std::string model_name = base_try.empty() ? std::string("Bambu Lab A1") : base_try;
                        nlohmann::json model_entry = nlohmann::json::object();
                        model_entry["name"] = model_name;
                        model_entry["sub_path"] = std::string("machine/") + model_name + ".json";
                        machine_models.push_back(model_entry);
                        jout["machine_model_list"] = machine_models;
                    }
                    // No processes / filaments
                    jout["process_list"] = nlohmann::json::array();
                    jout["filament_list"] = nlohmann::json::array();
                    // Build machine_list (only 'machine' presets, not model files) and copy required files
                    auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                    // Derive nozzle preset name from input (stem), e.g. "Bambu Lab A1 0.4 nozzle"
                    std::string nozzle_name = printer_name;
                    if (printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json")) {
                        try { nozzle_name = fs::path(printer_name).stem().string(); } catch (...) {}
                    }
                    std::string model_name = base_try.empty() ? std::string("Bambu Lab A1") : base_try;

                    // machine_list: commons + target nozzle preset
                    nlohmann::json machine_list = nlohmann::json::array();
                    auto push_machine = [&](const std::string &nm){
                        nlohmann::json item = nlohmann::json::object();
                        item["name"] = nm;
                        item["sub_path"] = std::string("machine/") + nm + ".json";
                        machine_list.push_back(item);
                    };
                    push_machine("fdm_machine_common");
                    push_machine("fdm_bbl_3dp_001_common");
                    if (!nozzle_name.empty()) push_machine(nozzle_name);
                    jout["machine_list"] = machine_list;

                    // Copy files needed into sandbox: model file + commons + nozzle preset
                    std::set<std::string> files_to_copy;
                    files_to_copy.insert(model_name); // machine_model json
                    files_to_copy.insert("fdm_machine_common");
                    files_to_copy.insert("fdm_bbl_3dp_001_common");
                    if (!nozzle_name.empty()) files_to_copy.insert(nozzle_name);
                    size_t copied = 0;
                    for (const auto &nm : files_to_copy) {
                        fs::path src = res_machines / (nm + ".json");
                        if (!fs::exists(src)) continue;
                        fs::path dst = sandbox_root / "BBL" / "machine" / (nm + ".json");
                        std::error_code ec_cp;
                        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec_cp);
                        if (ec_cp) continue;
                        ++copied;
                    }
                    // write BBL.json
                    {
                        std::ofstream ofs(sandbox_root / "BBL.json");
                        ofs << jout.dump(2);
                    }
                    std::cout << "DEBUG: loadPrinterProfile: attempting BBL machines-only sandbox load at '" << sandbox_root.string() << "', files=" << copied << std::endl;
                    m_impl->preset_bundle.load_vendor_configs_from_json(
                        sandbox_root.string(),
                        std::string("BBL"),
                        Slic3r::PresetBundle::LoadSystem,
                        Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent
                    );
                    m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
                    // Retry find
                    preset = m_impl->preset_bundle.printers.find_preset(printer_name, false, true, false);
                    if (!preset) {
                        std::string stem = nozzle_name;
                        if (!stem.empty()) preset = m_impl->preset_bundle.printers.find_preset(stem, false, true, false);
                    }
                    if (!preset && !base_try.empty()) {
                        preset = m_impl->preset_bundle.printers.find_preset(base_try, false, true, false);
                    }
                } catch (...) {
                    std::cout << "DEBUG: loadPrinterProfile: BBL machines-only sandbox load threw" << std::endl;
                }
            }


            if (!preset) {
                // As a last attempt, import the exact machine preset JSON directly (without loading the full vendor bundle)
                try {
                    namespace fs = std::filesystem;
                    fs::path machines_dir = fs::path(m_impl->resources_path) / "profiles" / "BBL" / "machine";
                    auto try_import = [&](const std::string &name) -> bool {
                        auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                        std::string base = name;
                        if (base.find('/') != std::string::npos || base.find('\\') != std::string::npos) {
                            base = fs::path(base).stem().string();
                        }
                        if (ends_with(base, ".json")) {
                            base = fs::path(base).stem().string();
                        }
                        fs::path candidate = machines_dir / (base + ".json");
                        bool exists = fs::exists(candidate);
                        std::cout << "DEBUG: direct-import: candidate='" << candidate.string() << "' exists=" << (exists?1:0) << " base='" << base << "'" << std::endl;
                        if (!exists) return false;
                        // Import the single preset JSON as an external preset
                        Slic3r::PresetsConfigSubstitutions subs;
                        std::string file = candidate.string();
                        int overwrite = 1; // overwrite if already exists
                        std::vector<std::string> result_names;
                        auto override_confirm = [](std::string const &) -> int { return 1; };
                        bool ok = m_impl->preset_bundle.import_json_presets(
                            subs,
                            file,
                            override_confirm,
                            Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent,
                            overwrite,
                            result_names
                        );
                        std::cout << "DEBUG: direct-import: import_json_presets ok=" << (ok?1:0) << " results=" << result_names.size() << std::endl;
                        if (ok) {
                            m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            // Try to resolve the preset by the imported name
                            if (!result_names.empty()) {
                                for (const auto &nm : result_names) {
                                    std::cout << "DEBUG: direct-import: checking imported name '" << nm << "'" << std::endl;
                                    auto *pp = m_impl->preset_bundle.printers.find_preset(nm, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                                    if (pp != nullptr) { preset = const_cast<Slic3r::Preset*>(pp); std::cout << "DEBUG: direct-import: resolved by imported name" << std::endl; return true; }
                                }
                            }
                            // Fallback to the requested name
                            auto *pp = m_impl->preset_bundle.printers.find_preset(name, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
                            if (pp != nullptr) { preset = const_cast<Slic3r::Preset*>(pp); std::cout << "DEBUG: direct-import: resolved by requested name" << std::endl; return true; }
                        }
                        return false;
                    };
                    bool imported = try_import(printer_name);
                    if (!imported && !base_try.empty()) imported = try_import(base_try);
                } catch (...) { /* ignore */ }
            }

            if (!preset) {
                std::cout << "DEBUG: Printer preset not found by name: '" << printer_name << "'. Available examples:" << std::endl;
                size_t count = 0;
                for (const auto &p : m_impl->preset_bundle.printers) {
                    if (count++ >= 10) break;
                    std::cout << "  - " << p.name << (p.is_visible ? "" : " (hidden)") << std::endl;
                }
                return OperationResult(false, "Printer profile not found", printer_name);
            }
        }

        // Ensure this model/variant is enabled in AppConfig so the preset becomes visible
        try {
            std::string vendor_id = preset->vendor ? preset->vendor->id : std::string();
            std::string model     = preset->config.has("printer_model")   ? preset->config.opt_string("printer_model")   : std::string();
            std::string variant   = preset->config.has("printer_variant") ? preset->config.opt_string("printer_variant") : std::string();
            if (vendor_id.empty()) vendor_id = "BBL"; // default to BBL vendor when unspecified
            if (!vendor_id.empty() && !model.empty() && !variant.empty()) {
                std::cout << "DEBUG: Enabling vendor/model/variant: vendor_id=" << vendor_id
                          << ", model=" << model << ", variant=" << variant << std::endl;
                m_impl->app_config.set_variant(vendor_id, model, variant, true);
                m_impl->preset_bundle.load_installed_printers(m_impl->app_config);
            }
        } catch (...) {
            // Don't fail due to visibility refresh errors
        }

        // Now select the printer preset. Prefer the resolved preset->name if it differs from the incoming string.
        std::string to_select = printer_name;
        if (preset != nullptr && !preset->name.empty() && preset->name != printer_name)
            to_select = preset->name;
        if (!m_impl->preset_bundle.printers.select_preset_by_name(to_select, /*force=*/true)) {
            std::cout << "DEBUG: Failed to select printer preset by name: '" << to_select << "'. Current selected: '"
                      << m_impl->preset_bundle.printers.get_selected_preset_name() << "'" << std::endl;
            return OperationResult(false, "Failed to select printer preset", to_select);
        }
        // Update compatibility of other presets with the selected printer
        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        // Update working config from full resolved config
        *m_impl->config = m_impl->preset_bundle.full_config_secure();
        std::cout << "DEBUG: Loaded printer profile (via PresetBundle): " << printer_name << std::endl;
        return OperationResult(true, "Printer profile loaded successfully: " + printer_name);
    } catch (const std::exception& e) {
        return OperationResult(false, "Error loading printer profile", e.what());
    }
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

CliCore::OperationResult CliCore::loadFilamentProfile(const std::string& filament_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

#if HAVE_LIBSLIC3R
    try {
        // Ensure a printer is selected to attach filament settings to
        // Ensure a printer is selected first
        const auto &active_printer = m_impl->preset_bundle.printers.get_selected_preset();
        if (active_printer.name.empty() || active_printer.name == "Default Printer") {
            return OperationResult(false, "No printer selected before filament profile");
        }
        // Resolve alias to canonical preset name if needed
        std::string fil_name = filament_name;
        {
            const std::string &canonical = m_impl->preset_bundle.get_preset_name_by_alias(Slic3r::Preset::TYPE_FILAMENT, filament_name);
            if (!canonical.empty()) fil_name = canonical;
        }
        // Validate filament exists
        auto *fil_preset = m_impl->preset_bundle.filaments.find_preset(fil_name, /*first_visible_if_not_found=*/false, /*real=*/false, /*only_from_library=*/false);
        if (fil_preset == nullptr) {
            return OperationResult(false, "Filament profile not found", fil_name);
        }
        // Select filament preset and bind it to extruder slot 0
        if (!m_impl->preset_bundle.filaments.select_preset_by_name(fil_name, /*force=*/true)) {
            return OperationResult(false, "Failed to select filament preset", fil_name);
        }

        // Update compatibility after selection and refresh working config
        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        *m_impl->config = m_impl->preset_bundle.full_config_secure();

        std::cout << "DEBUG: Loaded filament profile (via PresetBundle): " << fil_name << std::endl;
        return OperationResult(true, "Filament profile loaded successfully: " + fil_name);
    } catch (const std::exception& e) {
        return OperationResult(false, "Error loading filament profile", e.what());
    }
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

CliCore::OperationResult CliCore::loadProcessProfile(const std::string& process_name) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }



#if HAVE_LIBSLIC3R
    try {
        // Ensure a printer is selected to attach process settings to
        // Ensure a printer is selected first
        const auto &active_printer = m_impl->preset_bundle.printers.get_selected_preset();
        if (active_printer.name.empty() || active_printer.name == "Default Printer") {
            return OperationResult(false, "No printer selected before process profile");
        }
        // Resolve alias to canonical preset name if needed
        std::string proc_name = process_name;
        {
            const std::string &canonical = m_impl->preset_bundle.get_preset_name_by_alias(Slic3r::Preset::TYPE_PRINT, process_name);
            if (!canonical.empty()) proc_name = canonical;
        }
        // Validate process exists
        auto *proc_preset = m_impl->preset_bundle.prints.find_preset(proc_name, /*first_visible_if_not_found=*/false, /*real=*/false, /*only_from_library=*/false);
        if (proc_preset == nullptr) {
            return OperationResult(false, "Process profile not found", proc_name);
        }
        // Select process preset
        if (!m_impl->preset_bundle.prints.select_preset_by_name(proc_name, /*force=*/true)) {
            return OperationResult(false, "Failed to select process preset", proc_name);
        }
        // Update compatibility after selection
        m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        // Ensure wipe tower default position matches CoreXY defaults when multi-material is active


        *m_impl->config = m_impl->preset_bundle.full_config_secure();
        std::cout << "DEBUG: Loaded process profile (via PresetBundle): " << proc_name << std::endl;
        return OperationResult(true, "Process profile loaded successfully: " + proc_name);
    } catch (const std::exception& e) {
        return OperationResult(false, "Error loading process profile", e.what());
    }
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

CliCore::OperationResult CliCore::setConfigOption(const std::string& key, const std::string& value) {
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

std::string CliCore::getConfigOption(const std::string& key) const {
    if (!m_impl->initialized) {
        return "";
    }

    // TODO: Implement configuration getting when libslic3r is available
    return "";  // Return empty for now
}

std::vector<std::string> CliCore::getAvailablePresets() const {
    return {};
}

std::vector<std::string> CliCore::getAvailablePrinterProfiles() const {
    std::vector<std::string> profiles;

    if (!m_impl->initialized) {
        return profiles;
    }

    try {


        std::string profiles_dir = m_impl->resources_path + "/profiles/BBL/machine";
        if (!std::filesystem::exists(profiles_dir)) {
            return profiles;
        }

        for (const auto& entry : std::filesystem::directory_iterator(profiles_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                // Skip common files
                if (filename.find("common") == std::string::npos &&
                    filename.find("fdm_") == std::string::npos) {
                    profiles.push_back(filename);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning printer profiles: " << e.what() << std::endl;
    }

    return profiles;
}

std::vector<std::string> CliCore::getAvailableFilamentProfiles() const {
    std::vector<std::string> profiles;

    if (!m_impl->initialized) {
        return profiles;
    }

    try {
        std::string profiles_dir = m_impl->resources_path + "/profiles/BBL/filament";
        if (!std::filesystem::exists(profiles_dir)) {
            return profiles;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(profiles_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                // Skip common files
                if (filename.find("common") == std::string::npos &&
                    filename.find("fdm_") == std::string::npos &&
                    filename.find("@base") == std::string::npos) {
                    profiles.push_back(filename);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning filament profiles: " << e.what() << std::endl;
    }

    return profiles;
}

std::vector<std::string> CliCore::getAvailableProcessProfiles() const {
    std::vector<std::string> profiles;

    if (!m_impl->initialized) {
        return profiles;
    }

    try {
        std::string profiles_dir = m_impl->resources_path + "/profiles/BBL/process";
        if (!std::filesystem::exists(profiles_dir)) {
            return profiles;
        }

        for (const auto& entry : std::filesystem::directory_iterator(profiles_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                // Skip common files
                if (filename.find("common") == std::string::npos &&
                    filename.find("fdm_") == std::string::npos) {
                    profiles.push_back(filename);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning process profiles: " << e.what() << std::endl;
    }

    return profiles;
}

CliCore::ModelInfo CliCore::validateModel(const std::string& filename) const {
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


CliCore::OperationResult CliCore::loadVendor(const std::string& vendor_id) {
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }
#if HAVE_LIBSLIC3R
        std::cout << "DEBUG: CliCore::loadVendor request vendor_id='" << vendor_id << "'" << std::endl;

    try {
        namespace fs = std::filesystem;
        fs::path res_profiles = fs::path(m_impl->resources_path) / "profiles";
        if (!fs::exists(res_profiles)) {
            return OperationResult(false, "Resources profiles directory not found", res_profiles.string());
        }
        std::cout << "DEBUG: [TEST TRACE] loadVendor('" << vendor_id << "') from '" << res_profiles.string() << "'" << std::endl;
        std::cout << "DEBUG: [TEST TRACE] calling PresetBundle::load_vendor_configs_from_json with vendor '" << vendor_id << "'" << std::endl;
        m_impl->preset_bundle.load_vendor_configs_from_json(res_profiles.string(), vendor_id, Slic3r::PresetBundle::LoadSystem, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        m_impl->loaded_vendors.insert(vendor_id);
        try { m_impl->preset_bundle.load_installed_printers(m_impl->app_config); } catch (...) {}
        return OperationResult(true, std::string("Vendor loaded: ") + vendor_id);
    } catch (const std::exception& e) {
        return OperationResult(false, std::string("Error loading vendor: ") + vendor_id, e.what());
    }
#else
    return OperationResult(false, "libslic3r not available");
#endif
}

} // namespace OrcaSlicerCli



