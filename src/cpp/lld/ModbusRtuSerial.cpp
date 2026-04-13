#include "lld/ModbusRtuSerial.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ModbusRtuSerial::ModbusRtuSerial()
    : fd_(-1), deviceId_(1), timeoutMs_(2000)
{}

ModbusRtuSerial::~ModbusRtuSerial()
{
    disconnect();
}

// ---------------------------------------------------------------------------
// IIEM3250Communication
// ---------------------------------------------------------------------------

bool ModbusRtuSerial::connect(const std::string& port,
                               int deviceId,
                               int baudRate,
                               int timeoutMs)
{
    if (fd_ != -1) {
        disconnect();
    }

    deviceId_  = deviceId;
    timeoutMs_ = timeoutMs;

    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0) {
        return false;
    }

    // --- Configure serial port -------------------------------------------------
    struct termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Baud rate
    speed_t speed = B19200;
    switch (baudRate) {
        case 9600:  speed = B9600;  break;
        case 19200: speed = B19200; break;
        case 38400: speed = B38400; break;
        case 57600: speed = B57600; break;
        case 115200:speed = B115200;break;
        default:    speed = B19200; break;
    }
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // 8E1: 8 data bits, even parity, 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;        // 8 data bits
    tty.c_cflag |= PARENB;     // enable parity
    tty.c_cflag &= ~PARODD;    // even parity
    tty.c_cflag &= ~CSTOPB;    // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;   // no hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // no software flow control
    tty.c_iflag |= (INPCK | ISTRIP);        // enable parity check

    tty.c_lflag = 0; // raw mode
    tty.c_oflag = 0; // raw output

    // Non-blocking read with timeout handled via select()
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    ::tcflush(fd_, TCIOFLUSH);
    return true;
}

void ModbusRtuSerial::disconnect()
{
    if (fd_ != -1) {
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
                                            std::vector<uint16_t>& data)
{
    if (!isConnected()) {
        return false;
    }

    ::tcflush(fd_, TCIOFLUSH);

    if (!sendReadRequest(address, count)) {
        return false;
    }

    return receiveReadResponse(count, data);
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
    request[6] = static_cast<uint8_t>(crc & 0xFF);  // CRC low byte first
    request[7] = static_cast<uint8_t>(crc >> 8);

    ssize_t written = ::write(fd_, request, sizeof(request));
    return written == static_cast<ssize_t>(sizeof(request));
}

bool ModbusRtuSerial::receiveReadResponse(uint16_t expectedCount,
                                           std::vector<uint16_t>& data)
{
    // Expected response length: 3 header bytes + 2*count data bytes + 2 CRC bytes
    const size_t expectedLen = 3u + static_cast<size_t>(expectedCount) * 2u + 2u;
    std::vector<uint8_t> buf(expectedLen, 0);

    size_t received = 0;
    while (received < expectedLen) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);

        struct timeval tv{};
        int remaining = timeoutMs_ - static_cast<int>(received * 10); // rough estimate
        if (remaining < 50) remaining = 50;
        tv.tv_sec  = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = ::select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0) {
            return false; // timeout or error
        }

        ssize_t n = ::read(fd_, buf.data() + received, expectedLen - received);
        if (n <= 0) {
            return false;
        }
        received += static_cast<size_t>(n);
    }

    // Validate device ID and function code
    if (buf[0] != static_cast<uint8_t>(deviceId_) || buf[1] != 0x03) {
        return false;
    }

    // Validate byte count
    if (buf[2] != static_cast<uint8_t>(expectedCount * 2)) {
        return false;
    }

    // Validate CRC (covers all bytes except the trailing two CRC bytes)
    uint16_t computedCrc = crc16(buf.data(), expectedLen - 2);
    uint16_t receivedCrc = static_cast<uint16_t>(buf[expectedLen - 2]) |
                           (static_cast<uint16_t>(buf[expectedLen - 1]) << 8);
    if (computedCrc != receivedCrc) {
        return false;
    }

    // Extract register values (big-endian pairs)
    data.resize(expectedCount);
    for (uint16_t i = 0; i < expectedCount; ++i) {
        size_t offset = 3u + static_cast<size_t>(i) * 2u;
        data[i] = (static_cast<uint16_t>(buf[offset]) << 8) |
                   static_cast<uint16_t>(buf[offset + 1]);
    }

    return true;
}

uint16_t ModbusRtuSerial::crc16(const uint8_t* buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
