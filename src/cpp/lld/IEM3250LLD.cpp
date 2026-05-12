#include "lld/IEM3250LLD.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <thread>
#include <utility>

static constexpr int  MAX_ATTEMPTS         = 5;
static constexpr auto INTER_ATTEMPT_DELAY  = std::chrono::milliseconds(50);
static constexpr float INVALID_SENTINEL    = -9999.0f;

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

    static constexpr uint16_t PF = 3084;
    static constexpr uint16_t FREQUENCY = 3110;
}

static constexpr int MODBUS_OFFSET = -2;

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

    m.measuredAt = std::chrono::system_clock::now();

    // One mega-burst covers all 18 floats: registers 3000..3111 (= 112 regs).
    // Gaps (e.g. 3012-3019, 3038-3053) are read & discarded. At 38400 baud
    // one full burst is ~80-100 ms, so MAX_ATTEMPTS retries fit easily in
    // the 1 Hz budget (~700 ms worst case).
    constexpr uint16_t startReg = Reg::I1;
    constexpr uint16_t regCount = (Reg::FREQUENCY - Reg::I1) + 2;

    const std::vector<FloatExtract> extracts = {
        {Reg::I1,        static_cast<uint16_t>(Reg::I1        - startReg), &m.currentL1},
        {Reg::I2,        static_cast<uint16_t>(Reg::I2        - startReg), &m.currentL2},
        {Reg::I3,        static_cast<uint16_t>(Reg::I3        - startReg), &m.currentL3},
        {Reg::I_AVG,     static_cast<uint16_t>(Reg::I_AVG     - startReg), &m.currentAvg},
        {Reg::U12,       static_cast<uint16_t>(Reg::U12       - startReg), &m.voltageL1L2},
        {Reg::U23,       static_cast<uint16_t>(Reg::U23       - startReg), &m.voltageL2L3},
        {Reg::U31,       static_cast<uint16_t>(Reg::U31       - startReg), &m.voltageL3L1},
        {Reg::U_LL_AVG,  static_cast<uint16_t>(Reg::U_LL_AVG  - startReg), &m.voltageLLAvg},
        {Reg::U1N,       static_cast<uint16_t>(Reg::U1N       - startReg), &m.voltageL1N},
        {Reg::U2N,       static_cast<uint16_t>(Reg::U2N       - startReg), &m.voltageL2N},
        {Reg::U3N,       static_cast<uint16_t>(Reg::U3N       - startReg), &m.voltageL3N},
        {Reg::U_LN_AVG,  static_cast<uint16_t>(Reg::U_LN_AVG  - startReg), &m.voltageLNAvg},
        {Reg::P1,        static_cast<uint16_t>(Reg::P1        - startReg), &m.activePowerL1},
        {Reg::P2,        static_cast<uint16_t>(Reg::P2        - startReg), &m.activePowerL2},
        {Reg::P3,        static_cast<uint16_t>(Reg::P3        - startReg), &m.activePowerL3},
        {Reg::P_TOTAL,   static_cast<uint16_t>(Reg::P_TOTAL   - startReg), &m.totalActivePower},
        {Reg::PF,        static_cast<uint16_t>(Reg::PF        - startReg), &m.powerFactor},
        {Reg::FREQUENCY, static_cast<uint16_t>(Reg::FREQUENCY - startReg), &m.frequency},
    };

    bool ok = false;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        if (tryBurstOnce(startReg, regCount, extracts))
        {
            ok = true;
            break;
        }
        if (attempt < MAX_ATTEMPTS)
        {
            std::this_thread::sleep_for(INTER_ATTEMPT_DELAY);
        }
    }

    if (!ok)
    {
        std::cerr << "[IEM3250] burst read of " << regCount
                  << " regs failed after " << MAX_ATTEMPTS
                  << " attempts — writing sentinel " << INVALID_SENTINEL
                  << " to all 18 values\n";
        for (const auto& ext : extracts)
        {
            *ext.outValue = INVALID_SENTINEL;
        }
    }

    if (m.powerFactor != INVALID_SENTINEL)
    {
        if (m.powerFactor > 1.0f)
            m.powerFactor = 2.0f - m.powerFactor;
        else if (m.powerFactor < -1.0f)
            m.powerFactor = -2.0f - m.powerFactor;
    }

    return ok;
}

bool IEM3250LLD::tryBurstOnce(uint16_t startReg,
                              uint16_t regCount,
                              const std::vector<FloatExtract>& extracts)
{
    const auto modbusAddr = static_cast<uint16_t>(
        static_cast<int>(startReg) + MODBUS_OFFSET);

    std::vector<uint16_t> regs;
    if (!comm_.readHoldingRegisters(modbusAddr, regCount, regs))
    {
        return false;
    }
    if (regs.size() < regCount)
    {
        return false;
    }

    for (const auto& ext : extracts)
    {
        *ext.outValue = decodeCDAB(regs[ext.offsetInBlock],
                                   regs[ext.offsetInBlock + 1]);
    }
    return true;
}

float IEM3250LLD::decodeCDAB(uint16_t regLow, uint16_t regHigh)
{
    const uint32_t raw = (static_cast<uint32_t>(regHigh) << 16) |
                         static_cast<uint32_t>(regLow);
    float f;
    std::memcpy(&f, &raw, sizeof(f));
    return f;
}
