#pragma once

#include <cstdint>

/**
 * Raw measurement data as delivered by IMeterDriver (IEM3250_LLD → MeterHLD).
 * Values are technically correct but not yet validated or interpreted.
 */
struct RawMeasurement {
    float currentL1;       // I1  [A]
    float currentL2;       // I2  [A]
    float currentL3;       // I3  [A]
    float currentAvg;      // I avg [A]

    float voltageL1L2;     // U12 [V]
    float voltageL2L3;     // U23 [V]
    float voltageL3L1;     // U31 [V]
    float voltageLLAvg;    // U L-L avg [V]

    float voltageL1N;      // U1  [V]
    float voltageL2N;      // U2  [V]
    float voltageL3N;      // U3  [V]
    float voltageLNAvg;    // U L-N avg [V]

    float activePowerL1;   // P1  [kW]
    float activePowerL2;   // P2  [kW]
    float activePowerL3;   // P3  [kW]
    float totalActivePower;// P   [kW]

    float powerFactor;     // PF  [-]
    float frequency;       // f   [Hz]
};

/**
 * Validated and interpreted measurement data as provided by IMeterService
 * (MeterHLD → Controller). Hardware and protocol details are fully hidden.
 */
struct MeterMeasurement {
    float currentL1;
    float currentL2;
    float currentL3;
    float currentAvg;

    float voltageL1L2;
    float voltageL2L3;
    float voltageL3L1;
    float voltageLLAvg;

    float voltageL1N;
    float voltageL2N;
    float voltageL3N;
    float voltageLNAvg;

    float activePowerL1;
    float activePowerL2;
    float activePowerL3;
    float totalActivePower;

    float powerFactor;
    float frequency;

    bool valid;  ///< true when all values passed validation
};
