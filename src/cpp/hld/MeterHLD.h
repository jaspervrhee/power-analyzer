#pragma once

#include "interfaces/IMeterService.h"
#include "interfaces/IMeterDriver.h"

/**
 * @brief High Level Driver implementing IMeterService on top of an IMeterDriver.
 *
 * Reads raw register data via the injected driver, validates it (NaN/Inf
 * checks and physical range bounds) and exposes a MeterMeasurement to the
 * Controller. The HLD owns no hardware state; the LLD does.
 */
class MeterHLD : public IMeterService {
public:
    /**
     * @brief Construct a MeterHLD bound to a specific driver.
     * @param driver  Low-level driver. Caller retains ownership and must
     *                ensure it outlives this MeterHLD.
     */
    explicit MeterHLD(IMeterDriver& driver);
    ~MeterHLD() override = default;

    /**
     * @copydoc IMeterService::isAvailable
     * @return true when the underlying driver reports it is connected.
     */
    bool isAvailable() const override;

    /**
     * @copydoc IMeterService::getMeasurement
     *
     * Reads a raw snapshot from the driver, runs validation, and populates
     * @p measurement. @c measurement.valid is set to false when the read
     * failed or any value was out of range / non-finite.
     */
    bool getMeasurement(MeterMeasurement& measurement) override;

private:
    IMeterDriver& driver_;

    /**
     * @brief Range and finiteness validation for a raw measurement.
     * @param raw  Raw snapshot from the driver.
     * @return true when every field is finite and within physical bounds.
     */
    static bool validate(const RawMeasurement& raw);

    /**
     * @brief Copy a RawMeasurement into a MeterMeasurement and stamp the valid flag.
     * @param raw    Source snapshot.
     * @param valid  Result of validate(raw); written to @c MeterMeasurement::valid.
     * @return The fully populated MeterMeasurement.
     */
    static MeterMeasurement toMeterMeasurement(const RawMeasurement& raw,
                                               bool valid);
};
