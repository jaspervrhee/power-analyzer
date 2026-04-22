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

void Controller::setLogService(ILogService* logService)
{
    logService_ = logService;
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

    if (logService_) {
        logService_->logMeasurement(m);
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
    // Schedule on an absolute clock so poll duration doesn't leak into the
    // period: period == interval_ regardless of how long pollOnce() took.
    auto nextTick = std::chrono::steady_clock::now();

    while (running_.load()) {
        pollOnce();

        nextTick += interval_;

        // If pollOnce overran (bus unhealthy, heavy retries), skip missed
        // ticks instead of machine-gunning catch-up polls.
        const auto now = std::chrono::steady_clock::now();
        if (nextTick < now) {
            nextTick = now;
        }

        // Sleep in small increments so stop() stays responsive.
        const auto step = std::chrono::milliseconds(50);
        while (running_.load()) {
            const auto remaining = nextTick - std::chrono::steady_clock::now();
            if (remaining <= std::chrono::milliseconds(0)) break;
            std::this_thread::sleep_for(remaining < step ? remaining : step);
        }
    }
}
