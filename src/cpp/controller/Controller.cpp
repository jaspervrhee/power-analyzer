#include "controller/Controller.h"

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Controller::Controller(IMeterService& meterService,
                       std::chrono::milliseconds interval)
    : meterService_(meterService)
    , interval_(interval)
{
    latestMeasurement_.valid = false;
}

Controller::~Controller()
{
    stop();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Controller::onMeasurement(MeasurementCallback callback)
{
    callback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Controller::start()
{
    if (running_.load()) {
        return;
    }
    running_.store(true);
    pollThread_ = std::thread(&Controller::pollLoop, this);
}

void Controller::stop()
{
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
}

bool Controller::isRunning() const
{
    return running_.load();
}

// ---------------------------------------------------------------------------
// Measurement access
// ---------------------------------------------------------------------------

MeterMeasurement Controller::getLatestMeasurement() const
{
    std::lock_guard<std::mutex> lock(measurementMutex_);
    return latestMeasurement_;
}

bool Controller::pollOnce()
{
    MeterMeasurement m{};
    const bool ok = meterService_.getMeasurement(m);

    {
        std::lock_guard<std::mutex> lock(measurementMutex_);
        latestMeasurement_ = m;
    }

    if (ok && callback_) {
        callback_(m);
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Background loop
// ---------------------------------------------------------------------------

void Controller::pollLoop()
{
    while (running_.load()) {
        pollOnce();

        // Sleep in small increments so stop() is responsive.
        const auto step = std::chrono::milliseconds(50);
        auto remaining  = interval_;
        while (running_.load() && remaining > std::chrono::milliseconds(0)) {
            const auto sleep = remaining < step ? remaining : step;
            std::this_thread::sleep_for(sleep);
            remaining -= sleep;
        }
    }
}
