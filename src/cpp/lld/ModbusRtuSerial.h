#pragma once

#include <chrono>

#include "interfaces/IIEM3250Communication.h"

/**
 * @brief Modbus RTU transport over a POSIX serial port.
 *
 * Concrete implementation of IIEM3250Communication using Modbus RTU
 * framing on Linux / Raspberry Pi 4B serial devices.
 *
 * Framing: 8 data bits, even parity, 1 stop bit (8E1). Baud rate is
 * caller-supplied (the iEM3250 supports 9600/19200/38400).
 */
class ModbusRtuSerial : public IIEM3250Communication {
public:
    /**
     * @brief Construct a disconnected transport.
     *
     * Does not touch any hardware; the serial port is opened lazily by
     * connect().
     */
    ModbusRtuSerial();

    /**
     * @brief Destructor; closes the serial port if still open.
     */
    ~ModbusRtuSerial() override;

    /// @name IIEM3250Communication implementation
    /// @{

    /**
     * @copydoc IIEM3250Communication::connect
     *
     * Opens @p port with 8E1 framing at @p baudRate and stores @p deviceId
     * and @p timeoutMs for subsequent reads.
     */
    bool connect(const std::string& port,
                 int deviceId,
                 int baudRate,
                 int timeoutMs) override;

    /**
     * @copydoc IIEM3250Communication::disconnect
     *
     * Closes the serial port and resets the file descriptor.
     */
    void disconnect() override;

    /**
     * @copydoc IIEM3250Communication::isConnected
     * @return true while the serial port is open.
     */
    bool isConnected() const override;

    /**
     * @copydoc IIEM3250Communication::readHoldingRegisters
     *
     * Enforces the Modbus RTU t3.5 inter-frame silence, sends the request
     * via sendReadRequest() and parses the reply via receiveReadResponse().
     */
    bool readHoldingRegisters(uint16_t address,
                              uint16_t count,
                              std::vector<uint16_t>& data) override;

    /// @}

private:
    int    fd_;        ///< File descriptor for the serial port (-1 when closed).
    int    deviceId_;  ///< Modbus slave ID configured at connect time.
    int    timeoutMs_; ///< Response timeout in milliseconds.

    /**
     * @brief Timestamp of the last byte sent or received.
     *
     * Used to enforce the Modbus RTU t3.5 inter-frame silence before
     * starting a new request.
     */
    std::chrono::steady_clock::time_point lastBusActivity_{};

    /**
     * @brief Build and transmit a Modbus RTU Read Holding Registers request.
     * @param address  Modbus register address (already offset by the caller).
     * @param count    Number of 16-bit registers to request.
     * @return true when the request was written to the bus successfully.
     */
    bool sendReadRequest(uint16_t address, uint16_t count);

    /**
     * @brief Receive and validate a Modbus RTU Read Holding Registers response.
     * @param expectedCount  Register count requested by sendReadRequest().
     * @param[out] data      Decoded register values on success.
     * @return true when the response was received, CRC-valid, and matched
     *         the expected slave/function/length.
     */
    bool receiveReadResponse(uint16_t expectedCount,
                             std::vector<uint16_t>& data);

    /**
     * @brief Modbus CRC16 (polynomial 0xA001, initial value 0xFFFF).
     * @param buf  Pointer to the bytes to checksum.
     * @param len  Number of bytes at @p buf.
     * @return Computed CRC, low byte first per Modbus convention.
     */
    static uint16_t crc16(const uint8_t* buf, size_t len);
};
