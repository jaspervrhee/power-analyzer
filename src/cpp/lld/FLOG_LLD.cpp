#include "lld/FLOG_LLD.h"

#include <climits>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using NativeSocket = SOCKET;
#  define FLOG_INVALID_SOCKET INVALID_SOCKET
#  define FLOG_CLOSE(s)       closesocket(s)
static int flogLastError() { return WSAGetLastError(); }
#else
#  include <arpa/inet.h>
#  include <errno.h>
#  include <netdb.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <unistd.h>
   using NativeSocket = int;
#  define FLOG_INVALID_SOCKET (-1)
#  define FLOG_CLOSE(s)       ::close(s)
static int flogLastError() { return errno; }
#endif

// Enable aggressive TCP keepalive so a dead link (cable pulled, server killed)
// is detected in ~10s instead of the OS default (2h on Linux). Without this,
// send() keeps queuing bytes in the kernel socket buffer and we never notice
// we are offline.
static void enableTcpKeepalive(NativeSocket sock)
{
    int one = 1;
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE,
               reinterpret_cast<const char*>(&one), sizeof(one));
#ifdef __linux__
    int idle  = 5;   // seconds idle before probes start
    int intvl = 2;   // seconds between probes
    int cnt   = 3;   // probes before the connection is declared dead
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
#endif
}

namespace {

#ifdef _WIN32
// Reference-counted WSAStartup so multiple FLOG_LLD instances coexist.
class WsaInit {
public:
    bool acquire()
    {
        std::lock_guard<std::mutex> lock(m_);
        if (refs_ == 0) {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
        }
        ++refs_;
        return true;
    }
    void release()
    {
        std::lock_guard<std::mutex> lock(m_);
        if (refs_ > 0 && --refs_ == 0) {
            WSACleanup();
        }
    }
private:
    std::mutex m_;
    int        refs_ = 0;
};
WsaInit& wsa() { static WsaInit w; return w; }
#endif

NativeSocket toNative(std::int64_t fd)
{
    return static_cast<NativeSocket>(fd);
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FLOG_LLD::FLOG_LLD(std::string host, std::uint16_t port)
    : host_(std::move(host))
    , port_(port)
{}

FLOG_LLD::~FLOG_LLD()
{
    disconnect();
}

// ---------------------------------------------------------------------------
// ILogBackend
// ---------------------------------------------------------------------------

bool FLOG_LLD::connect()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_.load()) return true;

#ifdef _WIN32
    if (!wsa().acquire()) {
        std::cerr << "[FLOG] WSAStartup failed\n";
        return false;
    }
#endif

    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port_);
    if (getaddrinfo(host_.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        std::cerr << "[FLOG] getaddrinfo failed for " << host_ << ':' << port_ << '\n';
#ifdef _WIN32
        wsa().release();
#endif
        return false;
    }

    NativeSocket sock = FLOG_INVALID_SOCKET;
    for (addrinfo* a = res; a != nullptr; a = a->ai_next) {
        sock = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (sock == FLOG_INVALID_SOCKET) continue;
        if (::connect(sock, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) {
            break;
        }
        FLOG_CLOSE(sock);
        sock = FLOG_INVALID_SOCKET;
    }
    freeaddrinfo(res);

    if (sock == FLOG_INVALID_SOCKET) {
        std::cerr << "[FLOG] connect to " << host_ << ':' << port_
                  << " failed (err=" << flogLastError() << ")\n";
#ifdef _WIN32
        wsa().release();
#endif
        return false;
    }

    enableTcpKeepalive(sock);

    socketFd_ = static_cast<std::int64_t>(sock);
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

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

int FLOG_LLD::ensureTableRef(const std::string& path,
                             const std::string& tableName,
                             const std::vector<std::string>& columns)
{
    const TableKey key{path, tableName};
    auto it = tableRefs_.find(key);
    if (it != tableRefs_.end()) {
        return it->second;
    }

    const int ref = nextRef_++;

    std::ostringstream os;
    os << "@TABLE,ref=" << ref
       << ",path="      << path
       << ",name="      << tableName
       << ",columns=";
    for (std::size_t i = 0; i < columns.size(); ++i) {
        if (i) os << ';';
        os << columns[i];
    }

    if (!sendLineLocked(os.str())) {
        // sendLineLocked has already closed the socket; registry was cleared.
        return -1;
    }

    tableRefs_[key] = ref;
    return ref;
}

bool FLOG_LLD::sendLineLocked(const std::string& line)
{
    if (!connected_.load()) return false;

    std::string payload = line;
    payload.push_back('\n');

    NativeSocket sock = toNative(socketFd_);
    const char* data      = payload.data();
    std::size_t remaining = payload.size();

    while (remaining > 0) {
#ifdef _WIN32
        const int chunk = static_cast<int>(remaining > INT_MAX ? INT_MAX : remaining);
        const int n     = ::send(sock, data, chunk, 0);
#else
        const ssize_t n = ::send(sock, data, remaining, MSG_NOSIGNAL);
#endif
        if (n <= 0) {
            std::cerr << "[FLOG] send failed (err=" << flogLastError() << ")\n";
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
    if (!connected_.exchange(false)) return;

    NativeSocket sock = toNative(socketFd_);
    if (sock != FLOG_INVALID_SOCKET) {
        FLOG_CLOSE(sock);
    }
    socketFd_ = -1;
    tableRefs_.clear();
    nextRef_ = 0;

#ifdef _WIN32
    wsa().release();
#endif
    std::cout << "[FLOG] disconnected\n";
}
