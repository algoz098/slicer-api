#include "core/config/ConfigManager.hpp"

#include <filesystem>
#include <iostream>

#if !HAVE_LIBSLIC3R
#error "libslic3r is required. Placeholders are not allowed."
#endif

#if HAVE_LIBSLIC3R

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <set>


namespace OrcaSlicerCli { namespace config {

// Keep minimal shared state about resources and vendors loaded within this module.
static std::string g_resources_path;
static std::set<std::string> g_loaded_vendors;

bool load_json_config(const std::string& file_path,
                      Slic3r::DynamicPrintConfig& config,
                      std::string& last_error)
{
    try {
        if (!std::filesystem::exists(file_path)) {
            last_error = std::string("Profile file not found: ") + file_path;
            return false;
        }
        Slic3r::ConfigSubstitutions subs = config.load(file_path, Slic3r::ForwardCompatibilitySubstitutionRule::Enable);
        std::cout << "DEBUG: Loaded profile from " << file_path << " with " << subs.size() << " substitutions" << std::endl;
        return true;
    } catch (const std::exception& e) {
        last_error = std::string("Failed to load profile from ") + file_path + ": " + e.what();
        return false;
    }
}

std::string find_profile_file(const std::string& resources_path,
                              const std::string& profile_name,
                              const std::string& profile_type)
{
    namespace fs = std::filesystem;
    std::string profiles_dir = resources_path + "/profiles/BBL/" + profile_type;

    // Try exact path first
    std::string exact_path = profiles_dir + "/" + profile_name + ".json";
    if (fs::exists(exact_path)) return exact_path;

    // Recursive search
    try {
        for (const auto& entry : fs::recursive_directory_iterator(profiles_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename == profile_name)
                    return entry.path().string();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error searching for profile: " << e.what() << std::endl;
    }
    return std::string();
}

bool load_vendor_from_resources(const std::string& resources_path,
                                const std::string& vendor_id,
                                Slic3r::PresetBundle& preset_bundle,
                                Slic3r::AppConfig& app_config,
                                std::set<std::string>& loaded_vendors,
                                std::string& last_error)
{
    try {
        namespace fs = std::filesystem;
        fs::path res_profiles = fs::path(resources_path) / "profiles";
        if (!fs::exists(res_profiles)) {
            last_error = std::string("Resources profiles directory not found: ") + res_profiles.string();
            return false;
        }
        // Remember the resources path for later direct-import fallbacks.
        g_resources_path = resources_path;
        std::cout << "DEBUG: loadVendor from '" << res_profiles.string() << "' vendor='" << vendor_id << "'" << std::endl;

        // Ensure machine models are available even if vendor preset parsing hits an error.
        try {
            preset_bundle.load_system_models_from_json(Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
            preset_bundle.load_installed_printers(app_config);
        } catch (...) { /* non-fatal */ }

        bool vendor_ok = true;
        try {
            preset_bundle.load_vendor_configs_from_json(res_profiles.string(), vendor_id, Slic3r::PresetBundle::LoadSystem, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
        } catch (const std::exception &ex) {
            vendor_ok = false;
            last_error = std::string("Partial vendor load for ") + vendor_id + ": " + ex.what();
            std::cerr << "DEBUG: load_vendor_configs_from_json threw: " << last_error << std::endl;
        }

        // If vendor loading threw before reaching machine presets (common due to a bad filament/process),
        // attempt a resilient fallback: load just the vendor profile (models/variants), then import machine presets directly.
        if (!vendor_ok || preset_bundle.printers.has_defaults_only()) {
            try {
                // Load only vendor profile (no presets) to populate vendors map and models/variants.
                preset_bundle.load_vendor_configs_from_json(res_profiles.string(), vendor_id, Slic3r::PresetBundle::LoadVendorOnly, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                std::cerr << "DEBUG: fallback -> loaded vendor profile only for '" << vendor_id << "'\n";
            } catch (const std::exception &e) {
                std::cerr << "DEBUG: fallback -> LoadVendorOnly threw: " << e.what() << std::endl;
            }

            try {
                namespace fs = std::filesystem;
                fs::path vendor_root = fs::path(res_profiles) / (vendor_id + std::string(".json"));
                nlohmann::json jroot;
                std::ifstream ifs(vendor_root);
                if (ifs.good()) {
                    ifs >> jroot;
                    if (jroot.contains("machine_list") && jroot["machine_list"].is_array()) {
                        std::cerr << "DEBUG: fallback -> machine_list size=" << jroot["machine_list"].size() << std::endl;

                        // Build configs for base "common" machine presets first, so inheritance resolves correctly.
                        nlohmann::json machines = jroot["machine_list"];
                        std::map<std::string, Slic3r::DynamicPrintConfig> base_config_maps;

                        auto load_cfg_with_keys = [&](const fs::path &f, Slic3r::DynamicPrintConfig &out_cfg, std::map<std::string, std::string> &out_keys){
                            out_keys.clear(); std::string reason; auto rule = Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent;
                            out_cfg.load_from_json(f.string(), rule, out_keys, reason);
                        };

                        // Pass 1: prepare base configs (files with "common" in name).
                        std::vector<std::string> commons;
                        commons.reserve(machines.is_array() ? machines.size() : 0);
                        for (size_t i = 0; i < machines.size(); ++i) {
                            const auto &entry = machines[i];
                            if (!entry.is_object() || !entry.contains("sub_path")) continue;
                            std::string sub = entry["sub_path"].get<std::string>();
                            if (sub.find("common") != std::string::npos) commons.emplace_back(std::move(sub));
                        }

                        for (const auto &sub : commons) {
                            fs::path f = fs::path(res_profiles) / vendor_id / sub;
                            if (!fs::exists(f)) continue;
                            try {
                                Slic3r::DynamicPrintConfig cfg; std::map<std::string, std::string> kv; load_cfg_with_keys(f, cfg, kv);
                                std::string name = kv.count("name") ? kv["name"] : f.stem().string();
                                // Determine default (parent) config.
                                const Slic3r::DynamicPrintConfig *default_cfg = nullptr;
                                if (auto it = kv.find("inherits"); it != kv.end()) {
                                    auto itb = base_config_maps.find(it->second);
                                    if (itb != base_config_maps.end()) default_cfg = &itb->second;
                                }
                                if (default_cfg == nullptr) default_cfg = &preset_bundle.printers.default_preset_for(cfg).config;
                                Slic3r::DynamicPrintConfig merged = *default_cfg;
                                merged.apply(cfg);
                                base_config_maps[name] = std::move(merged);
                                std::cerr << "DEBUG: fallback -> prepared base machine '" << name << "'\n";
                            } catch (const std::exception &e) {
                                std::cerr << "DEBUG: fallback -> base prepare threw for '" << f.filename().string() << "': " << e.what() << std::endl;
                            }
                        }

                        // Helper to import one concrete machine preset using PresetCollection::load_preset like vendor loader does.
                        auto import_one = [&](const fs::path &f, const std::string &sub_path){
                            if (!fs::exists(f)) return false;
                            try {
                                std::cerr << "DEBUG: fallback -> import_one begin file='" << f.string() << "'" << std::endl;
                                Slic3r::DynamicPrintConfig cfg; std::map<std::string, std::string> kv; load_cfg_with_keys(f, cfg, kv);
                                std::string preset_name = kv.count("name") ? kv["name"] : f.stem().string();

                                // Resolve default config using inheritance if available.
                                const Slic3r::DynamicPrintConfig *default_cfg = nullptr;
                                if (auto it = kv.find("inherits"); it != kv.end()) {
                                    auto itb = base_config_maps.find(it->second);
                                    if (itb != base_config_maps.end()) default_cfg = &itb->second;
                                }
                                if (default_cfg == nullptr) default_cfg = &preset_bundle.printers.default_preset_for(cfg).config;
                                Slic3r::DynamicPrintConfig merged = *default_cfg;
                                merged.apply(cfg);

                                // Vendor info
                                auto itv = preset_bundle.vendors.find(vendor_id);
                                Slic3r::Semver vendor_ver = (itv != preset_bundle.vendors.end()) ? itv->second.config_version : Slic3r::Semver();

                                // Construct a deterministic path under system presets (same as vendor loader)
                                fs::path file_path = fs::path(Slic3r::data_dir()) / PRESET_SYSTEM_DIR / vendor_id / sub_path;
                                try { fs::create_directories(file_path.parent_path()); } catch (...) {}

                                // Load directly into printers collection to avoid the bundle path that expects full bundles.
                                Slic3r::Preset &loaded = preset_bundle.printers.load_preset(file_path.string(), preset_name, std::move(merged), /*select=*/false, vendor_ver, /*is_custom_defined=*/false);
                                loaded.is_system = true;
                                if (itv != preset_bundle.vendors.end()) {
                                    loaded.vendor  = &itv->second;
                                    loaded.version = itv->second.config_version;
                                }
                                std::cerr << "DEBUG: fallback -> loaded(machine) '" << preset_name << "' as system; printers.size=" << preset_bundle.printers.size() << "\n";
                                return true;
                            } catch (const std::exception &e) {
                                std::cerr << "DEBUG: fallback -> import threw for '" << f.filename().string() << "': " << e.what() << std::endl;
                            }
                            return false;
                        };

                        // Pass 2: import concrete machines (skip "common").
                        std::vector<std::string> subpaths;
                        subpaths.reserve(machines.is_array() ? machines.size() : 0);
                        for (size_t i = 0; i < machines.size(); ++i) {
                            const auto &entry = machines[i];
                            if (!entry.is_object() || !entry.contains("sub_path")) continue;
                            std::string sub = entry["sub_path"].get<std::string>();
                            if (sub.find("common") != std::string::npos) continue; // skip abstracts
                            subpaths.emplace_back(std::move(sub));
                        }
                        std::cerr << "DEBUG: fallback -> concrete machines planned=" << subpaths.size() << std::endl;
                        for (const auto &sub : subpaths) {
                            fs::path f = fs::path(res_profiles) / vendor_id / sub;
                            std::cerr << "DEBUG: fallback -> importing machine sub_path='" << sub << "'" << std::endl;
                            import_one(f, sub);
                        }
                    } else {
                        std::cerr << "DEBUG: fallback -> no machine_list array in vendor json" << std::endl;
                    }
                } else {
                    std::cerr << "DEBUG: fallback -> failed to open vendor json: " << vendor_root.string() << std::endl;
                }
            } catch (const std::exception &e) {
                std::cerr << "DEBUG: fallback -> resilient import phase threw: " << e.what() << std::endl;
            }
        }

        try { preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always); } catch (...) {}
        try { preset_bundle.load_installed_printers(app_config); } catch (...) {}

        // Debug counts after vendor load + fallback
        try {
            std::cerr << "DEBUG: post-vendor load counts => printers:" << preset_bundle.printers.size()
                      << " only_defaults:" << (preset_bundle.printers.has_defaults_only()?"1":"0") << std::endl;
        } catch (...) {}

        loaded_vendors.insert(vendor_id);
        g_loaded_vendors.insert(vendor_id);

        // Consider it a success if vendor load succeeded, or if printers are available beyond defaults.
        return vendor_ok || !preset_bundle.printers.has_defaults_only();
    } catch (const std::exception& e) {
        last_error = std::string("Error loading vendor: ") + vendor_id + ": " + e.what();
        return false;
    }
}


bool load_printer_profile(const std::string& resources_path,
                          const std::string& printer_name,
                          Slic3r::PresetBundle& preset_bundle,
                          Slic3r::AppConfig& app_config,
                          Slic3r::DynamicPrintConfig& out_config,
                          std::string& last_error)
{
    try {
        Slic3r::Preset* preset = nullptr;

        // Proactively enable BBL model/variant from name pattern "<Model> <diameter> nozzle"
        {
            const std::string suffix = " nozzle";
            if (printer_name.size() > suffix.size() && printer_name.rfind(suffix) == printer_name.size() - suffix.size()) {
                std::string tmp = printer_name.substr(0, printer_name.size() - suffix.size());
                auto sp = tmp.find_last_of(' ');
                if (sp != std::string::npos) {
                    std::string maybe_variant = tmp.substr(sp + 1);
                    auto is_numeric = [](const std::string &s){ return !s.empty() && (std::isdigit((unsigned char)s[0]) || s[0] == '.'); };
                    if (is_numeric(maybe_variant)) {
                        std::string model_name = tmp.substr(0, sp);
                        try {
                            app_config.set_variant("BBL", model_name, maybe_variant, true);
                            preset_bundle.load_installed_printers(app_config);
                        } catch (...) { /* non-fatal */ }
                    }
                }
            }
        }

        // If looks like a path / .json, attempt to import directly
        try {
            namespace fs = std::filesystem;
            auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
            bool looks_path = printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json");
            if (looks_path) {
                std::vector<fs::path> candidates;
                fs::path inp = fs::path(printer_name);
                candidates.push_back(inp);
                if (!inp.is_absolute()) {
                    candidates.push_back(fs::path(resources_path) / "profiles" / "BBL" / "machine" / inp);
                }
                for (const auto &cand : candidates) {
                    try {
                        if (!fs::exists(cand)) continue;
                        std::string stem = cand.stem().string();
                        preset = preset_bundle.printers.find_preset(stem, false, true, false);
                        if (!preset) {
                            Slic3r::PresetsConfigSubstitutions subs; std::string file = cand.string(); int overwrite=1; std::vector<std::string> out;
                            auto override_confirm = [](std::string const &){ return 1; };
                            bool ok = preset_bundle.import_json_presets(subs, file, override_confirm, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent, overwrite, out);
                            if (ok) {
                                preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                preset = preset_bundle.printers.find_preset(stem, false, true, false);
                            }
                        }
                        if (preset) break;
                    } catch (...) { /* ignore candidate error */ }
                }
            }
        } catch (...) { /* ignore path-mode errors; fall back to name lookup */ }

        if (!preset)
            preset = preset_bundle.printers.find_preset(printer_name, false, true, false);


        // Compute base_try (drop nozzle suffix and diameter token)
        std::string base_try;
        if (!preset) {
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
                    bool looks_diameter = !last.empty() && (std::isdigit((unsigned char)last[0]) || last[0] == '.');
                    if (looks_diameter) base_try = tmp.substr(0, sp);
                }
            }
            if (!base_try.empty())
                preset = preset_bundle.printers.find_preset(base_try, false, true, false);
        }

        // Early direct import of single machine preset JSON (before any vendor/system loads)
        if (!preset) {
            try {
                namespace fs = std::filesystem;
                fs::path machines_dir = fs::path(resources_path) / "profiles" / "BBL" / "machine";
                auto try_import = [&](const std::string &name) -> bool {
                    auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                    std::string base = name;
                    if (base.find('/') != std::string::npos || base.find('\\') != std::string::npos) base = fs::path(base).stem().string();
                    if (ends_with(base, ".json")) base = fs::path(base).stem().string();
                    fs::path candidate = machines_dir / (base + ".json");
                    if (!fs::exists(candidate)) return false;
                    Slic3r::PresetsConfigSubstitutions subs; std::string file = candidate.string(); int overwrite = 1; std::vector<std::string> result_names; auto override_confirm = [](std::string const &) -> int { return 1; };
                    bool ok = preset_bundle.import_json_presets(subs, file, override_confirm, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent, overwrite, result_names);
                    if (ok) {
                        // After importing, refresh installed printers to make the new preset visible in the bundle.
                        try { preset_bundle.load_installed_printers(app_config); } catch (...) {}
                        try { preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always); } catch (...) {}
                        if (!result_names.empty()) {
                            for (const auto &nm : result_names) {
                                if (auto *pp = preset_bundle.printers.find_preset(nm, false, true, false)) { preset = const_cast<Slic3r::Preset*>(pp); return true; }
                            }
                        }
                        if (auto *pp = preset_bundle.printers.find_preset(name, false, true, false)) { preset = const_cast<Slic3r::Preset*>(pp); return true; }
                    }
                    return false;
                };
                bool imported = try_import(printer_name);
                if (!imported && !base_try.empty()) imported = try_import(base_try);
            } catch (...) {}
        }


        // Enable AppConfig variant and retry; also try model_id + variant
        if (!preset) {
            try {
                // extract variant from name
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
                            bool looks = !last.empty() && (std::isdigit((unsigned char)last[0]) || last[0] == '.');
                            if (looks) variant = last;
                        }
                    }
                }
                if (!base_try.empty() && !variant.empty()) {
                    app_config.set_variant("BBL", base_try, variant, true);
                    preset_bundle.load_installed_printers(app_config);
                    preset = preset_bundle.printers.find_preset(printer_name, false, true, false);
                    if (!preset) preset = preset_bundle.printers.find_preset(base_try, false, true, false);
                    if (!preset) {
                        // Resolve model_id by reading machine JSONs
                        namespace fs = std::filesystem;
                        std::string model_id;
                        fs::path machines_dir = fs::path(resources_path) / "profiles" / "BBL" / "machine";
                        if (fs::exists(machines_dir) && fs::is_directory(machines_dir)) {
                            for (const auto &entry : fs::directory_iterator(machines_dir)) {
                                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                                try {
                                    std::ifstream ifs(entry.path());
                                    nlohmann::json j; ifs >> j;
                                    if (j.contains("name") && j["name"].is_string() && j["name"].get<std::string>() == base_try) {
                                        if (j.contains("model_id") && j["model_id"].is_string()) {
                                            model_id = j["model_id"].get<std::string>();
                                            break;
                                        }
                                    }
                                } catch (...) {}
                            }
                        }
                        if (!model_id.empty()) {
                            try {
                                app_config.set_variant("BBL", model_id, variant, true);
                                preset_bundle.load_installed_printers(app_config);
                            } catch (...) {}
                            if (const Slic3r::Preset* sys = preset_bundle.printers.find_system_preset_by_model_and_variant(model_id, variant)) {
                                preset = const_cast<Slic3r::Preset*>(sys);
                            }
                            if (!preset) {
                                for (const auto &p : preset_bundle.printers) {
                                    try {
                                        std::string m = p.config.has("printer_model")   ? p.config.opt_string("printer_model")   : std::string();
                                        std::string v = p.config.has("printer_variant") ? p.config.opt_string("printer_variant") : std::string();
                                        if (m == base_try && (v == variant || v == (variant + ".0"))) { preset = const_cast<Slic3r::Preset*>(&p); break; }
                                    } catch (...) {}
                                }
                            }
                        }
                    }
                }
            } catch (...) {}
        }

