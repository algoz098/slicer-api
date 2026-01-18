#pragma once

#include <string>
#include <vector>

namespace Slic3r {
    class Model;
    class DynamicPrintConfig;
    class PresetBundle;
    class DynamicConfig;
    using t_config_option_keys = std::vector<std::string>;
    struct PlateData;
    using PlateDataPtrs = std::vector<PlateData*>;
}

namespace OrcaSlicerCli { namespace model {

// Load an STL mesh into the provided model as a single object.
// Returns true on success, false on failure and sets last_error.
bool load_stl(const std::string& filename,
              Slic3r::Model& model,
              std::string& last_error);

// Ensure the model has at least one object and at least one instance per object.
// Returns true on success, false if model has no objects (sets last_error accordingly).
bool ensure_default_instances(Slic3r::Model& model,
                              std::string& last_error);

// Load a .3mf project into out_model and import its configuration into the provided PresetBundle/config.
// This mirrors GUI behavior, including project preset selection, override capture, and filament array expansion.
// Returns true on success; on failure sets last_error.
bool load_3mf_project(
    const std::string& filename,
    int plate_id,
    Slic3r::Model& out_model,
    Slic3r::DynamicPrintConfig& config,
    Slic3r::PresetBundle& preset_bundle,
    Slic3r::PlateDataPtrs& plate_data_src,
    // Project preset presence and names
    bool& has_project_embedded_presets,
    std::string& project_printer_preset,
    std::string& project_print_preset,
    std::string& project_filament_preset,
    // Plate metadata
    std::string& plate_printer_model_id,
    std::string& plate_nozzle_variant,
    int& total_plates_count,
    // Multi-material & 3MF color snapshot
    size_t& detected_extruders,
    std::vector<std::string>& saved_filament_colours,
    std::string& saved_change_filament_gcode,  // Preserve change_filament_gcode for Bambu AMS
    // Overrides snapshots
    Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    Slic3r::t_config_option_keys& project_overrides_keys,
    Slic3r::DynamicPrintConfig& print_cfg_overrides,
    Slic3r::t_config_option_keys& print_overrides_keys,
    // Transfer flags
    bool transfer_printer_customizations,
    bool transfer_filament_customizations,
    bool transfer_process_customizations,
    bool transfer_project_overrides,
    // Error out
    std::string& last_error);

}} // namespace OrcaSlicerCli::model

