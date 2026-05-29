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
#include <string>
#include <stdexcept>

static volatile std::sig_atomic_t keepRunning = 1;

static void handleSigint(int /*sig*/)
{
    keepRunning = 0;
}

struct Config {
    std::string serialPort   = "/dev/serial0";
    int         deviceId     = 1;
    int         baudRate     = 38400;
    int         timeoutMs    = 30;
    std::string flogHost     = "134.188.254.132";
    int         flogPort     = 17540;
    int         bufferSize   = 100000;
    int         pollInterval = 1;   // seconds
    int         runSeconds   = 3;   // 0 or negative = Ctrl-C mode
};

static void printHelp(const char* programName)
{
    std::cout << "Usage: " << programName << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --serial-port PATH       Serial port path                (default: /dev/serial0)\n";
    std::cout << "  --device-id N            Modbus slave ID                 (default: 1)\n";
    std::cout << "  --baud-rate N            Serial baud rate                (default: 38400)\n";
    std::cout << "  --timeout-ms N           Modbus timeout [ms]             (default: 30)\n";
    std::cout << "  --flog-host HOST         FLOG server address             (default: 134.188.254.132)\n";
    std::cout << "  --flog-port N            FLOG server port                (default: 17540)\n";
    std::cout << "  --buffer-size N          Log buffer size                 (default: 100000)\n";
    std::cout << "  --poll-interval N        Measurement interval [s]        (default: 1)\n";
    std::cout << "  --run-seconds N          Run duration [s], 0=run forever (default: 3)\n";
    std::cout << "  --help                   Show this message\n";
}

static Config parseArgs(int argc, char* argv[])
{
    Config cfg;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--help")
        {
            printHelp(argv[0]);
            exit(0);
        }
        else if (arg == "--serial-port")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --serial-port requires a value\n";
                exit(1);
            }
            cfg.serialPort = argv[++i];
        }
        else if (arg == "--device-id")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --device-id requires a value\n";
                exit(1);
            }
            try
            {
                cfg.deviceId = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --device-id value is not a valid integer\n";
                exit(1);
            }
        }
        else if (arg == "--baud-rate")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --baud-rate requires a value\n";
                exit(1);
            }
            try
            {
                cfg.baudRate = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --baud-rate value is not a valid integer\n";
                exit(1);
            }
        }
        else if (arg == "--timeout-ms")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --timeout-ms requires a value\n";
                exit(1);
            }
            try
            {
                cfg.timeoutMs = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --timeout-ms value is not a valid integer\n";
                exit(1);
            }
        }
        else if (arg == "--flog-host")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --flog-host requires a value\n";
                exit(1);
            }
            cfg.flogHost = argv[++i];
        }
        else if (arg == "--flog-port")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --flog-port requires a value\n";
                exit(1);
            }
            try
            {
                cfg.flogPort = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --flog-port value is not a valid integer\n";
                exit(1);
            }
        }
        else if (arg == "--buffer-size")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --buffer-size requires a value\n";
                exit(1);
            }
            try
            {
                cfg.bufferSize = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --buffer-size value is not a valid integer\n";
                exit(1);
            }
        }
        else if (arg == "--poll-interval")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --poll-interval requires a value\n";
                exit(1);
            }
            try
            {
                cfg.pollInterval = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --poll-interval value is not a valid integer\n";
                exit(1);
            }
        }
        else if (arg == "--run-seconds")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: --run-seconds requires a value\n";
                exit(1);
            }
            try
            {
                cfg.runSeconds = std::stoi(argv[++i]);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error: --run-seconds value is not a valid integer\n";
                exit(1);
            }
        }
        else
        {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            std::cerr << "Use --help for usage information\n";
            exit(1);
        }
    }

    return cfg;
}

static void printMeasurement(const MeterMeasurement &m)
{
    if (!m.valid)
    {
        std::cout << "[Controller] Measurement invalid\n";
        return;
    }

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
    Config cfg = parseArgs(argc, argv);

    // Meter stack: serial transport -> driver -> high-level service
    ModbusRtuSerial serial;
    IEM3250LLD meterLld(serial, cfg.serialPort, cfg.deviceId, cfg.baudRate, cfg.timeoutMs);
    MeterHLD meterHld(meterLld);

    // FLOG stack: TCP backend wrapped in a buffer, exposed via LogHLD
    FLOG_LLD           flogLld{cfg.flogHost, cfg.flogPort};
    BufferedLogBackend bufferedFlog{flogLld, cfg.bufferSize};
    LogHLD             logHld;
    logHld.addBackend(bufferedFlog);

    // Wire the controller: poll the meter at configured interval and log every cycle
    Controller controller(meterHld, std::chrono::seconds(cfg.pollInterval));
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

    // Either run for a fixed duration, or block until Ctrl-C
    if (cfg.runSeconds > 0)
    {
        std::this_thread::sleep_for(std::chrono::seconds(cfg.runSeconds));
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
