#include "hld/LogHLD.h"

#include <cmath>
#include <ctime>
#include <iomanip>
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
    entry.tablePath = "power";
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

// Format the measurement timestamp as "YYYY-MM-DD HH:MM:SS.mmm" — Excel/CSV
// readers parse this directly as a date-time. Local time is used so the
// column matches the operator's wall clock; if the Pi's TZ is UTC the values
// will be UTC, which is fine as long as it's consistent.
static std::string formatTimestamp(std::chrono::system_clock::time_point tp)
{
    using namespace std::chrono;
    if (tp.time_since_epoch().count() == 0) {
        return "";
    }
    const auto t   = system_clock::to_time_t(tp);
    const auto ms  = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return os.str();
}

static std::string f2s(float v)
{
    // NaN/Inf and sub-microunit noise are treated as 0 so the FLOG output
    // stays parseable by spreadsheets (no "nan" -> #NAAM? cells) and is
    // not polluted by denormalised near-zero readings from the meter.
    if (!std::isfinite(v) || std::fabs(v) < 1e-6f) {
        return "0";
    }
    std::ostringstream os;
    os << v;
    return os.str();
}

void LogHLD::fillMeasurementRow(const MeterMeasurement& m,
                                std::vector<std::string>& columns,
                                std::vector<std::string>& values)
{
    columns = {
        "t_meas",
        "valid",
        "I1", "I2", "I3", "Iavg",
        "U1N", "U2N", "U3N", "ULNavg",
        "U12", "U23", "U31", "ULLavg",
        "P1", "P2", "P3", "Ptot",
        "PF", "f"
    };
    values = {
        formatTimestamp(m.measuredAt),
        m.valid ? "1" : "0",
        f2s(m.currentL1),    f2s(m.currentL2),    f2s(m.currentL3),    f2s(m.currentAvg),
        f2s(m.voltageL1N),   f2s(m.voltageL2N),   f2s(m.voltageL3N),   f2s(m.voltageLNAvg),
        f2s(m.voltageL1L2),  f2s(m.voltageL2L3),  f2s(m.voltageL3L1),  f2s(m.voltageLLAvg),
        f2s(m.activePowerL1),f2s(m.activePowerL2),f2s(m.activePowerL3),f2s(m.totalActivePower),
        f2s(m.powerFactor),  f2s(m.frequency)
    };
}
