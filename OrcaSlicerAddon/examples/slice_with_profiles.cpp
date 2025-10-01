#include <iostream>
#include <string>
#include <vector>
#include "../src/core/CliCore.hpp"

using namespace OrcaSlicerCli;

void printAvailableProfiles(const CliCore& core) {
    std::cout << "\n=== Available Profiles ===" << std::endl;
    
    // Print available printer profiles
    std::cout << "\nPrinter Profiles:" << std::endl;
    auto printers = core.getAvailablePrinterProfiles();
    for (size_t i = 0; i < std::min(printers.size(), size_t(10)); ++i) {
        std::cout << "  - " << printers[i] << std::endl;
    }
    if (printers.size() > 10) {
        std::cout << "  ... and " << (printers.size() - 10) << " more" << std::endl;
    }
    
    // Print available filament profiles
    std::cout << "\nFilament Profiles (first 10):" << std::endl;
    auto filaments = core.getAvailableFilamentProfiles();
    for (size_t i = 0; i < std::min(filaments.size(), size_t(10)); ++i) {
        std::cout << "  - " << filaments[i] << std::endl;
    }
    if (filaments.size() > 10) {
        std::cout << "  ... and " << (filaments.size() - 10) << " more" << std::endl;
    }
    
    // Print available process profiles
    std::cout << "\nProcess Profiles (first 10):" << std::endl;
    auto processes = core.getAvailableProcessProfiles();
    for (size_t i = 0; i < std::min(processes.size(), size_t(10)); ++i) {
        std::cout << "  - " << processes[i] << std::endl;
    }
    if (processes.size() > 10) {
        std::cout << "  ... and " << (processes.size() - 10) << " more" << std::endl;
    }
}

void printUsage(const std::string& program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --input <file>       Input STL file" << std::endl;
    std::cout << "  --output <file>      Output G-code file" << std::endl;
    std::cout << "  --printer <profile>  Printer profile name" << std::endl;
    std::cout << "  --filament <profile> Filament profile name" << std::endl;
    std::cout << "  --process <profile>  Process profile name" << std::endl;
    std::cout << "  --list-profiles      List available profiles" << std::endl;
    std::cout << "  --help               Show this help message" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << program_name << " --list-profiles" << std::endl;
    std::cout << "  " << program_name << " --input model.stl --output model.gcode \\" << std::endl;
    std::cout << "                      --printer \"Bambu Lab X1 Carbon\" \\" << std::endl;
    std::cout << "                      --filament \"Bambu PLA Basic @BBL X1C\" \\" << std::endl;
    std::cout << "                      --process \"0.20mm Standard @BBL X1C\"" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "OrcaSlicerCli Profile Example" << std::endl;
    std::cout << "=============================" << std::endl;

    // Parse command line arguments
    std::string input_file;
    std::string output_file;
    std::string printer_profile;
    std::string filament_profile;
    std::string process_profile;
    bool list_profiles = false;
    bool show_help = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help") {
            show_help = true;
        } else if (arg == "--list-profiles") {
            list_profiles = true;
        } else if (arg == "--input" && i + 1 < argc) {
            input_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--printer" && i + 1 < argc) {
            printer_profile = argv[++i];
        } else if (arg == "--filament" && i + 1 < argc) {
            filament_profile = argv[++i];
        } else if (arg == "--process" && i + 1 < argc) {
            process_profile = argv[++i];
        }
    }

    if (show_help) {
        printUsage(argv[0]);
        return 0;
    }

    // Initialize CLI Core
    CliCore core;
    auto init_result = core.initialize("../OrcaSlicer/resources");
    if (!init_result.success) {
        std::cerr << "Failed to initialize CLI Core: " << init_result.message << std::endl;
        return 1;
    }

    std::cout << "CLI Core initialized successfully!" << std::endl;

    if (list_profiles) {
        printAvailableProfiles(core);
        return 0;
    }

    if (input_file.empty() || output_file.empty()) {
        std::cerr << "Error: Both input and output files must be specified" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Set up slicing parameters
    CliCore::SlicingParams params;
    params.input_file = input_file;
    params.output_file = output_file;
    params.printer_profile = printer_profile;
    params.filament_profile = filament_profile;
    params.process_profile = process_profile;
    params.verbose = true;

    std::cout << "\nSlicing Parameters:" << std::endl;
    std::cout << "  Input file: " << params.input_file << std::endl;
    std::cout << "  Output file: " << params.output_file << std::endl;
    if (!params.printer_profile.empty()) {
        std::cout << "  Printer profile: " << params.printer_profile << std::endl;
    }
    if (!params.filament_profile.empty()) {
        std::cout << "  Filament profile: " << params.filament_profile << std::endl;
    }
    if (!params.process_profile.empty()) {
        std::cout << "  Process profile: " << params.process_profile << std::endl;
    }

    // Perform slicing
    std::cout << "\nStarting slicing..." << std::endl;
    auto slice_result = core.slice(params);
    
    if (slice_result.success) {
        std::cout << "✓ " << slice_result.message << std::endl;
        return 0;
    } else {
        std::cerr << "✗ Slicing failed: " << slice_result.message << std::endl;
        if (!slice_result.error_details.empty()) {
            std::cerr << "  Details: " << slice_result.error_details << std::endl;
        }
        return 1;
    }
}
