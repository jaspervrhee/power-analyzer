#pragma once

#include <chrono>
#include <cstdint>


/**
 * @brief Raw, technically-correct measurement produced by a meter driver (LLD).
 *
 * Contains values decoded from the meter's registers without any validation
 * or business-logic conversion. The MeterHLD validates these values before
 * promoting them to a MeterMeasurement.
 */
struct RawMeasurement {
    std::chrono::system_clock::time_point measuredAt;  ///< Acquisition time of the snapshot.

    float currentL1;        ///< I1  [A]
    float currentL2;        ///< I2  [A]
    float currentL3;        ///< I3  [A]
    float currentAvg;       ///< I avg [A]

    float voltageL1L2;      ///< U12 [V]
    float voltageL2L3;      ///< U23 [V]
    float voltageL3L1;      ///< U31 [V]
    float voltageLLAvg;     ///< U L-L avg [V]

    float voltageL1N;       ///< U1  [V]
    float voltageL2N;       ///< U2  [V]
    float voltageL3N;       ///< U3  [V]
    float voltageLNAvg;     ///< U L-N avg [V]

    float activePowerL1;    ///< P1  [kW]
    float activePowerL2;    ///< P2  [kW]
    float activePowerL3;    ///< P3  [kW]
    float totalActivePower; ///< P   [kW]

    float powerFactor;      ///< PF  [-]
    float frequency;        ///< f   [Hz]
};


/**
 * @brief Validated measurement snapshot exposed to the Controller and loggers.
 *
 * Mirrors RawMeasurement but adds a @c valid flag indicating whether the
 * values passed range/NaN checks in the HLD.
 */
struct MeterMeasurement {
    /**
     * @brief Measurement timestamp copied from RawMeasurement::measuredAt.
     *
     * Propagated through validation so logging can record the actual
     * measurement time instead of the log dispatch time.
     */
    std::chrono::system_clock::time_point measuredAt;

    float currentL1;        ///< I1  [A]
    float currentL2;        ///< I2  [A]
    float currentL3;        ///< I3  [A]
    float currentAvg;       ///< I avg [A]

    float voltageL1L2;      ///< U12 [V]
    float voltageL2L3;      ///< U23 [V]
    float voltageL3L1;      ///< U31 [V]
    float voltageLLAvg;     ///< U L-L avg [V]

    float voltageL1N;       ///< U1  [V]
    float voltageL2N;       ///< U2  [V]
    float voltageL3N;       ///< U3  [V]
    float voltageLNAvg;     ///< U L-N avg [V]

    float activePowerL1;    ///< P1  [kW]
    float activePowerL2;    ///< P2  [kW]
    float activePowerL3;    ///< P3  [kW]
    float totalActivePower; ///< P   [kW]

    float powerFactor;      ///< PF  [-]
    float frequency;        ///< f   [Hz]

    bool valid;             ///< true when all values passed validation.
};
