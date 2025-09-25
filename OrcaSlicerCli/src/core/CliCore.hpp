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
class CliCore {
public:
    /**
     * @brief Result structure for operations
     */
    struct OperationResult {
        bool success = false;
        std::string message;
        std::string error_details;

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
        std::string printer_profile;
        std::string filament_profile;
        std::string process_profile;
        int plate_index = 1; // 1-based plate index for .3mf projects (defaults to 1)
        std::map<std::string, std::string> custom_settings;
        bool verbose = false;
        bool dry_run = false;
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
    CliCore();

    /**
     * @brief Destructor
     */
    ~CliCore();

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
     * @brief Load printer profile
     * @param printer_name Name of the printer profile (e.g., "Bambu Lab X1 Carbon")
     * @return Operation result
     */
    OperationResult loadPrinterProfile(const std::string& printer_name);

    /**
     * @brief Load filament profile
     * @param filament_name Name of the filament profile (e.g., "Bambu PLA Basic @BBL X1C")
     * @return Operation result
     */
    OperationResult loadFilamentProfile(const std::string& filament_name);

    /**
     * @brief Load process profile
     * @param process_name Name of the process profile (e.g., "0.20mm Standard @BBL X1C")
     * @return Operation result
     */
    OperationResult loadProcessProfile(const std::string& process_name);

	    /**
	     * @brief Load a vendor's presets into the bundle (lazy, on-demand)
	     * @param vendor_id Vendor identifier as used in resources/profiles (e.g., "BBL", "Flashforge")
	     * @return Operation result
	     */
	    OperationResult loadVendor(const std::string& vendor_id);


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
     * @brief Get version information
     * @return Version string
     */
    static std::string getVersion();

    /**
     * @brief Get build information
     * @return Build information string
     */
    static std::string getBuildInfo();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace OrcaSlicerCli
