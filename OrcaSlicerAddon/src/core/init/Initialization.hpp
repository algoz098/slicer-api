#pragma once

#include <memory>
#include <set>
#include <string>

namespace Slic3r {
    class AppConfig;
    class PresetBundle;
    class DynamicPrintConfig;
    class Model;
    class Print;
}

namespace OrcaSlicerCli { namespace init {

// Perform libslic3r global initialization, search paths and base objects allocation.
// Returns true on success and sets last_error on failure.
bool initialize_slic3r(const std::string& resources_path,
                       Slic3r::AppConfig& app_config,
                       Slic3r::PresetBundle& preset_bundle,
                       std::set<std::string>& loaded_vendors,
                       std::unique_ptr<Slic3r::DynamicPrintConfig>& config,
                       std::unique_ptr<Slic3r::Model>& model,
                       std::unique_ptr<Slic3r::Print>& print,
                       std::string& last_error);

// Destroy Print, Model, and Config in a safe order to avoid dangling references.
void cleanup(std::unique_ptr<Slic3r::Print>& print,
             std::unique_ptr<Slic3r::Model>& model,
             std::unique_ptr<Slic3r::DynamicPrintConfig>& config);

} } // namespace OrcaSlicerCli::init