        // Load full BBL vendor system presets and retry
        // Only do this if the vendor has not already been loaded by the caller.
        if (!preset && g_loaded_vendors.find("BBL") == g_loaded_vendors.end()) {
            try {
                // Use tolerant loader to avoid clobbering and to survive partial errors (e.g., AliZ profile)
                std::string err_bbl;
                bool bbl_ok = load_vendor_from_resources(resources_path, std::string("BBL"),
                                                         preset_bundle, app_config,
                                                         g_loaded_vendors, err_bbl);
                (void)bbl_ok; // even if false, some presets may still be available

                preset = preset_bundle.printers.find_preset(printer_name, false, true, false);
                if (!preset) {
                    auto ends_with = [](const std::string &s, const std::string &suf){ return s.size()>=suf.size() && s.rfind(suf)==s.size()-suf.size(); };
                    if (printer_name.find('/') != std::string::npos || printer_name.find('\\') != std::string::npos || ends_with(printer_name, ".json")) {
                        std::string stem = std::filesystem::path(printer_name).stem().string();
                        preset = preset_bundle.printers.find_preset(stem, false, true, false);
                    }
                }
                if (!preset && !base_try.empty()) preset = preset_bundle.printers.find_preset(base_try, false, true, false);
            } catch (...) {}

        // DEBUG: dump some printer presets around this point
        try {
            size_t n = preset_bundle.printers.size();
            bool only_def = preset_bundle.printers.has_defaults_only();
            std::cerr << "DEBUG: printers.size=" << n << ", has_defaults_only=" << (only_def?"1":"0") << std::endl;
            // Try a few alternative lookups just in case
            if (!preset) {
                auto *pp = preset_bundle.printers.find_preset(printer_name, true, true, false);
                if (pp) std::cerr << "DEBUG: find_preset(printer_name,first_visible=1) succeeded: " << pp->name << std::endl;
            }
        } catch (...) {}

        }

