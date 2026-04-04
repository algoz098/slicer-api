#pragma once

#include <string>
#include <vector>
#include <functional>
#include <map>

#include "core/AddonCore.hpp" // For AddonCore::OperationResult

// Forward declarations to avoid heavy includes in header
namespace Slic3r { class DynamicPrintConfig; class PresetBundle; class AppConfig; }

namespace OrcaSlicerCli { namespace slice {

// Helper: call performSlicing and package the result into OperationResult
AddonCore::OperationResult slice_and_package(
    const std::function<bool(const std::string& output_path)>& perform_slicing,
    const std::string& output_file,
    const std::vector<std::string>& used_override_keys,
    const std::vector<std::string>& ignored_override_keys,
    const double& last_estimated_time_sec,
    const double& last_filament_used_grams,
    const std::string& last_error);

// Helper: apply project-level overrides (3MF project parameters) with highest priority
void reapply_project_overrides(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    const std::vector<std::string>& keys_to_apply);

// Helper: apply project-level overrides excluding keys already set by API custom_settings
void reapply_project_overrides_excluding(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& project_cfg_after_3mf,
    const std::vector<std::string>& keys_to_apply,
    const std::vector<std::string>& exclude_keys);

// Helper: apply 3MF print-level (dirty) overrides on top of selected profiles
void reapply_print_overrides(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& print_cfg_overrides,
    const std::vector<std::string>& print_override_keys);

// Helper: re-apply print overrides excluding keys already set by options
void reapply_print_overrides_excluding(
    Slic3r::DynamicPrintConfig& working_config,
    const Slic3r::DynamicPrintConfig& print_cfg_overrides,
    const std::vector<std::string>& print_override_keys,
    const std::vector<std::string>& exclude_keys);

// Helper: apply custom_settings (CLI overrides) with key mapping and bed temp aliasing
void apply_custom_settings(
    Slic3r::DynamicPrintConfig* working_config,
    const std::map<std::string, std::string>& custom_settings,
    const std::function<AddonCore::OperationResult(const std::string&, const std::string&)>& set_option,
    std::vector<std::string>& used_override_keys,
    std::vector<std::string>& ignored_override_keys);

}} // namespace OrcaSlicerCli::slice

