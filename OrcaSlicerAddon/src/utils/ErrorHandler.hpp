#pragma once

#include <string>
#include <exception>
#include <functional>

namespace OrcaSlicerCli {

/**
 * @brief Error codes for OrcaSlicerCli
 */
enum class ErrorCode {
    Success = 0,
    InvalidArguments = 1,
    FileNotFound = 2,
    InvalidFile = 3,
    ConfigurationError = 4,
    SlicingError = 5,
    InitializationError = 6,
    InternalError = 7,
    UnknownError = 99
};

/**
 * @brief Custom exception class for OrcaSlicerCli
 */
class CliException : public std::exception {
public:
    CliException(ErrorCode code, const std::string& message, const std::string& details = "")
        : m_code(code), m_message(message), m_details(details) {
        m_what = "OrcaSlicerCli Error [" + std::to_string(static_cast<int>(code)) + "]: " + message;
        if (!details.empty()) {
            m_what += " (" + details + ")";
        }
    }

    const char* what() const noexcept override {
        return m_what.c_str();
    }

    ErrorCode getCode() const noexcept {
        return m_code;
    }

    const std::string& getMessage() const noexcept {
        return m_message;
    }

    const std::string& getDetails() const noexcept {
        return m_details;
    }

private:
    ErrorCode m_code;
    std::string m_message;
    std::string m_details;
    std::string m_what;
};

/**
 * @brief Error handling utilities
 */
class ErrorHandler {
public:
    /**
     * @brief Error handler function type
     */
    using ErrorHandlerFunc = std::function<void(const CliException&)>;

    /**
     * @brief Set global error handler
     * @param handler Error handler function
     */
    static void setErrorHandler(ErrorHandlerFunc handler);

    /**
     * @brief Handle an error
     * @param exception Exception to handle
     */
    static void handleError(const CliException& exception);

    /**
     * @brief Create and handle an error
     * @param code Error code
     * @param message Error message
     * @param details Additional details
     */
    static void handleError(ErrorCode code, const std::string& message, const std::string& details = "");

    /**
     * @brief Convert error code to string
     * @param code Error code
     * @return String representation
     */
    static std::string errorCodeToString(ErrorCode code);

    /**
     * @brief Convert error code to exit code
     * @param code Error code
     * @return Exit code for the application
     */
    static int errorCodeToExitCode(ErrorCode code);

    /**
     * @brief Execute a function with error handling
     * @param func Function to execute
     * @return Error code (Success if no exception)
     */
    template<typename Func>
    static ErrorCode safeExecute(Func&& func) {
        try {
            func();
            return ErrorCode::Success;
        } catch (const CliException& e) {
            handleError(e);
            return e.getCode();
        } catch (const std::exception& e) {
            handleError(ErrorCode::InternalError, "Unexpected error", e.what());
            return ErrorCode::InternalError;
        } catch (...) {
            handleError(ErrorCode::UnknownError, "Unknown error occurred");
            return ErrorCode::UnknownError;
        }
    }

    /**
     * @brief Execute a function with error handling and return result
     * @param func Function to execute
     * @param default_value Default value to return on error
     * @return Function result or default value on error
     */
    template<typename Func, typename T>
    static T safeExecuteWithResult(Func&& func, T default_value) {
        try {
            return func();
        } catch (const CliException& e) {
            handleError(e);
            return default_value;
        } catch (const std::exception& e) {
            handleError(ErrorCode::InternalError, "Unexpected error", e.what());
            return default_value;
        } catch (...) {
            handleError(ErrorCode::UnknownError, "Unknown error occurred");
            return default_value;
        }
    }

private:
    static ErrorHandlerFunc s_error_handler;
    static void defaultErrorHandler(const CliException& exception);
};

/**
 * @brief Convenience macros for error handling
 */
#define THROW_CLI_ERROR(code, message) \
    throw OrcaSlicerCli::CliException(code, message, __FILE__ ":" + std::to_string(__LINE__))

#define THROW_CLI_ERROR_WITH_DETAILS(code, message, details) \
    throw OrcaSlicerCli::CliException(code, message, details)

#define HANDLE_CLI_ERROR(code, message) \
    OrcaSlicerCli::ErrorHandler::handleError(code, message, __FILE__ ":" + std::to_string(__LINE__))

#define SAFE_EXECUTE(func) \
    OrcaSlicerCli::ErrorHandler::safeExecute([&]() { func; })

} // namespace OrcaSlicerCli
