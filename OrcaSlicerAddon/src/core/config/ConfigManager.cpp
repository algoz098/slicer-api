#include "core/config/ConfigManager.hpp"
#include "core/util/Utilities.hpp"
#include "utils/Logger.hpp"

#include <filesystem>
#include <system_error>

#if !HAVE_LIBSLIC3R
#error "libslic3r is required. Placeholders are not allowed."
#endif

#if HAVE_LIBSLIC3R

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Utils.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <set>


namespace OrcaSlicerCli { namespace config {

using OrcaSlicerCli::util::safe_build_config;

// Keep minimal shared state about resources and vendors loaded within this module.
static std::string g_resources_path;
static std::set<std::string> g_loaded_vendors;

bool load_json_config(const std::string& file_path,
                      Slic3r::DynamicPrintConfig& config,
                      std::string& last_error)
{
    try {
        std::error_code ec;
        if (!std::filesystem::exists(file_path, ec) || ec) {
            last_error = std::string("Profile file not found: ") + file_path;
            return false;
        }
        Slic3r::ConfigSubstitutions subs = config.load(file_path, Slic3r::ForwardCompatibilitySubstitutionRule::Enable);
        LOG_DEBUG("DEBUG: Loaded profile from " + file_path + " with " + std::to_string(subs.size()) + " substitutions");
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
    std::error_code ec;
    if (fs::exists(exact_path, ec) && !ec) return exact_path;

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
        LOG_ERROR(std::string("Error searching for profile: ") + e.what());
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
        std::error_code ec;
        if (!fs::exists(res_profiles, ec) || ec) {
            last_error = std::string("Resources profiles directory not found: ") + res_profiles.string();
            return false;
        }
        // Remember the resources path for later direct-import fallbacks.
        g_resources_path = resources_path;
        LOG_DEBUG("DEBUG: loadVendor from '" + res_profiles.string() + "' vendor='" + vendor_id + "'");

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
            LOG_DEBUG(std::string("DEBUG: load_vendor_configs_from_json threw: ") + last_error);
        }

        // If vendor loading threw before reaching machine presets (common due to a bad filament/process),
        // attempt a resilient fallback: load just the vendor profile (models/variants), then import machine presets directly.
        if (!vendor_ok || preset_bundle.printers.has_defaults_only()) {
            try {
                // Load only vendor profile (no presets) to populate vendors map and models/variants.
                preset_bundle.load_vendor_configs_from_json(res_profiles.string(), vendor_id, Slic3r::PresetBundle::LoadVendorOnly, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
                LOG_DEBUG(std::string("DEBUG: fallback -> loaded vendor profile only for '") + vendor_id + "'");
            } catch (const std::exception &e) {
                LOG_DEBUG(std::string("DEBUG: fallback -> LoadVendorOnly threw: ") + e.what());
            }

            try {
                namespace fs = std::filesystem;
                fs::path vendor_root = fs::path(res_profiles) / (vendor_id + std::string(".json"));
                nlohmann::json jroot;
                std::ifstream ifs(vendor_root);
                if (ifs.good()) {
                    ifs >> jroot;
                    if (jroot.contains("machine_list") && jroot["machine_list"].is_array()) {
                        LOG_DEBUG(std::string("DEBUG: fallback -> machine_list size=") + std::to_string(jroot["machine_list"].size()));

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
                            std::error_code ec;
                            if (!fs::exists(f, ec) || ec) continue;
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
                                LOG_DEBUG(std::string("DEBUG: fallback -> prepared base machine '") + name + "'");
                            } catch (const std::exception &e) {
                                LOG_DEBUG(std::string("DEBUG: fallback -> base prepare threw for '") + f.filename().string() + "': " + e.what());
                            }
                        }

                        // Helper to import one concrete machine preset using PresetCollection::load_preset like vendor loader does.
                        auto import_one = [&](const fs::path &f, const std::string &sub_path){
                            std::error_code ec;
                            if (!fs::exists(f, ec) || ec) return false;
                            try {
                                LOG_DEBUG(std::string("DEBUG: fallback -> import_one begin file='") + f.string() + "'");
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
                                Slic3r::Preset &loaded = preset_bundle.printers.load_preset(file_path.string(), preset_name, std::move(merged), /*select=*/false, vendor_ver);
                                loaded.is_system = true;
                                if (itv != preset_bundle.vendors.end()) {
                                    loaded.vendor  = &itv->second;
                                    loaded.version = itv->second.config_version;
                                }
                                LOG_DEBUG(std::string("DEBUG: fallback -> loaded(machine) '") + preset_name + "' as system; printers.size=" + std::to_string(preset_bundle.printers.size()));
                                return true;
                            } catch (const std::exception &e) {
                                LOG_DEBUG(std::string("DEBUG: fallback -> import threw for '") + f.filename().string() + "': " + e.what());
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
                        LOG_DEBUG(std::string("DEBUG: fallback -> concrete machines planned=") + std::to_string(subpaths.size()));
                        for (const auto &sub : subpaths) {
                            fs::path f = fs::path(res_profiles) / vendor_id / sub;
                            LOG_DEBUG(std::string("DEBUG: fallback -> importing machine sub_path='") + sub + "'");
                            import_one(f, sub);
                        }
                    } else {
                        LOG_DEBUG("DEBUG: fallback -> no machine_list array in vendor json");
                    }
                } else {
                    LOG_DEBUG(std::string("DEBUG: fallback -> failed to open vendor json: ") + vendor_root.string());
                }
            } catch (const std::exception &e) {
                LOG_DEBUG(std::string("DEBUG: fallback -> resilient import phase threw: ") + e.what());
            }
        }

        try { preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always); } catch (...) {}
        try { preset_bundle.load_installed_printers(app_config); } catch (...) {}

        // Debug counts after vendor load + fallback
        try {
            LOG_DEBUG(std::string("DEBUG: post-vendor load counts => printers:") + std::to_string(preset_bundle.printers.size())
                      + " only_defaults:" + (preset_bundle.printers.has_defaults_only()?"1":"0"));
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
                        std::error_code ec;
                        if (!fs::exists(cand, ec) || ec) continue;
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
                    std::error_code ec;
                    if (!fs::exists(candidate, ec) || ec) return false;
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
                        std::error_code ec;
                        if (fs::exists(machines_dir, ec) && !ec && fs::is_directory(machines_dir)) {
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
            LOG_DEBUG(std::string("DEBUG: printers.size=") + std::to_string(n) + ", has_defaults_only=" + (only_def?"1":"0"));
            // Try a few alternative lookups just in case
            if (!preset) {
                auto *pp = preset_bundle.printers.find_preset(printer_name, true, true, false);
                if (pp) LOG_DEBUG(std::string("DEBUG: find_preset(printer_name,first_visible=1) succeeded: ") + pp->name);
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
        safe_build_config(preset_bundle, out_config);
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
                        LOG_DEBUG(std::string("DEBUG: fallback import filament from: ") + file);
                        Slic3r::PresetsConfigSubstitutions subs; int overwrite = 1; std::vector<std::string> out;
                        auto override_confirm = [](std::string const &) -> int { return 1; };
                        bool ok = preset_bundle.import_json_presets(subs, file, override_confirm, Slic3r::ForwardCompatibilitySubstitutionRule::EnableSystemSilent, overwrite, out);
                        LOG_DEBUG(std::string("DEBUG: fallback import filament result ok=") + (ok?"1":"0") + ", out.size=" + std::to_string(out.size()));
                        if (ok) {
                            preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            fil_preset = preset_bundle.filaments.find_preset(fil_name, false, false, false);
                        }
                    } catch (const std::exception &e) { LOG_WARNING(std::string("WARN: filament import exception: ") + e.what()); }
                } else {
                    LOG_DEBUG(std::string("DEBUG: fallback import filament: file not found for name='") + fil_name + "' under resources_path='" + g_resources_path + "'");
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
        safe_build_config(preset_bundle, out_config);
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
        safe_build_config(preset_bundle, out_config);
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
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) return profiles;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename.find("common") == std::string::npos && filename.find("fdm_") == std::string::npos) profiles.push_back(filename);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Error scanning printer profiles: ") + e.what());
    }
    return profiles;
}

std::vector<std::string> list_filament_profiles(const std::string& resources_path)
{
    std::vector<std::string> profiles;
    try {
        std::string dir = resources_path + "/profiles/BBL/filament";
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) return profiles;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename.find("common") == std::string::npos && filename.find("fdm_") == std::string::npos && filename.find("@base") == std::string::npos) profiles.push_back(filename);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Error scanning filament profiles: ") + e.what());
    }
    return profiles;
}

