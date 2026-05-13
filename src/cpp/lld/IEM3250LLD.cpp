#include "lld/IEM3250LLD.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#endif

static constexpr int  MAX_ROUNDS         = 3;
static constexpr auto INTER_ROUND_DELAY  = std::chrono::milliseconds(100);
static constexpr float INVALID_SENTINEL  = -9999.0f;

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

namespace {

// One Modbus transaction reading exactly one float32 (= 2 registers).
// On failure we put the index back at the end of the queue, giving it the
// time of all other reads as natural backoff before the next attempt.
struct SingleRead {
    uint16_t regAddr;
    float*   outValue;
};

// ---------- Raspberry Pi GPIO fail-trigger (sysfs) ----------
// Pulses a GPIO pin HIGH on every failed Modbus transaction so an
// oscilloscope on that pin can trigger exactly on the moment of failure.
// Diagnostic only — leave it in while debugging signal integrity, remove
// the calls in tryBurstOnce when no longer needed. No-op on non-Linux
// builds (Windows stub-mode keeps compiling).
constexpr int GPIO_FAIL_PIN = 18;   // BCM numbering — change here if 18 is taken
constexpr int FAIL_PULSE_MS = 5;

#if defined(__linux__)
int  gpioValueFd = -1;
bool gpioReady   = false;

void gpioInit()
{
    if (gpioReady) return;

    char pinStr[8];
    const int pinLen = std::snprintf(pinStr, sizeof(pinStr), "%d", GPIO_FAIL_PIN);

    int fd = ::open("/sys/class/gpio/export", O_WRONLY);
    if (fd < 0) {
        std::cerr << "[IEM3250] gpio export open failed — fail-trigger disabled\n";
        return;
    }
    ::write(fd, pinStr, static_cast<size_t>(pinLen));  // EBUSY = already exported, fine
    ::close(fd);

    // udev needs a moment to chown the new gpio<N> dir.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    char dirPath[64];
    std::snprintf(dirPath, sizeof(dirPath), "/sys/class/gpio/gpio%d/direction", GPIO_FAIL_PIN);
    fd = ::open(dirPath, O_WRONLY);
    if (fd < 0) {
        std::cerr << "[IEM3250] gpio direction open failed — fail-trigger disabled\n";
        return;
    }
    ::write(fd, "out", 3);
    ::close(fd);

    char valuePath[64];
    std::snprintf(valuePath, sizeof(valuePath), "/sys/class/gpio/gpio%d/value", GPIO_FAIL_PIN);
    gpioValueFd = ::open(valuePath, O_WRONLY);
    if (gpioValueFd < 0) {
        std::cerr << "[IEM3250] gpio value open failed — fail-trigger disabled\n";
        return;
    }
    ::write(gpioValueFd, "0", 1);
    gpioReady = true;
}

void gpioPulseFailure()
{
    if (!gpioReady) return;
    ::write(gpioValueFd, "1", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(FAIL_PULSE_MS));
    ::write(gpioValueFd, "0", 1);
}

void gpioCleanup()
{
    if (gpioValueFd >= 0) {
        ::write(gpioValueFd, "0", 1);
        ::close(gpioValueFd);
        gpioValueFd = -1;
    }
    int fd = ::open("/sys/class/gpio/unexport", O_WRONLY);
    if (fd >= 0) {
        char pinStr[8];
        const int pinLen = std::snprintf(pinStr, sizeof(pinStr), "%d", GPIO_FAIL_PIN);
        ::write(fd, pinStr, static_cast<size_t>(pinLen));
        ::close(fd);
    }
    gpioReady = false;
}
#else
void gpioInit()         {}
void gpioPulseFailure() {}
void gpioCleanup()      {}
#endif

}

IEM3250LLD::IEM3250LLD(IIEM3250Communication &comm,
                       std::string port,
                       int deviceId,
                       int baudRate,
                       int timeoutMs)
    : comm_(comm), port_(std::move(port)), deviceId_(deviceId), baudRate_(baudRate), timeoutMs_(timeoutMs)
{
    gpioInit();
}

IEM3250LLD::~IEM3250LLD()
{
    disconnect();
    gpioCleanup();
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

    // 18 separate single-float reads. PF is read raw into m.powerFactor;
    // the Schneider sign-decode is applied below the rotation loop so it
    // doesn't run on the sentinel when PF fails permanently.
    std::vector<SingleRead> reads = {
        {Reg::I1,        &m.currentL1},
        {Reg::I2,        &m.currentL2},
        {Reg::I3,        &m.currentL3},
        {Reg::I_AVG,     &m.currentAvg},
        {Reg::U12,       &m.voltageL1L2},
        {Reg::U23,       &m.voltageL2L3},
        {Reg::U31,       &m.voltageL3L1},
        {Reg::U_LL_AVG,  &m.voltageLLAvg},
        {Reg::U1N,       &m.voltageL1N},
        {Reg::U2N,       &m.voltageL2N},
        {Reg::U3N,       &m.voltageL3N},
        {Reg::U_LN_AVG,  &m.voltageLNAvg},
        {Reg::P1,        &m.activePowerL1},
        {Reg::P2,        &m.activePowerL2},
        {Reg::P3,        &m.activePowerL3},
        {Reg::P_TOTAL,   &m.totalActivePower},
        {Reg::PF,        &m.powerFactor},
        {Reg::FREQUENCY, &m.frequency},
    };

    // Queue of indices into reads. A failed read is put at the back of the
    // queue for the next round; the natural backoff (time the rest of the
    // queue takes to drain) is enough to outlast typical EMI bursts and
    // slave busy-pockets.
    std::vector<size_t> pending;
    pending.reserve(reads.size());
    for (size_t i = 0; i < reads.size(); ++i)
    {
        pending.push_back(i);
    }

    for (int round = 1; round <= MAX_ROUNDS && !pending.empty(); ++round)
    {
        std::vector<size_t> stillFailing;
        for (size_t idx : pending)
        {
            const auto& r = reads[idx];
            std::vector<FloatExtract> extracts = { {r.regAddr, 0, r.outValue} };
            if (!tryBurstOnce(r.regAddr, 2, extracts))
            {
                stillFailing.push_back(idx);
            }
        }
        pending = std::move(stillFailing);

        if (round < MAX_ROUNDS && !pending.empty())
        {
            std::this_thread::sleep_for(INTER_ROUND_DELAY);
        }
    }

    const bool ok = pending.empty();
    for (size_t idx : pending)
    {
        const auto& r = reads[idx];
        std::cerr << "[IEM3250] reg " << r.regAddr
                  << " failed after " << MAX_ROUNDS
                  << " rounds — writing sentinel " << INVALID_SENTINEL << '\n';
        *r.outValue = INVALID_SENTINEL;
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
        gpioPulseFailure();
        return false;
    }
    if (regs.size() < regCount)
    {
        gpioPulseFailure();
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
