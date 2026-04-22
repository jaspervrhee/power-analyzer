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
 * BufferedLogBackend — decorator that adds in-memory buffering and
 * automatic reconnect to any ILogBackend.
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
     * @param wrapped            The downstream backend (e.g. FLOG_LLD).
     *                           Caller retains ownership; must outlive this object.
     * @param maxEntries         Maximum number of buffered entries. When full,
     *                           the oldest entry is dropped to make room.
     * @param reconnectInterval  How often the worker retries wrapped.connect()
     *                           while the wrapped backend is unavailable.
     */
    explicit BufferedLogBackend(ILogBackend& wrapped,
                                std::size_t maxEntries = 10000,
                                std::chrono::milliseconds reconnectInterval
                                    = std::chrono::seconds(5));
    ~BufferedLogBackend() override;

    // Non-copyable
    BufferedLogBackend(const BufferedLogBackend&)            = delete;
    BufferedLogBackend& operator=(const BufferedLogBackend&) = delete;

    // --- ILogBackend -----------------------------------------------------------
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    bool send(const LogEntry& entry) override;
    const char* name() const override { return "Buffered"; }

    /** Current number of entries waiting in the buffer. */
    std::size_t pendingCount() const;

    /** Total entries dropped due to buffer overflow since construction. */
    std::size_t droppedCount() const;

private:
    ILogBackend&                    wrapped_;
    const std::size_t               maxEntries_;
    const std::chrono::milliseconds reconnectInterval_;

    mutable std::mutex              queueMutex_;
    std::condition_variable         queueCv_;
    std::deque<LogEntry>            queue_;
    std::atomic<std::size_t>        dropped_{0};

    std::atomic<bool>               running_{false};
    std::thread                     worker_;

    void workerLoop();
};
