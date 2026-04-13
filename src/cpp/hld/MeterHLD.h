#pragma once

#include "interfaces/IMeterService.h"
#include "interfaces/IMeterDriver.h"

/**
 * MeterHLD — Meter High Level Driver.
 *
 * Consumes IMeterDriver (from the LLD) and exposes IMeterService
 * (to the Controller). Responsibilities:
 *  - Validate raw measurement values (range checks, NaN/Inf guards).
 *  - Convert units where needed (e.g. raw kW stays kW; raw A stays A).
 *  - Hide all hardware and protocol details from the Controller.
 */
class MeterHLD : public IMeterService {
public:
    /**
     * @param driver  The underlying meter driver (e.g. IEM3250LLD).
     *                The caller retains ownership and must ensure the driver
     *                outlives this object.
     */
    explicit MeterHLD(IMeterDriver& driver);
    ~MeterHLD() override = default;

    // --- IMeterService ---------------------------------------------------------
    bool isAvailable() const override;
    bool getMeasurement(MeterMeasurement& measurement) override;

private:
    IMeterDriver& driver_;

    /**
     * Validate a raw measurement snapshot.
     * Checks for NaN / Inf and physically plausible ranges.
     * @return true when all values are within expected bounds.
     */
    static bool validate(const RawMeasurement& raw);

    /** Copy and convert a validated RawMeasurement into a MeterMeasurement. */
    static MeterMeasurement toMeterMeasurement(const RawMeasurement& raw,
                                               bool valid);
};
