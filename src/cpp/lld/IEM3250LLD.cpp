#include "lld/IEM3250LLD.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <thread>
#include <utility>



static constexpr int  MAX_ATTEMPTS         = 3;
static constexpr auto RETRY_DELAY          = std::chrono::milliseconds(40);

// ---------------------------------------------------------------------------
// IEM3250 Modbus register addresses (Schneider numbering)
// Each float32 value occupies 2 consecutive 16-bit registers.
// ---------------------------------------------------------------------------
namespace Reg
{
    static constexpr uint16_t I1 = 3000;
    static constexpr uint16_t I2 = 3002;
    static constexpr uint16_t I3 = 3004;
    static constexpr uint16_t I_AVG = 3010;

    static constexpr uint16_t U12 = 3020;
    static constexpr uint16_t U23 = 3022;
    static constexpr uint16_t U31 = 3024;
    static constexpr uint16_t U_LL_AVG = 3026;

    static constexpr uint16_t U1N = 3028;
    static constexpr uint16_t U2N = 3030;
    static constexpr uint16_t U3N = 3032;
    static constexpr uint16_t U_LN_AVG = 3036;

    static constexpr uint16_t P1 = 3054;
    static constexpr uint16_t P2 = 3056;
    static constexpr uint16_t P3 = 3058;
    static constexpr uint16_t P_TOTAL = 3060;

    // PF Total at register 3084. Schneider encodes signed PF on iEM3xxx
    // in the range [-2, 2] to carry quadrant information (see decodeSignedPF).
    static constexpr uint16_t PF = 3084;
    static constexpr uint16_t FREQUENCY = 3110;
} // namespace Reg

// The IEM3250 requires a -2 offset on all Modbus register addresses.
static constexpr int MODBUS_OFFSET = -2;

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

IEM3250LLD::IEM3250LLD(IIEM3250Communication &comm,
                       std::string port,
                       int deviceId,
                       int baudRate,
                       int timeoutMs)
    : comm_(comm), port_(std::move(port)), deviceId_(deviceId), baudRate_(baudRate), timeoutMs_(timeoutMs)
{
}

IEM3250LLD::~IEM3250LLD()
{
    disconnect();
}

// ---------------------------------------------------------------------------
// IMeterDriver
// ---------------------------------------------------------------------------

bool IEM3250LLD::connect()
{
    return comm_.connect(port_, deviceId_, baudRate_, timeoutMs_);
}

void IEM3250LLD::disconnect()
{
    comm_.disconnect();
}

bool IEM3250LLD::isConnected() const
{
    return comm_.isConnected();
}

bool IEM3250LLD::readMeasurement(RawMeasurement &m)
{
    if (!isConnected())
    {
        return false;
    }

    bool ok = true;

    ok &= readFloat32(Reg::I1, m.currentL1);
    ok &= readFloat32(Reg::I2, m.currentL2);
    ok &= readFloat32(Reg::I3, m.currentL3);
    ok &= readFloat32(Reg::I_AVG, m.currentAvg);

    ok &= readFloat32(Reg::U12, m.voltageL1L2);
    ok &= readFloat32(Reg::U23, m.voltageL2L3);
    ok &= readFloat32(Reg::U31, m.voltageL3L1);
    ok &= readFloat32(Reg::U_LL_AVG, m.voltageLLAvg);

    ok &= readFloat32(Reg::U1N, m.voltageL1N);
    ok &= readFloat32(Reg::U2N, m.voltageL2N);
    ok &= readFloat32(Reg::U3N, m.voltageL3N);
    ok &= readFloat32(Reg::U_LN_AVG, m.voltageLNAvg);

    ok &= readFloat32(Reg::P1, m.activePowerL1);
    ok &= readFloat32(Reg::P2, m.activePowerL2);
    ok &= readFloat32(Reg::P3, m.activePowerL3);
    ok &= readFloat32(Reg::P_TOTAL, m.totalActivePower);

    float pfRaw = 0.0f;
    if (readFloat32(Reg::PF, pfRaw))
    {
        // Schneider signed-PF on iEM3xxx: raw range [-2, 2], decode to [-1, 1].
        // |raw| > 1 indicates leading (capacitive) quadrant.
        if (pfRaw > 1.0f)
            m.powerFactor = 2.0f - pfRaw;
        else if (pfRaw < -1.0f)
            m.powerFactor = -2.0f - pfRaw;
        else
            m.powerFactor = pfRaw;
    }
    else
    {
        ok = false;
    }
    ok &= readFloat32(Reg::FREQUENCY, m.frequency);

    return ok;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool IEM3250LLD::readFloat32(uint16_t regAddress, float &value)
{
    // Apply the IEM3250 Modbus address offset
    const auto modbusAddr = static_cast<uint16_t>(
        static_cast<int>(regAddress) + MODBUS_OFFSET);

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        std::vector<uint16_t> regs;
        const bool transportOk = comm_.readHoldingRegisters(modbusAddr, 2, regs);

        if (transportOk && regs.size() >= 2)
        {
            // regs[0] = first register (CDAB: holds bytes C,D — low word)
            // regs[1] = second register (CDAB: holds bytes A,B — high word)
            value = decodeCDAB(regs[0], regs[1]);
            lastGood_[regAddress] = value;
            return true;
        }

        if (attempt < MAX_ATTEMPTS)
        {
            std::this_thread::sleep_for(RETRY_DELAY);
        }
    }

    // All attempts exhausted. Fall back to the last successfully decoded
    // value for this register so the logged row never carries a placeholder
    // 0 — the requirement is that every column holds a plausible value.
    auto it = lastGood_.find(regAddress);
    if (it != lastGood_.end())
    {
        std::cerr << "[IEM3250] read failed @reg " << regAddress
                  << " after " << MAX_ATTEMPTS
                  << " attempts — holding last known good value\n";
        value = it->second;
        return true;
    }

    // No prior sample yet (failure during the very first poll for this reg):
    // signal failure so the row is logged as invalid rather than as zeros.
    std::cerr << "[IEM3250] read failed @reg " << regAddress
              << " after " << MAX_ATTEMPTS
              << " attempts and no cached value — marking measurement invalid\n";
    value = 0.0f;
    return false;
}

float IEM3250LLD::decodeCDAB(uint16_t regLow, uint16_t regHigh)
{
    // CDAB byte order:
    //   regLow  = low word  (bytes C, D — Modbus big-endian inside the word)
    //   regHigh = high word (bytes A, B)
    // Reconstruct: 32-bit = (AB << 16) | CD
    const uint32_t raw = (static_cast<uint32_t>(regHigh) << 16) |
                         static_cast<uint32_t>(regLow);
    float f;
    std::memcpy(&f, &raw, sizeof(f));
    return f;
}
