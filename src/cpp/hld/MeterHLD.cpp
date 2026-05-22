#include "hld/MeterHLD.h"

#include <cmath>

 
namespace Limits
{
    static constexpr float CURRENT_MAX_A = 10000.0f;   // [A]
    static constexpr float VOLTAGE_LL_MAX_V = 1500.0f; // [V]
    static constexpr float VOLTAGE_LN_MAX_V = 900.0f;  // [V]
    static constexpr float POWER_MAX_KW = 1e6f;        // [kW]
    static constexpr float PF_MAX = 1.5f;
    static constexpr float FREQ_MIN_HZ = 45.0f;
    static constexpr float FREQ_MAX_HZ = 65.0f;
} // namespace Limits

// Returns true if v is a finite number and non-negative.
static bool isFiniteAndNonNegative(float v)
{
    return std::isfinite(v) && v >= 0.0f;
}

// Returns true if v is finite and lies within [minVal, maxVal].
static bool inRange(float v, float minVal, float maxVal)
{
    return std::isfinite(v) && v >= minVal && v <= maxVal;
}

// Stores a reference to the underlying driver.
MeterHLD::MeterHLD(IMeterDriver &driver)
    : driver_(driver)
{
}

// Returns true if the underlying driver is connected.
bool MeterHLD::isAvailable() const
{
    return driver_.isConnected();
}

// Reads a raw measurement, validates it and populates the outgoing measurement struct.
bool MeterHLD::getMeasurement(MeterMeasurement &measurement)
{
    RawMeasurement raw{};
    const bool readOk = driver_.readMeasurement(raw);

    if (!readOk)
    {
        measurement = toMeterMeasurement(raw, false);
        return false;
    }

    const bool valid = validate(raw);
    measurement = toMeterMeasurement(raw, valid);
    return valid;
}


// Checks that every field is free of NaN/Inf and within physically plausible bounds.
bool MeterHLD::validate(const RawMeasurement &r)
{
    // Currents — non-negative, within rated range
    if (!isFiniteAndNonNegative(r.currentL1) || r.currentL1 > Limits::CURRENT_MAX_A)
        return false;
    if (!isFiniteAndNonNegative(r.currentL2) || r.currentL2 > Limits::CURRENT_MAX_A)
        return false;
    if (!isFiniteAndNonNegative(r.currentL3) || r.currentL3 > Limits::CURRENT_MAX_A)
        return false;
    if (!isFiniteAndNonNegative(r.currentAvg) || r.currentAvg > Limits::CURRENT_MAX_A)
        return false;

    // Line-to-line voltages
    if (!isFiniteAndNonNegative(r.voltageL1L2) || r.voltageL1L2 > Limits::VOLTAGE_LL_MAX_V)
        return false;
    if (!isFiniteAndNonNegative(r.voltageL2L3) || r.voltageL2L3 > Limits::VOLTAGE_LL_MAX_V)
        return false;
    if (!isFiniteAndNonNegative(r.voltageL3L1) || r.voltageL3L1 > Limits::VOLTAGE_LL_MAX_V)
        return false;
    if (!isFiniteAndNonNegative(r.voltageLLAvg) || r.voltageLLAvg > Limits::VOLTAGE_LL_MAX_V)
        return false;

    // Line-to-neutral voltages
    if (!isFiniteAndNonNegative(r.voltageL1N) || r.voltageL1N > Limits::VOLTAGE_LN_MAX_V)
        return false;
    if (!isFiniteAndNonNegative(r.voltageL2N) || r.voltageL2N > Limits::VOLTAGE_LN_MAX_V)
        return false;
    if (!isFiniteAndNonNegative(r.voltageL3N) || r.voltageL3N > Limits::VOLTAGE_LN_MAX_V)
        return false;
    if (!isFiniteAndNonNegative(r.voltageLNAvg) || r.voltageLNAvg > Limits::VOLTAGE_LN_MAX_V)
        return false;

    // Active power (can be negative for feed-in)
    if (!std::isfinite(r.activePowerL1) || std::abs(r.activePowerL1) > Limits::POWER_MAX_KW)
        return false;
    if (!std::isfinite(r.activePowerL2) || std::abs(r.activePowerL2) > Limits::POWER_MAX_KW)
        return false;
    if (!std::isfinite(r.activePowerL3) || std::abs(r.activePowerL3) > Limits::POWER_MAX_KW)
        return false;
    if (!std::isfinite(r.totalActivePower) || std::abs(r.totalActivePower) > Limits::POWER_MAX_KW)
        return false;

    // Power factor [-1, 1]
    if (!inRange(r.powerFactor, -Limits::PF_MAX, Limits::PF_MAX))
        return false;

    // Frequency [45, 65] Hz
    if (!inRange(r.frequency, Limits::FREQ_MIN_HZ, Limits::FREQ_MAX_HZ))
        return false;

    return true;
}

// Copies all fields from a RawMeasurement into a MeterMeasurement and sets the valid flag.
MeterMeasurement MeterHLD::toMeterMeasurement(const RawMeasurement &r, bool valid)
{
    MeterMeasurement m{};
    m.valid = valid;
    m.measuredAt = r.measuredAt;

    m.currentL1 = r.currentL1;
    m.currentL2 = r.currentL2;
    m.currentL3 = r.currentL3;
    m.currentAvg = r.currentAvg;

    m.voltageL1L2 = r.voltageL1L2;
    m.voltageL2L3 = r.voltageL2L3;
    m.voltageL3L1 = r.voltageL3L1;
    m.voltageLLAvg = r.voltageLLAvg;

    m.voltageL1N = r.voltageL1N;
    m.voltageL2N = r.voltageL2N;
    m.voltageL3N = r.voltageL3N;
    m.voltageLNAvg = r.voltageLNAvg;

    m.activePowerL1 = r.activePowerL1;
    m.activePowerL2 = r.activePowerL2;
    m.activePowerL3 = r.activePowerL3;
    m.totalActivePower = r.totalActivePower;

    m.powerFactor = r.powerFactor;
    m.frequency = r.frequency;

    return m;
}
