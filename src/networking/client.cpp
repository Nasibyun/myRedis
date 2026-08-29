#include "networking/client.h"
#include "protocol/protocol.h"

#include <iostream>
#include <cstring>

namespace myredis {

Client::Client(const std::string& host, int port)
    : host_(host), port_(port), fd_(INVALID_SOCKET_VAL) {}

Client::~Client() {
    disconnect();
}

bool Client::connect() {
    // Create a TCP socket (same as the server)
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!platform::is_valid(fd_)) {
        std::cerr << "Error: Failed to create socket: "
                  << platform::last_error() << std::endl;
        return false;
    }

    // Set up the server address we want to connect to
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    // inet_pton() converts a human-readable IP address ("127.0.0.1")
    // into its binary network representation
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "Error: Invalid address: " << host_ << std::endl;
        platform::close_socket(fd_);
        fd_ = INVALID_SOCKET_VAL;
        return false;
    }

    // connect() initiates a TCP handshake (SYN → SYN-ACK → ACK)
    // with the server. It blocks until the connection is established
    // or fails (e.g., connection refused if no server is listening).
    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Error: Connection failed: " << platform::last_error() << std::endl;
        platform::close_socket(fd_);
        fd_ = INVALID_SOCKET_VAL;
        return false;
    }

    return true;
}

std::string Client::send_command(const std::string& command) {
    if (!platform::is_valid(fd_)) return "Error: Not connected";

    // Encode and send the command using our length-prefixed protocol
    if (!Protocol::send_message(fd_, command)) {
        disconnect();
        return "Error: Failed to send command";
    }

    // Wait for the server's response
    bool ok;
    std::string response = Protocol::read_message(fd_, ok);
    if (!ok) {
        disconnect();
        return "Error: Failed to read response";
    }

    return response;
}

void Client::disconnect() {
    if (platform::is_valid(fd_)) {
        platform::close_socket(fd_);
        fd_ = INVALID_SOCKET_VAL;
    }
}

} // namespace myredis
