#pragma once

#include <chrono>
#include <string>
#include <vector>

/**
 * Severity classification of a log entry.
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

/**
 * Generic log entry shared between LogHLD and every ILogBackend.
 *
 * Two forms are supported, matching the FLOG logServer model:
 *
 *  - Structured (columns non-empty):
 *      tablePath + tableName identify the destination table;
 *      columns describe the schema (registered on first use);
 *      values hold the row (same size as columns).
 *
 *  - Free-form (columns empty):
 *      message carries a plain text line (dprintf-style).
 *      tablePath / tableName are metadata; backends that only support
 *      text logging (FLOG dprintf) may ignore them.
 */
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel    level       = LogLevel::Info;

    std::string              tablePath;
    std::string              tableName;
    std::vector<std::string> columns;
    std::vector<std::string> values;

    std::string message;
};

/** Human-readable label for a LogLevel (used by backends for formatting). */
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
