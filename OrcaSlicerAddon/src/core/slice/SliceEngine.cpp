#include "core/slice/SliceEngine.hpp"

#include <iostream>
#include <algorithm>

#include "core/util/Utilities.hpp"

#if HAVE_LIBSLIC3R
#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp" // For Slic3r::DynamicPrintConfig definition
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/AppConfig.hpp"
#include <filesystem>
#include <cctype>
#endif

namespace OrcaSlicerCli { namespace slice {

#if HAVE_LIBSLIC3R
// Use the safe_build_config from Utilities
using OrcaSlicerCli::util::safe_build_config;
#endif

AddonCore::OperationResult slice_and_package(
    const std::function<bool(const std::string& output_path)>& perform_slicing,
    const std::string& output_file,
    const std::vector<std::string>& used_override_keys,
    const std::vector<std::string>& ignored_override_keys,
    const double& last_estimated_time_sec,
    const double& last_filament_used_grams,
    const std::string& last_error)
{
    std::cout << "\xF0\x9F\x94\x8D [TRACE 31] About to call performSlicing()" << std::endl;
    if (perform_slicing(output_file)) {
        // Build compact JSON with which overrides were used vs ignored. Consumers (Node addon) may parse this.
        std::string json = "{\"used\":[";
        for (size_t i = 0; i < used_override_keys.size(); ++i) {
            if (i) json += ",";
            json += "\"" + used_override_keys[i] + "\"";
        }
        json += "],\"ignored\":[";
        for (size_t i = 0; i < ignored_override_keys.size(); ++i) {
            if (i) json += ",";
            json += "\"" + ignored_override_keys[i] + "\"";
        }
        json += "]}";

        AddonCore::OperationResult out(true, json);
        out.estimated_time_sec = last_estimated_time_sec;
        out.filament_used_grams = last_filament_used_grams;
        return out;
    } else {
        return AddonCore::OperationResult(false, "Slicing failed", last_error);
    }
}

void reapply_project_overrides(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    const std::vector<std::string>& keys_to_apply)
{
#if HAVE_LIBSLIC3R
    if (keys_to_apply.empty()) {
        std::cout << "DEBUG: No project override keys to apply (empty list)" << std::endl;
        return;
    }
    // Log which keys will be applied
    std::cout << "DEBUG: Project override keys to apply: ";
    for (size_t i = 0; i < keys_to_apply.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << keys_to_apply[i];
    }
    std::cout << std::endl;

    // Apply key by key to identify which key causes type mismatch
    size_t applied = 0;
    for (const auto& key : keys_to_apply) {
        try {
            std::vector<std::string> single_key = {key};
            // Log before/after value for debugging
            std::string before_val = "N/A", after_val = "N/A";
            if (const auto* opt = working_config.optptr(key)) before_val = opt->serialize();
            working_config.apply_only(project_cfg_after_3mf, single_key, /*ignore_nonexistent=*/true);
            if (const auto* opt = working_config.optptr(key)) after_val = opt->serialize();
            if (before_val != after_val) {
                std::cout << "DEBUG: project_override[" << key << "]: " << before_val << " -> " << after_val << std::endl;
            }
            ++applied;
        } catch (const std::exception& e) {
            std::cout << "WARN: Skipping project override key '" << key << "': " << e.what() << std::endl;
        }
    }
    std::cout << "DEBUG: Re-applied " << applied << "/" << keys_to_apply.size() << " 3MF project override(s) on top of selected profiles" << std::endl;
#endif
}

void reapply_print_overrides(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& print_cfg_overrides,
    const std::vector<std::string>& print_override_keys)
{
#if HAVE_LIBSLIC3R
    if (print_override_keys.empty()) return;
    // Apply key by key to identify which key causes type mismatch
    size_t applied = 0;
    for (const auto& key : print_override_keys) {
        try {
            std::vector<std::string> single_key = {key};
            working_config.apply_only(print_cfg_overrides, single_key, /*ignore_nonexistent=*/true);
            ++applied;
        } catch (const std::exception& e) {
            std::cout << "WARN: Skipping print override key '" << key << "': " << e.what() << std::endl;
        }
    }
    std::cout << "DEBUG: Re-applied " << applied << "/" << print_override_keys.size() << " 3MF print override(s) on top of selected profiles" << std::endl;
#endif
}

void reapply_print_overrides_excluding(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& print_cfg_overrides,
    const std::vector<std::string>& print_override_keys,
    const std::vector<std::string>& exclude_keys)
{
#if HAVE_LIBSLIC3R
    if (print_override_keys.empty()) return;
    std::vector<std::string> keys = print_override_keys;
    if (!exclude_keys.empty()) {
        keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const std::string& k){
            return std::find(exclude_keys.begin(), exclude_keys.end(), k) != exclude_keys.end();
        }), keys.end());
    }
    if (keys.empty()) {
        std::cout << "DEBUG: Skipped re-applying print overrides because options already set those keys" << std::endl;
        return;
    }
    // Apply key by key to identify which key causes type mismatch
    size_t applied = 0;
    for (const auto& key : keys) {
        try {
            std::vector<std::string> single_key = {key};
            working_config.apply_only(print_cfg_overrides, single_key, /*ignore_nonexistent=*/true);
            ++applied;
        } catch (const std::exception& e) {
            std::cout << "WARN: Skipping print override key (excluding) '" << key << "': " << e.what() << std::endl;
        }
    }
    std::cout << "DEBUG: Re-applied " << applied << "/" << keys.size() << " print override(s) after project overrides to ensure precedence" << std::endl;
