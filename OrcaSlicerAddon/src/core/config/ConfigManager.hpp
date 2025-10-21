#pragma once

#include <string>
#include <set>
#include <vector>

namespace Slic3r {
    class DynamicPrintConfig;
    class PresetBundle;
    class AppConfig;
}

namespace OrcaSlicerCli { namespace config {

// Load configuration options from a JSON file into the provided DynamicPrintConfig.
// Returns true on success; on failure sets last_error and returns false.
bool load_json_config(const std::string& file_path,
                      Slic3r::DynamicPrintConfig& config,
                      std::string& last_error);

// Find a profile JSON file under resources/profiles/BBL/<profile_type> by name.
// Returns absolute path string if found, or empty string if not found.
std::string find_profile_file(const std::string& resources_path,
                              const std::string& profile_name,
                              const std::string& profile_type);

// Load a vendor's system presets from resources into the PresetBundle and refresh installed printers.
// Adds the vendor_id into loaded_vendors on success. Returns true on success; on failure sets last_error and returns false.
bool load_vendor_from_resources(const std::string& resources_path,
                                const std::string& vendor_id,
                                Slic3r::PresetBundle& preset_bundle,
                                Slic3r::AppConfig& app_config,
                                std::set<std::string>& loaded_vendors,
                                std::string& last_error);

// Load and select a printer preset by name or path, applying all resolution strategies.
bool load_printer_profile(const std::string& resources_path,
                          const std::string& printer_name,
                          Slic3r::PresetBundle& preset_bundle,
                          Slic3r::AppConfig& app_config,
                          Slic3r::DynamicPrintConfig& out_config,
                          std::string& last_error);

// Load and select a filament preset by name, then refresh working config.
bool load_filament_profile(const std::string& filament_name,
                           Slic3r::PresetBundle& preset_bundle,
                           Slic3r::DynamicPrintConfig& out_config,
                           std::string& last_error);

// Load and select a process (print) preset by name, then refresh working config.
bool load_process_profile(const std::string& process_name,
                          Slic3r::PresetBundle& preset_bundle,
                          Slic3r::DynamicPrintConfig& out_config,
                          std::string& last_error);

// List available profile names under resources.
std::vector<std::string> list_printer_profiles(const std::string& resources_path);
std::vector<std::string> list_filament_profiles(const std::string& resources_path);
std::vector<std::string> list_process_profiles(const std::string& resources_path);

}} // namespace OrcaSlicerCli::config

