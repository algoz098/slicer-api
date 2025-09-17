#pragma once

#include "core/CliCore.hpp"
#include "utils/ArgumentParser.hpp"
#include "utils/ErrorHandler.hpp"

#include <string>
#include <memory>

namespace OrcaSlicerCli {

/**
 * @brief Main application class for OrcaSlicerCli
 * 
 * Coordinates the overall application flow, including argument parsing,
 * command execution, and error handling.
 */
class Application {
public:
    /**
     * @brief Constructor
     */
    Application();

    /**
     * @brief Destructor
     */
    ~Application();

    /**
     * @brief Run the application
     * @param argc Argument count
     * @param argv Argument values
     * @return Exit code
     */
    int run(int argc, char* argv[]);

    /**
     * @brief Get application version
     * @return Version string
     */
    static std::string getVersion();

    /**
     * @brief Get application name
     * @return Application name
     */
    static std::string getAppName();

private:
    /**
     * @brief Initialize the application
     * @return True if successful
     */
    bool initialize();

    /**
     * @brief Setup command line argument parser
     */
    void setupArgumentParser();

    /**
     * @brief Execute a command
     * @param command Command name
     * @param args Parsed arguments
     * @return Exit code
     */
    int executeCommand(const std::string& command, const ArgumentParser::ParseResult& args);

    /**
     * @brief Handle slice command
     * @param args Parsed arguments
     * @return Exit code
     */
    int handleSliceCommand(const ArgumentParser::ParseResult& args);

    /**
     * @brief Handle info command
     * @param args Parsed arguments
     * @return Exit code
     */
    int handleInfoCommand(const ArgumentParser::ParseResult& args);

    /**
     * @brief Handle version command
     * @param args Parsed arguments
     * @return Exit code
     */
    int handleVersionCommand(const ArgumentParser::ParseResult& args);

    /**
     * @brief Handle list-profiles command
     * @param args Parsed arguments
     * @return Exit code
     */
    int handleListProfilesCommand(const ArgumentParser::ParseResult& args);

    /**
     * @brief Handle help command
     * @param args Parsed arguments
     * @return Exit code
     */
    int handleHelpCommand(const ArgumentParser::ParseResult& args);

    /**
     * @brief Print application banner
     */
    void printBanner();

    /**
     * @brief Setup logging based on arguments
     * @param args Parsed arguments
     */
    void setupLogging(const ArgumentParser::ParseResult& args);

private:
    std::unique_ptr<CliCore> m_core;
    std::unique_ptr<ArgumentParser> m_parser;
    bool m_initialized = false;
};

} // namespace OrcaSlicerCli
