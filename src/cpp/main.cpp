#include "lld/IEM3250LLD.h"
#include "lld/FLOG_LLD.h"
#include "hld/MeterHLD.h"
#include "hld/LogHLD.h"
#include "controller/Controller.h"

#ifdef _WIN32
#  include "lld/ModbusRtuSerialStub.h"
   using SerialImpl = ModbusRtuSerialStub;
#else
#  include "lld/ModbusRtuSerial.h"
   using SerialImpl = ModbusRtuSerial;
#endif

#include <iostream>
#include <chrono>
#include <thread>

static void printMeasurement(const MeterMeasurement& m)
{
    if (!m.valid) {
        std::cout << "[Controller] Meting ongeldig\n";
        return;
    }

    std::cout << "\n=== IEM3250 Meting ===\n";
    std::cout << "Stroom  L1/L2/L3:     " << m.currentL1   << " / " << m.currentL2   << " / " << m.currentL3   << " A\n";
    std::cout << "Spanning L1-N/L2-N/L3-N: " << m.voltageL1N << " / " << m.voltageL2N << " / " << m.voltageL3N << " V\n";
    std::cout << "Spanning L1-L2/L2-L3/L3-L1: " << m.voltageL1L2 << " / " << m.voltageL2L3 << " / " << m.voltageL3L1 << " V\n";
    std::cout << "Vermogen P1/P2/P3:    " << m.activePowerL1 << " / " << m.activePowerL2 << " / " << m.activePowerL3 << " kW\n";
    std::cout << "Totaal vermogen:      " << m.totalActivePower << " kW\n";
    std::cout << "Vermogensfactor:      " << m.powerFactor << "\n";
    std::cout << "Frequentie:           " << m.frequency << " Hz\n";
}

int main()
{
    // --- Meter stack opbouwen -------------------------------------------------
    SerialImpl  serial;
    IEM3250LLD  meterLld(serial, "/dev/serial0", /*deviceId=*/1);
    MeterHLD    meterHld(meterLld);

    // --- Log stack opbouwen ---------------------------------------------------
    // Meer backends? Maak een nieuw ILogBackend en voeg 'm toe met addBackend().
    FLOG_LLD flogLld{"134.188.254.132", 17540};
    LogHLD   logHld;
    logHld.addBackend(flogLld);

    // --- Controller met beide services ---------------------------------------
    Controller controller(meterHld, std::chrono::seconds(2));
    controller.setLogService(&logHld);

    // --- Verbindingen openen --------------------------------------------------
    if (!meterLld.connect()) {
        std::cerr << "Kan niet verbinden met IEM3250\n";
        return 1;
    }
    std::cout << "Verbonden met IEM3250\n";

    if (!flogLld.connect()) {
        std::cerr << "Waarschuwing: FLOG backend niet beschikbaar, logging uit.\n";
    }

    // --- Optie 1: eenmalig handmatig pollen -----------------------------------
    std::cout << "\n[pollOnce] Eenmalige meting:\n";
    controller.pollOnce();
    printMeasurement(controller.getLatestMeasurement());

    // --- Optie 2: automatische poll-loop met callback -------------------------
    controller.onMeasurement([](const MeterMeasurement& m) {
        printMeasurement(m);
    });

    std::cout << "\n[start] Poll-loop gestart (elke 2 seconden)...\n";
    controller.start();

    // Laat de loop 10 seconden draaien
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // --- Laatste meting opvragen terwijl loop draait -------------------------
    MeterMeasurement latest = controller.getLatestMeasurement();
    std::cout << "\n[getLatestMeasurement] Laatste bekende frequentie: "
              << latest.frequency << " Hz\n";

    // --- Stoppen --------------------------------------------------------------
    controller.stop();
    std::cout << "\n[stop] Poll-loop gestopt\n";

    flogLld.disconnect();
    meterLld.disconnect();
    return 0;
}
