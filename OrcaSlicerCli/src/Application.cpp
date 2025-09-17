#include "Application.hpp"
#include "utils/Logger.hpp"

#include <iostream>
#include <filesystem>

namespace OrcaSlicerCli {

Application::Application()
    : m_core(std::make_unique<CliCore>())
    , m_parser(std::make_unique<ArgumentParser>(getAppName(), "Extended CLI for OrcaSlicer")) {
}

Application::~Application() = default; // Rely on OS cleanup to avoid destructor order issues in libslic3r


int Application::run(int argc, char* argv[]) {
    try {
        // Flush std::cout automatically so DEBUG lines show without buffering
        std::cout.setf(std::ios::unitbuf);

        // Setup argument parser
        setupArgumentParser();

        // Parse arguments
        auto parse_result = m_parser->parse(argc, argv);

        if (!parse_result.success) {
            std::cerr << "Error: " << parse_result.error_message << std::endl;
            std::cerr << "Use --help for usage information." << std::endl;
            return ErrorHandler::errorCodeToExitCode(ErrorCode::InvalidArguments);
        }

        // Setup logging based on arguments
        setupLogging(parse_result);

        // Handle help command specially
        if (parse_result.command == "help" || parse_result.hasArgument("help")) {
            return handleHelpCommand(parse_result);
        }

        // Initialize application
        if (!initialize()) {
            return ErrorHandler::errorCodeToExitCode(ErrorCode::InitializationError);
        }

        // Execute command
        int _exit_code = executeCommand(parse_result.command, parse_result);
        // Ensure orderly shutdown of libslic3r objects to avoid destructor-order segfaults
        m_core->shutdown();
        return _exit_code;

    } catch (const CliException& e) {
        ErrorHandler::handleError(e);
        return ErrorHandler::errorCodeToExitCode(e.getCode());
    } catch (const std::exception& e) {
        ErrorHandler::handleError(ErrorCode::InternalError, "Unexpected error", e.what());
        return ErrorHandler::errorCodeToExitCode(ErrorCode::InternalError);
    } catch (...) {
        ErrorHandler::handleError(ErrorCode::UnknownError, "Unknown error occurred");
        return ErrorHandler::errorCodeToExitCode(ErrorCode::UnknownError);
    }
}

bool Application::initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO("Initializing OrcaSlicerCli...");

    // Find OrcaSlicer resources directory
    std::string resources_path;

    // Try common locations (relative to current working dir and repo layout)
    std::vector<std::string> search_paths = {
        "OrcaSlicer/resources",
        "./OrcaSlicer/resources",
        "../OrcaSlicer/resources",
        "../../OrcaSlicer/resources",
        "/usr/share/orcaslicer/resources",
        "/usr/local/share/orcaslicer/resources"
    };

    for (const auto& path : search_paths) {
        if (std::filesystem::exists(path)) {
            resources_path = path;
            break;
        }
    }

    // Initialize core
    auto result = m_core->initialize(resources_path);
    if (!result.success) {
        LOG_ERROR("Failed to initialize CLI core: " + result.message);
        if (!result.error_details.empty()) {
            LOG_DEBUG("Details: " + result.error_details);
        }
        return false;
    }

    LOG_INFO("OrcaSlicerCli initialized successfully");
    m_initialized = true;
    return true;
}

