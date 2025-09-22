#include "CliCore.hpp"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <exception>
#include <cmath>

#include <algorithm>
#include <cctype>


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


namespace OrcaSlicerCli {



/**
 * @brief Private implementation class for CliCore
 */
class CliCore::Impl {
public:
    bool initialized = false;
    std::string resources_path;
    int plate_id = 0; // 0-based plate index for .3mf projects

    std::string last_error;

#if HAVE_LIBSLIC3R
    std::unique_ptr<Slic3r::Model> model;
    std::unique_ptr<Slic3r::Print> print;
    std::unique_ptr<Slic3r::DynamicPrintConfig> config;
    // Whether current 3MF contains embedded presets (print/filament/printer) imported from GUI
    bool has_project_embedded_presets = false;

    // Important: app_config must be destroyed after preset_bundle
    Slic3r::AppConfig app_config;
    Slic3r::PresetBundle preset_bundle;
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

            Slic3r::set_logging_level(4); // Debug level to surface vendor/system preset logs

            // Seed PresetBundle system directory from resources if empty/missing
            try {
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
                // If system root is missing key vendors (first-run), seed from resources via official loader
                try {
                    bool need_bbl = true;
                    try {
                        bool has_bbl = false;
                        for (const auto &e : fs::directory_iterator(sys_dir)) {
                            if (e.is_regular_file() && e.path().filename() == "BBL.json") { has_bbl = true; break; }
                        }
                        need_bbl = !has_bbl;
                    } catch (...) {}

                    fs::path res_profiles = fs::path(Slic3r::resources_dir()) / "profiles";
                    if (!res_profiles.empty() && fs::exists(res_profiles)) {
                        if (need_bbl && fs::exists(res_profiles / "BBL.json")) {
                            std::cout << "DEBUG: Seeding BBL vendor directly from resources into system dir..." << std::endl;
                            auto seeded = preset_bundle.load_vendor_configs_from_json(res_profiles.string(), "BBL", Slic3r::PresetBundle::LoadSystem, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                            (void)seeded;
                        }
                        // Ensure OrcaFilamentLibrary is present too as some presets depend on it
                        bool have_orca_lib = false;
                        try {
                            for (const auto &e : fs::directory_iterator(sys_dir)) {
                                if (e.is_regular_file() && e.path().filename() == "OrcaFilamentLibrary.json") { have_orca_lib = true; break; }
                            }
                        } catch (...) {}
                        if (!have_orca_lib && fs::exists(res_profiles / "OrcaFilamentLibrary.json")) {
                            std::cout << "DEBUG: Seeding OrcaFilamentLibrary from resources..." << std::endl;
                            auto seeded2 = preset_bundle.load_vendor_configs_from_json(res_profiles.string(), "OrcaFilamentLibrary", Slic3r::PresetBundle::LoadSystem, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                            (void)seeded2;
                        }
                    }
                } catch (...) {}


            } catch (...) { /* ignore seeding errors */ }

            // Initialize AppConfig and load defaults (and existing file if any)
            app_config.reset();

            // Load system and user presets using PresetBundle's official API (handles vendor order and merges internally).
            preset_bundle.load_presets(app_config, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
            try {
                size_t total = preset_bundle.printers.size();
                size_t visible = 0; for (const auto &p : preset_bundle.printers) if (p.is_visible) ++visible;
                std::cout << "DEBUG: After load_presets: printers total=" << total << " visible=" << visible << std::endl;
            } catch (...) {}

                // Ensure system models are loaded (required for BBL vendor printers to materialize)
                try {
                    auto res_models = preset_bundle.load_system_models_from_json(Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                    std::cout << "DEBUG: load_system_models_from_json done" << std::endl;
                } catch (...) {
                    std::cout << "WARN: load_system_models_from_json failed (continuing)" << std::endl;
                }
                // Prime installed printers based on current AppConfig (may be empty visibility, will be adjusted later)
                try {
                    preset_bundle.load_installed_printers(app_config);
                    size_t totalp = preset_bundle.printers.size();
                    size_t visiblep = 0; for (const auto &p : preset_bundle.printers) if (p.is_visible) ++visiblep;
                    std::cout << "DEBUG: After initial load_installed_printers: printers total=" << totalp << " visible=" << visiblep << std::endl;
                } catch (...) {}



            // Note: We rely on load_presets() above, which internally calls
            // load_system_presets_from_json(LoadSystem) and populates printers as well.
            // The previous call to load_system_models_from_json() used LoadVendorOnly
            // under the hood and did not paste printer configs; removing it avoids
            // confusion and duplicate state.

            // Ensure installed printers (and related presets) are materialized based on AppConfig.
            // If the vendor section is missing in AppConfig, libslic3r will enable all models/variants by default per vendor.
            try {
                // Materialize installed printers so system presets are present in the collection.
                preset_bundle.load_installed_printers(app_config);
                size_t total = preset_bundle.printers.size();
                size_t visible = 0; for (const auto &p : preset_bundle.printers) if (p.is_visible) ++visible;
                std::cout << "DEBUG: After load_installed_printers: printers total=" << total << " visible=" << visible << std::endl;
                // Note: load_installed_filaments() is private in some Orca forks; skip here to keep compatibility.
            } catch (...) {
                // Non-fatal: continue with defaults
            }

            // Compose full config
            config = std::make_unique<Slic3r::DynamicPrintConfig>();
            *config = preset_bundle.full_config_secure();

            // Initialize model and print objects
            model = std::make_unique<Slic3r::Model>();
            print = std::make_unique<Slic3r::Print>();
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
                Slic3r::PlateDataPtrs plate_data_src;
                std::vector<Slic3r::Preset*> project_presets;
                bool is_bbl_3mf = false;
                Slic3r::Semver file_version;

                // Read model and project config from 3mf (includes per-plate content)
                Slic3r::Model loaded = Slic3r::Model::read_from_file(
                    filename,
                    config.get(),
                    &config_substitutions,
                    Slic3r::LoadStrategy::LoadModel | Slic3r::LoadStrategy::LoadConfig,
                    &plate_data_src,
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


                // Import the 3MF project configuration into the PresetBundle (mirror GUI behavior)
                try {
                    // Preserve wipe tower positions from the 3MF before PresetBundle manipulations (GUI parity)
                    std::optional<Slic3r::ConfigOptionFloats> file_wipe_tower_x;
                    std::optional<Slic3r::ConfigOptionFloats> file_wipe_tower_y;
                    if (auto *wt_x = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_x")) file_wipe_tower_x = *wt_x;
                    if (auto *wt_y = config->opt<Slic3r::ConfigOptionFloats>("wipe_tower_y")) file_wipe_tower_y = *wt_y;

                    // Capture config before loading 3MF to detect project-level parameter overrides
                    Slic3r::DynamicPrintConfig _cfg_before(*config);

                    preset_bundle.load_config_model(filename, *config, file_version);

                    // After loading 3MF, the GUI stores project-level overrides into preset_bundle.project_config.
                    // For CLI parity, snapshot those overrides and re-apply onto the working config later.
                    project_cfg_after_3mf = Slic3r::DynamicPrintConfig();
                    // Capture print-level overrides: keys where edited preset differs from selected base preset
                    try {
                        auto dirty = preset_bundle.prints.current_dirty_options(true /*deep_compare*/);
                        print_overrides_keys.assign(dirty.begin(), dirty.end());
                        print_cfg_overrides = Slic3r::DynamicPrintConfig();
                        print_cfg_overrides.apply_only(preset_bundle.prints.get_edited_preset().config, print_overrides_keys);
                        std::cout << "DEBUG: Detected " << print_overrides_keys.size() << " print override key(s) from 3MF" << std::endl;
                    } catch (...) {}

                    project_cfg_after_3mf.apply(preset_bundle.project_config, /*ignore_nonexistent=*/true);
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

                    // Refresh working config from bundle selections
                    *config = preset_bundle.full_config_secure();
                    std::cout << "DEBUG: Loaded 3MF project config into PresetBundle -> printer='"
                              << preset_bundle.printers.get_selected_preset_name()
                              << "', print='" << preset_bundle.prints.get_selected_preset_name()
                              << "', filament='" << (preset_bundle.filament_presets.empty()?std::string():preset_bundle.filament_presets.front())
                              << "' (project overrides keys: " << project_overrides_keys.size() << ")" << std::endl;
                } catch (const std::exception &e) {
                    std::cout << "WARN: Failed to load 3MF project config into PresetBundle: " << e.what() << std::endl;
                }

                // Load and activate project-embedded presets via PresetBundle official API
                try {
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

    bool performSlicing(const std::string& output_file) {
#if HAVE_LIBSLIC3R
        try {
            if (!model || model->objects.empty()) {
                last_error = "No model loaded for slicing";
                return false;
            }

            std::cout << "DEBUG: Starting slicing process..." << std::endl;
            std::cout << "DEBUG: Model has " << model->objects.size() << " objects" << std::endl;
            std::cout << "DEBUG: Config is " << (config ? "valid" : "null") << std::endl;
            std::cout << "DEBUG: Print is " << (print ? "valid" : "null") << std::endl;
            // Log currently selected presets inside PresetBundle
            std::cout << "DEBUG: Selected printer preset: " << preset_bundle.printers.get_selected_preset_name() << std::endl;
            std::cout << "DEBUG: Selected print preset:   " << preset_bundle.prints.get_selected_preset_name() << std::endl;
            if (!preset_bundle.filament_presets.empty())
                std::cout << "DEBUG: Selected filament[0]:   " << preset_bundle.filament_presets[0] << std::endl;

            // Ensure Print knows whether this is a BBL printer (affects header + CONFIG placement)
            try {
                bool is_bbl = preset_bundle.is_bbl_vendor();
                print->is_BBL_printer() = is_bbl;
                std::cout << "DEBUG: is_BBL_printer set to " << (is_bbl ? "true" : "false") << std::endl;
            } catch (...) {
                std::cout << "WARN: Failed to set is_BBL_printer flag (continuing)" << std::endl;
            }

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
            // DEBUG: dump a couple of key values just before apply()
            try {
                auto dump_one = [&](const char* k){ if (const Slic3r::ConfigOption* o = config->optptr(k)) std::cout << "DEBUG: before_apply[" << k << "] = " << o->serialize() << std::endl; };
                dump_one("sparse_infill_density");
                dump_one("top_shell_layers");
            } catch (...) {}
            // Enforce project-level overrides from 3MF with highest priority just before apply
            try { config->apply(project_cfg_after_3mf, /*ignore_nonexistent=*/true); std::cout << "DEBUG: enforced project_cfg_after_3mf onto working config before apply()" << std::endl; } catch (...) {}


                            // Ensure selected plate index is propagated to Print & Model for GUI parity
                            model->curr_plate_index = idx0;
                            print->set_plate_index(idx0);
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
                } catch (const std::exception &e) {
                    std::cout << "WARN: set_plate_origin (BEFORE process) failed: " << e.what() << std::endl;
                }
            }

            // Apply model and config to print
            try { if (const auto* o = config->optptr("sparse_infill_density")) std::cout << "DEBUG: before_apply[sparse_infill_density]=" << o->serialize() << std::endl; } catch (...) {}
            try { if (const auto* o = config->optptr("top_shell_layers")) std::cout << "DEBUG: before_apply[top_shell_layers]=" << o->serialize() << std::endl; } catch (...) {}

            std::cout << "DEBUG: Applying model and config to print..." << std::endl;
            print->apply(*model, *config);
            std::cout << "DEBUG: Apply completed successfully" << std::endl;

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
                } catch (const std::exception &e) {
                    std::cout << "WARN: set_plate_origin (AFTER apply) failed: " << e.what() << std::endl;
                }
            }


            // Process the print (this does the actual slicing)
            std::cout << "DEBUG: Starting print processing..." << std::endl;
            print->process();
            std::cout << "DEBUG: Print processing completed" << std::endl;

            // GUI parity: compute plate_origin from plate index and bed stride AFTER process, before export
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
                            // Primeiro, tente calcular a partir dos offsets reais das inst e2ncias (assembly)
                            bool ok = false; // after-process block: force fallback, origin was set before process
                            if (!ok) {
                                // Fallback determin edstico: usar  edndice da placa e stride POSITIVO (writer subtrai este offset)
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
                // 3MF production export with embedded G-code (parity with GUI export_gcode_3mf)
                std::cout << "DEBUG: Exporting 3MF (production) to: " << output_file << std::endl;

                // Prepare temp G-code path next to target: filename.stem() + ".gcode"
                std::filesystem::path tmp_gcode = out_path;
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
                    last_error = "Intermediate G-code not found for 3MF packaging";
                    return false;
                }

                // Prepare PlateData for store_bbs_3mf
                Slic3r::PlateData plate;
                plate.plate_index = (plate_id > 0 ? plate_id - 1 : 0); // zero-based
                plate.is_sliced_valid = true;
                plate.gcode_file = tmp_gcode.string();
                plate.parse_filament_info(&proc_result);

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
                Slic3r::PlateDataPtrs pd_list; pd_list.push_back(&plate);
                sp.plate_data_list = pd_list;
                sp.export_plate_idx = plate.plate_index; // export just this plate
                sp.strategy = Slic3r::SaveStrategy::Silence | Slic3r::SaveStrategy::SplitModel | Slic3r::SaveStrategy::WithGcode | Slic3r::SaveStrategy::SkipModel | Slic3r::SaveStrategy::Zip64;

                bool ok3mf = false;
                try {
                    ok3mf = Slic3r::store_bbs_3mf(sp);
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
}

CliCore::~CliCore() = default;

CliCore::OperationResult CliCore::initialize(const std::string& resources_path) {
    if (m_impl->initialized) {
        return OperationResult(true, "Already initialized");
    }

    if (m_impl->initializeSlic3r(resources_path)) {
        m_impl->initialized = true;
        return OperationResult(true, "CLI Core initialized successfully");
    } else {
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
    if (!m_impl->initialized) {
        return OperationResult(false, "CLI Core not initialized");
    }

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
                        // Apply only the explicit names from the 3MF (strict, no heuristics)
                        bool all_ok = true;
                        if (!m_impl->project_printer_preset.empty() && m_impl->project_printer_preset != "Default Printer") {
                            auto r = loadPrinterProfile(m_impl->project_printer_preset);
                            all_ok = all_ok && r.success;
                        }
                        if (!m_impl->project_print_preset.empty() && m_impl->project_print_preset != "Default Setting") {
                            auto r = loadProcessProfile(m_impl->project_print_preset);
                            all_ok = all_ok && r.success;
                        }
                        if (!m_impl->project_filament_preset.empty() && m_impl->project_filament_preset != "Default Filament") {
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

                // Final guard: if still on Default Printer and project presets exist, select them
                {
                    const std::string curr_pr = m_impl->preset_bundle.printers.get_selected_preset_name();
                    if ((curr_pr.empty() || curr_pr == "Default Printer") && !m_impl->project_printer_preset.empty()) {
                        auto rr = loadPrinterProfile(m_impl->project_printer_preset);
                        if (rr.success) {
                            m_impl->preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            *m_impl->config = m_impl->preset_bundle.full_config_secure();
                            std::cout << "DEBUG: Final-guard selected printer from project preset: '"
                                      << m_impl->project_printer_preset << "'" << std::endl;
                        }
                    }
                    // Ensure project print and filament presets are selected if provided by 3MF
                    if (!m_impl->project_print_preset.empty()) {
                        if (m_impl->preset_bundle.prints.select_preset_by_name(m_impl->project_print_preset, /*force=*/true)) {
                            std::cout << "DEBUG: Final-guard selected process from project preset: '"
                                      << m_impl->project_print_preset << "'" << std::endl;
                        }
                    }
                    if (!m_impl->project_filament_preset.empty()) {
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
    try {
        if (!m_impl->print_overrides_keys.empty()) {
            m_impl->config->apply_only(m_impl->print_cfg_overrides, m_impl->print_overrides_keys, /*ignore_nonexistent=*/true);
            std::cout << "DEBUG: Re-applied " << m_impl->print_overrides_keys.size() << " 3MF print override(s) on top of selected profiles" << std::endl;
            // Dump key values after re-apply
            if (const auto* o = m_impl->config->optptr("sparse_infill_density")) std::cout << "DEBUG: synced_after_overrides[sparse_infill_density]=" << o->serialize() << std::endl;
            if (const auto* o2 = m_impl->config->optptr("top_shell_layers")) std::cout << "DEBUG: synced_after_overrides[top_shell_layers]=" << o2->serialize() << std::endl;
        }
    } catch (const std::exception &e) {
        std::cout << "WARN: Failed to re-apply 3MF print overrides: " << e.what() << std::endl;
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
    // Handle bed temperature aliases correctly for current bed type.
    if (!params.custom_settings.empty()) {
        // 1) Apply curr_bed_type first if provided, so alias resolution uses the right type.
        auto it_bed = params.custom_settings.find("curr_bed_type");
        if (it_bed != params.custom_settings.end()) {
            auto r = setConfigOption(it_bed->first, it_bed->second);
            if (!r.success) return OperationResult(false, "Failed to set config option: " + it_bed->first, r.error_details);
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
                    return OperationResult(false, std::string("Unable to map alias '") + key + "' for current bed type");
                }
                auto rr = setConfigOption(actual_key, val);
                if (!rr.success) return OperationResult(false, "Failed to set config option: " + actual_key, rr.error_details);
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

            auto result = setConfigOption(mapped_key, mapped_val);
            if (!result.success) {
                return OperationResult(false, "Failed to set config option: " + mapped_key, result.error_details);
            }
        }
    }

    if (params.dry_run) {
        return OperationResult(true, "Dry run completed - no actual slicing performed");
    }

#if HAVE_LIBSLIC3R
    // Re-apply 3MF project parameter overrides with highest priority
    if (!m_impl->project_overrides_keys.empty()) {
        try {
            m_impl->config->apply_only(m_impl->project_cfg_after_3mf, m_impl->project_overrides_keys, /*ignore_nonexistent=*/true);
            std::cout << "DEBUG: Re-applied " << m_impl->project_overrides_keys.size() << " 3MF project override(s) on top of selected profiles" << std::endl;
        } catch (const std::exception &e) {
            std::cout << "WARN: Failed to re-apply 3MF overrides: " << e.what() << std::endl;
        }
    }

#if HAVE_LIBSLIC3R
#endif

#endif

    if (m_impl->performSlicing(params.output_file)) {
        return OperationResult(true, "Slicing completed successfully: " + params.output_file);
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

        // Find the preset regardless of its current visibility
        auto *preset = m_impl->preset_bundle.printers.find_preset(printer_name, /*first_visible_if_not_found=*/false, /*real=*/true, /*only_from_library=*/false);
        if (preset == nullptr) {
            // Attempt a compatibility fallback: many G-code headers encode printer as "<model> <nozzle> nozzle",
            // while installed presets may be named just by model (e.g., "Bambu Lab X1 Carbon").
            std::string base_try;
            {
                const std::string &s = printer_name;
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
                        const std::string &s = printer_name;
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

} // namespace OrcaSlicerCli



