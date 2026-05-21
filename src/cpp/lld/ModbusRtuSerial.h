#pragma once

#include <chrono>

#include "interfaces/IIEM3250Communication.h"

/**
 * ModbusRtuSerial
 *
 * Concrete implementation of IIEM3250Communication using Modbus RTU
 * framing over a POSIX serial port (Linux / Raspberry Pi 4B).
 *
 * Framing: 8 data bits, even parity, 1 stop bit (8E1). Baud rate is
 * caller-supplied (the IEM3250 supports 9600/19200/38400).
 */
class ModbusRtuSerial : public IIEM3250Communication {
public:
    ModbusRtuSerial();
    ~ModbusRtuSerial() override;

    bool connect(const std::string& port,
                 int deviceId,
                 int baudRate,
                 int timeoutMs) override;

    void disconnect() override;

    bool isConnected() const override;

    bool readHoldingRegisters(uint16_t address,
                              uint16_t count,
                              std::vector<uint16_t>& data) override;

private:
    int    fd_;        ///< File descriptor for the serial port (-1 = closed)
    int    deviceId_;
    int    timeoutMs_;

    /// Timestamp of the last byte sent or received. Used to enforce the
    /// Modbus RTU t3.5 inter-frame silence before starting a new request.
    std::chrono::steady_clock::time_point lastBusActivity_{};

    /** Build and send a Modbus RTU Read Holding Registers request. */
    bool sendReadRequest(uint16_t address, uint16_t count);

    /**
     * Receive and validate the Modbus RTU response.
     * Populates data with the register values on success.
     */
    bool receiveReadResponse(uint16_t expectedCount,
                             std::vector<uint16_t>& data);

    /** Modbus CRC16 (polynomial 0xA001, initial value 0xFFFF). */
    static uint16_t crc16(const uint8_t* buf, size_t len);
};
