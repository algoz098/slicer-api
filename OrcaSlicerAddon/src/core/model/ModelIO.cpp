#include "core/model/ModelIO.hpp"
#include "core/util/Utilities.hpp"
#include "utils/Logger.hpp"

#if !HAVE_LIBSLIC3R
#error "libslic3r is required. Placeholders are not allowed."
#endif

#if HAVE_LIBSLIC3R

#include "libslic3r/Model.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/3mf.hpp"

#include <filesystem>
#include <optional>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace OrcaSlicerCli { namespace model {

using OrcaSlicerCli::util::safe_build_config;

bool load_stl(const std::string& filename,
              Slic3r::Model& model,
              std::string& last_error)
{
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
    model.add_object(object_name.c_str(), filename.c_str(), std::move(mesh));
    return true;
}

bool ensure_default_instances(Slic3r::Model& model,
                              std::string& last_error)
{
    // Ensure model has objects
    if (model.objects.empty()) {
        last_error = "No objects found in model file";
        return false;
    }

    // Add default instance if none exists
    for (auto* obj : model.objects) {
        if (obj && obj->instances.empty())
            obj->add_instance();
    }
    return true;
}

bool load_3mf_project(
    const std::string& filename,
    int plate_id,
    Slic3r::Model& out_model,
    Slic3r::DynamicPrintConfig& config,
    Slic3r::PresetBundle& preset_bundle,
    Slic3r::PlateDataPtrs& plate_data_src,
    bool& has_project_embedded_presets,
    std::string& project_printer_preset,
    std::string& project_print_preset,
    std::string& project_filament_preset,
    std::string& plate_printer_model_id,
    std::string& plate_nozzle_variant,
    int& total_plates_count,
    size_t& detected_extruders,
    std::vector<std::string>& saved_filament_colours,
    std::string& saved_change_filament_gcode,
    Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    Slic3r::t_config_option_keys& project_overrides_keys,
    Slic3r::DynamicPrintConfig& print_cfg_overrides,
    Slic3r::t_config_option_keys& print_overrides_keys,
    std::string& last_error)
{
    using namespace Slic3r;

    ConfigSubstitutionContext config_substitutions{ForwardCompatibilitySubstitutionRule::Enable};
    std::vector<Preset*> project_presets;
    bool is_bbl_3mf = false;
    Semver file_version;

    // TODO: verificar se podemos remover esse codigo (lógica de retry)
    // OrcaSlicer GUI (Plater.cpp) não tem retry explícito — ele usa load_bbs_3mf internamente
    // sem plate_id filter e separa os objetos por placa depois. O retry aqui é workaround
    // para arquivos 3MF re-salvos com metadados de placa quebrados; não existe equivalente no GUI.
    // `config` is the engine's long-lived working config: metadata left by a previous
    // project must not leak into this load. A stale different_settings_to_system from
    // another 3MF makes the override detection below pick the wrong key set, dropping
    // this file's customizations (e.g. enable_prime_tower=0) in favor of preset values.
    config.erase("different_settings_to_system");

    // Read model+config from 3MF (per-plate)
    // If loading a specific plate returns empty (broken plate metadata in re-saved 3MFs),
    // retry without plate filter to load all objects — matching OrcaSlicer GUI behavior.
    Model loaded;
    try {
        loaded = Model::read_from_file(
            filename,
            &config,
            &config_substitutions,
            LoadStrategy::LoadModel | LoadStrategy::LoadConfig,
            &plate_data_src,
            &project_presets,
            &is_bbl_3mf,
            &file_version,
            nullptr,
            nullptr,
            nullptr,
            plate_id);
    } catch (const std::exception& e) {
        std::string msg = e.what();
        // Check if this is the "empty" error for a plate-filtered load
        if (plate_id > 0 && (msg.find("empty") != std::string::npos || msg.find("Empty") != std::string::npos)) {
            LOG_WARNING(std::string("WARN: [ModelIO] Plate ") + std::to_string(plate_id)
                      + " returned empty model, retrying without plate filter...");
            // Reset state for retry
            config_substitutions = ConfigSubstitutionContext{ForwardCompatibilitySubstitutionRule::Enable};
            project_presets.clear();
            is_bbl_3mf = false;
            plate_data_src.clear();

            loaded = Model::read_from_file(
                filename,
                &config,
                &config_substitutions,
                LoadStrategy::LoadModel | LoadStrategy::LoadConfig,
                &plate_data_src,
                &project_presets,
                &is_bbl_3mf,
                &file_version,
                nullptr,
                nullptr,
                nullptr,
                0); // plate_id = 0 loads all objects
            LOG_INFO(std::string("INFO: [ModelIO] Retry succeeded, loaded ")
                      + std::to_string(loaded.objects.size()) + " object(s) without plate filter");
        } else {
            last_error = e.what();
            throw;
        }
    }

    // TODO: Implementação correta baseada no arquivo OrcaSlicer src/OrcaSlicer.cpp:1805-1808
    // OrcaSlicer CLI também lê different_settings_to_system para saber quais chaves foram
    // customizadas. A captura do raw_3mf_config antes das operações de preset é necessária
    // para o fallback quando different_settings_to_system está vazio.
    // CRITICAL: Capture the raw config from 3MF BEFORE any preset operations
    // This is needed because when different_settings_to_system is empty,
    // we need to preserve ALL values from project_settings.config as overrides
    DynamicPrintConfig raw_3mf_config;
    try {
        raw_3mf_config.apply(config, /*ignore_nonexistent=*/true);
        LOG_DEBUG(std::string("DEBUG: [ModelIO] Captured raw 3MF config with ") + std::to_string(raw_3mf_config.keys().size()) + " keys");
    } catch (...) {
        LOG_WARNING("WARN: [ModelIO] Failed to capture raw 3MF config");
    }

    // Capture project preset names
    project_printer_preset.clear();
    project_print_preset.clear();
    project_filament_preset.clear();
    for (const auto* pp : project_presets) {
        if (!pp) continue;
        switch (pp->type) {
            case Preset::TYPE_PRINTER:  if (project_printer_preset.empty())  project_printer_preset  = pp->name; break;
            case Preset::TYPE_PRINT:    if (project_print_preset.empty())    project_print_preset    = pp->name; break;
            case Preset::TYPE_FILAMENT: if (project_filament_preset.empty()) project_filament_preset = pp->name; break;
            default: break;
        }
    }
    has_project_embedded_presets = !project_presets.empty();

    // Plate metadata hints
    if (!plate_data_src.empty()) {
        int idx_i = plate_id;
        if (idx_i < 0) idx_i = 0;
        int max_i = int(plate_data_src.size()) - 1;
        if (idx_i > max_i) idx_i = max_i;
        PlateData* pd = plate_data_src[size_t(idx_i)];
        if (pd) {
            plate_printer_model_id = pd->printer_model_id;
            std::string nd = pd->nozzle_diameters;
            auto comma = nd.find(',');
            std::string first = (comma == std::string::npos) ? nd : nd.substr(0, comma);
            auto ltrim = [](std::string &s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); })); };
            auto rtrim = [](std::string &s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end()); };
            ltrim(first); rtrim(first);
            if (!first.empty()) plate_nozzle_variant = first;
        }
        total_plates_count = int(plate_data_src.size());
    }

    // Detect extruders from filament_colour (GUI parity)
    try {
        if (auto* filament_colour = config.opt<ConfigOptionStrings>("filament_colour"))
            detected_extruders = filament_colour->size();
        else
            detected_extruders = 0;
    } catch (...) {
        detected_extruders = 0;
    }

    // Save change_filament_gcode from 3MF (critical for Bambu AMS multi-color printing)
    try {
        if (auto* cfg_gcode = config.opt<ConfigOptionString>("change_filament_gcode")) {
            saved_change_filament_gcode = cfg_gcode->value;
            if (!saved_change_filament_gcode.empty()) {
                LOG_DEBUG(std::string("DEBUG: [ModelIO] Saved change_filament_gcode from 3MF (")
                          + std::to_string(saved_change_filament_gcode.size()) + " chars)");
            }
        }
    } catch (...) {
        // Ignore errors - some 3MFs may not have this field
    }

    // Snapshot overrides and import project config into bundle
    try {
        // Preserve wipe tower before bundle ops
        std::optional<ConfigOptionFloats> file_wipe_tower_x;
        std::optional<ConfigOptionFloats> file_wipe_tower_y;
        if (auto *wt_x = config.opt<ConfigOptionFloats>("wipe_tower_x")) file_wipe_tower_x = *wt_x;
        if (auto *wt_y = config.opt<ConfigOptionFloats>("wipe_tower_y")) file_wipe_tower_y = *wt_y;

        // Load project config into bundle (this applies all 3MF configs)
        preset_bundle.load_config_model(filename, config, file_version);

        // Snapshot project-level overrides into project_cfg_after_3mf (robust per-key copy)
        project_cfg_after_3mf = DynamicPrintConfig();
        try {
            project_cfg_after_3mf.apply(preset_bundle.project_config, /*ignore_nonexistent=*/true);
        } catch (...) {
            try {
                auto keys = preset_bundle.project_config.keys();
                for (const auto& k : keys)
                    if (const auto* opt = preset_bundle.project_config.optptr(k))
                        project_cfg_after_3mf.set_key_value(k, opt->clone());
            } catch (...) {}
        }
        project_overrides_keys = project_cfg_after_3mf.keys();

        // Capture print-level overrides (prefer different_settings_to_system; fallback to raw 3MF config)
        print_overrides_keys.clear();
        try {
            bool found = false;
            if (auto* diff = config.opt<ConfigOptionStrings>("different_settings_to_system")) {
                if (!diff->values.empty() && !diff->values[0].empty()) {
                    std::istringstream ss(diff->values[0]);
                    std::string key;
                    while (std::getline(ss, key, ';')) if (!key.empty()) print_overrides_keys.push_back(key);
                    found = true;
                    LOG_DEBUG(std::string("DEBUG: [ModelIO] Using different_settings_to_system with ") + std::to_string(print_overrides_keys.size()) + " keys");
                }
            }
            if (!found) {
                // CRITICAL FIX: When different_settings_to_system is empty, use ALL keys from raw 3MF config
                // This ensures values like skirt_loops=0 from project_settings.config are preserved
                // even when the 3MF was saved without marking them as "different from system"
                if (!raw_3mf_config.keys().empty()) {
                    print_overrides_keys = raw_3mf_config.keys();
                    LOG_DEBUG(std::string("DEBUG: [ModelIO] Using ALL raw 3MF config keys as overrides (") + std::to_string(print_overrides_keys.size()) + " keys)");
                } else {
                    auto dirty = preset_bundle.prints.current_different_from_parent_options(true);
                    print_overrides_keys.assign(dirty.begin(), dirty.end());
                    LOG_DEBUG(std::string("DEBUG: [ModelIO] Using parent diff with ") + std::to_string(print_overrides_keys.size()) + " keys");
                }
            }
        } catch (...) {}

        // Build print_cfg_overrides from raw 3MF config values (not current config which may have been modified)
        //
        // BACKGROUND: The 3MF format stores configuration in multiple places:
        //   1. project_settings.config - Global project-level settings (stored in Metadata/project_settings.config)
        //   2. slice_info.config - Per-plate settings including plate-specific overrides (stored in Metadata/slice_info.config)
        //
        // The "different_settings_to_system" field in project_settings.config contains keys that differ from
        // the system defaults. However, some plate-specific settings like "spiral_mode" (vase mode) are stored
        // ONLY in slice_info.config (plate_data.config) and NOT in project_settings.config.
        //
        // This caused spiral_mode to be ignored because print_cfg_overrides was built only from
        // project_settings.config, missing the plate-specific settings.
        //
        print_cfg_overrides = DynamicPrintConfig();
        for (const auto& k : print_overrides_keys) {
            // Prefer raw 3MF config value, fallback to current config
            if (const auto* opt = raw_3mf_config.optptr(k)) {
                print_cfg_overrides.set_key_value(k, opt->clone());
                LOG_DEBUG(std::string("  override: ") + k + " (value omitted) (from raw 3MF)");
            } else if (const auto* opt = config.optptr(k)) {
                print_cfg_overrides.set_key_value(k, opt->clone());
                LOG_DEBUG(std::string("  override: ") + k + " (value omitted) (from config)");
            } else {
                LOG_WARNING(std::string("WARN: [ModelIO] Key '") + k + "' not found in raw 3MF config or config");
            }
        }

        // ============================================================================
        // MERGE PLATE-SPECIFIC SETTINGS INTO print_cfg_overrides
        // ============================================================================
        //
        // WHY: OrcaSlicer stores some per-plate settings (like spiral_mode, curr_bed_type, print_sequence)
        //      in plate_data.config (from Metadata/slice_info.config), NOT in project_settings.config.
        //      These settings were being ignored because print_cfg_overrides was built only from
        //      project_settings.config.
        //
        // WHAT: We merge the plate-specific settings from plate_data_src[plate_id].config into
        //       print_cfg_overrides so they are applied along with other process customizations.
        //
        // SAFETY: This is safe because:
        //   - In the API context, we always slice a single specific plate
        //   - The settings in plate_data.config (spiral_mode, curr_bed_type, print_sequence, etc.)
        //     are all relevant to the slicing process
        //   - This maintains the priority order: Profiles -> 3MF customizations -> API custom_settings
        //   - Settings from plate_data.config can still be overridden by API custom_settings later
        //
        if (!plate_data_src.empty()) {
            int idx = plate_id;
            if (idx < 0) idx = 0;
            int max_idx = static_cast<int>(plate_data_src.size()) - 1;
            if (idx > max_idx) idx = max_idx;

            PlateData* pd = plate_data_src[static_cast<size_t>(idx)];
            if (pd && !pd->config.empty()) {
                LOG_DEBUG(std::string("DEBUG: [ModelIO] Merging plate_data.config (plate ") + std::to_string(idx + 1) + ") into print_cfg_overrides");

                // List of known plate-specific settings that should be merged
                // These are settings stored in slice_info.config, not project_settings.config
                static const std::vector<std::string> plate_specific_keys = {
                    "spiral_mode",                      // Vase mode / spiral vase
                    "curr_bed_type",                    // Bed type (PEI, textured, etc.)
                    "print_sequence",                   // Print sequence (by layer, by object)
                    "first_layer_print_sequence",       // First layer print order
                    "other_layers_print_sequence",      // Other layers print order
                    "other_layers_print_sequence_nums"  // Sequence numbers
                };

                for (const auto& key : plate_specific_keys) {
                    if (pd->config.has(key)) {
                        const auto* opt = pd->config.optptr(key);
                        if (opt) {
                            print_cfg_overrides.set_key_value(key, opt->clone());
                            // Track the key if not already in print_overrides_keys
                            if (std::find(print_overrides_keys.begin(), print_overrides_keys.end(), key) == print_overrides_keys.end()) {
                                print_overrides_keys.push_back(key);
                            }
                            LOG_DEBUG(std::string("  override: ") + key + " (value omitted) (from plate_data.config)");
                        }
                    }
                }
            }
        }

        // Use sanitized project snapshot as project_config (before wipe tower restoration)
        preset_bundle.project_config = project_cfg_after_3mf;

        // Restore wipe tower into project_config
        try {
            DynamicConfig &proj_cfg = preset_bundle.project_config;
            if (file_wipe_tower_x) if (auto *opt = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x")) *opt = *file_wipe_tower_x;
            if (file_wipe_tower_y) if (auto *opt = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y")) *opt = *file_wipe_tower_y;
        } catch (...) {}

        // Prefer model-provided wipe tower positions, if any
        try {
            if (!loaded.wipe_tower.positions.empty()) {
                ConfigOptionFloats wtx, wty;
                wtx.values.resize(loaded.wipe_tower.positions.size());
                wty.values.resize(loaded.wipe_tower.positions.size());
                for (size_t i = 0; i < loaded.wipe_tower.positions.size(); ++i) {
                    wtx.values[i] = float(loaded.wipe_tower.positions[i].x());
                    wty.values[i] = float(loaded.wipe_tower.positions[i].y());
                }
                preset_bundle.project_config.set_key_value("wipe_tower_x", new ConfigOptionFloats(wtx));
                preset_bundle.project_config.set_key_value("wipe_tower_y", new ConfigOptionFloats(wty));
                project_cfg_after_3mf.set_key_value("wipe_tower_x", new ConfigOptionFloats(wtx));
                project_cfg_after_3mf.set_key_value("wipe_tower_y", new ConfigOptionFloats(wty));
            }
        } catch (...) {}

        // Build working config robustly using safe_build_config to avoid hangs
        safe_build_config(preset_bundle, config);
    } catch (const std::exception& e) {
        last_error = std::string("Config setup failed: ") + e.what();
        return false;
    } catch (...) {
        last_error = "Config setup failed: unknown error";
        return false;
    }

    // Enable multi-material settings if needed
    if (detected_extruders > 1) {
        config.set_key_value("single_extruder_multi_material", new ConfigOptionBool(true));
        config.set_key_value("enable_prime_tower", new ConfigOptionBool(true));
    }

    // Expand filament arrays to match detected_extruders; save colours for later restore
    if (detected_extruders > 0) {
        auto* fil_diameter = config.opt<ConfigOptionFloats>("filament_diameter", false);
        auto* fil_density  = config.opt<ConfigOptionFloats>("filament_density", false);
        auto* fil_colour   = config.opt<ConfigOptionStrings>("filament_colour", false);
        auto* fil_type     = config.opt<ConfigOptionStrings>("filament_type", false);
        auto* fil_ids      = config.opt<ConfigOptionStrings>("filament_ids", false);

        if (fil_diameter && fil_diameter->values.size() < detected_extruders)
            while (fil_diameter->values.size() < detected_extruders)
                fil_diameter->values.push_back(fil_diameter->values.empty()? 1.75 : fil_diameter->values.back());
        if (fil_density && fil_density->values.size() < detected_extruders)
            while (fil_density->values.size() < detected_extruders)
                fil_density->values.push_back(fil_density->values.empty()? 1.26 : fil_density->values.back());
        if (fil_colour && fil_colour->values.size() < detected_extruders)
            while (fil_colour->values.size() < detected_extruders)
                fil_colour->values.push_back(fil_colour->values.empty()? std::string("#FFFFFF") : fil_colour->values.back());
        if (fil_colour && fil_colour->values.size() >= detected_extruders)
            saved_filament_colours = fil_colour->values;
        if (fil_type && fil_type->values.size() < detected_extruders)
            while (fil_type->values.size() < detected_extruders)
                fil_type->values.push_back(fil_type->values.empty()? std::string("PLA") : fil_type->values.back());
        if (fil_ids && fil_ids->values.size() < detected_extruders)
            while (fil_ids->values.size() < detected_extruders)
                fil_ids->values.push_back(fil_ids->values.empty()? std::string("GFL99") : fil_ids->values.back());
    }

    try {
        has_project_embedded_presets = !project_presets.empty();
        LOG_DEBUG("DEBUG: [ModelIO] About to call load_project_embedded_presets...");
        (void)preset_bundle.load_project_embedded_presets(project_presets, ForwardCompatibilitySubstitutionRule::Enable);
        LOG_DEBUG("DEBUG: [ModelIO] load_project_embedded_presets completed, building config...");
        // Refresh config using safe_build_config to avoid potential hangs
        safe_build_config(preset_bundle, config);
        LOG_DEBUG("DEBUG: [ModelIO] config build completed");
        try {
            if (const ConfigOption *opt = preset_bundle.project_config.optptr("wipe_tower_x"))
                config.set_key_value("wipe_tower_x", opt->clone());
            if (const ConfigOption *opt = preset_bundle.project_config.optptr("wipe_tower_y"))
                config.set_key_value("wipe_tower_y", opt->clone());
        } catch (const std::exception &e) {
            LOG_ERROR(std::string("Failed to load embedded presets: ") + e.what());
        } catch (...) {
            LOG_ERROR("Failed to load embedded presets: unknown error");
        }

    LOG_DEBUG("DEBUG: [ModelIO] About to move loaded model to out_model...");
    // Replace current model with loaded one
    out_model = std::move(loaded);
    LOG_DEBUG("DEBUG: [ModelIO] load_3mf_project returning true");
    return true;
}

}} // namespace OrcaSlicerCli::model

#endif // HAVE_LIBSLIC3R

