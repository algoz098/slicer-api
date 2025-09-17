#include "Logger.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <sstream>
#include <mutex>
#include <map>

namespace OrcaSlicerCli {

/**
 * @brief Private implementation for Logger
 */
class Logger::Impl {
public:
    LogLevel min_level = LogLevel::Info;
    std::ostream* output_stream = &std::cout;
    bool timestamp_enabled = true;
    bool color_enabled = true;
    std::mutex log_mutex;

    // ANSI color codes
    static const std::map<LogLevel, std::string> color_codes;
    static const std::string reset_code;

    void writeLog(LogLevel level, const std::string& message) {
        if (level < min_level) {
            return;
        }

        std::lock_guard<std::mutex> lock(log_mutex);

        std::ostringstream log_line;

        // Add timestamp
        if (timestamp_enabled) {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;

            log_line << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
            log_line << "." << std::setfill('0') << std::setw(3) << ms.count();
            log_line << " ";
        }

        // Add color code
        if (color_enabled && isTerminal()) {
            auto color_it = color_codes.find(level);
            if (color_it != color_codes.end()) {
                log_line << color_it->second;
            }
        }

        // Add level
        log_line << "[" << std::setw(7) << levelToString(level) << "]";

        // Reset color
        if (color_enabled && isTerminal()) {
            log_line << reset_code;
        }

        log_line << " " << message << std::endl;

        *output_stream << log_line.str();
        output_stream->flush();
    }

    bool isTerminal() const {
        // Simple check if output is a terminal
        return output_stream == &std::cout || output_stream == &std::cerr;
    }

    static std::string levelToString(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:   return "DEBUG";
            case LogLevel::Info:    return "INFO";
            case LogLevel::Warning: return "WARNING";
            case LogLevel::Error:   return "ERROR";
            case LogLevel::Fatal:   return "FATAL";
            default:                return "UNKNOWN";
        }
    }

    static LogLevel stringToLevel(const std::string& level_str) {
        std::string upper_str = level_str;
        std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);

        if (upper_str == "DEBUG") return LogLevel::Debug;
        if (upper_str == "INFO") return LogLevel::Info;
        if (upper_str == "WARNING" || upper_str == "WARN") return LogLevel::Warning;
        if (upper_str == "ERROR") return LogLevel::Error;
        if (upper_str == "FATAL") return LogLevel::Fatal;

        return LogLevel::Debug; // Default
    }
};

// Color code definitions
const std::map<LogLevel, std::string> Logger::Impl::color_codes = {
    {LogLevel::Debug,   "\033[36m"},  // Cyan
    {LogLevel::Info,    "\033[32m"},  // Green
    {LogLevel::Warning, "\033[33m"},  // Yellow
    {LogLevel::Error,   "\033[31m"},  // Red
    {LogLevel::Fatal,   "\033[35m"}   // Magenta
};

const std::string Logger::Impl::reset_code = "\033[0m";

// Logger implementation

Logger::Logger() : m_impl(std::make_unique<Impl>()) {
}

Logger::~Logger() = default;

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::setLogLevel(LogLevel level) {
    m_impl->min_level = level;
}

LogLevel Logger::getLogLevel() const {
    return m_impl->min_level;
}

void Logger::setOutputStream(std::ostream& stream) {
    m_impl->output_stream = &stream;
}

void Logger::setTimestampEnabled(bool enable) {
    m_impl->timestamp_enabled = enable;
}

void Logger::setColorEnabled(bool enable) {
    m_impl->color_enabled = enable;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::fatal(const std::string& message) {
    log(LogLevel::Fatal, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    m_impl->writeLog(level, message);
}

std::string Logger::levelToString(LogLevel level) {
    return Impl::levelToString(level);
}

LogLevel Logger::stringToLevel(const std::string& level_str) {
    return Impl::stringToLevel(level_str);
}

} // namespace OrcaSlicerCli
