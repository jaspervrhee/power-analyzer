#include "lld/IEM3250LLD.h"
#include "lld/FLOG_LLD.h"
#include "lld/BufferedLogBackend.h"
#include "hld/MeterHLD.h"
#include "hld/LogHLD.h"
#include "controller/Controller.h"

// #define USE_REAL_SERIAL  // Uncomment for real serial (Linux), comment for stub

#ifdef USE_REAL_SERIAL
#include "lld/ModbusRtuSerial.h"
using SerialImpl = ModbusRtuSerial;
#else
#ifdef _WIN32
#include "lld/ModbusRtuSerialStub.h"
using SerialImpl = ModbusRtuSerialStub;
#else
#include "lld/ModbusRtuSerial.h"
using SerialImpl = ModbusRtuSerial;
#endif
#endif

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
    if (!m.valid)
    {
        std::cout << "[Controller] Meting ongeldig\n";
        return;
    }

    std::cout << "\n=== IEM3250 Meting ===\n";
    std::cout << "Stroom  L1/L2/L3:     " << m.currentL1 << " / " << m.currentL2 << " / " << m.currentL3 << " A\n";
    std::cout << "Spanning L1-N/L2-N/L3-N: " << m.voltageL1N << " / " << m.voltageL2N << " / " << m.voltageL3N << " V\n";
    std::cout << "Spanning L1-L2/L2-L3/L3-L1: " << m.voltageL1L2 << " / " << m.voltageL2L3 << " / " << m.voltageL3L1 << " V\n";
    std::cout << "Vermogen P1/P2/P3:    " << m.activePowerL1 << " / " << m.activePowerL2 << " / " << m.activePowerL3 << " kW\n";
    std::cout << "Totaal vermogen:      " << m.totalActivePower << " kW\n";
    std::cout << "Vermogensfactor:      " << m.powerFactor << "\n";
    std::cout << "Frequentie:           " << m.frequency << " Hz\n";
}

int main(int argc, char *argv[])
{
    // --- Meter stack opbouwen -------------------------------------------------
    SerialImpl serial;
    IEM3250LLD meterLld(serial, "/dev/serial0", /*deviceId=*/1);
    MeterHLD meterHld(meterLld);

    // --- Log stack opbouwen ---------------------------------------------------
    // FLOG zit achter een buffer: valt de logServer weg, dan blijven we meten
    // en loggen we alles na zodra de verbinding terug is.
    // Meer backends? Maak een nieuw ILogBackend en voeg 'm toe met addBackend().
    FLOG_LLD           flogLld{"134.188.254.132", 17540};
    BufferedLogBackend bufferedFlog{flogLld};
    LogHLD             logHld;
    logHld.addBackend(bufferedFlog);

    // --- Controller met beide services ---------------------------------------
    // Polling interval: 1 second -> 1 Hz
    Controller controller(meterHld, std::chrono::seconds(1));
    controller.setLogService(&logHld);

    // --- Verbindingen openen --------------------------------------------------
    if (!meterLld.connect())
    {
        std::cerr << "Kan niet verbinden met IEM3250\n";
        return 1;
    }
    std::cout << "Verbonden met IEM3250\n";

    // Start de buffer-worker: die probeert zelf te (re)connecten met FLOG.
    // Het systeem draait sowieso door, ook als de logServer nu offline is.
    bufferedFlog.connect();

    // --- Optie 1: eenmalig handmatig pollen -----------------------------------
    std::cout << "\n[pollOnce] Eenmalige meting:\n";
    controller.pollOnce();
    printMeasurement(controller.getLatestMeasurement());

    // --- Optie 2: Automatic polling with callback
    controller.onMeasurement([](const MeterMeasurement &m)
                             { printMeasurement(m); });

    std::cout.flush();
    controller.start();

    // Run for 3 seconds
    // Allow runtime control: optional first arg = seconds to run.
    // If omitted, default to 3 seconds. If <= 0, run until Ctrl-C.
    std::signal(SIGINT, handleSigint);

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

    // Stop
    controller.stop();

    // bufferedFlog.disconnect() stopt de worker-thread én disconnect'et de FLOG.
    bufferedFlog.disconnect();
    meterLld.disconnect();
    return 0;
}
