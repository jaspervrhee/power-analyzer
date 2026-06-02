#include "lld/IEM3250LLD.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <random>
#include <thread>
#include <utility>

#if defined(__linux__)
#include <gpiod.h>
#endif


static constexpr auto MEASUREMENT_BUDGET = std::chrono::milliseconds(900);
static constexpr float INVALID_SENTINEL  = -9999.0f;
static constexpr int SACRIFICIAL_THRESHOLD = 3;

static std::chrono::milliseconds retryBackoff(int failCount, std::mt19937& rng)
{
    // Pick base delay + jitter for the current retry attempt
    int base = 0;
    int jitter = 0;
    switch (failCount)
    {
    case 0:  return std::chrono::milliseconds(0);
    case 1:  base = 20;  jitter = 10; break;
    case 2:  base = 40;  jitter = 20; break;
    case 3:  base = 80;  jitter = 40; break;
    default: base = 150; jitter = 50; break;
    }
    // Add random jitter to avoid lock-step retry storms
    std::uniform_int_distribution<int> dist(0, jitter);
    return std::chrono::milliseconds(base + dist(rng));
}

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

struct SingleRead {
    uint16_t regAddr;
    float*   outValue;
};

struct CycleCounters {
    uint64_t totalCycles      = 0;
    uint64_t completeCycles   = 0;
    uint64_t incompleteCycles = 0;
    uint64_t totalSentinels   = 0;
    uint64_t worstSentinels   = 0;  // largest single-cycle sentinel count seen
};

CycleCounters cycleCounters;

constexpr uint64_t CYCLE_LOG_EVERY = 60;  // ~once per minute at 1 Hz

void logCycleStats()
{
    const auto& c = cycleCounters;
    const double completePct = c.totalCycles
        ? (100.0 * static_cast<double>(c.completeCycles)
                 / static_cast<double>(c.totalCycles))
        : 0.0;
    std::cerr << "[IEM3250 cycles] total=" << c.totalCycles
              << " complete=" << c.completeCycles
              << " incomplete=" << c.incompleteCycles
              << " sentinels=" << c.totalSentinels
              << " worst=" << c.worstSentinels
              << " | complete=" << std::fixed << std::setprecision(2)
              << completePct << "%\n";
}


constexpr const char* GPIO_CHIP_PATH = "/dev/gpiochip0";
constexpr unsigned    GPIO_FAIL_PIN  = 18;   // BCM offset — header pin 12
constexpr int         FAIL_PULSE_MS  = 5;

#if defined(__linux__)
gpiod_chip*         gpioChip    = nullptr;
gpiod_line_request* gpioRequest = nullptr;

void gpioInit()
{
    // Already initialized
    if (gpioRequest) return;

    // Open the gpiochip device
    gpioChip = gpiod_chip_open(GPIO_CHIP_PATH);
    if (!gpioChip) {
        std::cerr << "[IEM3250] gpiod_chip_open(\"" << GPIO_CHIP_PATH
                  << "\") failed — fail-trigger disabled\n";
        return;
    }

    // Allocate libgpiod config objects
    gpiod_line_settings* settings = gpiod_line_settings_new();
    gpiod_line_config*   lineCfg  = gpiod_line_config_new();
    gpiod_request_config* reqCfg  = gpiod_request_config_new();
    if (!settings || !lineCfg || !reqCfg) {
        std::cerr << "[IEM3250] gpiod alloc failed — fail-trigger disabled\n";
        if (reqCfg)   gpiod_request_config_free(reqCfg);
        if (lineCfg)  gpiod_line_config_free(lineCfg);
        if (settings) gpiod_line_settings_free(settings);
        gpiod_chip_close(gpioChip);
        gpioChip = nullptr;
        return;
    }

    // Configure pin as output, initially low
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    // Attach the settings to our single fail-trigger line
    const unsigned offset = GPIO_FAIL_PIN;
    if (gpiod_line_config_add_line_settings(lineCfg, &offset, 1, settings) < 0) {
        std::cerr << "[IEM3250] gpiod_line_config_add_line_settings failed — fail-trigger disabled\n";
        gpiod_request_config_free(reqCfg);
        gpiod_line_config_free(lineCfg);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(gpioChip);
        gpioChip = nullptr;
        return;
    }

    // Tag the request so other tools can see who owns the line
    gpiod_request_config_set_consumer(reqCfg, "power-analyzer");

    // Actually claim the line
    gpioRequest = gpiod_chip_request_lines(gpioChip, reqCfg, lineCfg);

    // Free the temporary config objects (the request now owns the state)
    gpiod_request_config_free(reqCfg);
    gpiod_line_config_free(lineCfg);
    gpiod_line_settings_free(settings);

    if (!gpioRequest) {
        std::cerr << "[IEM3250] gpiod_chip_request_lines failed (line busy?) — fail-trigger disabled\n";
        gpiod_chip_close(gpioChip);
        gpioChip = nullptr;
        return;
    }
}

void gpioPulseFailure()
{
    if (!gpioRequest) return;
    // Drive the fail-trigger line high for a short pulse, then back low
    gpiod_line_request_set_value(gpioRequest, GPIO_FAIL_PIN, GPIOD_LINE_VALUE_ACTIVE);
    std::this_thread::sleep_for(std::chrono::milliseconds(FAIL_PULSE_MS));
    gpiod_line_request_set_value(gpioRequest, GPIO_FAIL_PIN, GPIOD_LINE_VALUE_INACTIVE);
}

