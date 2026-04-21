#include "hld/LogHLD.h"

#include <sstream>

// ---------------------------------------------------------------------------
// Backend registration
// ---------------------------------------------------------------------------

void LogHLD::addBackend(ILogBackend& backend)
{
    std::lock_guard<std::mutex> lock(backendsMutex_);
    backends_.push_back(&backend);
}

// ---------------------------------------------------------------------------
// ILogService
// ---------------------------------------------------------------------------

bool LogHLD::isAvailable() const
{
    std::lock_guard<std::mutex> lock(backendsMutex_);
    for (const auto* backend : backends_) {
        if (backend && backend->isConnected()) {
            return true;
        }
    }
    return false;
}

void LogHLD::logMessage(LogLevel level,
                        const std::string& tablePath,
                        const std::string& tableName,
                        const std::string& message)
{
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level     = level;
    entry.tablePath = tablePath;
    entry.tableName = tableName;
    entry.message   = message;

    dispatch(entry);
}

void LogHLD::logMeasurement(const MeterMeasurement& measurement)
{
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level     = measurement.valid ? LogLevel::Info : LogLevel::Warning;
    entry.tablePath = "functional/power";
    entry.tableName = "measurements";
    fillMeasurementRow(measurement, entry.columns, entry.values);

    dispatch(entry);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void LogHLD::dispatch(const LogEntry& entry)
{
    // Snapshot the backend list so send() can do blocking I/O without
    // serialising unrelated registrations on the same mutex.
    std::vector<ILogBackend*> snapshot;
    {
        std::lock_guard<std::mutex> lock(backendsMutex_);
        snapshot = backends_;
    }
    for (auto* backend : snapshot) {
        if (backend && backend->isConnected()) {
            backend->send(entry);
        }
    }
}

static std::string f2s(float v)
{
    std::ostringstream os;
    os << v;
    return os.str();
}

void LogHLD::fillMeasurementRow(const MeterMeasurement& m,
                                std::vector<std::string>& columns,
                                std::vector<std::string>& values)
{
    columns = {
        "valid",
        "I1", "I2", "I3", "Iavg",
        "U1N", "U2N", "U3N", "ULNavg",
        "U12", "U23", "U31", "ULLavg",
        "P1", "P2", "P3", "Ptot",
        "PF", "f"
    };
    values = {
        m.valid ? "1" : "0",
        f2s(m.currentL1),    f2s(m.currentL2),    f2s(m.currentL3),    f2s(m.currentAvg),
        f2s(m.voltageL1N),   f2s(m.voltageL2N),   f2s(m.voltageL3N),   f2s(m.voltageLNAvg),
        f2s(m.voltageL1L2),  f2s(m.voltageL2L3),  f2s(m.voltageL3L1),  f2s(m.voltageLLAvg),
        f2s(m.activePowerL1),f2s(m.activePowerL2),f2s(m.activePowerL3),f2s(m.totalActivePower),
        f2s(m.powerFactor),  f2s(m.frequency)
    };
}