#endif
}



void apply_custom_settings(
    Slic3r::DynamicPrintConfig* working_config,
    const std::map<std::string, std::string>& custom_settings,
    const std::function<AddonCore::OperationResult(const std::string&, const std::string&)>& set_option,
    std::vector<std::string>& used_override_keys,
    std::vector<std::string>& ignored_override_keys)
{
#if HAVE_LIBSLIC3R
    using OrcaSlicerCli::util::dbg_log;

    std::cout << "DEBUG [apply_custom_settings]: Received " << custom_settings.size() << " custom settings to apply" << std::endl;
    for (const auto& kv : custom_settings) {
        std::cout << "DEBUG [apply_custom_settings]: Input key=" << kv.first << " value=" << kv.second << std::endl;
    }

    if (!working_config) {
        std::cout << "DEBUG [apply_custom_settings]: working_config is NULL, returning" << std::endl;
        return;
    }

    // Apply curr_bed_type first if provided
    auto it_bedtype = custom_settings.find("curr_bed_type");
    if (it_bedtype != custom_settings.end()) {
        const auto res = set_option("curr_bed_type", it_bedtype->second);
        if (res.success) used_override_keys.push_back("curr_bed_type");
        else ignored_override_keys.push_back("curr_bed_type");
    }

    // Compute current bed type from config if present
    int bed_type_int = 0;
    if (const auto* o = working_config->optptr("curr_bed_type")) {
        bed_type_int = o->getInt();
    }
    const auto bed_type = static_cast<Slic3r::BedType>(bed_type_int);

    for (const auto& kv : custom_settings) {
        const std::string& key = kv.first;
        const std::string& val = kv.second;
        if (key == "curr_bed_type") continue; // handled above

        std::string mapped_key = key;
        std::string mapped_val = val;

        // Alias: bed temps that depend on bed type and first layer
        if (key == "first_layer_bed_temperature") {
            const std::string alias = ::OrcaSlicerCli::util::bed_temp_key_for(bed_type, /*first_layer=*/true);
            if (!alias.empty()) mapped_key = alias;
        } else if (key == "bed_temperature") {
            const std::string alias = ::OrcaSlicerCli::util::bed_temp_key_for(bed_type, /*first_layer=*/false);
            if (!alias.empty()) mapped_key = alias;
        }

        // Compatibility mappings
        if (key == "perimeters") { mapped_key = "wall_loops"; }
        else if (key == "top_solid_layers") { mapped_key = "top_shell_layers"; }
        else if (key == "bottom_solid_layers") { mapped_key = "bottom_shell_layers"; }
        else if (key == "infill_pattern") { mapped_key = "sparse_infill_pattern"; }
        else if (key == "fill_angle") { mapped_key = "infill_direction"; }
        else if (key == "external_perimeters_first") {
            mapped_key = "wall_sequence";
            // bool -> enum-like string mapping
            if (val == "1" || val == "true" || val == "True") mapped_val = "outer_inner";
            else mapped_val = "inner_outer";
        }
        else if (key == "skirts") { mapped_key = "skirt_loops"; }
        else if (key == "fan_speed") { mapped_key = "overhang_fan_speed"; }
        else if (key == "fan_always_on") { mapped_key = "reduce_fan_stop_start_freq"; }

        // If the option does not exist in config, ignore to match previous behavior
        if (!working_config->has(mapped_key.c_str())) {
            ignored_override_keys.push_back(key);
            continue;
        }

        auto res = set_option(mapped_key, mapped_val);
        if (res.success) {
            used_override_keys.push_back(mapped_key);
            std::cout << "DEBUG [apply_custom_settings]: APPLIED " << mapped_key << "=" << mapped_val << std::endl;
        } else {
            ignored_override_keys.push_back(mapped_key);
            std::cout << "DEBUG [apply_custom_settings]: FAILED to apply " << mapped_key << "=" << mapped_val << " reason: " << res.message << std::endl;
        }
    }
    std::cout << "DEBUG [apply_custom_settings]: Finished. Used=" << used_override_keys.size() << " Ignored=" << ignored_override_keys.size() << std::endl;
#else
    (void)working_config; (void)custom_settings; (void)set_option;
    (void)used_override_keys; (void)ignored_override_keys;
#endif
}





