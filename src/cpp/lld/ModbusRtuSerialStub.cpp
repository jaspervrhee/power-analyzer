#include "lld/ModbusRtuSerialStub.h"

#include <cstring>
#include <iostream>

// ---------------------------------------------------------------------------
// Nep-waarden per Modbus-adres (Schneider-adres minus 2 offset)
//
// Formule: Modbus-adres = Schneider-adres - 2
//   I1        3000 - 2 = 2998
//   I2        3002 - 2 = 3000
//   I3        3004 - 2 = 3002
//   I_AVG     3010 - 2 = 3008
//   U12       3020 - 2 = 3018
//   U23       3022 - 2 = 3020
//   U31       3024 - 2 = 3022
//   U_LL_AVG  3026 - 2 = 3024
//   U1N       3028 - 2 = 3026
//   U2N       3030 - 2 = 3028
//   U3N       3032 - 2 = 3030
//   U_LN_AVG  3036 - 2 = 3034
//   P1        3054 - 2 = 3052
//   P2        3056 - 2 = 3054
//   P3        3058 - 2 = 3056
//   P_TOTAL   3060 - 2 = 3058
//   PF        3084 - 2 = 3082
//   FREQUENCY 3110 - 2 = 3108
// ---------------------------------------------------------------------------
const std::unordered_map<uint16_t, float> ModbusRtuSerialStub::FAKE_VALUES = {
    {2998, 10.5f},   // I1        [A]
    {3000, 10.3f},   // I2        [A]
    {3002, 10.7f},   // I3        [A]
    {3008, 10.5f},   // I avg     [A]
    {3018, 400.1f},  // U12       [V]
    {3020, 399.8f},  // U23       [V]
    {3022, 400.3f},  // U31       [V]
    {3024, 400.1f},  // U LL avg  [V]
    {3026, 230.2f},  // U1N       [V]
    {3028, 229.9f},  // U2N       [V]
    {3030, 230.5f},  // U3N       [V]
    {3034, 230.2f},  // U LN avg  [V]
    {3052,   2.4f},  // P1        [kW]
    {3054,   2.3f},  // P2        [kW]
    {3056,   2.5f},  // P3        [kW]
    {3058,   7.2f},  // P total   [kW]
    {3082,   0.95f}, // PF        [-]
    {3108,  50.0f},  // Frequency [Hz]
};

// ---------------------------------------------------------------------------

ModbusRtuSerialStub::ModbusRtuSerialStub() = default;

bool ModbusRtuSerialStub::connect(const std::string& port,
                                   int deviceId,
                                   int /*baudRate*/,
                                   int /*timeoutMs*/)
{
    std::cout << "[Stub] connect() op poort " << port
              << " device " << deviceId << "\n";
    connected_ = true;
    return true;
}

void ModbusRtuSerialStub::disconnect()
{
    std::cout << "[Stub] disconnect()\n";
    connected_ = false;
}

bool ModbusRtuSerialStub::isConnected() const
{
    return connected_;
}

bool ModbusRtuSerialStub::readHoldingRegisters(uint16_t address,
                                                uint16_t count,
                                                std::vector<uint16_t>& data)
{
    if (!connected_ || count < 2) {
        return false;
    }

    auto it = FAKE_VALUES.find(address);
    if (it == FAKE_VALUES.end()) {
        // Adres niet bekend: stuur nul-registers terug
        data.assign(count, 0);
        return true;
    }

    RegPair pair = encodeFloat(it->second);
    data.resize(count);
    data[0] = pair.low;   // CD — low word
    data[1] = pair.high;  // AB — high word
    return true;
}

// ---------------------------------------------------------------------------
// CDAB encoding: float → {lowWord (CD), highWord (AB)}
// Spiegelbeeld van IEM3250LLD::decodeCDAB()
// ---------------------------------------------------------------------------
ModbusRtuSerialStub::RegPair ModbusRtuSerialStub::encodeFloat(float value)
{
    uint32_t raw;
    std::memcpy(&raw, &value, sizeof(raw));

    RegPair pair;
    pair.low  = static_cast<uint16_t>(raw & 0xFFFF);         // CD
    pair.high = static_cast<uint16_t>((raw >> 16) & 0xFFFF); // AB
    return pair;
}
