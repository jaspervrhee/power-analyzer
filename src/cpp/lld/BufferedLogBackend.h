#pragma once

#include "interfaces/ILogBackend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

/**
 * @brief Decorator that adds in-memory buffering and auto-reconnect to any ILogBackend.
 *
 * Purpose: decouple the measurement loop from the availability of the
 * downstream log server. The system can start and keep measuring while
 * the wrapped backend (e.g. FLOG_LLD) is offline; entries queue up in
 * a bounded FIFO and are drained as soon as the connection is back.
 *
 * Ownership: the BufferedLogBackend owns the lifecycle of the wrapped
 * backend's connection (connect / disconnect) once its own connect()
 * has been called. Callers should not touch the wrapped backend
 * directly while this wrapper is active.
 */
class BufferedLogBackend : public ILogBackend {
public:
    /**
     * @brief Construct a buffered wrapper around a downstream backend.
     *
     * @param wrapped            The downstream backend (e.g. FLOG_LLD).
     *                           Caller retains ownership; must outlive this object.
     * @param maxEntries         Maximum number of buffered entries. When full,
     *                           the oldest entry is dropped to make room.
     * @param reconnectInterval  How often the worker retries @c wrapped.connect()
     *                           while the wrapped backend is unavailable.
     */
    explicit BufferedLogBackend(ILogBackend& wrapped,
                                std::size_t maxEntries = 10000,
                                std::chrono::milliseconds reconnectInterval
                                    = std::chrono::seconds(5));
    ~BufferedLogBackend() override;

    /// @name Non-copyable
    /// @{
    BufferedLogBackend(const BufferedLogBackend&)            = delete;
    BufferedLogBackend& operator=(const BufferedLogBackend&) = delete;
    /// @}

    /// @name ILogBackend implementation
    /// @{

    /**
     * @copydoc ILogBackend::connect
     *
     * Starts the worker thread that drains the queue and reconnects the
     * wrapped backend in the background. Always returns true — failures
     * are absorbed by the worker.
     */
    bool connect() override;

    /**
     * @copydoc ILogBackend::disconnect
     *
     * Signals the worker to stop, joins it, and disconnects the wrapped
     * backend. Any entries still queued are discarded.
     */
    void disconnect() override;

    /**
     * @copydoc ILogBackend::isConnected
     * @return true while the worker thread is running, regardless of
     *         whether the wrapped backend is currently reachable.
     */
    bool isConnected() const override;

    /**
     * @copydoc ILogBackend::send
     *
     * Enqueues the entry. When the queue is full the oldest entry is
     * dropped and @c droppedCount() is incremented. Never blocks on the
     * wrapped backend.
     */
    bool send(const LogEntry& entry) override;

    /// @copydoc ILogBackend::name
    const char* name() const override { return "Buffered"; }

    /// @}

    /**
     * @brief Current number of entries waiting in the buffer.
     * @return Queue depth at the moment of the call.
     */
    std::size_t pendingCount() const;

    /**
     * @brief Total entries dropped due to buffer overflow since construction.
     * @return Cumulative drop counter (never reset).
     */
    std::size_t droppedCount() const;

private:
    ILogBackend&                    wrapped_;            ///< Downstream backend being decorated.
    const std::size_t               maxEntries_;         ///< Hard cap on the FIFO depth.
    const std::chrono::milliseconds reconnectInterval_;  ///< Retry cadence while @c wrapped_ is offline.

    mutable std::mutex              queueMutex_;         ///< Protects @c queue_ and dropped accounting.
    std::condition_variable         queueCv_;            ///< Signals the worker on new entries / shutdown.
    std::deque<LogEntry>            queue_;              ///< Pending entries awaiting transmission.
    std::atomic<std::size_t>        dropped_{0};         ///< Lifetime count of overflow drops.

    std::atomic<bool>               running_{false};     ///< true between connect() and disconnect().
    std::thread                     worker_;             ///< Background drain/reconnect thread.

    /**
     * @brief Background loop: maintains the connection and drains the queue.
     *
     * Reconnects @c wrapped_ when needed (with @c reconnectInterval_ backoff),
     * then forwards queued entries one at a time. Exits when @c running_
     * becomes false.
     */
    void workerLoop();
};
