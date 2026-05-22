#include "controller/Controller.h"


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


void Controller::onMeasurement(MeasurementCallback callback)
{
    callback_ = std::move(callback);
}

void Controller::setLogService(ILogService* logService)
{
    logService_ = logService;
}

void Controller::start()
{
    // Already running — nothing to do
    if (running_.load()) {
        return;
    }
    // Mark running and spin up the polling thread
    running_.store(true);
    pollThread_ = std::thread(&Controller::pollLoop, this);
}

void Controller::stop()
{
    // Already stopped — nothing to do
    if (!running_.load()) {
        return;
    }
    // Signal the loop to exit and wait for it
    running_.store(false);
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
}

bool Controller::isRunning() const
{
    return running_.load();
}


MeterMeasurement Controller::getLatestMeasurement() const
{
    std::lock_guard<std::mutex> lock(measurementMutex_);
    return latestMeasurement_;
}

bool Controller::pollOnce()
{
    // Fetch a fresh measurement from the meter service
    MeterMeasurement m{};
    const bool ok = meterService_.getMeasurement(m);

    // Publish it as the latest known measurement
    {
        std::lock_guard<std::mutex> lock(measurementMutex_);
        latestMeasurement_ = m;
    }

    // Log every poll, even invalid ones (logger decides level)
    if (logService_) {
        logService_->logMeasurement(m);
    }

    // Notify subscribers only on successful reads
    if (ok && callback_) {
        callback_(m);
    }

    return ok;
}


void Controller::pollLoop()
{
    auto nextTick = std::chrono::steady_clock::now();

    while (running_.load()) {
        // Do the work for this tick
        pollOnce();

        // Schedule the next tick
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
