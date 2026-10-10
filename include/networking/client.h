#pragma once
///
/// client.h — TCP client for connecting to myredis-server
///
/// Lifecycle:
///   1. Create a TCP socket
///   2. Connect to the server's address:port
///   3. Send commands using inline format (COMMAND arg1 arg2\r\n)
///   4. Receive RESP-formatted responses
///   5. Disconnect when done
///
/// Milestone 12: Updated to use RESP protocol instead of the custom
/// 4-byte length-prefixed protocol. The client sends inline commands
/// (compatible with Redis inline mode) and parses RESP responses.
///

#include "platform/platform.h"
#include <string>

namespace myredis {

class Client {
public:
    Client(const std::string& host, int port);
    ~Client();

    /// Connect to the server. Returns true on success.
    bool connect();

    /// Send a command string and return the server's response.
    /// The command is sent in Redis inline format (command\r\n).
    /// The response is parsed from RESP wire format and returned
    /// as a human-readable string.
    std::string send_command(const std::string& command);

    /// Close the connection.
    void disconnect();

    /// Check if we're currently connected.
    bool is_connected() const { return platform::is_valid(fd_); }

private:
    std::string host_;
    int port_;
    socket_t fd_;  // Socket file descriptor (INVALID_SOCKET_VAL if not connected)
    std::string read_buffer_;  // Accumulates partial RESP responses

    /// Read a complete RESP response from the socket.
    /// Returns the RESP data as a human-readable string.
    /// Sets 'ok' to true on success, false on error/disconnect.
    std::string read_resp_response(bool& ok);

    /// Parse one complete RESP value from the read buffer.
    /// Returns the human-readable representation.
    /// Sets 'bytes_consumed' to the number of bytes consumed.
    /// Sets 'ok' to false if the buffer doesn't contain a complete value.
    std::string parse_resp_value(size_t& bytes_consumed, bool& ok);
};

} // namespace myredis
