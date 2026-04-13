#pragma once

#include "interfaces/IIEM3250Communication.h"
#include <unordered_map>

/**
 * ModbusRtuSerialStub
 *
 * Windows-compatible stub die IIEM3250Communication implementeert met
 * nep-data. Geeft realistische maar gesimuleerde meetwaarden terug zodat
 * de volledige stack (IEM3250LLD → MeterHLD → Controller) lokaal te
 * testen is zonder echte hardware of POSIX serial port.
 */
class ModbusRtuSerialStub : public IIEM3250Communication {
public:
    ModbusRtuSerialStub();

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
    bool connected_{false};

    // Fake waarden per Modbus-adres (Schneider adres - 2 offset al verrekend).
    // Elk float32 staat als {lowWord (CD), highWord (AB)} klaar.
    struct RegPair { uint16_t low; uint16_t high; };

    static RegPair encodeFloat(float value);

    // Map: Modbus-adres → nep float waarde
    static const std::unordered_map<uint16_t, float> FAKE_VALUES;
};