        // After full vendor load, try force-select by name instead of sandbox to avoid clobbering other presets.
        if (!preset) {
            bool sel_ok = false;
            try { sel_ok = preset_bundle.printers.select_preset_by_name(printer_name, true); } catch (...) {}
            if (!sel_ok && !base_try.empty()) {
                try { sel_ok = preset_bundle.printers.select_preset_by_name(base_try, true); } catch (...) {}
            }
            if (sel_ok) {
                try { preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always); } catch (...) {}
                preset = preset_bundle.printers.find_preset(printer_name, false, true, false);
                if (!preset && !base_try.empty())
                    preset = preset_bundle.printers.find_preset(base_try, false, true, false);
            }
        }


        if (!preset) {
            last_error = std::string("Printer profile not found: ") + printer_name;
            return false;
        }

        // Ensure visibility in AppConfig
        try {
            std::string vendor_id = preset->vendor ? preset->vendor->id : std::string();
            std::string model     = preset->config.has("printer_model")   ? preset->config.opt_string("printer_model")   : std::string();
            std::string variant   = preset->config.has("printer_variant") ? preset->config.opt_string("printer_variant") : std::string();
            if (vendor_id.empty()) vendor_id = "BBL";
            if (!vendor_id.empty() && !model.empty() && !variant.empty()) {
                app_config.set_variant(vendor_id, model, variant, true);
                preset_bundle.load_installed_printers(app_config);
            }
        } catch (...) {}

        // Select preset and refresh config
        std::string to_select = (!preset->name.empty() ? preset->name : printer_name);
        if (!preset_bundle.printers.select_preset_by_name(to_select, true)) {
            last_error = std::string("Failed to select printer preset: ") + to_select;
            return false;
        }
        preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        out_config = preset_bundle.full_config_secure();
        return true;
    } catch (const std::exception &e) {
        last_error = std::string("Error loading printer profile: ") + e.what();
        return false;
    }
}

