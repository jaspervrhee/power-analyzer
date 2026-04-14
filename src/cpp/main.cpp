#include "lld/IEM3250LLD.h"
#include "hld/MeterHLD.h"
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

int main()
{
    // --- Stack opbouwen -------------------------------------------------------
    SerialImpl serial;
    IEM3250LLD lld(serial, "/dev/serial0", /*deviceId=*/1);
    MeterHLD hld(lld);
    Controller controller(hld, std::chrono::seconds(2));

    // --- Verbinding maken -----------------------------------------------------
    if (!lld.connect())
    {
        std::cerr << "Kan niet verbinden met IEM3250\n";
        return 1;
    }
    std::cout << "Verbonden met IEM3250\n";

    // --- Optie 1: Single poll
    controller.pollOnce();
    printMeasurement(controller.getLatestMeasurement());

    // --- Optie 2: Automatic polling with callback
    controller.onMeasurement([](const MeterMeasurement &m)
                             { printMeasurement(m); });

    std::cout.flush();
    controller.start();

    // Run for 3 seconds
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Stop
    controller.stop();

    lld.disconnect();
    return 0;
}
