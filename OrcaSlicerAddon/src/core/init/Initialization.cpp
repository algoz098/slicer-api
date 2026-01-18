#include "core/init/Initialization.hpp"
#include "core/util/Utilities.hpp"

#if HAVE_LIBSLIC3R

#include <filesystem>
#include <iostream>
#include <set>
#include <vector>
#include <algorithm>
#include <clocale>
#include <cstdlib>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

namespace OrcaSlicerCli { namespace init {

using OrcaSlicerCli::util::safe_build_config;

static inline bool truthy_env(const char* name) {
    if (const char* s = std::getenv(name)) return (s[0]=='1'||s[0]=='T'||s[0]=='t'||s[0]=='Y'||s[0]=='y');
    return false;
}

bool initialize_slic3r(const std::string& resources_path,
                       Slic3r::AppConfig& app_config,
                       Slic3r::PresetBundle& preset_bundle,
                       std::set<std::string>& loaded_vendors,
                       std::unique_ptr<Slic3r::DynamicPrintConfig>& config,
                       std::unique_ptr<Slic3r::Model>& model,
                       std::unique_ptr<Slic3r::Print>& print,
                       std::string& last_error)
{
    try {
        // Force deterministic numeric formatting independent of OS locale
        try { std::setlocale(LC_NUMERIC, "C"); } catch (...) {}

        // Initialize libslic3r search paths first
        Slic3r::set_resources_dir(resources_path);
        namespace fs = std::filesystem;
        fs::path cwd = fs::current_path();
        fs::path data_dir = cwd / ".orcaslicercli";
        if (!fs::exists(data_dir)) fs::create_directories(data_dir);
        Slic3r::set_data_dir(data_dir.string());

        // Ensure a writable temporary directory for libslic3r
        try {
            fs::path tmp_dir = data_dir / "tmp";
            if (!fs::exists(tmp_dir)) fs::create_directories(tmp_dir);
            Slic3r::set_temporary_dir(tmp_dir.string());
        } catch (const std::exception&) {}

        // Optional directories if present
        if (fs::exists(fs::path(resources_path) / "i18n"))
            Slic3r::set_local_dir((fs::path(resources_path) / "i18n").string());
        if (fs::exists(fs::path(resources_path) / "shapes"))
            Slic3r::set_sys_shapes_dir((fs::path(resources_path) / "shapes").string());
        if (fs::exists(fs::path(resources_path) / "custom_gcodes"))
            Slic3r::set_custom_gcodes_dir((fs::path(resources_path) / "custom_gcodes").string());

        // Logging level from env
        unsigned int lvl = 4;
        if (const char* lv = std::getenv("ORCACLI_LOGLEVEL")) { try { lvl = (unsigned int)std::stoi(lv); } catch (...) {} }
        Slic3r::set_logging_level(lvl);

        // Disable any env-driven autoloads (API-only control)
        (void)truthy_env; // keep helper used conditionally
        bool strict_no_autoload = true;

        if (strict_no_autoload) {
            try {
                fs::path sys_dir = fs::path(Slic3r::data_dir()) / "system";
                if (fs::exists(sys_dir)) {
                    std::error_code ec; fs::remove_all(sys_dir, ec); (void)ec;
                }
                fs::create_directories(sys_dir);
            } catch (...) {}
        }

        // Optionally seed vendor profiles if enabled
        if (!strict_no_autoload) {
            if (const char* seed = std::getenv("ORCACLI_SEED_ALL"); seed && (seed[0]=='1'||seed[0]=='T'||seed[0]=='t'||seed[0]=='Y'||seed[0]=='y')) {
                try {
                    preset_bundle.setup_directories();
                    fs::path profiles_dir = fs::path(resources_path) / "profiles";
                    fs::path sys_dir      = fs::path(Slic3r::data_dir()) / "system";
                    if (!fs::exists(sys_dir)) fs::create_directories(sys_dir);
                    if (fs::exists(profiles_dir) && fs::is_directory(profiles_dir)) {
                        for (const auto &entry : fs::directory_iterator(profiles_dir)) {
                            if (!entry.is_regular_file()) continue;
                            if (entry.path().extension() != ".json") continue;
                            const std::string fname = entry.path().filename().string();
                            if (fname == "OrcaFilamentLibrary.json") continue;
                            try { fs::copy_file(entry.path(), sys_dir / fname, fs::copy_options::overwrite_existing); } catch (...) {}
                        }
                        for (const auto &entry : fs::directory_iterator(profiles_dir)) {
                            if (!entry.is_directory()) continue;
                            const std::string dname = entry.path().filename().string();
                            if (dname == "OrcaFilamentLibrary") continue;
                            try { fs::copy(entry.path(), sys_dir / dname, fs::copy_options::recursive | fs::copy_options::overwrite_existing); } catch (...) {}
                        }
                    }
                } catch (...) {}
            }
        }

        // Initialize AppConfig and default preset bundle state
        app_config.reset();

        // Env-driven vendor autoload, only if not strict
        if (!strict_no_autoload) {
            if (const char* ev = std::getenv("ORCACLI_VENDORS")) {
                try {
                    std::string s(ev); std::vector<std::string> vendors; vendors.reserve(4); std::string cur;
                    for (char c : s) { if (c==',') { if (!cur.empty()) { vendors.push_back(cur); cur.clear(); } } else if (!std::isspace((unsigned char)c)) { cur.push_back(c);} }
                    if (!cur.empty()) vendors.push_back(cur);
                    std::filesystem::path res_profiles = std::filesystem::path(resources_path) / "profiles";
                    for (const auto &v : vendors) {
                        try { preset_bundle.load_vendor_configs_from_json(res_profiles.string(), v, Slic3r::PresetBundle::LoadSystem, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent); loaded_vendors.insert(v);} catch (...) {}
                    }
                    try { preset_bundle.load_installed_printers(app_config); } catch (...) {}
                } catch (...) {}
            }
        }

        // Eager load presets if requested (but disabled by strict mode)
        if (!strict_no_autoload) {
            if (const char* eager = std::getenv("ORCACLI_EAGER_LOAD_PRESETS")) {
                if (eager[0]=='1'||eager[0]=='T'||eager[0]=='t'||eager[0]=='Y'||eager[0]=='y') {
                    try { preset_bundle.load_presets(app_config, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent); } catch (...) {}
                }
            }
        }

        // Base objects
        config = std::make_unique<Slic3r::DynamicPrintConfig>();
        model  = std::make_unique<Slic3r::Model>();
        print  = nullptr; // Print is created fresh per slice

        // CRITICAL: Always initialize config with FullPrintConfig::defaults()
        // This provides all default values for slicing, making profiles optional.
        // Options passed via JSON will override these defaults.
        Slic3r::FullPrintConfig full_defaults = Slic3r::FullPrintConfig::defaults();
        config->apply(full_defaults, true);
        std::cout << "DEBUG: Initialized config with FullPrintConfig::defaults()" << std::endl;

        // If vendors are loaded, merge their config on top of defaults
        if (!loaded_vendors.empty()) {
            Slic3r::DynamicPrintConfig vendor_config;
            safe_build_config(preset_bundle, vendor_config);
            config->apply(vendor_config, true);
            std::cout << "DEBUG: Applied vendor config on top of defaults" << std::endl;
        }

        return true;
    } catch (const std::exception& e) {
        last_error = std::string("Failed to initialize: ") + e.what();
        return false;
    }
}

void cleanup(std::unique_ptr<Slic3r::Print>& print,
             std::unique_ptr<Slic3r::Model>& model,
             std::unique_ptr<Slic3r::DynamicPrintConfig>& config)
{
    try {
        if (print) print.reset();
        if (model) { model->clear_objects(); model.reset(); }
        if (config) config.reset();
    } catch (const std::exception& e) {
        std::cerr << "Warning: Error during cleanup: " << e.what() << std::endl;
    }
}

} } // namespace OrcaSlicerCli::init

#else

namespace OrcaSlicerCli { namespace init {
bool initialize_slic3r(const std::string&, Slic3r::AppConfig&, Slic3r::PresetBundle&, std::set<std::string>&, std::unique_ptr<Slic3r::DynamicPrintConfig>&, std::unique_ptr<Slic3r::Model>&, std::unique_ptr<Slic3r::Print>&, std::string& last_error) { last_error = "libslic3r not available"; return false; }
void cleanup(std::unique_ptr<Slic3r::Print>&, std::unique_ptr<Slic3r::Model>&, std::unique_ptr<Slic3r::DynamicPrintConfig>&) {}
} }

#endif

