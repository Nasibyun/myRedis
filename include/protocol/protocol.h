#pragma once
///
/// protocol.h — Length-prefixed wire protocol
///
/// TCP is a byte stream — it does NOT preserve message boundaries.
/// If the client sends "SET name Nasib" and then "GET name",
/// the server might receive "SET name NasibGET name" in a single
/// recv() call, or "SET na" in one call and "me Nasib" in the next.
///
/// Our protocol solves this by prepending a 4-byte length header:
///
///   [4 bytes: payload length in network byte order][payload bytes]
///
/// The receiver reads exactly 4 bytes to learn the message length,
/// then reads exactly that many bytes to get the complete message.
///
/// Network byte order (big-endian) is used so that machines with
/// different endianness can communicate correctly.
///

#include "platform/platform.h"
#include <string>
#include <cstdint>

namespace myredis {

class Protocol {
public:
    /// Encode a message with a 4-byte length prefix.
    /// Returns the complete wire-format bytes (header + payload).
    static std::string encode(const std::string& message);

    /// Read exactly one complete message from a socket.
    /// Sets 'ok' to true on success, false on error or disconnect.
    /// Returns the decoded payload (without the length header).
    static std::string read_message(socket_t sock, bool& ok);

    /// Send a complete message (with length header) to a socket.
    /// Returns true on success, false on error.
    static bool send_message(socket_t sock, const std::string& message);

private:
    /// Read exactly 'n' bytes from socket into buffer.
    /// Handles partial reads (when the OS returns fewer bytes than requested).
    static bool read_exact(socket_t sock, char* buffer, size_t n);

    /// Write exactly 'n' bytes from buffer to socket.
    /// Handles partial writes.
    static bool write_exact(socket_t sock, const char* buffer, size_t n);
};

} // namespace myredis
