#pragma once

#include "common/MeterData.h"

/**
 * IMeterService
 *
 * Exposes meter-independent measurement functionality to the Controller.
 * Values are validated and interpreted; all hardware and protocol details
 * are hidden behind this interface.
 * Implemented by MeterHLD.
 */
class IMeterService {
public:
    virtual ~IMeterService() = default;

    /**
     * @return true when a meter is connected and delivering valid data.
     */
    virtual bool isAvailable() const = 0;

    /**
     * Retrieve the latest validated measurement snapshot.
     *
     * @param measurement  Output: populated on success; measurement.valid is
     *                     set to false when data could not be obtained or
     *                     failed validation.
     * @return true when measurement.valid is true
     */
    virtual bool getMeasurement(MeterMeasurement& measurement) = 0;
};
