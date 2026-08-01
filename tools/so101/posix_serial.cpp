#include "posix_serial.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace plcopen::tools::so101
{

PosixSerial::~PosixSerial()
{
    if(descriptor_ >= 0) ::close(descriptor_);
}

bool PosixSerial::open_port(const char *path)
{
    if(path == nullptr || path[0] == '\0') {
        error_number_ = EINVAL;
        return false;
    }
    descriptor_ = ::open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(descriptor_ < 0) {
        error_number_ = errno;
        return false;
    }

    termios attributes{};
    if(::tcgetattr(descriptor_, &attributes) != 0) {
        error_number_ = errno;
        ::close(descriptor_);
        descriptor_ = -1;
        return false;
    }
    ::cfmakeraw(&attributes);
    if(::cfsetispeed(&attributes, B1000000) != 0 ||
       ::cfsetospeed(&attributes, B1000000) != 0) {
        error_number_ = errno;
        ::close(descriptor_);
        descriptor_ = -1;
        return false;
    }
    attributes.c_cflag |= CLOCAL | CREAD;
    attributes.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    attributes.c_cflag |= CS8;
    attributes.c_cc[VMIN] = 0;
    attributes.c_cc[VTIME] = 0;
    if(::tcsetattr(descriptor_, TCSANOW, &attributes) != 0) {
        error_number_ = errno;
        ::close(descriptor_);
        descriptor_ = -1;
        return false;
    }
    return discard_input();
}

bool PosixSerial::discard_input()
{
    if(descriptor_ < 0) {
        error_number_ = EBADF;
        return false;
    }
    if(::tcflush(descriptor_, TCIFLUSH) != 0) {
        error_number_ = errno;
        return false;
    }
    return true;
}

bool PosixSerial::write_all(const std::uint8_t *bytes, std::size_t size)
{
    if(descriptor_ < 0 || bytes == nullptr || size == 0) {
        error_number_ = EINVAL;
        return false;
    }
    std::size_t offset = 0;
    while(offset < size) {
        const ssize_t written =
            ::write(descriptor_, bytes + offset, size - offset);
        if(written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if(written < 0 && errno == EINTR) continue;
        if(written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd descriptor{descriptor_, POLLOUT, 0};
            const int ready = ::poll(&descriptor, 1, 1000);
            if(ready > 0 && (descriptor.revents & POLLOUT) != 0) continue;
            if(ready < 0) {
                error_number_ = errno;
            } else {
                error_number_ = ready == 0 ? ETIMEDOUT : EIO;
            }
            return false;
        }
        error_number_ = written == 0 ? EIO : errno;
        return false;
    }
    return true;
}

ReadResult PosixSerial::read_byte(std::uint8_t &byte, int timeout_ms)
{
    if(descriptor_ < 0 || timeout_ms <= 0) {
        error_number_ = EINVAL;
        return ReadResult::error;
    }
    pollfd descriptor{descriptor_, POLLIN, 0};
    int ready = 0;
    do {
        ready = ::poll(&descriptor, 1, timeout_ms);
    } while(ready < 0 && errno == EINTR);
    if(ready == 0) return ReadResult::timeout;
    if(ready < 0) {
        error_number_ = errno;
        return ReadResult::error;
    }
    if((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        error_number_ = EIO;
        return ReadResult::error;
    }
    const ssize_t count = ::read(descriptor_, &byte, 1);
    if(count == 1) return ReadResult::byte;
    if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return ReadResult::timeout;
    }
    error_number_ = count == 0 ? EIO : errno;
    return ReadResult::error;
}

const char *PosixSerial::error_text() const
{
    return std::strerror(error_number_);
}

} // namespace plcopen::tools::so101