std::vector<std::string> list_process_profiles(const std::string& resources_path)
{
    std::vector<std::string> profiles;
    try {
        std::string dir = resources_path + "/profiles/BBL/process";
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) return profiles;
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filename = entry.path().stem().string();
                if (filename.find("common") == std::string::npos && filename.find("fdm_") == std::string::npos) profiles.push_back(filename);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("Error scanning process profiles: ") + e.what());
    }
    return profiles;
}


bool apply_generic_fallback_config(Slic3r::DynamicPrintConfig& config,
                                   const std::string& resources_path)
{
    LOG_DEBUG("DEBUG: Applying generic fallback config for on-the-fly slicing...");

    try {
        // CRITICAL: Preserve 3MF process settings BEFORE applying fallback defaults
        // These settings are essential for special modes like vase/spiral mode
        // and should NOT be overwritten by fallback defaults
        struct PreservedSetting {
            std::string key;
            std::unique_ptr<Slic3r::ConfigOption> value;
        };
        std::vector<PreservedSetting> preserved_settings;

        // List of keys to preserve from 3MF (process-level settings that affect slicing behavior)
        const std::vector<std::string> keys_to_preserve = {
            "spiral_mode",
            "spiral_mode_smooth",
            "spiral_mode_max_xy_smoothing",
            "wall_loops",
            "top_shell_layers",
            "bottom_shell_layers",
            "sparse_infill_density",
            "sparse_infill_pattern",
            "enable_support",
            "layer_height",
            "initial_layer_print_height",
            "seam_position",
            "ironing_type",
            "detect_thin_wall",
        };

        // Save current values from 3MF config
        for (const auto& key : keys_to_preserve) {
            if (const auto* opt = config.optptr(key)) {
                preserved_settings.push_back({key, std::unique_ptr<Slic3r::ConfigOption>(opt->clone())});
            }
        }

        if (!preserved_settings.empty()) {
            LOG_DEBUG("DEBUG: Preserving " + std::to_string(preserved_settings.size()) + " 3MF process settings before fallback");
        }

        // Set minimal printer configuration for generic slicing
        // These values are based on fdm_machine_common.json from OrcaSlicer

        // Printer basics
        config.set_key_value("gcode_flavor", new Slic3r::ConfigOptionEnum<Slic3r::GCodeFlavor>(Slic3r::gcfMarlinLegacy));
        config.set_key_value("printer_technology", new Slic3r::ConfigOptionEnum<Slic3r::PrinterTechnology>(Slic3r::ptFFF));

        // Bed and printable area - default 256x256x256 (common for modern printers)
        // ConfigOptionPoints uses Vec2d (double coordinates in mm), not scaled Points
        std::vector<Slic3r::Vec2d> printable_points;
        printable_points.push_back(Slic3r::Vec2d(0, 0));
        printable_points.push_back(Slic3r::Vec2d(256, 0));
        printable_points.push_back(Slic3r::Vec2d(256, 256));
        printable_points.push_back(Slic3r::Vec2d(0, 256));
        config.set_key_value("printable_area", new Slic3r::ConfigOptionPoints(printable_points));
        config.set_key_value("printable_height", new Slic3r::ConfigOptionFloat(256.0));

        // Nozzle
        Slic3r::ConfigOptionFloats* nozzle_dia = new Slic3r::ConfigOptionFloats();
        nozzle_dia->values.push_back(0.4);
        config.set_key_value("nozzle_diameter", nozzle_dia);

        // Retraction defaults
        Slic3r::ConfigOptionFloats* retract_len = new Slic3r::ConfigOptionFloats();
        retract_len->values.push_back(0.8);
        config.set_key_value("retraction_length", retract_len);

        Slic3r::ConfigOptionFloats* retract_speed = new Slic3r::ConfigOptionFloats();
        retract_speed->values.push_back(30);
        config.set_key_value("retraction_speed", retract_speed);

        Slic3r::ConfigOptionFloats* deretract_speed = new Slic3r::ConfigOptionFloats();
        deretract_speed->values.push_back(30);
        config.set_key_value("deretraction_speed", deretract_speed);

        Slic3r::ConfigOptionFloats* z_hop = new Slic3r::ConfigOptionFloats();
        z_hop->values.push_back(0.4);
        config.set_key_value("z_hop", z_hop);

        // Filament basics
        Slic3r::ConfigOptionStrings* fil_type = new Slic3r::ConfigOptionStrings();
        fil_type->values.push_back("PLA");
        config.set_key_value("filament_type", fil_type);

        Slic3r::ConfigOptionFloats* fil_dia = new Slic3r::ConfigOptionFloats();
        fil_dia->values.push_back(1.75);
        config.set_key_value("filament_diameter", fil_dia);

        Slic3r::ConfigOptionInts* nozzle_temp = new Slic3r::ConfigOptionInts();
        nozzle_temp->values.push_back(220);
        config.set_key_value("nozzle_temperature", nozzle_temp);

        Slic3r::ConfigOptionInts* nozzle_temp_init = new Slic3r::ConfigOptionInts();
        nozzle_temp_init->values.push_back(220);
        config.set_key_value("nozzle_temperature_initial_layer", nozzle_temp_init);

        Slic3r::ConfigOptionInts* bed_temp = new Slic3r::ConfigOptionInts();
        bed_temp->values.push_back(60);
        config.set_key_value("hot_plate_temp", bed_temp);

        Slic3r::ConfigOptionInts* bed_temp_init = new Slic3r::ConfigOptionInts();
        bed_temp_init->values.push_back(60);
        config.set_key_value("hot_plate_temp_initial_layer", bed_temp_init);

        // Process/print defaults
        config.set_key_value("layer_height", new Slic3r::ConfigOptionFloat(0.2));
        config.set_key_value("initial_layer_print_height", new Slic3r::ConfigOptionFloat(0.2));
        config.set_key_value("line_width", new Slic3r::ConfigOptionFloatOrPercent(0.42, false));
        config.set_key_value("wall_loops", new Slic3r::ConfigOptionInt(3));
        config.set_key_value("top_shell_layers", new Slic3r::ConfigOptionInt(4));
        config.set_key_value("bottom_shell_layers", new Slic3r::ConfigOptionInt(4));
        config.set_key_value("sparse_infill_density", new Slic3r::ConfigOptionPercent(15));

        // Speeds
        config.set_key_value("outer_wall_speed", new Slic3r::ConfigOptionFloat(200));
        config.set_key_value("inner_wall_speed", new Slic3r::ConfigOptionFloat(300));
        config.set_key_value("sparse_infill_speed", new Slic3r::ConfigOptionFloat(270));
        config.set_key_value("travel_speed", new Slic3r::ConfigOptionFloat(400));
        config.set_key_value("initial_layer_speed", new Slic3r::ConfigOptionFloat(50));

        // Acceleration
        config.set_key_value("default_acceleration", new Slic3r::ConfigOptionFloatOrPercent(10000, false));
        config.set_key_value("travel_acceleration", new Slic3r::ConfigOptionFloatOrPercent(10000, false));

        // Machine limits
        Slic3r::ConfigOptionFloats* max_speed_x = new Slic3r::ConfigOptionFloats();
        max_speed_x->values.push_back(500);
        max_speed_x->values.push_back(200);
        config.set_key_value("machine_max_speed_x", max_speed_x);

        Slic3r::ConfigOptionFloats* max_speed_y = new Slic3r::ConfigOptionFloats();
        max_speed_y->values.push_back(500);
        max_speed_y->values.push_back(200);
        config.set_key_value("machine_max_speed_y", max_speed_y);

        Slic3r::ConfigOptionFloats* max_speed_z = new Slic3r::ConfigOptionFloats();
        max_speed_z->values.push_back(20);
        max_speed_z->values.push_back(20);
        config.set_key_value("machine_max_speed_z", max_speed_z);

        Slic3r::ConfigOptionFloats* max_accel_x = new Slic3r::ConfigOptionFloats();
        max_accel_x->values.push_back(20000);
        max_accel_x->values.push_back(20000);
        config.set_key_value("machine_max_acceleration_x", max_accel_x);

        Slic3r::ConfigOptionFloats* max_accel_y = new Slic3r::ConfigOptionFloats();
        max_accel_y->values.push_back(20000);
        max_accel_y->values.push_back(20000);
        config.set_key_value("machine_max_acceleration_y", max_accel_y);

        Slic3r::ConfigOptionFloats* max_accel_z = new Slic3r::ConfigOptionFloats();
        max_accel_z->values.push_back(500);
        max_accel_z->values.push_back(200);
        config.set_key_value("machine_max_acceleration_z", max_accel_z);

        // G-code placeholders (minimal)
        config.set_key_value("machine_start_gcode", new Slic3r::ConfigOptionString(
            "G28 ; Home\n"
            "G1 Z5 F3000 ; Lift nozzle\n"
            "M104 S[nozzle_temperature_initial_layer] ; Set nozzle temp\n"
            "M140 S[hot_plate_temp_initial_layer] ; Set bed temp\n"
            "M190 S[hot_plate_temp_initial_layer] ; Wait for bed\n"
            "M109 S[nozzle_temperature_initial_layer] ; Wait for nozzle\n"
            "G92 E0 ; Reset extruder\n"
        ));
        config.set_key_value("machine_end_gcode", new Slic3r::ConfigOptionString(
            "G91 ; Relative positioning\n"
            "G1 E-2 F2700 ; Retract\n"
            "G1 Z10 F3000 ; Lift nozzle\n"
            "G90 ; Absolute positioning\n"
            "G1 X0 Y200 F6000 ; Move to front\n"
            "M104 S0 ; Turn off nozzle\n"
            "M140 S0 ; Turn off bed\n"
            "M84 ; Disable motors\n"
        ));

        // CRITICAL: Restore preserved 3MF process settings AFTER applying fallback defaults
        // This ensures that special modes like vase/spiral mode are honored
        for (auto& ps : preserved_settings) {
            if (ps.value) {
                LOG_DEBUG("DEBUG: Restored 3MF setting: " + ps.key + " = " + ps.value->serialize());
                config.set_key_value(ps.key, ps.value.release());
            }
        }

        LOG_DEBUG("DEBUG: Generic fallback config applied successfully");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR(std::string("ERROR: Failed to apply generic fallback config: ") + e.what());
        return false;
    }
}

}} // namespace OrcaSlicerCli::config

#endif // HAVE_LIBSLIC3R

