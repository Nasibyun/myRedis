#pragma once
///
/// client.h — TCP client for connecting to myredis-server
///
/// Lifecycle:
///   1. Create a TCP socket
///   2. Connect to the server's address:port
///   3. Send commands (encoded with our length-prefixed protocol)
///   4. Receive responses
///   5. Disconnect when done
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
    std::string send_command(const std::string& command);

    /// Close the connection.
    void disconnect();

    /// Check if we're currently connected.
    bool is_connected() const { return platform::is_valid(fd_); }

private:
    std::string host_;
    int port_;
    socket_t fd_;  // Socket file descriptor (INVALID_SOCKET_VAL if not connected)
};

} // namespace myredis
