#pragma once

#include "interfaces/IMeterDriver.h"
#include "interfaces/IIEM3250Communication.h"

#include <cstdint>
#include <string>
#include <vector>

class IEM3250LLD : public IMeterDriver {
public:
    IEM3250LLD(IIEM3250Communication& comm,
               std::string port     = "/dev/serial0",
               int deviceId         = 1,
               int baudRate         = 19200,
               int timeoutMs        = 2000);

    ~IEM3250LLD() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    bool readMeasurement(RawMeasurement& measurement) override;

    // One float32 to extract from a (possibly multi-register) read.
    //   regAddr        — Schneider register address (logged on failure).
    //   offsetInBlock  — index of the LOW word inside the response.
    //                    The HIGH word sits at offsetInBlock + 1.
    //   outValue       — destination for the decoded float (or sentinel on fail).
    // Public so the .cpp anonymous-namespace burst-spec table can name it.
    struct FloatExtract {
        uint16_t regAddr;
        uint16_t offsetInBlock;
        float*   outValue;
    };

private:
    IIEM3250Communication& comm_;
    std::string port_;
    int deviceId_;
    int baudRate_;
    int timeoutMs_;

    // Single Modbus transaction. No internal retries — readMeasurement
    // re-queues failures to give them a long natural backoff.
    bool tryBurstOnce(uint16_t startReg,
                      uint16_t regCount,
                      const std::vector<FloatExtract>& extracts);

    static float decodeCDAB(uint16_t regLow, uint16_t regHigh);
};
