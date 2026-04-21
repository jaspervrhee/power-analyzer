#pragma once

#include "common/LogData.h"

/**
 * ILogBackend
 *
 * Defines the contract between LogHLD and a concrete logging backend (LLD).
 * Each logging driver implements this interface to send log data to a
 * specific external system (FLOG, SDL, file, syslog, ...).
 *
 * Adding a new backend is a matter of creating a class that implements
 * this interface and registering it on the LogHLD via addBackend().
 */
class ILogBackend {
public:
    virtual ~ILogBackend() = default;

    /**
     * Open the underlying connection / file / socket.
     * @return true on success.
     */
    virtual bool connect() = 0;

    /** Close the underlying connection. */
    virtual void disconnect() = 0;

    /** @return true when the backend is ready to accept entries. */
    virtual bool isConnected() const = 0;

    /**
     * Send a single log entry to the external system.
     * A failing backend must not throw; it returns false so the HLD
     * can continue forwarding to the remaining backends.
     *
     * @return true on successful transmission.
     */
    virtual bool send(const LogEntry& entry) = 0;

    /** Short human-readable backend name, used for diagnostics. */
    virtual const char* name() const = 0;
};
