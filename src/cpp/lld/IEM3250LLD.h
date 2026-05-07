#pragma once

#include "interfaces/IMeterDriver.h"
#include "interfaces/IIEM3250Communication.h"

#include <cstdint>
#include <initializer_list>
#include <string>

/**
 * IEM3250_LLD — Low Level Driver for the Schneider iEM3250 energy meter.
 *
 * Implements IMeterDriver by consuming IIEM3250Communication (Modbus RTU).
 * Responsibilities:
 *  - Apply the IEM3250-specific Modbus address offset (-2).
 *  - Decode CDAB-ordered float32 register pairs.
 *  - Read all measurement registers and populate a RawMeasurement.
 *
 * No validation or business logic is performed here.
 */
class IEM3250LLD : public IMeterDriver {
public:
    /**
     * @param comm      The communication layer (e.g. ModbusRtuSerial).
     * @param port      Serial port path (e.g. "/dev/serial0").
     * @param deviceId  Modbus slave ID of the iEM3250 (default: 1).
     * @param baudRate  Baud rate (default: 19200 — IEM3250 factory setting).
     * @param timeoutMs Response timeout in milliseconds (default: 2000).
     */
    IEM3250LLD(IIEM3250Communication& comm,
               std::string port     = "/dev/serial0",
               int deviceId         = 1,
               int baudRate         = 19200,
               int timeoutMs        = 2000);

    ~IEM3250LLD() override;

    // --- IMeterDriver ----------------------------------------------------------
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    bool readMeasurement(RawMeasurement& measurement) override;

private:
    IIEM3250Communication& comm_;
    std::string port_;
    int deviceId_;
    int baudRate_;
    int timeoutMs_;

    // One float32 to extract from a burst response.
    //   regAddr        — Schneider register address (logged on failure).
    //   offsetInBlock  — index of the LOW word inside the burst's reg vector.
    //                    The HIGH word sits at offsetInBlock + 1.
    //   outValue       — destination for the decoded float (or sentinel on fail).
    struct FloatExtract {
        uint16_t regAddr;
        uint16_t offsetInBlock;
        float*   outValue;
    };

    /**
     * Read a contiguous register block in one Modbus transaction and decode
     * the requested float32 values from CDAB pairs.
     *
     * On failure of the burst itself (after MAX_ATTEMPTS retries), each
     * requested float is set to a clearly-invalid sentinel value so that
     * downstream consumers (CSV/Excel/FLOG) can filter or visually identify
     * failed samples. The row is also marked invalid via the return value.
     *
     * @return true iff every requested float came from a fresh burst.
     *         false on any failure — output values are filled with sentinels.
     */
    bool readBurst(uint16_t startRegAddr,
                   uint16_t regCount,
                   std::initializer_list<FloatExtract> extracts);

    /**
     * Decode a CDAB-ordered float32 from two raw 16-bit register values.
     * CDAB: register[0] holds the low word (bytes C,D); register[1] the high word (A,B).
     * Reconstructed 32-bit word: (AB << 16) | CD → IEEE 754 float.
     */
    static float decodeCDAB(uint16_t regLow, uint16_t regHigh);
};