void auto_select_presets_from_3mf(
    const std::string& input_file,
    bool transfer_printer_customizations,
    bool transfer_filament_customizations,
    bool transfer_process_customizations,
    bool has_project_embedded_presets,
    const std::string& project_printer_preset,
    const std::string& project_print_preset,
    const std::string& project_filament_preset,
    const std::string& plate_printer_model_id,
    const std::string& plate_nozzle_variant,
    Slic3r::PresetBundle& preset_bundle,
    Slic3r::AppConfig& app_config,
    Slic3r::DynamicPrintConfig& config,
    const std::function<AddonCore::OperationResult(const std::string&)>& load_printer_by_name,
    const std::function<AddonCore::OperationResult(const std::string&)>& load_filament_by_name,
    const std::function<AddonCore::OperationResult(const std::string&)>& load_process_by_name,
    const std::string& user_printer_profile_name,
    const std::string& user_filament_profile_name,
    const std::string& user_process_profile_name)
{
#if HAVE_LIBSLIC3R
    namespace fs = std::filesystem;
    fs::path p(input_file);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return char(std::tolower(c)); });
    if (ext != ".3mf") return;

    try {
        // Prefer project preset names captured during load
        std::string prn = project_printer_preset;
        std::string pro = project_print_preset;
        std::string fil = project_filament_preset;
        if (prn.empty() && pro.empty() && fil.empty()) {
            if (auto *op = config.option<Slic3r::ConfigOptionString>("printer_settings_id", false)) prn = op->value;
            if (auto *op2 = config.option<Slic3r::ConfigOptionString>("print_settings_id", false)) pro = op2->value;
            if (auto *opf = config.option<Slic3r::ConfigOptionStrings>("filament_settings_id", false)) {
                if (!opf->values.empty()) fil = opf->values.front();
            }
            if (pro.empty() && config.has("default_print_profile")) pro = config.opt_string("default_print_profile");
            // default_filament_profile is coStrings (array), not coString
            if (fil.empty()) {
                if (auto *opdf = config.option<Slic3r::ConfigOptionStrings>("default_filament_profile", false)) {
                    if (!opdf->values.empty()) fil = opdf->values.front();
                }
            }
        }

        const bool user_prn = !user_printer_profile_name.empty();
        const bool user_proc = !user_process_profile_name.empty();
        const bool user_fil = !user_filament_profile_name.empty();
        if (user_prn) prn.clear();
        if (user_proc) pro.clear();
        if (user_fil) fil.clear();

        // CRITICAL: When transfer flags are false, do NOT use 3MF config values for heuristics
        // This ensures the addon uses a clean slate when the caller explicitly disables transfer
        std::string cfg_model   = (transfer_printer_customizations && config.has("printer_model"))   ? config.opt_string("printer_model")   : std::string();
        std::string cfg_variant = (transfer_printer_customizations && config.has("printer_variant")) ? config.opt_string("printer_variant") : std::string();

        // Clear 3MF preset hints when transfer is disabled
        if (!transfer_printer_customizations) {
            prn.clear();
            std::cout << "DEBUG: transfer_printer_customizations=false -> clearing 3MF printer hints" << std::endl;
        }
        if (!transfer_process_customizations) {
            pro.clear();
            std::cout << "DEBUG: transfer_process_customizations=false -> clearing 3MF process hints" << std::endl;
        }
        if (!transfer_filament_customizations) {
            fil.clear();
            std::cout << "DEBUG: transfer_filament_customizations=false -> clearing 3MF filament hints" << std::endl;
        }

        // STRICT: only if 3MF embeds explicit names (not defaults)
        const bool any_cli = user_prn || user_proc || user_fil;
        if (!any_cli) {
            const bool any_project_named = ((!project_printer_preset.empty() && project_printer_preset != "Default Printer") ||
                                            (!project_print_preset.empty()   && project_print_preset   != "Default Setting") ||
                                            (!project_filament_preset.empty()&& project_filament_preset!= "Default Filament"));
            if (any_project_named) {
                bool all_ok = true;
                if (transfer_printer_customizations && !project_printer_preset.empty() && project_printer_preset != "Default Printer")
                    all_ok = all_ok && load_printer_by_name(project_printer_preset).success;
                if (transfer_process_customizations && !project_print_preset.empty() && project_print_preset != "Default Setting")
                    all_ok = all_ok && load_process_by_name(project_print_preset).success;
                if (transfer_filament_customizations && !project_filament_preset.empty() && project_filament_preset != "Default Filament")
                    all_ok = all_ok && load_filament_by_name(project_filament_preset).success;
                if (!all_ok) {
                    std::cout << "ERROR: Failed to apply 3MF embedded preset names strictly" << std::endl;
                    return;
                }
                preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                // Build config manually to avoid potential hangs in full_config_secure()
                try {
                    Slic3r::DynamicPrintConfig out;
                    out.apply(Slic3r::FullPrintConfig::defaults());
                    try { out.apply(preset_bundle.prints.get_edited_preset().config); } catch (...) {}
                    try { out.apply(preset_bundle.filaments.default_preset().config); } catch (...) {}
                    try { out.apply(preset_bundle.printers.get_edited_preset().config); } catch (...) {}
                    try { out.apply(preset_bundle.project_config, /*ignore_nonexistent=*/true); } catch (...) {}
                    config = out;
                } catch (...) {}
                std::cout << "DEBUG: Strict 3MF preset names applied -> printer='"
                          << preset_bundle.printers.get_selected_preset_name()
                          << "', process='" << preset_bundle.prints.get_selected_preset_name()
                          << "', filament='" << (preset_bundle.filament_presets.empty()?std::string():preset_bundle.filament_presets.front())
                          << "'" << std::endl;
                // Clear hints to neuter heuristics
                (void)plate_printer_model_id; (void)plate_nozzle_variant;
                cfg_model.clear(); cfg_variant.clear();
                prn.clear(); pro.clear(); fil.clear();
            }
        }

        if (cfg_model.empty() && config.has("default_print_profile")) {
            std::string dp = config.opt_string("default_print_profile");
            auto pos = dp.find("@BBL ");
            if (pos != std::string::npos) {
                std::string suffix = dp.substr(pos + 5);
                if (!suffix.empty()) cfg_model = std::string("Bambu Lab ") + suffix;
            }
        }

        // Enable visibility for (model,variant)
        if (!cfg_model.empty() && !cfg_variant.empty()) {
            try { app_config.set_variant("BBL", cfg_model, cfg_variant, true); preset_bundle.load_installed_printers(app_config); }
            catch (...) { std::cout << "WARN: Failed to enable model/variant in AppConfig (continuing)" << std::endl; }
        }

        const bool project_has_embedded = has_project_embedded_presets;

        // 1) Select printer - only if transfer_printer_customizations is enabled
        std::string selected_printer_name;
        if (!project_has_embedded && !user_prn && transfer_printer_customizations) {
            if (selected_printer_name.empty() && !plate_printer_model_id.empty() && !plate_nozzle_variant.empty()) {
                const Slic3r::Preset *sys = preset_bundle.printers.find_system_preset_by_model_and_variant(plate_printer_model_id, plate_nozzle_variant);
                if (sys && preset_bundle.printers.select_preset_by_name(sys->name, /*force=*/true)) {
                    selected_printer_name = sys->name;
                    preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                    safe_build_config(preset_bundle, config);
                    std::cout << "DEBUG: Selected printer from plate hints: '" << selected_printer_name << "'" << std::endl;
                }
            }
            if (selected_printer_name.empty() && !cfg_model.empty() && !cfg_variant.empty()) {
                std::string derived_printer = cfg_model + " " + cfg_variant + " nozzle";
                auto r = load_printer_by_name(derived_printer); if (r.success) selected_printer_name = derived_printer;
            }
            if (selected_printer_name.empty() && !prn.empty() && prn != "Default Printer") {
                auto r = load_printer_by_name(prn); if (r.success) selected_printer_name = prn;
            }
            if (selected_printer_name.empty() && !pro.empty() && pro != "Default Setting") {
                const Slic3r::Preset *proc = preset_bundle.prints.find_preset(pro, false, false, false);
                if (proc) {
                    std::string compat_list;
                    if (proc->config.has("print_compatible_printers")) compat_list = proc->config.opt_string("print_compatible_printers");
                    if (!compat_list.empty()) {
                        std::vector<std::string> cands; cands.reserve(8); std::string tok; tok.reserve(64);
                        for (char c : compat_list) { if (c=='\n' || c==';') { if (!tok.empty()) { cands.push_back(tok); tok.clear(); } } else tok.push_back(c); }
                        if (!tok.empty()) cands.push_back(tok);
                        for (auto &cand : cands) {
                            while (!cand.empty() && (cand.front()==' '||cand.front()=='\t')) cand.erase(cand.begin());
                            while (!cand.empty() && (cand.back()==' '||cand.back()=='\t')) cand.pop_back();
                            if (cand.empty()) continue;
                            auto rr = load_printer_by_name(cand); if (rr.success) { selected_printer_name = cand; break; }
                        }
                    }
                }
            }
            if (selected_printer_name.empty() && !cfg_model.empty() && cfg_variant.empty()) {
                for (const auto &p : preset_bundle.printers) {
                    try {
                        std::string m = p.config.has("printer_model") ? p.config.opt_string("printer_model") : std::string();
                        if (m == cfg_model) {
                            if (preset_bundle.printers.select_preset_by_name(p.name, /*force=*/true)) {
                                selected_printer_name = p.name;
                                preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                safe_build_config(preset_bundle, config);
                                std::string v = p.config.has("printer_variant") ? p.config.opt_string("printer_variant") : std::string();
                                if (!v.empty()) { try { app_config.set_variant("BBL", m, v, true); preset_bundle.load_installed_printers(app_config); } catch (...) {} }
                                break;
                            }
                        }
                    } catch (...) {}
                }
            }
        }

        // 2) Filament - only if transfer_filament_customizations is enabled
        std::string selected_filament_name;
        if (!project_has_embedded && !user_fil && transfer_filament_customizations) {
            if (!fil.empty() && fil != "Default Filament") {
                auto r = load_filament_by_name(fil); if (r.success) selected_filament_name = fil;
            }
            if (selected_filament_name.empty() && !project_filament_preset.empty()) {
                auto r = load_filament_by_name(project_filament_preset); if (r.success) selected_filament_name = project_filament_preset;
            }
            if (selected_filament_name.empty() && !cfg_model.empty()) {
                std::string model_suffix = cfg_model; size_t pos = model_suffix.rfind(' '); if (pos != std::string::npos) model_suffix = model_suffix.substr(pos + 1);
                const std::vector<std::string> filament_candidates = { std::string("Bambu PLA Basic @BBL ") + model_suffix, std::string("Bambu PLA Basic") };
                for (const auto &cand : filament_candidates) { auto r = load_filament_by_name(cand); if (r.success) { selected_filament_name = cand; break; } }
            }
        }

        // 3) Process - only if transfer_process_customizations is enabled
        std::string selected_process_name;
        if (!project_has_embedded && !user_proc && transfer_process_customizations) {
            if (!pro.empty() && pro != "Default Setting") {
                const std::string curr_pr = preset_bundle.printers.get_selected_preset_name();
                if (curr_pr.empty() || curr_pr == "Default Printer") {
                    if (preset_bundle.prints.select_preset_by_name(pro, /*force=*/true)) selected_process_name = pro;
                } else {
                    auto r = load_process_by_name(pro); if (r.success) selected_process_name = pro;
                }
            }
            if (selected_process_name.empty() && !selected_printer_name.empty()) {
                std::string model_suffix; if (!cfg_model.empty()) { auto pos = cfg_model.rfind(' '); model_suffix = (pos==std::string::npos)?cfg_model:cfg_model.substr(pos+1); }
                auto prefers = [&](const std::string& name){ bool for_model = model_suffix.empty() ? true : (name.find("@BBL "+model_suffix) != std::string::npos); bool std20 = (name.find("0.20mm Standard") != std::string::npos); return for_model && std20; };
                const std::string &spn = preset_bundle.printers.get_selected_preset().name;
                std::string fallback_name;
                for (const auto &pr : preset_bundle.prints) {
                    bool is_compat = true;
                    if (pr.config.has("print_compatible_printers")) {
                        const std::string &compat_ref = pr.config.opt_string("print_compatible_printers");
                        is_compat = compat_ref.empty() || (compat_ref.find(spn) != std::string::npos);
                    }
                    if (!is_compat) continue;
                    if (prefers(pr.name)) { if (preset_bundle.prints.select_preset_by_name(pr.name, /*force=*/true)) { selected_process_name = pr.name; break; } }
                    if (fallback_name.empty() && pr.name.find("Standard") != std::string::npos) fallback_name = pr.name;
                }
                if (selected_process_name.empty() && !fallback_name.empty()) {
                    if (preset_bundle.prints.select_preset_by_name(fallback_name, /*force=*/true)) selected_process_name = fallback_name;
                }
                if (!selected_process_name.empty()) {
                    preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                    safe_build_config(preset_bundle, config);
                }
            }
        }

        // 3.1) Derive printer from process compatibility
        {
            const std::string curr_pr = preset_bundle.printers.get_selected_preset_name();
            std::string proc_for_compat = !selected_process_name.empty() ? selected_process_name : preset_bundle.prints.get_selected_preset_name();
            if ((curr_pr.empty() || curr_pr == "Default Printer") && !proc_for_compat.empty() && proc_for_compat != "Default Setting") {
                const Slic3r::Preset *proc = preset_bundle.prints.find_preset(proc_for_compat, false, false, false);
                if (proc && proc->config.has("print_compatible_printers")) {
                    std::string compat_list = proc->config.opt_string("print_compatible_printers");
                    if (!compat_list.empty()) {
                        std::vector<std::string> cands; cands.reserve(8); std::string tok;
                        for (char c : compat_list) { if (c=='\n' || c==';') { if (!tok.empty()) { cands.push_back(tok); tok.clear(); } } else tok.push_back(c); }
                        if (!tok.empty()) cands.push_back(tok);
                        std::string selected_from_compat;
                        for (auto &cand : cands) {
                            while (!cand.empty() && (cand.front()==' '||cand.front()=='\t')) cand.erase(cand.begin());
                            while (!cand.empty() && (cand.back()==' '||cand.back()=='\t')) cand.pop_back();
                            if (cand.empty()) continue;
                            auto rr = load_printer_by_name(cand); if (rr.success) { selected_from_compat = cand; break; }
                        }
                        if (!selected_from_compat.empty()) {
                            preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                            safe_build_config(preset_bundle, config);
                            // Re-select the process to keep it after compatibility update
                            if (!proc_for_compat.empty()) {
                                preset_bundle.prints.select_preset_by_name(proc_for_compat, /*force=*/true);
                                preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                                safe_build_config(preset_bundle, config);
                            }
                        }
                    }
                }
            }
        }

        std::cout << "DEBUG: After applying 3MF presets -> selected printer='"
                  << preset_bundle.printers.get_selected_preset_name()
                  << "', print='" << preset_bundle.prints.get_selected_preset_name()
                  << "', filament='" << preset_bundle.filaments.get_selected_preset_name()
                  << "'" << std::endl;

        // Final guard: honor transfer_* and only apply if user did not provide CLI profiles
        {
            const std::string curr_pr = preset_bundle.printers.get_selected_preset_name();
            if (transfer_printer_customizations && (curr_pr.empty() || curr_pr == "Default Printer") && !project_printer_preset.empty()) {
                auto rr = load_printer_by_name(project_printer_preset);
                if (rr.success) {
                    preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
                    safe_build_config(preset_bundle, config);
                    std::cout << "DEBUG: Final-guard selected printer from project preset: '" << project_printer_preset << "'" << std::endl;
                }
            }
            if (transfer_process_customizations && user_process_profile_name.empty() && !project_print_preset.empty()) {
                if (preset_bundle.prints.select_preset_by_name(project_print_preset, /*force=*/true)) {
                    std::cout << "DEBUG: Final-guard selected process from project preset: '" << project_print_preset << "'" << std::endl;
                }
            }
            if (transfer_filament_customizations && user_filament_profile_name.empty() && !project_filament_preset.empty()) {
                if (preset_bundle.filaments.select_preset_by_name(project_filament_preset, /*force=*/true)) {
                    std::cout << "DEBUG: Final-guard selected filament from project preset: '" << project_filament_preset << "'" << std::endl;
                }
            }
            if (!user_filament_profile_name.empty()) {
                std::cout << "DEBUG: Final-guard: Re-ensuring user-provided filament profile: '" << user_filament_profile_name << "'" << std::endl;
                auto r = load_filament_by_name(user_filament_profile_name);
                (void)r;
            }
            preset_bundle.update_compatible(Slic3r::PresetSelectCompatibleType::Always);
            safe_build_config(preset_bundle, config);
        }

    } catch (const std::exception& e) {
        std::cout << "WARN: Failed to apply project presets from 3MF: " << e.what() << std::endl;
    }
#else
    (void)input_file; (void)transfer_printer_customizations; (void)transfer_filament_customizations; (void)transfer_process_customizations;
    (void)has_project_embedded_presets; (void)project_printer_preset; (void)project_print_preset; (void)project_filament_preset;
    (void)plate_printer_model_id; (void)plate_nozzle_variant; (void)preset_bundle; (void)app_config; (void)config;
    (void)load_printer_by_name; (void)load_filament_by_name; (void)load_process_by_name; (void)user_printer_profile_name;
    (void)user_filament_profile_name; (void)user_process_profile_name;
#endif
}

}} // namespace OrcaSlicerCli::slice


