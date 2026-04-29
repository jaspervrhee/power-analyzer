#pragma once

#include "interfaces/IMeterDriver.h"
#include "interfaces/IIEM3250Communication.h"

#include <cstdint>
#include <string>
#include <unordered_map>

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

    // Per-register last successfully decoded value. Used as a hold-over when
    // a single register read fails after all retries, so the row never gets
    // a placeholder 0 written into it. The 50 Hz grid changes slowly compared
    // to our 1 Hz cadence, so reusing the previous sample for one cycle is a
    // safe approximation while the bus recovers.
    std::unordered_map<uint16_t, float> lastGood_;

    /**
     * Read a single float32 value from two consecutive IEM3250 registers.
     * Applies the -2 Modbus address offset and decodes CDAB byte order.
     *
     * @param regAddress  Schneider register address (e.g. 3000 for I1).
     * @param value       Output: decoded float value.
     * @return true on success.
     */
    bool readFloat32(uint16_t regAddress, float& value);

    /**
     * Decode a CDAB-ordered float32 from two raw 16-bit register values.
     * CDAB: register[0] holds the low word (bytes C,D); register[1] the high word (A,B).
     * Reconstructed 32-bit word: (AB << 16) | CD → IEEE 754 float.
     */
    static float decodeCDAB(uint16_t regLow, uint16_t regHigh);
};
