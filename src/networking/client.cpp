#include "networking/client.h"

#include <iostream>
#include <cstring>
#include <sstream>

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

    read_buffer_.clear();
    return true;
}

std::string Client::send_command(const std::string& command) {
    if (!platform::is_valid(fd_)) return "Error: Not connected";

    // Send the command in Redis inline format: COMMAND args...\r\n
    // This is the simplest wire format that Redis servers accept.
    std::string wire = command + "\r\n";

    // Write all bytes to the socket
    size_t total = 0;
    while (total < wire.size()) {
        ssize_t bytes = platform::socket_write(fd_, wire.data() + total, wire.size() - total);
        if (bytes <= 0) {
            disconnect();
            return "Error: Failed to send command";
        }
        total += static_cast<size_t>(bytes);
    }

    // Read and parse the RESP response
    bool ok;
    std::string response = read_resp_response(ok);
    if (!ok) {
        disconnect();
        return "Error: Failed to read response";
    }

    return response;
}

std::string Client::read_resp_response(bool& ok) {
    // Try to parse a complete RESP value from whatever we already have
    // in the read buffer. If we don't have enough, read more from the socket.
    while (true) {
        size_t consumed = 0;
        bool parse_ok = false;
        std::string result = parse_resp_value(consumed, parse_ok);

        if (parse_ok) {
            read_buffer_.erase(0, consumed);
            ok = true;
            return result;
        }

        // Need more data — read from socket
        char buf[4096];
        ssize_t bytes = platform::socket_read(fd_, buf, sizeof(buf));
        if (bytes <= 0) {
            ok = false;
            return "";
        }
        read_buffer_.append(buf, static_cast<size_t>(bytes));
    }
}

/// Find \r\n in buffer starting at 'start'. Returns index of \r or npos.
static size_t find_crlf(const std::string& buf, size_t start) {
    for (size_t i = start; i + 1 < buf.size(); ++i) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            return i;
        }
    }
    return std::string::npos;
}

std::string Client::parse_resp_value(size_t& bytes_consumed, bool& ok) {
    ok = false;
    if (read_buffer_.empty()) return "";

    char type = read_buffer_[0];

    switch (type) {
        case '+': {
            // Simple String: +OK\r\n
            size_t crlf = find_crlf(read_buffer_, 0);
            if (crlf == std::string::npos) return "";
            std::string value = read_buffer_.substr(1, crlf - 1);
            bytes_consumed = crlf + 2;
            ok = true;
            return value;
        }

        case '-': {
            // Error: -ERR message\r\n
            size_t crlf = find_crlf(read_buffer_, 0);
            if (crlf == std::string::npos) return "";
            std::string value = read_buffer_.substr(1, crlf - 1);
            bytes_consumed = crlf + 2;
            ok = true;
            return value;
        }

        case ':': {
            // Integer: :1000\r\n
            size_t crlf = find_crlf(read_buffer_, 0);
            if (crlf == std::string::npos) return "";
            std::string num_str = read_buffer_.substr(1, crlf - 1);
            bytes_consumed = crlf + 2;
            ok = true;
            return "(integer) " + num_str;
        }

        case '$': {
            // Bulk String: $6\r\nfoobar\r\n  or  $-1\r\n (null)
            size_t crlf = find_crlf(read_buffer_, 0);
            if (crlf == std::string::npos) return "";

            std::string len_str = read_buffer_.substr(1, crlf - 1);
            int len;
            try {
                len = std::stoi(len_str);
            } catch (...) {
                ok = true;
                bytes_consumed = crlf + 2;
                return "(error) invalid bulk string length";
            }

            if (len < 0) {
                // Null bulk string
                bytes_consumed = crlf + 2;
                ok = true;
                return "(nil)";
            }

            // Check if we have the complete data + trailing \r\n
            size_t data_start = crlf + 2;
            if (data_start + static_cast<size_t>(len) + 2 > read_buffer_.size()) {
                return "";  // Need more data
            }

            std::string value = read_buffer_.substr(data_start, len);
            bytes_consumed = data_start + len + 2;
            ok = true;

            // Return the value with quotes for display (like redis-cli)
            return "\"" + value + "\"";
        }

        case '*': {
            // Array: *N\r\n<elements...>  or  *-1\r\n (null)  or  *0\r\n (empty)
            size_t crlf = find_crlf(read_buffer_, 0);
            if (crlf == std::string::npos) return "";

            std::string count_str = read_buffer_.substr(1, crlf - 1);
            int count;
            try {
                count = std::stoi(count_str);
            } catch (...) {
                ok = true;
                bytes_consumed = crlf + 2;
                return "(error) invalid array count";
            }

            if (count < 0) {
                bytes_consumed = crlf + 2;
                ok = true;
                return "(nil)";
            }

            if (count == 0) {
                bytes_consumed = crlf + 2;
                ok = true;
                return "(empty array)";
            }

            // Parse each element by temporarily adjusting our buffer view
            // We need to parse 'count' sub-values from the buffer.
            size_t pos = crlf + 2;
            std::ostringstream oss;

            // Save the original buffer and create a temporary one for sub-parsing
            std::string saved = read_buffer_;

            for (int i = 0; i < count; ++i) {
                // Set up buffer for sub-parsing starting at pos
                read_buffer_ = saved.substr(pos);
                size_t sub_consumed = 0;
                bool sub_ok = false;
                std::string elem = parse_resp_value(sub_consumed, sub_ok);

                if (!sub_ok) {
                    // Need more data — restore buffer and return
                    read_buffer_ = saved;
                    return "";
                }

                pos += sub_consumed;

                // Format like redis-cli: "N) value"
                oss << (i + 1) << ") " << elem;
                if (i + 1 < count) oss << "\n";
            }

            // Restore the original buffer
            read_buffer_ = saved;
            bytes_consumed = pos;
            ok = true;
            return oss.str();
        }

        default: {
            // Unknown type — try to read until \r\n and return as-is
            size_t crlf = find_crlf(read_buffer_, 0);
            if (crlf == std::string::npos) return "";
            std::string value = read_buffer_.substr(0, crlf);
            bytes_consumed = crlf + 2;
            ok = true;
            return value;
        }
    }
}

void Client::disconnect() {
    if (platform::is_valid(fd_)) {
        platform::close_socket(fd_);
        fd_ = INVALID_SOCKET_VAL;
    }
    read_buffer_.clear();
}

} // namespace myredis