void Application::setupArgumentParser() {
    // Global options
    m_parser->addFlag("verbose", "v", "Enable verbose output")
           .addFlag("quiet", "q", "Suppress non-error output")
           .addFlag("help", "h", "Show help information")
           .addOption("log-level", "", "Set log level (debug, info, warning, error, fatal)", false, "info");

    // Slice command
    ArgumentParser::CommandDef slice_cmd("slice", "Slice a 3D model to generate G-code");

    ArgumentParser::ArgumentDef input_arg("input", ArgumentParser::ArgumentType::Option, "Input model file");
    input_arg.required = true;

    ArgumentParser::ArgumentDef output_arg("output", ArgumentParser::ArgumentType::Option, "Output G-code file");
    output_arg.required = true;

    slice_cmd.arguments = {
        input_arg,
        output_arg,
        ArgumentParser::ArgumentDef("plate", ArgumentParser::ArgumentType::Option, "Plate index to slice from .3mf (1-based, default: 1)"),
        ArgumentParser::ArgumentDef("config", ArgumentParser::ArgumentType::Option, "Configuration file"),
        ArgumentParser::ArgumentDef("preset", ArgumentParser::ArgumentType::Option, "Preset name"),
        ArgumentParser::ArgumentDef("printer", ArgumentParser::ArgumentType::Option, "Printer profile (e.g., 'Bambu Lab X1 Carbon')"),
        ArgumentParser::ArgumentDef("filament", ArgumentParser::ArgumentType::Option, "Filament profile (e.g., 'Bambu PLA Basic @BBL X1C')"),
        ArgumentParser::ArgumentDef("process", ArgumentParser::ArgumentType::Option, "Process profile (e.g., '0.20mm Standard @BBL X1C')"),
        // Comma-separated overrides: key=value[,key=value...]
        ArgumentParser::ArgumentDef("set", ArgumentParser::ArgumentType::Option, "Override config options as key=value pairs separated by commas (e.g., --set \"curr_bed_type=High Temp Plate,first_layer_bed_temperature=65\")"),
        ArgumentParser::ArgumentDef("dry-run", ArgumentParser::ArgumentType::Flag, "Validate without slicing")
    };
    m_parser->addCommand(slice_cmd);

    // Info command
    ArgumentParser::CommandDef info_cmd("info", "Show information about a 3D model");

    ArgumentParser::ArgumentDef info_input_arg("input", ArgumentParser::ArgumentType::Option, "Input model file");
    info_input_arg.required = true;

    info_cmd.arguments = {
        info_input_arg
    };
    m_parser->addCommand(info_cmd);

    // Version command
    ArgumentParser::CommandDef version_cmd("version", "Show version information");
    m_parser->addCommand(version_cmd);

    // List profiles command
    ArgumentParser::CommandDef list_profiles_cmd("list-profiles", "List available printer, filament, and process profiles");
    list_profiles_cmd.arguments = {
        ArgumentParser::ArgumentDef("type", ArgumentParser::ArgumentType::Option, "Profile type to list (printer, filament, process, all)")
    };
    m_parser->addCommand(list_profiles_cmd);

    // Help command
    ArgumentParser::CommandDef help_cmd("help", "Show help information");
    help_cmd.arguments = {
        ArgumentParser::ArgumentDef("command", ArgumentParser::ArgumentType::Positional, "Command to show help for")
    };
    m_parser->addCommand(help_cmd);
}

int Application::executeCommand(const std::string& command, const ArgumentParser::ParseResult& args) {
    LOG_DEBUG("Executing command: " + command);

    if (command == "slice") {
        return handleSliceCommand(args);
    } else if (command == "info") {
        return handleInfoCommand(args);
    } else if (command == "version") {
        return handleVersionCommand(args);
    } else if (command == "list-profiles") {
        return handleListProfilesCommand(args);
    } else if (command == "help") {
        return handleHelpCommand(args);
    } else if (command.empty()) {
        // No command specified, show help
        m_parser->printHelp();
        return 0;
    } else {
        LOG_ERROR("Unknown command: " + command);
        return ErrorHandler::errorCodeToExitCode(ErrorCode::InvalidArguments);
    }
}

