#pragma once

#include <chrono>
#include <string>
#include <vector>


/**
 * @brief Severity classification for log entries.
 */
enum class LogLevel {
    Debug,    ///< Verbose diagnostics, normally suppressed in production.
    Info,     ///< Routine operational events.
    Warning,  ///< Recoverable issue or unexpected state worth attention.
    Error     ///< Failure that prevents an operation from completing.
};

/**
 * @brief Transport-agnostic log record dispatched from LogHLD to each backend.
 *
 * Carries either a free-form @c message (text line) or a structured row
 * defined by parallel @c columns / @c values vectors. The @c tablePath and
 * @c tableName identify the destination table for structured entries.
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;  ///< Moment the entry was created.
    LogLevel    level       = LogLevel::Info;         ///< Severity of the entry.

    std::string              tablePath;  ///< Destination table path (e.g. "functional/power").
    std::string              tableName;  ///< Destination table name (e.g. "events").
    std::vector<std::string> columns;    ///< Column names for a structured row.
    std::vector<std::string> values;     ///< Values aligned 1:1 with @c columns.

    std::string message;  ///< Free-form payload text (empty for pure structured rows).
};

/**
 * @brief Convert a LogLevel to its short textual form ("DEBUG"/"INFO"/"WARN"/"ERROR").
 * @param level  Severity to stringify.
 * @return Pointer to a static, NUL-terminated string. Never null.
 */
inline const char* toString(LogLevel level)
{
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
    }
    return "?";
}
