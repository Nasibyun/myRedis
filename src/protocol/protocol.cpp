#include "protocol/protocol.h"

// htonl()/ntohl() are provided by platform.h
// (winsock2.h on Windows, arpa/inet.h on POSIX)
#include <cstring>       // memcpy

namespace myredis {

std::string Protocol::encode(const std::string& message) {
    // Convert the message length to network byte order (big-endian)
    // This ensures machines with different endianness can communicate
    uint32_t net_len = htonl(static_cast<uint32_t>(message.size()));

    // Build the wire-format: [4-byte length header][payload]
    std::string result(reinterpret_cast<const char*>(&net_len), sizeof(net_len));
    result += message;
    return result;
}

bool Protocol::read_exact(socket_t sock, char* buffer, size_t n) {
    // A single read/recv call may return fewer bytes than requested.
    // This happens because:
    //   - The kernel's receive buffer might not have all the data yet
    //   - The data might arrive in multiple TCP segments
    //   - The OS might give us a partial result
    //
    // So we loop until we've read exactly n bytes.
    size_t total = 0;
    while (total < n) {
        ssize_t bytes = platform::socket_read(sock, buffer + total, n - total);
        if (bytes <= 0) {
            // bytes == 0: connection closed by peer
            // bytes < 0: error (errno is set)
            return false;
        }
        total += static_cast<size_t>(bytes);
    }
    return true;
}

bool Protocol::write_exact(socket_t sock, const char* buffer, size_t n) {
    // Same issue as read_exact — write/send might not send all bytes at once.
    // The kernel's send buffer might be full, so we loop.
    size_t total = 0;
    while (total < n) {
        ssize_t bytes = platform::socket_write(sock, buffer + total, n - total);
        if (bytes <= 0) {
            return false;
        }
        total += static_cast<size_t>(bytes);
    }
    return true;
}

std::string Protocol::read_message(socket_t sock, bool& ok) {
    // ── Step 1: Read the 4-byte length header ──
    uint32_t net_len;
    if (!read_exact(sock, reinterpret_cast<char*>(&net_len), sizeof(net_len))) {
        ok = false;
        return "";
    }

    uint32_t len = ntohl(net_len);  // Convert from network to host byte order

    // Safety check: reject absurdly large messages (max 64 MB)
    // This prevents a malicious client from making us allocate huge buffers
    if (len > 64 * 1024 * 1024) {
        ok = false;
        return "";
    }

    // ── Step 2: Read exactly 'len' bytes of payload ──
    std::string message(len, '\0');
    if (!read_exact(sock, &message[0], len)) {
        ok = false;
        return "";
    }

    ok = true;
    return message;
}

bool Protocol::send_message(socket_t sock, const std::string& message) {
    std::string encoded = encode(message);
    return write_exact(sock, encoded.data(), encoded.size());
}

} // namespace myredis
