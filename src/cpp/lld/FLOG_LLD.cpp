#include "lld/FLOG_LLD.h"

#include <iostream>
#include <sstream>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>


static constexpr int CONNECT_TIMEOUT_SEC = 2;
static constexpr int SEND_TIMEOUT_MS     = 5000;

static void enableTcpKeepalive(int sock)
{
    // Enable SO_KEEPALIVE
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));

    // Aggressive keepalive timings: probe quickly so dead links are noticed fast
    int idle  = 5;
    int intvl = 2;
    int cnt   = 3;
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));

#ifdef TCP_USER_TIMEOUT
    // Cap retransmission stalls so send() fails fast on broken links
    unsigned int userTimeoutMs = 10000;
    if (setsockopt(sock, IPPROTO_TCP, TCP_USER_TIMEOUT,
                   &userTimeoutMs, sizeof(userTimeoutMs)) == 0) {
        std::cout << "[FLOG] TCP_USER_TIMEOUT=" << userTimeoutMs << "ms\n";
    } else {
        std::cerr << "[FLOG] TCP_USER_TIMEOUT setsockopt failed (errno="
                  << errno << ")\n";
    }
#else
    std::cerr << "[FLOG] TCP_USER_TIMEOUT not available in headers\n";
#endif

    // Small send buffer so a stalled link backs up immediately
    int sndbuf = 4096;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
}

static void enableSendTimeout(int sock, int timeoutMs)
{
    timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static bool setNonBlocking(int sock, bool nonBlocking)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return false;
    flags = nonBlocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(sock, F_SETFL, flags) == 0;
}

static bool tryConnectWithTimeout(int sock,
                                  const sockaddr* addr,
                                  std::size_t addrLen,
                                  int timeoutSec)
{
    // Flip to non-blocking so connect() returns immediately
    if (!setNonBlocking(sock, true)) return false;

    // Kick off the connection
    const int rc = ::connect(sock, addr, static_cast<socklen_t>(addrLen));
    if (rc == 0) {
        // Connected synchronously (e.g. loopback) — restore blocking
        return setNonBlocking(sock, false);
    }
    if (errno != EINPROGRESS) return false;

    // Wait until the socket becomes writable, or we time out
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sock, &wfds);
    timeval tv{};
    tv.tv_sec  = timeoutSec;
    tv.tv_usec = 0;

    const int selectRc = select(sock + 1, nullptr, &wfds, nullptr, &tv);
    if (selectRc <= 0) return false; // timeout or select error

    // Check whether the connect actually succeeded
    int soError = 0;
    socklen_t optLen = sizeof(soError);
    if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &soError, &optLen) != 0) {
        return false;
    }
    if (soError != 0) return false;

    // Restore blocking mode for the rest of the session
    return setNonBlocking(sock, false);
}


FLOG_LLD::FLOG_LLD(std::string host, std::uint16_t port)
    : host_(std::move(host))
    , port_(port)
{}

FLOG_LLD::~FLOG_LLD()
{
    disconnect();
}

bool FLOG_LLD::connect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_.load()) return true;

    // Resolve host:port to one or more sockaddrs (v4/v6)
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        std::cerr << "[FLOG] getaddrinfo failed for " << host_ << ':' << port_ << '\n';
        return false;
    }

    // Walk the resolved addresses until one connects within the timeout
    int sock = -1;
    for (addrinfo* a = res; a != nullptr; a = a->ai_next) {
        int s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == -1) continue;
        if (tryConnectWithTimeout(s, a->ai_addr, a->ai_addrlen,
                                  CONNECT_TIMEOUT_SEC)) {
            sock = s;
            break;
        }
        ::close(s);
    }
    freeaddrinfo(res);

    if (sock == -1) {
        std::cerr << "[FLOG] connect to " << host_ << ':' << port_
                  << " failed (err=" << errno << ")\n";
        return false;
    }

    // Tune the socket for fast failure detection
    enableTcpKeepalive(sock);
    enableSendTimeout(sock, SEND_TIMEOUT_MS);

    // Adopt the socket and reset the per-connection state
    socketFd_ = sock;
    tableRefs_.clear();
    nextRef_ = 0;
    connected_.store(true);

    std::cout << "[FLOG] connected to " << host_ << ':' << port_ << '\n';
    return true;
}

void FLOG_LLD::disconnect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    closeSocketLocked();
}

bool FLOG_LLD::isConnected() const
{
    return connected_.load();
}

bool FLOG_LLD::send(const LogEntry& entry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!connected_.load()) return false;

    // Free-form dprintf: no schema, send the raw text line.
    if (entry.columns.empty()) {
        return sendLineLocked(entry.message);
    }

    // Structured: register table on first use, then push the row.
    if (entry.values.size() != entry.columns.size()) {
        std::cerr << "[FLOG] column/value count mismatch for "
                  << entry.tablePath << '/' << entry.tableName << '\n';
        return false;
    }

    const int ref = ensureTableRef(entry.tablePath, entry.tableName, entry.columns);
    if (ref < 0) return false;

    std::ostringstream os;
    os << "@LOGGING,ref=" << ref << ",values=";
    for (std::size_t i = 0; i < entry.values.size(); ++i) {
        if (i) os << ';';
        os << entry.values[i];
    }
    return sendLineLocked(os.str());
}


int FLOG_LLD::ensureTableRef(const std::string& path,
                             const std::string& tableName,
                             const std::vector<std::string>& columns)
{
    // Already registered? Reuse the existing ref
    const TableKey key{path, tableName};
    auto it = tableRefs_.find(key);
    if (it != tableRefs_.end()) {
        return it->second;
    }

    // Allocate a fresh ref id for this table
    const int ref = nextRef_++;

    // Build the @TABLE registration line
    std::ostringstream os;
    os << "@TABLE,ref=" << ref
       << ",path="      << path
       << ",name="      << tableName
       << ",columns=";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i) os << ';';
        os << columns[i];
    }

    // Send it; on failure the socket is already closed and the cache wiped
    if (!sendLineLocked(os.str())) {
        return -1;
    }

    // Cache the mapping so future rows skip the registration
    tableRefs_[key] = ref;
    return ref;
}

bool FLOG_LLD::sendLineLocked(const std::string& line)
{
    if (!connected_.load()) return false;

    // Append the line terminator
    std::string payload = line;
    payload.push_back('\n');

    const int sock = static_cast<int>(socketFd_);
    const char* data      = payload.data();
    std::size_t remaining = payload.size();

    // Write loop: handles partial sends, bails on any error
    while (remaining > 0) {
        const ssize_t n = ::send(sock, data, remaining, MSG_NOSIGNAL);
        if (n <= 0) {
            std::cerr << "[FLOG] send failed (err=" << errno << ")\n";
            closeSocketLocked();
            return false;
        }
        data      += n;
        remaining -= static_cast<std::size_t>(n);
    }
    return true;
}

void FLOG_LLD::closeSocketLocked()
{
    // Idempotent: only the first caller does the real teardown
    if (!connected_.exchange(false)) return;

    // Close the underlying fd
    const int sock = static_cast<int>(socketFd_);
    if (sock != -1) {
        ::close(sock);
    }
    // Reset per-connection state (table refs must be re-registered next time)
    socketFd_ = -1;
    tableRefs_.clear();
    nextRef_ = 0;

    std::cout << "[FLOG] disconnected\n";
}
