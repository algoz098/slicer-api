#include "core/model/ModelIO.hpp"

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
    Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    Slic3r::t_config_option_keys& project_overrides_keys,
    Slic3r::DynamicPrintConfig& print_cfg_overrides,
    Slic3r::t_config_option_keys& print_overrides_keys,
    bool transfer_printer_customizations,
    bool transfer_filament_customizations,
    bool transfer_process_customizations,
    bool /*transfer_project_overrides*/,
    std::string& last_error)
{
    using namespace Slic3r;

    ConfigSubstitutionContext config_substitutions{ForwardCompatibilitySubstitutionRule::Enable};
    std::vector<Preset*> project_presets;
    bool is_bbl_3mf = false;
    Semver file_version;

    // Read model+config from 3MF (per-plate)
    Model loaded = Model::read_from_file(
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

    // Snapshot overrides and import project config into bundle
    try {
        // Preserve wipe tower before bundle ops
        std::optional<ConfigOptionFloats> file_wipe_tower_x;
        std::optional<ConfigOptionFloats> file_wipe_tower_y;
        if (auto *wt_x = config.opt<ConfigOptionFloats>("wipe_tower_x")) file_wipe_tower_x = *wt_x;
        if (auto *wt_y = config.opt<ConfigOptionFloats>("wipe_tower_y")) file_wipe_tower_y = *wt_y;

        // Load project config into bundle
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

        // Capture print-level overrides (prefer different_settings_to_system; fallback to parent diff)
        print_overrides_keys.clear();
        try {
            bool found = false;
            if (auto* diff = config.opt<ConfigOptionStrings>("different_settings_to_system")) {
                if (!diff->values.empty() && !diff->values[0].empty()) {
                    std::istringstream ss(diff->values[0]);
                    std::string key;
                    while (std::getline(ss, key, ';')) if (!key.empty()) print_overrides_keys.push_back(key);
                    found = true;
                }
            }
            if (!found) {
                auto dirty = preset_bundle.prints.current_different_from_parent_options(true);
                print_overrides_keys.assign(dirty.begin(), dirty.end());
            }
        } catch (...) {}

        // Build print_cfg_overrides from current config values
        print_cfg_overrides = DynamicPrintConfig();
        for (const auto& k : print_overrides_keys)
            if (const auto* opt = config.optptr(k)) print_cfg_overrides.set_key_value(k, opt->clone());

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

        // Use sanitized project snapshot as project_config
        preset_bundle.project_config = project_cfg_after_3mf;

        // Build working config robustly
        try {
            config = preset_bundle.full_config_secure();
        } catch (...) {
            DynamicPrintConfig out;
            try { out.apply(preset_bundle.prints.get_edited_preset().config); } catch (...) {}
            try { out.apply(preset_bundle.filaments.default_preset().config); } catch (...) {}
            try { out.apply(preset_bundle.printers.get_edited_preset().config); } catch (...) {}
            try { out.apply(project_cfg_after_3mf, /*ignore_nonexistent=*/true); } catch (...) {}
            config = out;
        }
    } catch (const std::exception& e) {
        // Non-fatal: continue; slice() may enforce policies later
        (void)e;
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

    // Filter and load project-embedded presets according to transfer_* flags
    try {
        if (!transfer_printer_customizations || !transfer_filament_customizations || !transfer_process_customizations) {
            std::vector<Preset*> filtered;
            filtered.reserve(project_presets.size());
            for (Preset* pp : project_presets) {
                if (!pp) continue;
                if (pp->type == Preset::TYPE_PRINTER  && !transfer_printer_customizations)  continue;
                if (pp->type == Preset::TYPE_FILAMENT && !transfer_filament_customizations) continue;
                if (pp->type == Preset::TYPE_PRINT    && !transfer_process_customizations)  continue;
                filtered.push_back(pp);
            }
            project_presets.swap(filtered);
        }
        has_project_embedded_presets = !project_presets.empty();
        (void)preset_bundle.load_project_embedded_presets(project_presets, ForwardCompatibilitySubstitutionRule::Enable);
        // Refresh config and mirror wipe tower positions
        config = preset_bundle.full_config_secure();
        try {
            if (const ConfigOption *opt = preset_bundle.project_config.optptr("wipe_tower_x"))
                config.set_key_value("wipe_tower_x", opt->clone());
            if (const ConfigOption *opt = preset_bundle.project_config.optptr("wipe_tower_y"))
                config.set_key_value("wipe_tower_y", opt->clone());
        } catch (...) {}

        // Re-apply project-level overrides onto working config
        for (const auto &k : project_overrides_keys)
            if (const ConfigOption *opt = project_cfg_after_3mf.optptr(k))
                config.set_key_value(k, opt->clone());
    } catch (...) {}

    // Replace current model with loaded one
    out_model = std::move(loaded);
    return true;
}

}} // namespace OrcaSlicerCli::model

#endif // HAVE_LIBSLIC3R

