#pragma once

#include <chrono>
#include <cstdint>


/**
 * @brief Measurement snapshot produced by a meter driver and validated by the HLD.
 *
 * Contains values decoded from the meter's registers. The @c valid flag
 * indicates whether the values passed range/NaN checks in the HLD.
 */
struct MeterMeasurement {
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

    bool valid = false;     ///< true when all values passed validation.
};