void gpioCleanup()
{
    // Release the line first (leave it low to be safe)
    if (gpioRequest) {
        gpiod_line_request_set_value(gpioRequest, GPIO_FAIL_PIN, GPIOD_LINE_VALUE_INACTIVE);
        gpiod_line_request_release(gpioRequest);
        gpioRequest = nullptr;
    }
    // Then close the chip handle
    if (gpioChip) {
        gpiod_chip_close(gpioChip);
        gpioChip = nullptr;
    }
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

bool IEM3250LLD::readMeasurement(MeterMeasurement &m)
{
    if (!isConnected())
    {
        return false;
    }

    // Stamp the measurement with the moment the cycle started
    m.measuredAt = std::chrono::system_clock::now();

    // IEM3250 does not measure Q and S — zero them out
    m.reactivePowerL1 = 0.0f;
    m.reactivePowerL2 = 0.0f;
    m.reactivePowerL3 = 0.0f;
    m.totalReactivePower = 0.0f;
    m.apparentPowerL1 = 0.0f;
    m.apparentPowerL2 = 0.0f;
    m.apparentPowerL3 = 0.0f;
    m.totalApparentPower = 0.0f;

    // Map of register -> destination field for this cycle
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

    // Hard deadline for the whole cycle
    const auto deadline = std::chrono::steady_clock::now() + MEASUREMENT_BUDGET;

    // Start with every read pending
    std::vector<size_t> pending;
    pending.reserve(reads.size());
    for (size_t i = 0; i < reads.size(); ++i)
    {
        pending.push_back(i);
    }

    // Per-register failure counter (drives backoff and sacrificial reads)
    std::vector<int> failCount(reads.size(), 0);

    static std::mt19937 rng{std::random_device{}()};

    // Retry rounds until everything is read or the deadline hits
    int round = 0;
    while (!pending.empty())
    {
        ++round;
        std::vector<size_t> stillFailing;
        for (size_t idx : pending)
        {
            // Out of budget — give up on this read
            if (std::chrono::steady_clock::now() >= deadline)
            {
                stillFailing.push_back(idx);
                continue;
            }

            // Apply backoff before retrying a previously failed read
            if (failCount[idx] > 0)
            {
                const auto backoff = retryBackoff(failCount[idx], rng);
                if (std::chrono::steady_clock::now() + backoff < deadline)
                {
                    std::this_thread::sleep_for(backoff);
                }
            }

            // After repeated failures, do a throwaway read to nudge the bus
            if (failCount[idx] >= SACRIFICIAL_THRESHOLD)
            {
                const size_t sacIdx = (idx + 1) % reads.size();
                const auto sacAddr = static_cast<uint16_t>(
                    static_cast<int>(reads[sacIdx].regAddr) + MODBUS_OFFSET);
                std::vector<uint16_t> sacRegs;
                comm_.readHoldingRegisters(sacAddr, 2, sacRegs); // result discarded
            }

            // The actual read attempt
            const auto& r = reads[idx];
            std::vector<FloatExtract> extracts = { {r.regAddr, 0, r.outValue} };
            if (!tryBurstOnce(r.regAddr, 2, extracts))
            {
                ++failCount[idx];
                stillFailing.push_back(idx);
            }
        }
        pending = std::move(stillFailing);

        // Stop conditions
        if (pending.empty()) break;
        if (std::chrono::steady_clock::now() >= deadline) break;
    }

    const bool ok = pending.empty();

    // Report unfinished reads
    if (!pending.empty())
    {
        std::cerr << "[IEM3250] " << pending.size() << " of " << reads.size()
                  << " reads unfinished after " << round << " rounds (deadline) — "
                  << "writing sentinel " << INVALID_SENTINEL << " to those\n";
    }
    // Fill the unfinished slots with the invalid sentinel
    for (size_t idx : pending)
    {
        const auto& r = reads[idx];
        *r.outValue = INVALID_SENTINEL;
    }

    // Update cycle stats
    ++cycleCounters.totalCycles;
    if (ok)
    {
        ++cycleCounters.completeCycles;
    }
    else
    {
        ++cycleCounters.incompleteCycles;
        const uint64_t n = pending.size();
        cycleCounters.totalSentinels += n;
        if (n > cycleCounters.worstSentinels) cycleCounters.worstSentinels = n;
    }
    // Periodic stats dump
    if (cycleCounters.totalCycles % CYCLE_LOG_EVERY == 0)
    {
        logCycleStats();
    }

    // IEM3250 reports PF outside [-1,1] for leading/lagging — fold it back
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
    // Convert the datasheet register number to a Modbus PDU address
    const auto modbusAddr = static_cast<uint16_t>(
        static_cast<int>(startReg) + MODBUS_OFFSET);

    // Issue the burst read
    std::vector<uint16_t> regs;
    if (!comm_.readHoldingRegisters(modbusAddr, regCount, regs))
    {
        gpioPulseFailure();
        return false;
    }
    // Short reply — treat as a failure too
    if (regs.size() < regCount)
    {
        gpioPulseFailure();
        return false;
    }

    // Decode every requested float out of the register block
    for (const auto& ext : extracts)
    {
        *ext.outValue = decodeCDAB(regs[ext.offsetInBlock],
                                   regs[ext.offsetInBlock + 1]);
    }
    return true;
}

float IEM3250LLD::decodeCDAB(uint16_t regLow, uint16_t regHigh)
{
    // Recombine the two 16-bit words into a 32-bit IEEE-754 float (CDAB order)
    const uint32_t raw = (static_cast<uint32_t>(regHigh) << 16) |
                         static_cast<uint32_t>(regLow);
    float f;
    std::memcpy(&f, &raw, sizeof(f));
    return f;
}
