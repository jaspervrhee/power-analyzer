#pragma once

#include "interfaces/IMeterDriver.h"
#include "interfaces/IIEM3250Communication.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Low Level Driver for the Schneider iEM3250 energy meter.
 *
 * Implements IMeterDriver by issuing Modbus burst reads via an injected
 * IIEM3250Communication transport. Decodes the iEM3250's CDAB-ordered
 * float32 registers into a MeterMeasurement. Performs no validation —
 * range/finiteness checks live in MeterHLD.
 */
class IEM3250LLD : public IMeterDriver {
public:
    /**
     * @brief Construct an iEM3250 driver bound to a transport.
     *
     * @param comm       Communication transport (Modbus RTU or stub).
     *                   Caller retains ownership; must outlive this driver.
     * @param port       Serial port path passed through to @p comm.
     * @param deviceId   Modbus slave ID of the meter.
     * @param baudRate   Serial baud rate (iEM3250 supports 9600/19200/38400).
     * @param timeoutMs  Modbus response timeout in milliseconds.
     */
    IEM3250LLD(IIEM3250Communication& comm,
               std::string port     = "/dev/serial0",
               int deviceId         = 1,
               int baudRate         = 38400,
               int timeoutMs        = 30);

    ~IEM3250LLD() override;

    /// @name IMeterDriver implementation
    /// @{

    /**
     * @copydoc IMeterDriver::connect
     *
     * Opens the underlying communication transport with the configured
     * port, device ID, baud rate and timeout.
     */
    bool connect() override;

    /// @copydoc IMeterDriver::disconnect
    void disconnect() override;

    /// @copydoc IMeterDriver::isConnected
    bool isConnected() const override;

    /**
     * @copydoc IMeterDriver::readMeasurement
     *
     * Issues the configured Modbus burst reads, decodes each CDAB-encoded
     * float32 and populates @p measurement. Any failed burst leaves the
     * affected fields at their sentinel value and causes the call to
     * return false.
     */
    bool readMeasurement(MeterMeasurement& measurement) override;

    /// @}

    /**
     * @brief Specification of one float32 to extract from a burst read.
     *
     * Public because the .cpp anonymous-namespace burst-spec table needs to
     * name it. Each instance describes where a single float lives inside a
     * (possibly multi-register) Modbus response and where to write it.
     */
    struct FloatExtract {
        uint16_t regAddr;       ///< Schneider register address (used in failure logs).
        uint16_t offsetInBlock; ///< Index of the LOW word inside the response; HIGH word at +1.
        float*   outValue;      ///< Destination for the decoded float (or sentinel on failure).
    };

private:
    IIEM3250Communication& comm_;       ///< Modbus transport (real serial or stub).
    std::string port_;                  ///< Serial port path passed to @c comm_.connect().
    int deviceId_;                      ///< Modbus slave ID of the meter.
    int baudRate_;                      ///< Serial baud rate.
    int timeoutMs_;                     ///< Modbus response timeout in milliseconds.

    /**
     * @brief Issue one Modbus burst and extract the requested floats.
     *
     * No internal retries — readMeasurement() re-queues failures to give
     * them a long natural backoff.
     *
     * @param startReg  First Schneider register address in the burst.
     * @param regCount  Number of 16-bit registers to read.
     * @param extracts  Float extraction specs to apply to the response.
     * @return true when the Modbus transaction succeeded.
     */
    bool tryBurstOnce(uint16_t startReg,
                      uint16_t regCount,
                      const std::vector<FloatExtract>& extracts);

    /**
     * @brief Decode a CDAB-ordered float32 from two Modbus registers.
     * @param regLow   Low-order word (C,D bytes).
     * @param regHigh  High-order word (A,B bytes).
     * @return Decoded IEEE-754 single-precision float.
     */
    static float decodeCDAB(uint16_t regLow, uint16_t regHigh);
};
