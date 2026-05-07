#include "lld/IEM3250LLD.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <thread>
#include <utility>



static constexpr int  MAX_ATTEMPTS         = 3;
static constexpr auto RETRY_DELAY          = std::chrono::milliseconds(40);

// Sentinel written to a column when its burst read fails after all retries.
// Chosen to be visually distinct and outside the plausible physical range
// for currents (≥0 A), voltages (≥0 V) and frequency (45-65 Hz). Active power
// can legitimately be negative (grid feed-in), but -9999 kW lies far outside
// the expected operating range for this installation, so the sentinel still
// stands out clearly. The row's `valid` flag (set to 0 on any fallback) is
// the authoritative invalid-marker; the sentinel just makes failed samples
// immediately recognizable when inspecting the data in a spreadsheet.
static constexpr float INVALID_SENTINEL = -9999.0f;

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
    // in the range [-2, 2] to carry quadrant information (see decode below).
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

    // Stamp the moment the sample starts. The Modbus burst takes hundreds of
    // ms; the consumer cares when the grid was sampled, not when the row was
    // logged.
    m.measuredAt = std::chrono::system_clock::now();

    // ok stays true only if every burst returned FRESH data. Any failure
    // flips it to false so the row is logged with valid=0 and the failed
    // columns carry the sentinel value.
    bool ok = true;

    // Burst 1 — currents (registers 3000..3011, 6 float pairs of which 4 used).
    ok &= readBurst(Reg::I1, 12, {
        {Reg::I1,    0,  &m.currentL1},
        {Reg::I2,    2,  &m.currentL2},
        {Reg::I3,    4,  &m.currentL3},
        {Reg::I_AVG, 10, &m.currentAvg},
    });

    // Burst 2 — line-line and line-neutral voltages (3020..3037).
    ok &= readBurst(Reg::U12, 18, {
        {Reg::U12,      0,  &m.voltageL1L2},
        {Reg::U23,      2,  &m.voltageL2L3},
        {Reg::U31,      4,  &m.voltageL3L1},
        {Reg::U_LL_AVG, 6,  &m.voltageLLAvg},
        {Reg::U1N,      8,  &m.voltageL1N},
        {Reg::U2N,      10, &m.voltageL2N},
        {Reg::U3N,      12, &m.voltageL3N},
        {Reg::U_LN_AVG, 16, &m.voltageLNAvg},
    });

    // Burst 3 — active powers (3054..3061).
    ok &= readBurst(Reg::P1, 8, {
        {Reg::P1,      0, &m.activePowerL1},
        {Reg::P2,      2, &m.activePowerL2},
        {Reg::P3,      4, &m.activePowerL3},
        {Reg::P_TOTAL, 6, &m.totalActivePower},
    });

    // Burst 4 — power factor (single float). We decode the Schneider sign
    // convention here: raw range [-2, 2] carries the quadrant; |raw| > 1
    // indicates leading (capacitive) operation. Normalised PF stays in [-1, 1].
    // On burst failure the sentinel passes through the decode untouched
    // (|sentinel| > 1) which would corrupt the displayed value, so we route
    // around it explicitly.
    float pfRaw = 0.0f;
    const bool pfFresh = readBurst(Reg::PF, 2, {
        {Reg::PF, 0, &pfRaw},
    });
    if (!pfFresh)
    {
        m.powerFactor = INVALID_SENTINEL;
    }
    else if (pfRaw > 1.0f)
    {
        m.powerFactor = 2.0f - pfRaw;
    }
    else if (pfRaw < -1.0f)
    {
        m.powerFactor = -2.0f - pfRaw;
    }
    else
    {
        m.powerFactor = pfRaw;
    }
    ok &= pfFresh;

    // Burst 5 — frequency.
    ok &= readBurst(Reg::FREQUENCY, 2, {
        {Reg::FREQUENCY, 0, &m.frequency},
    });

    return ok;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool IEM3250LLD::readBurst(uint16_t startRegAddr,
                           uint16_t regCount,
                           std::initializer_list<FloatExtract> extracts)
{
    // Apply the IEM3250 Modbus address offset on the burst's start address.
    const auto modbusAddr = static_cast<uint16_t>(
        static_cast<int>(startRegAddr) + MODBUS_OFFSET);

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; ++attempt)
    {
        std::vector<uint16_t> regs;
        const bool transportOk = comm_.readHoldingRegisters(modbusAddr, regCount, regs);

        if (transportOk && regs.size() >= regCount)
        {
            // Fresh burst — decode every requested float.
            for (const auto& ext : extracts)
            {
                *ext.outValue = decodeCDAB(regs[ext.offsetInBlock],
                                           regs[ext.offsetInBlock + 1]);
            }
            return true;
        }

        if (attempt < MAX_ATTEMPTS)
        {
            std::this_thread::sleep_for(RETRY_DELAY);
        }
    }

    // All attempts exhausted. Fill every requested float with the sentinel
    // so downstream tooling can filter the bad sample by value, not just by
    // the row's `valid` flag.
    std::cerr << "[IEM3250] burst @reg " << startRegAddr
              << " failed after " << MAX_ATTEMPTS
              << " attempts — writing sentinel " << INVALID_SENTINEL
              << " for " << extracts.size() << " column(s)\n";
    for (const auto& ext : extracts)
    {
        *ext.outValue = INVALID_SENTINEL;
    }
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
