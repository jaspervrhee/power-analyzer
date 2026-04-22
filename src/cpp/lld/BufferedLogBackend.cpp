#include "lld/BufferedLogBackend.h"

#include <iostream>
#include <utility>

BufferedLogBackend::BufferedLogBackend(ILogBackend& wrapped,
                                       std::size_t maxEntries,
                                       std::chrono::milliseconds reconnectInterval)
    : wrapped_(wrapped)
    , maxEntries_(maxEntries > 0 ? maxEntries : 1)
    , reconnectInterval_(reconnectInterval)
{}

BufferedLogBackend::~BufferedLogBackend()
{
    disconnect();
}

bool BufferedLogBackend::connect()
{
    if (running_.exchange(true)) {
        return true; // already running
    }
    worker_ = std::thread(&BufferedLogBackend::workerLoop, this);
    return true;
}

void BufferedLogBackend::disconnect()
{
    if (!running_.exchange(false)) {
        return;
    }
    queueCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    wrapped_.disconnect();
}

bool BufferedLogBackend::isConnected() const
{
    // Once the buffer is running we always accept entries — whether the
    // wrapped backend is reachable is transparent to upstream callers.
    return running_.load();
}

bool BufferedLogBackend::send(const LogEntry& entry)
{
    if (!running_.load()) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (queue_.size() >= maxEntries_) {
            queue_.pop_front();
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        queue_.push_back(entry);
    }
    queueCv_.notify_one();
    return true;
}

std::size_t BufferedLogBackend::pendingCount() const
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    return queue_.size();
}

std::size_t BufferedLogBackend::droppedCount() const
{
    return dropped_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Worker thread: reconnects when needed, drains buffer when possible.
// ---------------------------------------------------------------------------

void BufferedLogBackend::workerLoop()
{
    std::unique_lock<std::mutex> lock(queueMutex_);

    while (running_.load()) {
        // Wake up on: new entry enqueued, shutdown, or reconnect tick.
        queueCv_.wait_for(lock, reconnectInterval_, [this] {
            return !running_.load() || !queue_.empty();
        });

        if (!running_.load()) break;

        // Ensure the wrapped backend is connected; if not, attempt a connect.
        if (!wrapped_.isConnected()) {
            lock.unlock();
            wrapped_.connect(); // failure is fine: retry next tick
            lock.lock();
            if (!wrapped_.isConnected()) {
                continue; // still down, wait another tick
            }
        }

        // Drain the queue while we have work and the link stays up.
        while (running_.load() && !queue_.empty() && wrapped_.isConnected()) {
            LogEntry entry = std::move(queue_.front());
            queue_.pop_front();

            lock.unlock();
            const bool sent = wrapped_.send(entry);
            lock.lock();

            if (!sent) {
                // Assume the send failed because the link dropped.
                // Put the entry back at the front so ordering is preserved,
                // then exit the drain loop and let the next tick reconnect.
                queue_.push_front(std::move(entry));
                break;
            }
        }
    }
}