bool load_filament_profile(const std::string& filament_name,
                           Slic3r::PresetBundle& preset_bundle,
                           Slic3r::DynamicPrintConfig& out_config,
                           std::string& last_error)
{
    try {
        const auto &active_printer = preset_bundle.printers.get_selected_preset();
        if (active_printer.name.empty() || active_printer.name == "Default Printer") {
            last_error = "No printer selected before filament profile";
            return false;
        }
        std::string fil_name = filament_name;
        {
            const std::string &canonical = preset_bundle.get_preset_name_by_alias(Slic3r::Preset::TYPE_FILAMENT, filament_name);
            if (!canonical.empty()) fil_name = canonical;
        }
        auto *fil_preset = preset_bundle.filaments.find_preset(fil_name, false, false, false);
        if (fil_preset == nullptr) {
            // Fallback: directly import the specific filament JSON from resources, if available.
            if (!g_resources_path.empty()) {
                std::string file = find_profile_file(g_resources_path, fil_name, "filament");
                if (!file.empty()) {
                    try {
                        std::cerr << "DEBUG: fallback import filament from: " << file << std::endl;
                        Slic3r::PresetsConfigSubstitutions subs; int overwrite = 1; std::vector<std::string> out;
                        auto override_confirm = [](std::string const &) -> int { return 1; };
                        bool ok = preset_bundle.import_json_presets(subs, file, override_confirm, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent, overwrite, out);
                        std::cerr << "DEBUG: fallback import filament result ok=" << (ok?"1":"0") << ", out.size=" << out.size() << std::endl;
                        if (ok) {
                            preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            fil_preset = preset_bundle.filaments.find_preset(fil_name, false, false, false);
                        }
                    } catch (const std::exception &e) { std::cerr << "WARN: filament import exception: " << e.what() << std::endl; }
                } else {
                    std::cerr << "DEBUG: fallback import filament: file not found for name='" << fil_name << "' under resources_path='" << g_resources_path << "'" << std::endl;
                }
            }
        }
        if (fil_preset == nullptr) {
            last_error = std::string("Filament profile not found: ") + fil_name;
            return false;
        }
        if (!preset_bundle.filaments.select_preset_by_name(fil_name, true)) {
            last_error = std::string("Failed to select filament preset: ") + fil_name;
            return false;
        }
        preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        out_config = preset_bundle.full_config_secure();
        return true;
    } catch (const std::exception& e) {
        last_error = std::string("Error loading filament profile: ") + e.what();
        return false;
    }
}

