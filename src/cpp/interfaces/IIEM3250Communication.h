#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * IEM3250_communcation
 *
 * Defines low-level communication with the iEM3250 energy meter.
 * Implemented by specific transport drivers (e.g. Modbus RTU over serial).
 * Delivers raw 16-bit register values to the LLD.
 */
class IIEM3250Communication {
public:
    virtual ~IIEM3250Communication() = default;

    /**
     * Open the physical connection to the iEM3250.
     *
     * @param port      Serial port path (e.g. "/dev/serial0")
     * @param deviceId  Modbus slave / device ID
     * @param baudRate  Baud rate (IEM3250 default: 19200)
     * @param timeoutMs Response timeout in milliseconds
     * @return true on success
     */
    virtual bool connect(const std::string& port,
                         int deviceId,
                         int baudRate,
                         int timeoutMs) = 0;

    /** Close the physical connection. */
    virtual void disconnect() = 0;

    /** @return true when a connection is currently open. */
    virtual bool isConnected() const = 0;

    /**
     * Read a block of holding registers from the iEM3250.
     *
     * @param address  Schneider register address (driver applies the -2 offset internally)
     * @param count    Number of 16-bit registers to read
     * @param data     Output: the raw register values
     * @return true on success
     */
    virtual bool readHoldingRegisters(uint16_t address,
                                      uint16_t count,
                                      std::vector<uint16_t>& data) = 0;
};
