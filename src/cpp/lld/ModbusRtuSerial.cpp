#include "lld/ModbusRtuSerial.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>

// Modbus RTU requires ≥3.5 character times of silence between frames.
// At 38400 8E1 (11 bits/char) that's ~1.0 ms; at 19200 ~2.0 ms; at 9600 ~4.0 ms.
// We use a fixed 5 ms guard which is comfortable above t3.5 for all supported
// baud rates and still fits the 1 Hz measurement budget (18 reads × 5 ms = 90 ms).
static constexpr auto MODBUS_T35_GUARD = std::chrono::milliseconds(5);

// ---------------------------------------------------------------------------
// Failure-reason counters (file-local). Lets us tell apart
// master->slave damage from slave->master damage without needing
// Modbus diagnostics on the meter (the iEM3250 doesn't expose them).
// ---------------------------------------------------------------------------
namespace {

struct Counters {
    uint64_t total        = 0;
    uint64_t ok           = 0;
    uint64_t sendFail     = 0;   // write() to serial returned short
    uint64_t timeout0     = 0;   // timeout, 0 bytes received  -> meter didn't reply at all
    uint64_t timeoutP     = 0;   // timeout, partial response  -> reply got cut off
    uint64_t readError    = 0;   // read() returned <= 0
    uint64_t badHeader    = 0;   // slave id or FC mismatch    -> garbled start of reply
    uint64_t badByteCount = 0;   // byte-count field wrong     -> byte dropped in reply
    uint64_t crcFail      = 0;   // CRC mismatch               -> bit flip somewhere in reply
};

Counters counters;

constexpr uint64_t LOG_EVERY = 100;

void logModbusStats(const char* tag)
{
    const auto& c = counters;
    const uint64_t fail = c.sendFail + c.timeout0 + c.timeoutP + c.readError
                        + c.badHeader + c.badByteCount + c.crcFail;
    std::cerr << "[Modbus " << tag << "] total=" << c.total
              << " ok=" << c.ok
              << " fail=" << fail
              << " | sendFail=" << c.sendFail
              << " timeout0=" << c.timeout0
              << " timeoutP=" << c.timeoutP
              << " rdErr=" << c.readError
              << " badHdr=" << c.badHeader
              << " badBC=" << c.badByteCount
              << " crc=" << c.crcFail
              << '\n';
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ModbusRtuSerial::ModbusRtuSerial()
    : fd_(-1), deviceId_(1), timeoutMs_(2000)
{
}

ModbusRtuSerial::~ModbusRtuSerial()
{
    disconnect();
    logModbusStats("final");
}

// ---------------------------------------------------------------------------
// IIEM3250Communication
// ---------------------------------------------------------------------------

bool ModbusRtuSerial::connect(const std::string &port,
                              int deviceId,
                              int baudRate,
                              int timeoutMs)
{
    if (fd_ != -1)
    {
        disconnect();
    }

    deviceId_ = deviceId;
    timeoutMs_ = timeoutMs;

    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0)
    {
        return false;
    }

    // --- Configure serial port -------------------------------------------------
    struct termios tty{};
    if (::tcgetattr(fd_, &tty) != 0)
    {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Baud rate
    speed_t speed = B19200;
    switch (baudRate)
    {
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    default:
        speed = B19200;
        break;
    }
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // 8E1: 8 data bits, even parity, 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;      // 8 data bits
    tty.c_cflag |= PARENB;   // enable parity
    tty.c_cflag &= ~PARODD;  // even parity
    tty.c_cflag &= ~CSTOPB;  // 1 stop bit
    tty.c_cflag &= ~CRTSCTS; // no hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // no software flow control
    tty.c_iflag &= ~(INPCK | ISTRIP);       // disable parity check (might corrupt data)

    tty.c_lflag = 0; // raw mode
    tty.c_oflag = 0; // raw output

    // Non-blocking read with timeout handled via select()
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    ::tcflush(fd_, TCIOFLUSH);
    return true;
}

void ModbusRtuSerial::disconnect()
{
    if (fd_ != -1)
    {
        ::close(fd_);
        fd_ = -1;
    }
}

bool ModbusRtuSerial::isConnected() const
{
    return fd_ != -1;
}

bool ModbusRtuSerial::readHoldingRegisters(uint16_t address,
                                           uint16_t count,
                                           std::vector<uint16_t> &data)
{
    if (!isConnected())
    {
        return false;
    }

    // Enforce Modbus RTU t3.5 inter-frame silence. Without this the meter
    // can merge our request into the tail of its previous reply, which shows
    // up as timeout0 (no answer) and CRC errors (collision on the bus).
    const auto nextAllowed = lastBusActivity_ + MODBUS_T35_GUARD;
    const auto now = std::chrono::steady_clock::now();
    if (now < nextAllowed)
    {
        std::this_thread::sleep_for(nextAllowed - now);
    }

    ::tcflush(fd_, TCIOFLUSH);
    ++counters.total;

    if (!sendReadRequest(address, count))
    {
        ++counters.sendFail;
        lastBusActivity_ = std::chrono::steady_clock::now();
        return false;
    }

    const bool ok = receiveReadResponse(count, data);
    lastBusActivity_ = std::chrono::steady_clock::now();

    if (counters.total % LOG_EVERY == 0)
    {
        logModbusStats("periodic");
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool ModbusRtuSerial::sendReadRequest(uint16_t address, uint16_t count)
{
    // Modbus RTU Read Holding Registers (FC 0x03)
    // Frame: [deviceId, 0x03, addrHi, addrLo, cntHi, cntLo, CRClo, CRChi]
    uint8_t request[8];
    request[0] = static_cast<uint8_t>(deviceId_);
    request[1] = 0x03; // function code
    request[2] = static_cast<uint8_t>(address >> 8);
    request[3] = static_cast<uint8_t>(address & 0xFF);
    request[4] = static_cast<uint8_t>(count >> 8);
    request[5] = static_cast<uint8_t>(count & 0xFF);

    uint16_t crc = crc16(request, 6);
    request[6] = static_cast<uint8_t>(crc & 0xFF); // CRC low byte first
    request[7] = static_cast<uint8_t>(crc >> 8);

    ssize_t written = ::write(fd_, request, 8);
    if (written != 8)
    {
        return false;
    }

    // Ensure all data is transmitted before waiting for response
    ::tcdrain(fd_);
    return true;
}

bool ModbusRtuSerial::receiveReadResponse(uint16_t expectedCount,
                                          std::vector<uint16_t> &data)
{
    // Expected response length: 3 header bytes + 2*count data bytes + 2 CRC bytes
    const size_t expectedLen = 3u + static_cast<size_t>(expectedCount) * 2u + 2u;
    std::vector<uint8_t> buf(expectedLen, 0);

    // Note: do not flush the input buffer here. The TCIOFLUSH at the start of
    // readHoldingRegisters already cleared stale data, and by the time tcdrain
    // returns the slave may already have started replying — flushing now would
    // discard the first byte(s) of the response and surface as badHeader/CRC.

    auto startTime = std::chrono::steady_clock::now();
    size_t received = 0;

    while (received < expectedLen)
    {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        int remaining = timeoutMs_ - static_cast<int>(elapsed);

        if (remaining <= 0)
        {
            if (received == 0) ++counters.timeout0; else ++counters.timeoutP;
            return false;
        }

        struct timeval tv{};
        tv.tv_sec = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0)
        {
            if (received == 0) ++counters.timeout0; else ++counters.timeoutP;
            return false; // timeout or error
        }

        ssize_t n = ::read(fd_, buf.data() + received, expectedLen - received);
        if (n <= 0)
        {
            ++counters.readError;
            return false;
        }

        received += static_cast<size_t>(n);
    }

    // Validate device ID and function code
    if (buf[0] != static_cast<uint8_t>(deviceId_) || buf[1] != 0x03)
    {
        ++counters.badHeader;
        return false;
    }

    // Validate byte count
    if (buf[2] != static_cast<uint8_t>(expectedCount * 2))
    {
        ++counters.badByteCount;
        return false;
    }

    // Validate CRC (covers all bytes except the trailing two CRC bytes)
    uint16_t computedCrc = crc16(buf.data(), expectedLen - 2);
    uint16_t receivedCrc = static_cast<uint16_t>(buf[expectedLen - 2]) |
                           (static_cast<uint16_t>(buf[expectedLen - 1]) << 8);

    if (computedCrc != receivedCrc)
    {
        ++counters.crcFail;
        return false;
    }

    // Extract register values (big-endian pairs)
    data.resize(expectedCount);
    for (uint16_t i = 0; i < expectedCount; ++i)
    {
        size_t offset = 3u + static_cast<size_t>(i) * 2u;
        data[i] = (static_cast<uint16_t>(buf[offset]) << 8) |
                  static_cast<uint16_t>(buf[offset + 1]);
    }

    ++counters.ok;
    return true;
}

uint16_t ModbusRtuSerial::crc16(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= buf[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}
