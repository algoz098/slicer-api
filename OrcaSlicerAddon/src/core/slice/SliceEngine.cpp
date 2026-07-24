#include "core/slice/SliceEngine.hpp"

#include <algorithm>
#include <set>
#include <sstream>

#include "core/util/Utilities.hpp"
#include "utils/Logger.hpp"

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
    LOG_DEBUG("[TRACE 31] About to call performSlicing()");
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
    // Call the version with empty exclude list
    reapply_project_overrides_excluding(working_config, project_cfg_after_3mf, keys_to_apply, {});
}

void reapply_project_overrides_excluding(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    const std::vector<std::string>& keys_to_apply,
    const std::vector<std::string>& exclude_keys)
{
#if HAVE_LIBSLIC3R
    if (keys_to_apply.empty()) {
        LOG_DEBUG("No project override keys to apply (empty list)");
        return;
    }

    // Build a set of excluded keys for fast lookup
    std::set<std::string> excluded(exclude_keys.begin(), exclude_keys.end());

    // Log which keys will be applied
    {
        std::ostringstream oss;
        oss << "Project override keys to apply: ";
        for (size_t i = 0; i < keys_to_apply.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << keys_to_apply[i];
        }
        LOG_DEBUG(oss.str());
    }
    if (!exclude_keys.empty()) {
        std::ostringstream oss;
        oss << "Project override keys to EXCLUDE (API custom_settings have priority): ";
        for (size_t i = 0; i < exclude_keys.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << exclude_keys[i];
        }
        LOG_DEBUG(oss.str());
    }

    // Apply key by key to identify which key causes type mismatch
    size_t applied = 0;
    size_t skipped = 0;
    for (const auto& key : keys_to_apply) {
        // Skip keys that were provided via API custom_settings (they have highest priority)
        if (excluded.count(key) > 0) {
            LOG_DEBUG(std::string("project_override[") + key + "] SKIPPED (API custom_settings has priority)");
            ++skipped;
            continue;
        }
        try {
            std::vector<std::string> single_key = {key};
            // Log before/after value for debugging
            std::string before_val = "N/A", after_val = "N/A";
            if (const auto* opt = working_config.optptr(key)) before_val = opt->serialize();
            working_config.apply_only(project_cfg_after_3mf, single_key, /*ignore_nonexistent=*/true);
            if (const auto* opt = working_config.optptr(key)) after_val = opt->serialize();
            if (before_val != after_val) {
                LOG_DEBUG(std::string("project_override[") + key + "]: " + before_val + " -> " + after_val);
            }
            ++applied;
        } catch (const std::exception& e) {
            LOG_WARNING(std::string("Skipping project override key '") + key + "': " + e.what());
        }
    }
    LOG_DEBUG(std::string("Re-applied ") + std::to_string(applied) + "/" + std::to_string(keys_to_apply.size()) + " 3MF project override(s) (skipped " + std::to_string(skipped) + " due to API custom_settings priority)");
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
            LOG_WARNING(std::string("Skipping print override key '") + key + "': " + e.what());
        }
    }
    LOG_DEBUG(std::string("Re-applied ") + std::to_string(applied) + "/" + std::to_string(print_override_keys.size()) + " 3MF print override(s) on top of selected profiles");
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
        LOG_DEBUG("Skipped re-applying print overrides because options already set those keys");
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
            LOG_WARNING(std::string("Skipping print override key (excluding) '") + key + "': " + e.what());
        }
    }
    LOG_DEBUG(std::string("Re-applied ") + std::to_string(applied) + "/" + std::to_string(keys.size()) + " print override(s) after project overrides to ensure precedence");
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

    LOG_DEBUG(std::string("apply_custom_settings: Received ") + std::to_string(custom_settings.size()) + " custom settings to apply");
    for (const auto& kv : custom_settings) {
        LOG_DEBUG(std::string("apply_custom_settings: key=") + kv.first + " value=" + kv.second);
    }

    if (!working_config) {
        LOG_DEBUG("apply_custom_settings: working_config is NULL, returning");
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
            if (!alias.empty()) {
                mapped_key = alias;
            } else {
                ignored_override_keys.push_back(key + " (bed type unrecognized, setting may not be applied)");
                continue;
            }
        } else if (key == "bed_temperature") {
            const std::string alias = ::OrcaSlicerCli::util::bed_temp_key_for(bed_type, /*first_layer=*/false);
            if (!alias.empty()) {
                mapped_key = alias;
            } else {
                ignored_override_keys.push_back(key + " (bed type unrecognized, setting may not be applied)");
                continue;
            }
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
        // NOTE: "fan_always_on" is NOT mapped because its boolean semantics
        // ("always on") conflict with reduce_fan_stop_start_freq's integer semantics
        // (minimum seconds between fan stops). Use "reduce_fan_stop_start_freq"
        // directly with an appropriate integer value (e.g., 0 to prevent stopping).

        // If the option does not exist in config, ignore to match previous behavior
        if (!working_config->has(mapped_key.c_str())) {
            ignored_override_keys.push_back(key);
            continue;
        }

        auto res = set_option(mapped_key, mapped_val);
        if (res.success) {
            used_override_keys.push_back(mapped_key);
            LOG_DEBUG(std::string("apply_custom_settings: APPLIED ") + mapped_key + "=" + mapped_val);
        } else {
            ignored_override_keys.push_back(mapped_key);
            LOG_DEBUG(std::string("apply_custom_settings: FAILED to apply ") + mapped_key + "=" + mapped_val + " reason: " + res.message);
        }
    }
    LOG_DEBUG(std::string("apply_custom_settings: Finished. Used=") + std::to_string(used_override_keys.size()) + " Ignored=" + std::to_string(ignored_override_keys.size()));
#else
    (void)working_config; (void)custom_settings; (void)set_option;
    (void)used_override_keys; (void)ignored_override_keys;
#endif
}

}} // namespace OrcaSlicerCli::slice
