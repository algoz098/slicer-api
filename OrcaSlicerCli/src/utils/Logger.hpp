#pragma once

#include <string>
#include <memory>
#include <ostream>

namespace OrcaSlicerCli {

/**
 * @brief Logging levels
 */
enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Fatal = 4
};

/**
 * @brief Simple logging system for OrcaSlicerCli
 * 
 * Provides structured logging with different levels and output targets.
 * Thread-safe and lightweight implementation.
 */
class Logger {
public:
    /**
     * @brief Get the global logger instance
     * @return Reference to the global logger
     */
    static Logger& getInstance();

    /**
     * @brief Set the minimum log level
     * @param level Minimum level to log
     */
    void setLogLevel(LogLevel level);

    /**
     * @brief Get the current log level
     * @return Current minimum log level
     */
    LogLevel getLogLevel() const;

    /**
     * @brief Set output stream for logging
     * @param stream Output stream (default: std::cout)
     */
    void setOutputStream(std::ostream& stream);

    /**
     * @brief Enable/disable timestamps in log messages
     * @param enable True to enable timestamps
     */
    void setTimestampEnabled(bool enable);

    /**
     * @brief Enable/disable colored output
     * @param enable True to enable colors
     */
    void setColorEnabled(bool enable);

    /**
     * @brief Log a debug message
     * @param message Message to log
     */
    void debug(const std::string& message);

    /**
     * @brief Log an info message
     * @param message Message to log
     */
    void info(const std::string& message);

    /**
     * @brief Log a warning message
     * @param message Message to log
     */
    void warning(const std::string& message);

    /**
     * @brief Log an error message
     * @param message Message to log
     */
    void error(const std::string& message);

    /**
     * @brief Log a fatal error message
     * @param message Message to log
     */
    void fatal(const std::string& message);

    /**
     * @brief Log a message with specific level
     * @param level Log level
     * @param message Message to log
     */
    void log(LogLevel level, const std::string& message);

    /**
     * @brief Convert log level to string
     * @param level Log level
     * @return String representation
     */
    static std::string levelToString(LogLevel level);

    /**
     * @brief Parse log level from string
     * @param level_str String representation
     * @return Log level (Debug if invalid)
     */
    static LogLevel stringToLevel(const std::string& level_str);

private:
    Logger();
    ~Logger();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};

/**
 * @brief Convenience macros for logging
 */
#define LOG_DEBUG(msg) OrcaSlicerCli::Logger::getInstance().debug(msg)
#define LOG_INFO(msg) OrcaSlicerCli::Logger::getInstance().info(msg)
#define LOG_WARNING(msg) OrcaSlicerCli::Logger::getInstance().warning(msg)
#define LOG_ERROR(msg) OrcaSlicerCli::Logger::getInstance().error(msg)
#define LOG_FATAL(msg) OrcaSlicerCli::Logger::getInstance().fatal(msg)

} // namespace OrcaSlicerCli
