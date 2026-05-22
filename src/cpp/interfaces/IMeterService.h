#pragma once

#include "common/MeterData.h"

/**
 * @brief Service interface for retrieving validated meter measurements.
 *
 * Exposes the meter to higher layers (e.g. Controller) without coupling
 * them to a specific driver or transport. Implemented by MeterHLD.
 */
class IMeterService {
public:
    virtual ~IMeterService() = default;

    /**
     * @brief Check whether the underlying meter is reachable.
     * @return true when the meter is connected and ready to deliver data.
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief Retrieve the latest validated measurement snapshot.
     *
     * @param[out] measurement  Populated on success; @c measurement.valid is
     *                          set to false when data could not be obtained
     *                          or failed validation.
     * @return true when @c measurement.valid is true.
     */
    virtual bool getMeasurement(MeterMeasurement& measurement) = 0;
};
