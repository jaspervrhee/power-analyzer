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
    // Idempotent — bail out if the worker thread is already up
    if (running_.exchange(true)) {
        return true;
    }
    // Spawn the worker that drains the queue into the wrapped backend
    worker_ = std::thread(&BufferedLogBackend::workerLoop, this);
    return true;
}

void BufferedLogBackend::disconnect()
{
    // Signal the worker to stop; first caller wins
    if (!running_.exchange(false)) {
        return;
    }
    // Wake the worker so it observes !running_ and exits
    queueCv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    // Tear down the downstream link
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
        // Drop the oldest entry when the buffer is full
        if (queue_.size() >= maxEntries_) {
            queue_.pop_front();
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        // Enqueue at the back
        queue_.push_back(entry);
    }
    // Wake the worker so it can drain
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


void BufferedLogBackend::workerLoop()
{
    std::unique_lock<std::mutex> lock(queueMutex_);

    while (running_.load()) {
        // Make sure the downstream link is up before trying to drain
        if (!wrapped_.isConnected()) {
            lock.unlock();
            wrapped_.connect(); // failure is fine: we retry next tick
            lock.lock();
        }

        // Wait for: shutdown, new entry + live link, or the reconnect tick
        queueCv_.wait_for(lock, reconnectInterval_, [this] {
            return !running_.load() ||
                   (!queue_.empty() && wrapped_.isConnected());
        });

        // Re-check state after waking
        if (!running_.load()) break;
        if (!wrapped_.isConnected()) continue; // still down, retry connect

        // Drain the queue while we have work and the link stays up
        while (running_.load() && !queue_.empty() && wrapped_.isConnected()) {
            LogEntry entry = std::move(queue_.front());
            queue_.pop_front();

            // Send outside the lock so producers aren't blocked on the network
            lock.unlock();
            const bool sent = wrapped_.send(entry);
            lock.lock();

            if (!sent) {
                // Link died mid-drain — preserve ordering and retry next tick
                queue_.push_front(std::move(entry));
                break;
            }
        }
    }
}
