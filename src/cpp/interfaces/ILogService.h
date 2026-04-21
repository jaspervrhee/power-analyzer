#pragma once

#include "common/LogData.h"
#include "common/MeterData.h"

#include <string>

/**
 * ILogService
 *
 * Exposes logging functionality to the Controller.
 * Abstracts how and where data is logged, so the Controller remains
 * independent of logging backends (FLOG, SDL, file, ...).
 * Implemented by LogHLD.
 */
class ILogService {
public:
    virtual ~ILogService() = default;

    /**
     * @return true when at least one backend is connected and accepting data.
     */
    virtual bool isAvailable() const = 0;

    /**
     * Log a free-form message.
     *
     * @param level      Severity of the entry.
     * @param tablePath  Destination table path (e.g. "functional/power").
     * @param tableName  Destination table name (e.g. "events").
     * @param message    Payload text.
     */
    virtual void logMessage(LogLevel level,
                            const std::string& tablePath,
                            const std::string& tableName,
                            const std::string& message) = 0;

    /**
     * Log a validated measurement as a structured entry.
     * When measurement.valid is false the entry is tagged as a Warning.
     */
    virtual void logMeasurement(const MeterMeasurement& measurement) = 0;
};
