/**
 * TOS Miner - Logging Utility
 */

#pragma once

#include <string>
#include <mutex>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace tos {

/**
 * Log level enumeration
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

/**
 * Simple logging class
 */
class Log {
public:
    /**
     * Set minimum log level
     */
    static void setLevel(LogLevel level) {
        s_level = level;
    }

    /**
     * Get current log level
     */
    static LogLevel getLevel() {
        return s_level;
    }

    /**
     * Enable/disable timestamps
     */
    static void setShowTimestamp(bool show) {
        s_showTimestamp = show;
    }

    /**
     * Log debug message
     */
    static void debug(const std::string& msg) {
        log(LogLevel::Debug, msg);
    }

    /**
     * Log info message
     */
    static void info(const std::string& msg) {
        log(LogLevel::Info, msg);
    }

    /**
     * Log warning message
     */
    static void warning(const std::string& msg) {
        log(LogLevel::Warning, msg);
    }

    /**
     * Log error message
     */
    static void error(const std::string& msg) {
        log(LogLevel::Error, msg);
    }

    /**
     * Log a message at specified level
     */
    static void log(LogLevel level, const std::string& msg) {
        if (level < s_level) {
            return;
        }

        std::lock_guard<std::mutex> lock(s_mutex);

        std::ostream& out = (level >= LogLevel::Warning) ? std::cerr : std::cout;

        if (s_showTimestamp) {
            out << getTimestamp() << " ";
        }

        out << getLevelPrefix(level) << " " << msg << std::endl;
    }

private:
    static std::string getTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::ostringstream ss;
        ss << std::put_time(std::localtime(&time), "%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
        return ss.str();
    }

    static std::string getLevelPrefix(LogLevel level) {
        switch (level) {
            case LogLevel::Debug:   return "[D]";
            case LogLevel::Info:    return "[I]";
            case LogLevel::Warning: return "[W]";
            case LogLevel::Error:   return "[E]";
            default:                return "[?]";
        }
    }

    static inline LogLevel s_level = LogLevel::Info;
    static inline bool s_showTimestamp = true;
    static inline std::mutex s_mutex;
};

}  // namespace tos
