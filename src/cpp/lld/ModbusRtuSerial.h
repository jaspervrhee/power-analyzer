#pragma once

#include "interfaces/IIEM3250Communication.h"

/**
 * ModbusRtuSerial
 *
 * Concrete implementation of IIEM3250Communication using Modbus RTU
 * framing over a POSIX serial port (Linux / Raspberry Pi 4B).
 *
 * Serial settings match the IEM3250 factory defaults:
 *   19200 baud, 8 data bits, even parity, 1 stop bit (8E1).
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
