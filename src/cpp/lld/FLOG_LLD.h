#pragma once

#include "interfaces/ILogBackend.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Low Level Driver for the Canon FLOG (logServer) backend.
 *
 * Implements ILogBackend by speaking the logServer wire protocol:
 *   - Free-form dprintf: a single text line terminated by '\n'.
 *   - Table registration: `@TABLE,ref=<n>,path=<p>,name=<t>,columns=a;b;c`
 *   - Structured row:     `@LOGGING,ref=<n>,values=v1;v2;v3`
 *
 * Tables are registered lazily on first use; the assigned ref is cached
 * in a local registry so subsequent rows only send an @LOGGING line.
 * On disconnect the registry is cleared, so reconnecting re-registers
 * all tables on demand.
 */
class FLOG_LLD : public ILogBackend {
public:
    /**
     * @brief Construct a FLOG backend pointing at a logServer endpoint.
     * @param host  logServer hostname or IP.
     * @param port  logServer TCP port (17540 is the default in startLogServer.bat).
     */
    explicit FLOG_LLD(std::string host = "127.0.0.1",
                      std::uint16_t port = 17540);
    ~FLOG_LLD() override;

    /// @name Non-copyable
    /// @{
    FLOG_LLD(const FLOG_LLD&)            = delete;
    FLOG_LLD& operator=(const FLOG_LLD&) = delete;
    /// @}

    /// @name ILogBackend implementation
    /// @{

    /**
     * @copydoc ILogBackend::connect
     *
     * Opens a TCP socket to @c host_:@c port_ and clears the table-ref cache.
     */
    bool connect() override;

    /**
     * @copydoc ILogBackend::disconnect
     *
     * Closes the socket and clears the table-ref cache so a subsequent
     * connect() re-registers tables on demand.
     */
    void disconnect() override;

    /// @copydoc ILogBackend::isConnected
    bool isConnected() const override;

    /**
     * @copydoc ILogBackend::send
     *
     * For text entries: emits a single line via dprintf. For structured
     * entries: lazily registers the table and emits an @LOGGING row.
     */
    bool send(const LogEntry& entry) override;

    /// @copydoc ILogBackend::name
    const char* name() const override { return "FLOG"; }

    /// @}

private:
    /**
     * @brief Composite key (path + name) identifying a logServer table.
     */
    struct TableKey {
        std::string path;  ///< Table path (e.g. "functional/power").
        std::string name;  ///< Table name (e.g. "events").
        bool operator==(const TableKey& o) const noexcept
        { return path == o.path && name == o.name; }
    };

    /**
     * @brief Hash combiner for @c TableKey, used by the table-ref cache.
     */
    struct TableKeyHash {
        std::size_t operator()(const TableKey& k) const noexcept
        {
            return std::hash<std::string>{}(k.path) ^
                   (std::hash<std::string>{}(k.name) << 1);
        }
    };

    std::string       host_;                ///< logServer hostname or IP.
    std::uint16_t     port_;                ///< logServer TCP port.
    std::atomic<bool> connected_{false};    ///< true while the socket is open.

    mutable std::mutex mutex_;              ///< Serializes socket and registry access.

    /**
     * @brief Native socket handle (POSIX fd, -1 when closed).
     */
    std::int64_t      socketFd_ = -1;

    std::unordered_map<TableKey, int, TableKeyHash> tableRefs_;  ///< Cached table ref numbers.
    int nextRef_ = 0;                       ///< Monotonic counter assigning the next table ref.

    // All helpers below assume mutex_ is held.

    /**
     * @brief Look up or register the @LOGGING ref for a structured table.
     * @param path        Table path.
     * @param tableName   Table name.
     * @param columns     Column names sent with the registration line.
     * @return The ref number to use in @LOGGING rows, or -1 on send failure.
     * @pre @c mutex_ must be held by the caller.
     */
    int  ensureTableRef(const std::string& path,
                        const std::string& tableName,
                        const std::vector<std::string>& columns);

    /**
     * @brief Write a single newline-terminated line to the open socket.
     * @param line  Line to send (a trailing '\n' is appended if missing).
     * @return true on a successful full write.
     * @pre @c mutex_ must be held by the caller.
     */
    bool sendLineLocked(const std::string& line);

    /**
     * @brief Close the socket if open and reset @c socketFd_.
     * @pre @c mutex_ must be held by the caller.
     */
    void closeSocketLocked();
};
