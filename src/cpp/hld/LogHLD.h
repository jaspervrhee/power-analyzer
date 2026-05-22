#pragma once

#include "interfaces/ILogBackend.h"
#include "interfaces/ILogService.h"

#include <mutex>
#include <vector>

/**
 * @brief High Level Driver implementing ILogService.
 *
 * Fans out every log entry to all registered ILogBackend instances. The HLD
 * formats free-form messages and structured measurements into a LogEntry
 * and dispatches them; individual backend failures do not affect siblings.
 */
class LogHLD : public ILogService {
public:
    LogHLD() = default;
    ~LogHLD() override = default;

    /// @name Non-copyable
    /// @{
    LogHLD(const LogHLD&)            = delete;
    LogHLD& operator=(const LogHLD&) = delete;
    /// @}

    /**
     * @brief Register a backend to receive every dispatched LogEntry.
     *
     * @param backend  Backend to attach. Caller retains ownership and must
     *                 ensure it outlives this LogHLD.
     */
    void addBackend(ILogBackend& backend);

    /**
     * @copydoc ILogService::isAvailable
     * @return true when at least one registered backend is currently connected.
     */
    bool isAvailable() const override;

    /**
     * @copydoc ILogService::logMessage
     *
     * Builds a LogEntry containing the timestamp, level and message and
     * dispatches it to all backends.
     */
    void logMessage(LogLevel level,
                    const std::string& tablePath,
                    const std::string& tableName,
                    const std::string& message) override;

    /**
     * @copydoc ILogService::logMeasurement
     *
     * Builds a LogEntry with the measurement encoded as parallel column/value
     * vectors and dispatches it to all backends.
     */
    void logMeasurement(const MeterMeasurement& measurement) override;

private:
    mutable std::mutex        backendsMutex_;
    std::vector<ILogBackend*> backends_;

    /**
     * @brief Snapshot the backend list and send @p entry to each connected backend.
     *
     * The actual I/O is performed outside the lock to avoid blocking
     * addBackend() / isAvailable() while a slow backend is writing.
     */
    void dispatch(const LogEntry& entry);

    /**
     * @brief Populate parallel column and value vectors from a MeterMeasurement.
     *
     * @param[in]  m        Source measurement (includes formatted timestamp).
     * @param[out] columns  Column names, cleared and refilled.
     * @param[out] values   Stringified values aligned 1:1 with @p columns.
     */
    static void fillMeasurementRow(const MeterMeasurement& m,
                                   std::vector<std::string>& columns,
                                   std::vector<std::string>& values);
};