int Application::handleSliceCommand(const ArgumentParser::ParseResult& args) {
    LOG_INFO("Starting slice operation...");

    CliCore::SlicingParams params;
    params.input_file = args.getArgument("input");
    params.output_file = args.getArgument("output");
    params.config_file = args.getArgument("config");
    params.preset_name = args.getArgument("preset");
    params.printer_profile = args.getArgument("printer");
    params.filament_profile = args.getArgument("filament");
    params.process_profile = args.getArgument("process");
    params.dry_run = args.getFlag("dry-run");
    params.verbose = args.getFlag("verbose");

    // Plate index (1-based) for .3mf projects
    int plate = 1;

    {
        std::string plate_str = args.getArgument("plate");
        if (!plate_str.empty()) {
            try { plate = std::max(1, std::stoi(plate_str)); } catch (...) {}
        }
    }
    params.plate_index = plate;
    LOG_INFO("Plate index: " + std::to_string(params.plate_index));

    // Parse overrides from --set "k=v,k=v,..."
    {
        std::string set_arg = args.getArgument("set");
        if (!set_arg.empty()) {
            // Split by commas
            size_t start = 0;
            while (start < set_arg.size()) {
                size_t comma = set_arg.find(',', start);
                std::string kv = (comma == std::string::npos) ? set_arg.substr(start) : set_arg.substr(start, comma - start);
                // Trim spaces
                auto ltrim = [](std::string &s){ s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); })); };
                auto rtrim = [](std::string &s){ s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end()); };
                ltrim(kv); rtrim(kv);
                if (!kv.empty()) {
                    size_t eq = kv.find('=');
                    if (eq != std::string::npos) {
                        std::string key = kv.substr(0, eq);
                        std::string val = kv.substr(eq + 1);
                        ltrim(key); rtrim(key);
                        ltrim(val); rtrim(val);
                        // Strip surrounding quotes if present
                        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') || (val.front() == '\'' && val.back() == '\''))) {
                            val = val.substr(1, val.size() - 2);
                        }
                        if (!key.empty()) {
                            params.custom_settings[key] = val;
                            LOG_INFO(std::string("Override set: ") + key + "=" + val);
                        }
                    }
                }
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        }
    }

    LOG_INFO("Input file: " + params.input_file);
    LOG_INFO("Output file: " + params.output_file);

    if (!params.printer_profile.empty()) {
        LOG_INFO("Printer profile: " + params.printer_profile);
    }
    if (!params.filament_profile.empty()) {
        LOG_INFO("Filament profile: " + params.filament_profile);
    }
    if (!params.process_profile.empty()) {
        LOG_INFO("Process profile: " + params.process_profile);
    }

    auto result = m_core->slice(params);
    if (!result.success) {
        LOG_ERROR("Slicing failed: " + result.message);
        if (!result.error_details.empty()) {
            LOG_DEBUG("Details: " + result.error_details);
        }
        return ErrorHandler::errorCodeToExitCode(ErrorCode::SlicingError);
    }

    LOG_INFO("Slicing completed successfully");
    if (!args.getFlag("quiet")) {
        std::cout << "Slicing completed: " << params.output_file << std::endl;
    }

    return 0;
}

int Application::handleInfoCommand(const ArgumentParser::ParseResult& args) {
    std::string input_file = args.getArgument("input");
    LOG_INFO("Getting model information for: " + input_file);

    // First validate the file
    auto validation_info = m_core->validateModel(input_file);
    if (!validation_info.is_valid) {
        if (!args.getFlag("quiet")) {
            std::cout << "Model Information:" << std::endl;
            std::cout << "  File: " << input_file << std::endl;
            std::cout << "  Valid: No" << std::endl;

            if (!validation_info.errors.empty()) {
                std::cout << "  Errors:" << std::endl;
                for (const auto& error : validation_info.errors) {
                    std::cout << "    - " << error << std::endl;
                }
            }
        }
        return ErrorHandler::errorCodeToExitCode(ErrorCode::InvalidFile);
    }

    // Load the model to get detailed information
    auto load_result = m_core->loadModel(input_file);
    if (!load_result.success) {
        if (!args.getFlag("quiet")) {
            std::cout << "Model Information:" << std::endl;
            std::cout << "  File: " << input_file << std::endl;
            std::cout << "  Valid: No" << std::endl;
            std::cout << "  Errors:" << std::endl;
            std::cout << "    - " << load_result.message << std::endl;
            if (!load_result.error_details.empty()) {
                std::cout << "    - " << load_result.error_details << std::endl;
            }
        }
        return ErrorHandler::errorCodeToExitCode(ErrorCode::InvalidFile);
    }

    // Get detailed model information
    auto model_info = m_core->getModelInfo();

    if (!args.getFlag("quiet")) {
        std::cout << "Model Information:" << std::endl;
        std::cout << "  File: " << input_file << std::endl;
        std::cout << "  Valid: " << (model_info.is_valid ? "Yes" : "No") << std::endl;

        if (model_info.is_valid) {
            std::cout << "  Objects: " << model_info.object_count << std::endl;
            std::cout << "  Triangles: " << model_info.triangle_count << std::endl;
            std::cout << "  Volume: " << model_info.volume << " mm³" << std::endl;
            std::cout << "  Bounding Box: " << model_info.bounding_box << std::endl;
        }

        if (!model_info.warnings.empty()) {
            std::cout << "  Warnings:" << std::endl;
            for (const auto& warning : model_info.warnings) {
                std::cout << "    - " << warning << std::endl;
            }
        }

        if (!model_info.errors.empty()) {
            std::cout << "  Errors:" << std::endl;
            for (const auto& error : model_info.errors) {
                std::cout << "    - " << error << std::endl;
            }
        }
    }

    return model_info.is_valid ? 0 : ErrorHandler::errorCodeToExitCode(ErrorCode::InvalidFile);
}

