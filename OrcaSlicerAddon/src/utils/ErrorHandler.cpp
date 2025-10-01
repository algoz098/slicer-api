#include "ErrorHandler.hpp"
#include "Logger.hpp"

#include <iostream>

namespace OrcaSlicerCli {

// Static member initialization
ErrorHandler::ErrorHandlerFunc ErrorHandler::s_error_handler = ErrorHandler::defaultErrorHandler;

void ErrorHandler::setErrorHandler(ErrorHandlerFunc handler) {
    s_error_handler = handler ? handler : defaultErrorHandler;
}

void ErrorHandler::handleError(const CliException& exception) {
    if (s_error_handler) {
        s_error_handler(exception);
    }
}

void ErrorHandler::handleError(ErrorCode code, const std::string& message, const std::string& details) {
    CliException exception(code, message, details);
    handleError(exception);
}

std::string ErrorHandler::errorCodeToString(ErrorCode code) {
    switch (code) {
        case ErrorCode::Success:
            return "Success";
        case ErrorCode::InvalidArguments:
            return "Invalid Arguments";
        case ErrorCode::FileNotFound:
            return "File Not Found";
        case ErrorCode::InvalidFile:
            return "Invalid File";
        case ErrorCode::ConfigurationError:
            return "Configuration Error";
        case ErrorCode::SlicingError:
            return "Slicing Error";
        case ErrorCode::InitializationError:
            return "Initialization Error";
        case ErrorCode::InternalError:
            return "Internal Error";
        case ErrorCode::UnknownError:
            return "Unknown Error";
        default:
            return "Undefined Error";
    }
}

int ErrorHandler::errorCodeToExitCode(ErrorCode code) {
    return static_cast<int>(code);
}

void ErrorHandler::defaultErrorHandler(const CliException& exception) {
    // Log the error
    Logger& logger = Logger::getInstance();
    
    switch (exception.getCode()) {
        case ErrorCode::Success:
            // Should not happen, but handle gracefully
            logger.info(exception.getMessage());
            break;
        case ErrorCode::InvalidArguments:
        case ErrorCode::FileNotFound:
        case ErrorCode::InvalidFile:
        case ErrorCode::ConfigurationError:
            logger.error(exception.getMessage());
            if (!exception.getDetails().empty()) {
                logger.debug("Details: " + exception.getDetails());
            }
            break;
        case ErrorCode::SlicingError:
        case ErrorCode::InitializationError:
        case ErrorCode::InternalError:
        case ErrorCode::UnknownError:
            logger.fatal(exception.getMessage());
            if (!exception.getDetails().empty()) {
                logger.error("Details: " + exception.getDetails());
            }
            break;
    }

    // Also output to stderr for critical errors
    if (exception.getCode() >= ErrorCode::SlicingError) {
        std::cerr << "FATAL: " << exception.getMessage() << std::endl;
        if (!exception.getDetails().empty()) {
            std::cerr << "Details: " << exception.getDetails() << std::endl;
        }
    }
}

} // namespace OrcaSlicerCli
