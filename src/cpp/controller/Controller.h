#pragma once

#include "interfaces/IMeterService.h"
#include "common/MeterData.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

/**
 * Controller
 *
 * Orchestrates periodic measurement retrieval via IMeterService.
 * Runs a background poll loop at a configurable interval.
 * The latest measurement is always accessible via getLatestMeasurement().
 * An optional callback is invoked on every new valid measurement.
 */
class Controller {
public:
    using MeasurementCallback = std::function<void(const MeterMeasurement&)>;

    /**
     * @param meterService  The meter service to poll (e.g. MeterHLD).
     * @param interval      Time between consecutive reads (default: 1 s).
     */
    explicit Controller(IMeterService& meterService,
                        std::chrono::milliseconds interval = std::chrono::seconds(1));

    ~Controller();

    // Non-copyable
    Controller(const Controller&)            = delete;
    Controller& operator=(const Controller&) = delete;

    /**
     * Register a callback that is invoked on every new valid measurement.
     * Must be called before start(). Only one callback is supported.
     */
    void onMeasurement(MeasurementCallback callback);

    /**
     * Start the background poll loop.
     * Has no effect when already running.
     */
    void start();

    /**
     * Stop the background poll loop and wait for it to finish.
     * Has no effect when already stopped.
     */
    void stop();

    /** @return true when the poll loop is active. */
    bool isRunning() const;

    /**
     * Return the most recent measurement snapshot.
     * measurement.valid is false when no valid measurement has been
     * received yet or when the last read failed.
     */
    MeterMeasurement getLatestMeasurement() const;

    /**
     * Perform a single measurement immediately (blocking).
     * Updates the internal latest measurement and invokes the callback.
     * Can be called independently of start()/stop().
     *
     * @return true when measurement succeeded and passed validation.
     */
    bool pollOnce();

private:
    IMeterService&        meterService_;
    std::chrono::milliseconds interval_;
    MeasurementCallback   callback_;

    mutable std::mutex    measurementMutex_;
    MeterMeasurement      latestMeasurement_{};

    std::atomic<bool>     running_{false};
    std::thread           pollThread_;

    void pollLoop();
};
