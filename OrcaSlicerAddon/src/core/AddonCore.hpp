#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>

// Forward declarations for OrcaSlicer types
namespace Slic3r {
    class Model;
    class Print;
    class PrintConfig;
    class DynamicPrintConfig;
    class FullPrintConfig;
}

namespace OrcaSlicerCli {

/**
 * @brief Core class that provides high-level interface to OrcaSlicer functionality
 *
 * This class encapsulates the main OrcaSlicer engine and provides a simplified
 * interface for CLI operations like slicing, configuration management, and
 * file operations.
 */
class AddonCore {
public:
    /**
     * @brief Result structure for operations
     */
    struct OperationResult {
        bool success = false;
        std::string message;
        std::string error_details;
        // Native engine statistics (if available)
        double estimated_time_sec = -1.0;
        double filament_used_grams = -1.0;

        OperationResult() = default;
        OperationResult(bool success, const std::string& message = "", const std::string& error_details = "")
            : success(success), message(message), error_details(error_details) {}
    };

    /**
     * @brief Slicing parameters structure
     */
    struct SlicingParams {
        std::string input_file;
        std::string output_file;
        std::string config_file;
        std::string preset_name;
        int plate_index = 1; // 1-based plate index for .3mf projects (defaults to 1)
        std::map<std::string, std::string> profile_settings; // Base profile (applied before 3MF; 3MF overrides these)
        std::map<std::string, std::string> custom_settings; // Explicit user overrides (applied after 3MF; highest priority)
        bool verbose = false;
        bool dry_run = false;
        // Behavior flags
        bool center_on_bed = false;
        bool auto_realign_if_needed = false; // Realinha automaticamente na mesa caso itens estejam fora da area
    };

    /**
     * @brief Model information structure
     */
    struct ModelInfo {
        std::string filename;
        size_t object_count = 0;
        size_t triangle_count = 0;
        double volume = 0.0;
        std::string bounding_box;
        bool is_valid = false;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

public:
    /**
     * @brief Constructor
     */
    AddonCore();

    /**
     * @brief Destructor
     */
    ~AddonCore();

    /**
     * @brief Initialize the CLI core with OrcaSlicer components
     * @param resources_path Path to OrcaSlicer resources directory
     * @return Operation result
     */
    OperationResult initialize(const std::string& resources_path = "");

    /**
     * @brief Shutdown and cleanup resources
     */
    void shutdown();

    /**
     * @brief Check if the core is initialized
     * @return True if initialized
     */
    bool isInitialized() const;

    /**
     * @brief Load a 3D model file
     * @param filename Path to the model file (STL, 3MF, OBJ, etc.)
     * @return Operation result
     */
    OperationResult loadModel(const std::string& filename);

    /**
     * @brief Get information about the currently loaded model
     * @return Model information structure
     */
    ModelInfo getModelInfo() const;

    /**
     * @brief Perform slicing operation
     * @param params Slicing parameters
     * @return Operation result
     */
    OperationResult slice(const SlicingParams& params);

    /**
     * @brief Load configuration from file
     * @param config_file Path to configuration file
     * @return Operation result
     */
    OperationResult loadConfig(const std::string& config_file);

    /**
     * @brief Load preset configuration
     * @param preset_name Name of the preset
     * @return Operation result
     */
    OperationResult loadPreset(const std::string& preset_name);

    /**
     * @brief Set a configuration option
     * @param key Configuration key
     * @param value Configuration value
     * @return Operation result
     */
    OperationResult setConfigOption(const std::string& key, const std::string& value);

    /**
     * @brief Get a configuration option value
     * @param key Configuration key
     * @return Configuration value or empty string if not found
     */
    std::string getConfigOption(const std::string& key) const;

    /**
     * @brief Get list of available presets
     * @return Vector of preset names
     */
    std::vector<std::string> getAvailablePresets() const;

    /**
     * @brief Get list of available printer profiles
     * @return Vector of printer profile names
     */
    std::vector<std::string> getAvailablePrinterProfiles() const;

    /**
     * @brief Get list of available filament profiles
     * @return Vector of filament profile names
     */
    std::vector<std::string> getAvailableFilamentProfiles() const;

    /**
     * @brief Get list of available process profiles
     * @return Vector of process profile names
     */
    std::vector<std::string> getAvailableProcessProfiles() const;

    /**
     * @brief Validate model file without loading
     * @param filename Path to the model file
     * @return Model information with validation results
     */
    ModelInfo validateModel(const std::string& filename) const;

    /**
     * @brief Load a vendor's system presets from resources
     * @param vendor_id Vendor identifier (e.g. "BBL")
     * @return Operation result
     */
    OperationResult loadVendor(const std::string& vendor_id);

    /**
     * @brief Get version information
     * @return Version string
     */
    static std::string getVersion();

    /**
     * @brief Get build information
     * @return Build information string
     */
    static std::string getBuildInfo();

    // Global logging control: suppress or restore stdout/stderr without touching orcaslicer/ sources
    static void setLoggingSilenced(bool silent);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace OrcaSlicerCli