int Application::handleVersionCommand(const ArgumentParser::ParseResult& args) {
    if (!args.getFlag("quiet")) {
        std::cout << CliCore::getVersion() << std::endl;
        std::cout << CliCore::getBuildInfo() << std::endl;
    }
    return 0;
}

int Application::handleListProfilesCommand(const ArgumentParser::ParseResult& args) {
    LOG_INFO("Listing available profiles...");

    std::string profile_type = args.getArgument("type", "all");

    if (!args.getFlag("quiet")) {
        std::cout << "Available Profiles" << std::endl;
        std::cout << "==================" << std::endl;
    }

    if (profile_type == "all" || profile_type == "printer") {
        auto printers = m_core->getAvailablePrinterProfiles();
        if (!args.getFlag("quiet")) {
            std::cout << "\nPrinter Profiles (" << printers.size() << "):" << std::endl;
            for (const auto& profile : printers) {
                std::cout << "  - " << profile << std::endl;
            }
        }
    }

    if (profile_type == "all" || profile_type == "filament") {
        auto filaments = m_core->getAvailableFilamentProfiles();
        if (!args.getFlag("quiet")) {
            std::cout << "\nFilament Profiles (" << filaments.size() << "):" << std::endl;
            // Show first 20 to avoid overwhelming output
            size_t count = 0;
            for (const auto& profile : filaments) {
                if (count >= 20) {
                    std::cout << "  ... and " << (filaments.size() - 20) << " more" << std::endl;
                    break;
                }
                std::cout << "  - " << profile << std::endl;
                count++;
            }
        }
    }

    if (profile_type == "all" || profile_type == "process") {
        auto processes = m_core->getAvailableProcessProfiles();
        if (!args.getFlag("quiet")) {
            std::cout << "\nProcess Profiles (" << processes.size() << "):" << std::endl;
            // Show first 20 to avoid overwhelming output
            size_t count = 0;
            for (const auto& profile : processes) {
                if (count >= 20) {
                    std::cout << "  ... and " << (processes.size() - 20) << " more" << std::endl;
                    break;
                }
                std::cout << "  - " << profile << std::endl;
                count++;
            }
        }
    }

    if (profile_type != "all" && profile_type != "printer" &&
        profile_type != "filament" && profile_type != "process") {
        LOG_ERROR("Invalid profile type: " + profile_type);
        std::cerr << "Error: Invalid profile type '" << profile_type << "'. Valid types: all, printer, filament, process" << std::endl;
        return ErrorHandler::errorCodeToExitCode(ErrorCode::InvalidArguments);
    }

    return 0;
}

int Application::handleHelpCommand(const ArgumentParser::ParseResult& args) {
    std::string command = args.getArgument("command");
    m_parser->printHelp(command);
    return 0;
}

void Application::setupLogging(const ArgumentParser::ParseResult& args) {
    Logger& logger = Logger::getInstance();

    // Set log level
    std::string log_level_str = args.getArgument("log-level", "info");
    LogLevel log_level = Logger::stringToLevel(log_level_str);
    logger.setLogLevel(log_level);

    // Handle verbose/quiet flags
    if (args.getFlag("verbose")) {
        logger.setLogLevel(LogLevel::Debug);
    } else if (args.getFlag("quiet")) {
        logger.setLogLevel(LogLevel::Error);
    }

    // Disable colors if not a terminal
    // This is a simple check - in a full implementation you might want more sophisticated detection
    logger.setColorEnabled(true);
}

std::string Application::getVersion() {
    return "1.0.0";
}

std::string Application::getAppName() {
    return "orcaslicer-cli";
}

} // namespace OrcaSlicerCli
