#include "Application.hpp"
#include "utils/ErrorHandler.hpp"
#include "utils/Logger.hpp"

#include <iostream>
#include <cstdlib>
#include <clocale>
#include <locale>


/**
 * @brief Main entry point for OrcaSlicerCli
 *
 * This is the main entry point that creates and runs the Application instance.

 * It handles any top-level exceptions and ensures proper exit codes.
 */
int main(int argc, char* argv[]) {
    try {
        // Create and run the application
        OrcaSlicerCli::Application app;
        return app.run(argc, argv);

    } catch (const OrcaSlicerCli::CliException& e) {
        // Handle CLI-specific exceptions
        OrcaSlicerCli::ErrorHandler::handleError(e);
        return OrcaSlicerCli::ErrorHandler::errorCodeToExitCode(e.getCode());

    } catch (const std::exception& e) {
        // Handle standard exceptions
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return OrcaSlicerCli::ErrorHandler::errorCodeToExitCode(OrcaSlicerCli::ErrorCode::InternalError);

    } catch (...) {
        // Handle unknown exceptions
        std::cerr << "Fatal error: Unknown exception occurred" << std::endl;
        return OrcaSlicerCli::ErrorHandler::errorCodeToExitCode(OrcaSlicerCli::ErrorCode::UnknownError);
    }
}
