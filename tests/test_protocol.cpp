///
/// test_protocol.cpp — Unit tests for the wire protocol
///
/// Tests encoding and round-trip message handling.
///
/// On POSIX (Linux/macOS): uses pipe() to simulate a socket connection.
/// On Windows: uses a loopback TCP socketpair since Windows pipes
/// don't work with Winsock recv()/send().
///

#include "protocol/protocol.h"
#include "platform/platform.h"
#include <cassert>
#include <iostream>
#include <cstring>
#include <vector>

// ── Cross-platform "pipe" abstraction for testing ────────────
// Returns two connected socket_t values: fds[0] for reading, fds[1] for writing.

#ifdef _WIN32

/// On Windows, create a connected socket pair via a loopback TCP connection.
/// This mimics POSIX socketpair() which Windows doesn't support.
bool create_test_pipe(socket_t fds[2]) {
    // Create a temporary listening socket on loopback
    socket_t listener = socket(AF_INET, SOCK_STREAM, 0);
    if (!myredis::platform::is_valid(listener)) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // Let the OS pick an available port

    if (bind(listener, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        myredis::platform::close_socket(listener);
        return false;
    }

    // Find out what port the OS assigned
    socklen_t addrlen = sizeof(addr);
    if (getsockname(listener, reinterpret_cast<struct sockaddr*>(&addr), &addrlen) < 0) {
        myredis::platform::close_socket(listener);
        return false;
    }

    if (listen(listener, 1) < 0) {
        myredis::platform::close_socket(listener);
        return false;
    }

    // Connect from the write end
    fds[1] = socket(AF_INET, SOCK_STREAM, 0);
    if (!myredis::platform::is_valid(fds[1])) {
        myredis::platform::close_socket(listener);
        return false;
    }

    if (::connect(fds[1], reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        myredis::platform::close_socket(fds[1]);
        myredis::platform::close_socket(listener);
        return false;
    }

    // Accept on the read end
    fds[0] = accept(listener, nullptr, nullptr);
    myredis::platform::close_socket(listener);

    if (!myredis::platform::is_valid(fds[0])) {
        myredis::platform::close_socket(fds[1]);
        return false;
    }

    return true;
}

void close_test_pipe(socket_t fds[2]) {
    myredis::platform::close_socket(fds[0]);
    myredis::platform::close_socket(fds[1]);
}

#else  // POSIX

#include <unistd.h>

bool create_test_pipe(socket_t fds[2]) {
    return pipe(fds) == 0;
}

void close_test_pipe(socket_t fds[2]) {
    close(fds[0]);
    close(fds[1]);
}

#endif

// ── Tests ────────────────────────────────────────────────────

void test_encode_basic() {
    std::string encoded = myredis::Protocol::encode("HELLO");
    // Should be 4 bytes (length header) + 5 bytes (payload) = 9 bytes total
    assert(encoded.size() == 9);

    // Verify the length header is correct
    uint32_t net_len;
    memcpy(&net_len, encoded.data(), 4);
    assert(ntohl(net_len) == 5);

    // Verify the payload
    assert(encoded.substr(4) == "HELLO");
    std::cout << "  PASS: test_encode_basic" << std::endl;
}

void test_encode_empty_message() {
    std::string encoded = myredis::Protocol::encode("");
    assert(encoded.size() == 4);  // Just the header, no payload

    uint32_t net_len;
    memcpy(&net_len, encoded.data(), 4);
    assert(ntohl(net_len) == 0);
    std::cout << "  PASS: test_encode_empty_message" << std::endl;
}

void test_roundtrip_via_pipe() {
    socket_t fds[2];
    assert(create_test_pipe(fds));

    std::string original = "SET name Nasib";
    assert(myredis::Protocol::send_message(fds[1], original));

    bool ok;
    std::string received = myredis::Protocol::read_message(fds[0], ok);
    assert(ok);
    assert(received == original);

    close_test_pipe(fds);
    std::cout << "  PASS: test_roundtrip_via_pipe" << std::endl;
}

void test_multiple_messages() {
    socket_t fds[2];
    assert(create_test_pipe(fds));

    // Send multiple messages through the pipe
    std::vector<std::string> messages = {
        "PING",
        "SET key value",
        "GET key",
        "DEL key",
        "EXISTS key",
    };

    for (const auto& msg : messages) {
        assert(myredis::Protocol::send_message(fds[1], msg));
    }

    // Read them all back — each message should be perfectly framed
    for (const auto& expected : messages) {
        bool ok;
        std::string received = myredis::Protocol::read_message(fds[0], ok);
        assert(ok);
        assert(received == expected);
    }

    close_test_pipe(fds);
    std::cout << "  PASS: test_multiple_messages" << std::endl;
}

void test_message_with_special_chars() {
    socket_t fds[2];
    assert(create_test_pipe(fds));

    std::string msg = "SET key \"hello\nworld\t!\"";
    assert(myredis::Protocol::send_message(fds[1], msg));

    bool ok;
    std::string received = myredis::Protocol::read_message(fds[0], ok);
    assert(ok);
    assert(received == msg);

    close_test_pipe(fds);
    std::cout << "  PASS: test_message_with_special_chars" << std::endl;
}

void test_closed_pipe_returns_error() {
    socket_t fds[2];
    assert(create_test_pipe(fds));

    // Close write end
    myredis::platform::close_socket(fds[1]);

    bool ok;
    std::string received = myredis::Protocol::read_message(fds[0], ok);
    assert(!ok);  // Should fail because the pipe is closed

    myredis::platform::close_socket(fds[0]);
    std::cout << "  PASS: test_closed_pipe_returns_error" << std::endl;
}

void test_1kb_message() {
    socket_t fds[2];
    assert(create_test_pipe(fds));

    // Create a 1KB message
    std::string msg(1024, 'A');
    assert(myredis::Protocol::send_message(fds[1], msg));

    bool ok;
    std::string received = myredis::Protocol::read_message(fds[0], ok);
    assert(ok);
    assert(received.size() == 1024);
    assert(received == msg);

    close_test_pipe(fds);
    std::cout << "  PASS: test_1kb_message" << std::endl;
}

int main() {
    // Initialize platform (needed for Winsock on Windows)
    myredis::platform::init();

    std::cout << "=== Protocol Tests ===" << std::endl;

    test_encode_basic();
    test_encode_empty_message();
    test_roundtrip_via_pipe();
    test_multiple_messages();
    test_message_with_special_chars();
    test_closed_pipe_returns_error();
    test_1kb_message();

    std::cout << std::endl;
    std::cout << "All protocol tests passed! (" << 7 << " tests)" << std::endl;

    myredis::platform::cleanup();
    return 0;
}
