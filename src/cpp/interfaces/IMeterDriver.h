#pragma once

#include "common/MeterData.h"

/**
 * IMeterDriver
 *
 * Defines the contract between a Low Level Driver (LLD) and the MeterHLD.
 * Implemented by device-specific LLD classes (e.g. IEM3250LLD, PAC4200LLD).
 * Delivers technically correct raw measurement data; no validation or
 * business-logic conversion is performed at this layer.
 */
class IMeterDriver {
public:
    virtual ~IMeterDriver() = default;

    /**
     * Activate the driver and open the underlying hardware connection.
     * @return true on success
     */
    virtual bool connect() = 0;

    /** Deactivate the driver and release the hardware connection. */
    virtual void disconnect() = 0;

    /** @return true when the driver is connected and ready to deliver data. */
    virtual bool isConnected() const = 0;

    /**
     * Read a complete measurement snapshot from the meter.
     *
     * @param measurement  Output: populated with values on success; valid flag not set by driver
     * @return true when all register reads succeeded
     */
    virtual bool readMeasurement(MeterMeasurement& measurement) = 0;
};
