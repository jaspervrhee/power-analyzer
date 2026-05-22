#include "lld/IEM3250LLD.h"
#include "lld/FLOG_LLD.h"
#include "lld/BufferedLogBackend.h"
#include "lld/ModbusRtuSerial.h"
#include "hld/MeterHLD.h"
#include "hld/LogHLD.h"
#include "controller/Controller.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>

// Signal flag used when running until Ctrl-C
static volatile std::sig_atomic_t keepRunning = 1;

static void handleSigint(int /*sig*/)
{
    keepRunning = 0;
}

static void printMeasurement(const MeterMeasurement &m)
{
    // Invalid measurements get a one-line marker instead of the full dump
    if (!m.valid)
    {
        std::cout << "[Controller] Measurement invalid\n";
        return;
    }

    // Full multi-line dump of all measured quantities
    std::cout << "\n=== IEM3250 Measurement ===\n";
    std::cout << "Current  L1/L2/L3:     " << m.currentL1 << " / " << m.currentL2 << " / " << m.currentL3 << " A\n";
    std::cout << "Voltage L1-N/L2-N/L3-N: " << m.voltageL1N << " / " << m.voltageL2N << " / " << m.voltageL3N << " V\n";
    std::cout << "Voltage L1-L2/L2-L3/L3-L1: " << m.voltageL1L2 << " / " << m.voltageL2L3 << " / " << m.voltageL3L1 << " V\n";
    std::cout << "Power P1/P2/P3:        " << m.activePowerL1 << " / " << m.activePowerL2 << " / " << m.activePowerL3 << " kW\n";
    std::cout << "Total power:           " << m.totalActivePower << " kW\n";
    std::cout << "Power factor:          " << m.powerFactor << "\n";
    std::cout << "Frequency:             " << m.frequency << " Hz\n";
}

int main(int argc, char *argv[])
{
    // Meter stack: serial transport -> driver -> high-level service
    ModbusRtuSerial serial;
    IEM3250LLD meterLld(serial, "/dev/serial0", /*deviceId=*/1);
    MeterHLD meterHld(meterLld);

    // FLOG stack: TCP backend wrapped in a buffer, exposed via LogHLD
    FLOG_LLD           flogLld{"134.188.254.132", 17540};
    BufferedLogBackend bufferedFlog{flogLld, 100000};  // ~28h buffer at 1Hz, ~150MB RAM
    LogHLD             logHld;
    logHld.addBackend(bufferedFlog);

    // Wire the controller: poll the meter at 1 Hz and log every cycle
    Controller controller(meterHld, std::chrono::seconds(1));
    controller.setLogService(&logHld);

    // Bring up the meter link first — without it there's nothing to poll
    if (!meterLld.connect())
    {
        std::cerr << "Cannot connect to IEM3250\n";
        return 1;
    }
    std::cout << "Connected to IEM3250\n";

    // Start the logging worker thread (FLOG connect happens lazily inside)
    bufferedFlog.connect();

    // One synchronous poll for immediate feedback at startup
    std::cout << "\n[pollOnce] Single measurement:\n";
    controller.pollOnce();
    printMeasurement(controller.getLatestMeasurement());

    // Hook printing into every subsequent measurement
    controller.onMeasurement([](const MeterMeasurement &m)
                             { printMeasurement(m); });

    std::cout.flush();
    // Kick off the polling thread
    controller.start();

    // Allow Ctrl-C to gracefully exit the run-forever path
    std::signal(SIGINT, handleSigint);

    // Parse optional run-duration argument (seconds)
    int runSeconds = 3;
    if (argc > 1)
    {
        try
        {
            runSeconds = std::stoi(argv[1]);
        }
        catch (...)
        {
            runSeconds = 3;
        }
    }

    // Either run for a fixed duration, or block until Ctrl-C
    if (runSeconds > 0)
    {
        std::this_thread::sleep_for(std::chrono::seconds(runSeconds));
    }
    else
    {
        std::cout << "Running until Ctrl-C (press Ctrl-C to stop)\n";
        while (keepRunning)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    // Shut down in reverse order of bring-up
    controller.stop();
    bufferedFlog.disconnect();
    meterLld.disconnect();
    return 0;
}