bool load_process_profile(const std::string& process_name,
                          Slic3r::PresetBundle& preset_bundle,
                          Slic3r::DynamicPrintConfig& out_config,
                          std::string& last_error)
{
    try {
        const auto &active_printer = preset_bundle.printers.get_selected_preset();
        if (active_printer.name.empty() || active_printer.name == "Default Printer") {
            last_error = "No printer selected before process profile";
            return false;
        }
        std::string proc_name = process_name;
        {
            const std::string &canonical = preset_bundle.get_preset_name_by_alias(Slic3r::Preset::TYPE_PRINT, process_name);
            if (!canonical.empty()) proc_name = canonical;
        }
        auto *proc_preset = preset_bundle.prints.find_preset(proc_name, false, false, false);
        if (proc_preset == nullptr) {
            // Fallback: directly import the specific process JSON from resources, if available.
            if (!g_resources_path.empty()) {
                std::string file = find_profile_file(g_resources_path, proc_name, "process");
                if (!file.empty()) {
                    try {
                        Slic3r::PresetsConfigSubstitutions subs; int overwrite = 1; std::vector<std::string> out;
                        auto override_confirm = [](std::string const &) -> int { return 1; };
                        bool ok = preset_bundle.import_json_presets(subs, file, override_confirm, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent, overwrite, out);
                        if (ok) {
                            preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            proc_preset = preset_bundle.prints.find_preset(proc_name, false, false, false);
                        }
                    } catch (...) {}
                }
            }
        }
        if (proc_preset == nullptr) {
            last_error = std::string("Process profile not found: ") + proc_name;
            return false;
        }
        if (!preset_bundle.prints.select_preset_by_name(proc_name, true)) {
            last_error = std::string("Failed to select process preset: ") + proc_name;
            return false;
        }
        preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
        out_config = preset_bundle.full_config_secure();
        return true;
    } catch (const std::exception& e) {
        last_error = std::string("Error loading process profile: ") + e.what();
        return false;
    }
}

