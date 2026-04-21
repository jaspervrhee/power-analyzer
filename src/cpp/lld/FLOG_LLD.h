#pragma once

#include "interfaces/ILogBackend.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * FLOG_LLD — Low Level Driver for the Canon FLOG (logServer) backend.
 *
 * Implements ILogBackend by speaking the logServer wire protocol:
 *   - Free-form dprintf: a single text line terminated by '\n'.
 *   - Table registration: "@TABLE,ref=<n>,path=<p>,name=<t>,columns=a;b;c"
 *   - Structured row:     "@LOGGING,ref=<n>,values=v1;v2;v3"
 *
 * Tables are registered lazily on first use; the assigned ref is cached
 * in a local registry so subsequent rows only send an @LOGGING line.
 * On disconnect the registry is cleared, so reconnecting re-registers
 * all tables on demand.
 */
class FLOG_LLD : public ILogBackend {
public:
    /**
     * @param host  logServer hostname or IP.
     * @param port  logServer TCP port (17540 is the default in startLogServer.bat).
     */
    explicit FLOG_LLD(std::string host = "127.0.0.1",
                      std::uint16_t port = 17540);
    ~FLOG_LLD() override;

    // Non-copyable
    FLOG_LLD(const FLOG_LLD&)            = delete;
    FLOG_LLD& operator=(const FLOG_LLD&) = delete;

    // --- ILogBackend -----------------------------------------------------------
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    bool send(const LogEntry& entry) override;
    const char* name() const override { return "FLOG"; }

private:
    struct TableKey {
        std::string path;
        std::string name;
        bool operator==(const TableKey& o) const noexcept
        { return path == o.path && name == o.name; }
    };
    struct TableKeyHash {
        std::size_t operator()(const TableKey& k) const noexcept
        {
            return std::hash<std::string>{}(k.path) ^
                   (std::hash<std::string>{}(k.name) << 1);
        }
    };

    std::string       host_;
    std::uint16_t     port_;
    std::atomic<bool> connected_{false};

    mutable std::mutex mutex_;
    // Holds a POSIX fd (int) or a Windows SOCKET (UINT_PTR); -1 when closed.
    std::int64_t      socketFd_ = -1;

    std::unordered_map<TableKey, int, TableKeyHash> tableRefs_;
    int nextRef_ = 0;

    // All helpers below assume mutex_ is held.
    int  ensureTableRef(const std::string& path,
                        const std::string& tableName,
                        const std::vector<std::string>& columns);
    bool sendLineLocked(const std::string& line);
    void closeSocketLocked();
};
