#pragma once

#include "interfaces/ILogBackend.h"
#include "interfaces/ILogService.h"

#include <mutex>
#include <vector>

/**
 * LogHLD — Log High Level Driver.
 *
 * Exposes ILogService to the Controller and forwards every entry to
 * a set of registered ILogBackend implementations (FLOG_LLD, SDL_LLD, ...).
 *
 * Responsibilities:
 *  - Timestamp and format entries into a LogEntry.
 *  - Fan out to every registered backend.
 *  - Remain independent of any specific backend protocol.
 *
 * Adding a new backend: instantiate it, call addBackend(),
 * no other change in the HLD is required.
 */
class LogHLD : public ILogService {
public:
    LogHLD() = default;
    ~LogHLD() override = default;

    // Non-copyable
    LogHLD(const LogHLD&)            = delete;
    LogHLD& operator=(const LogHLD&) = delete;

    /**
     * Register a backend. The caller retains ownership and must ensure
     * the backend outlives this LogHLD. Multiple backends are supported;
     * every log entry is forwarded to all of them.
     */
    void addBackend(ILogBackend& backend);

    // --- ILogService -----------------------------------------------------------
    bool isAvailable() const override;

    void logMessage(LogLevel level,
                    const std::string& tablePath,
                    const std::string& tableName,
                    const std::string& message) override;

    void logMeasurement(const MeterMeasurement& measurement) override;

private:
    mutable std::mutex        backendsMutex_;
    std::vector<ILogBackend*> backends_;

    void dispatch(const LogEntry& entry);

    static void fillMeasurementRow(const MeterMeasurement& m,
                                   std::vector<std::string>& columns,
                                   std::vector<std::string>& values);
};