std::vector<std::string> list_printer_profiles(const std::string& resources_path)
{
    std::vector<std::string> profiles;
    try {
        std::string dir = resources_path + "/profiles/BBL/machine";
        if (!std::filesystem::exists(dir)) return profiles;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename.find("common") == std::string::npos && filename.find("fdm_") == std::string::npos) profiles.push_back(filename);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning printer profiles: " << e.what() << std::endl;
    }
    return profiles;
}

std::vector<std::string> list_filament_profiles(const std::string& resources_path)
{
    std::vector<std::string> profiles;
    try {
        std::string dir = resources_path + "/profiles/BBL/filament";
        if (!std::filesystem::exists(dir)) return profiles;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename.find("common") == std::string::npos && filename.find("fdm_") == std::string::npos && filename.find("@base") == std::string::npos) profiles.push_back(filename);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning filament profiles: " << e.what() << std::endl;
    }
    return profiles;
}

std::vector<std::string> list_process_profiles(const std::string& resources_path)
{
    std::vector<std::string> profiles;
    try {
        std::string dir = resources_path + "/profiles/BBL/process";
        if (!std::filesystem::exists(dir)) return profiles;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename.find("common") == std::string::npos && filename.find("fdm_") == std::string::npos) profiles.push_back(filename);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning process profiles: " << e.what() << std::endl;
    }
    return profiles;
}


}} // namespace OrcaSlicerCli::config

#endif // HAVE_LIBSLIC3R

