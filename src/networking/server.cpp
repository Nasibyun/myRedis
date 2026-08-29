#include "networking/server.h"
#include "protocol/protocol.h"
#include "parser/parser.h"
#include "commands/command_handler.h"

#include <iostream>
#include <cstring>

namespace myredis {

Server::Server(const std::string& host, int port, Database& db)
    : host_(host), port_(port), listen_fd_(INVALID_SOCKET_VAL), db_(db), running_(false) {}

Server::~Server() {
    stop();
    // Detach all client threads so they can finish on their own
    for (auto& t : client_threads_) {
        if (t.joinable()) t.detach();
    }
    if (platform::is_valid(listen_fd_)) {
        platform::close_socket(listen_fd_);
    }
}

void Server::start() {
    // ═══════════════════════════════════════════════════════════
    // STEP 1: Create a socket
    // ═══════════════════════════════════════════════════════════
    //
    // socket() creates an endpoint for communication and returns
    // a file descriptor (a small integer that the OS uses to
    // identify this resource).
    //
    //   AF_INET     = IPv4 address family
    //   SOCK_STREAM = TCP (reliable, ordered, connection-based byte stream)
    //   0           = let the OS choose the protocol (TCP for SOCK_STREAM)
    //
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!platform::is_valid(listen_fd_)) {
        std::cerr << "[myredis] ERROR: Failed to create socket: "
                  << platform::last_error() << std::endl;
        return;
    }

    // Allow reuse of the address immediately after the server stops.
    // Without this, restarting the server quickly gives "Address already in use"
    // because the OS keeps the old socket in TIME_WAIT state for ~60 seconds.
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    // ═══════════════════════════════════════════════════════════
    // STEP 2: Bind the socket to an address
    // ═══════════════════════════════════════════════════════════
    //
    // bind() assigns a local address (IP + port) to the socket.
    // This tells the OS: "I want to receive connections on this address."
    //
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));  // host-to-network byte order

    if (host_ == "0.0.0.0" || host_.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all network interfaces
    } else {
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);
    }

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[myredis] ERROR: Failed to bind to " << host_ << ":" << port_
                  << ": " << platform::last_error() << std::endl;
        platform::close_socket(listen_fd_);
        listen_fd_ = INVALID_SOCKET_VAL;
        return;
    }

    // ═══════════════════════════════════════════════════════════
    // STEP 3: Listen
    // ═══════════════════════════════════════════════════════════
    //
    // listen() marks this socket as a "passive" socket — one that
    // will be used to accept incoming connections rather than
    // initiate outgoing ones.
    //
    // The backlog (128) is the maximum number of connections that
    // can be waiting in the queue before we call accept().
    //
    if (listen(listen_fd_, 128) < 0) {
        std::cerr << "[myredis] ERROR: Failed to listen: "
                  << platform::last_error() << std::endl;
        platform::close_socket(listen_fd_);
        listen_fd_ = INVALID_SOCKET_VAL;
        return;
    }

    running_ = true;
    std::cout << "[myredis] Server listening on " << host_ << ":" << port_ << std::endl;
    std::cout << "[myredis] Ready to accept connections." << std::endl;

    // ═══════════════════════════════════════════════════════════
    // STEP 4: Accept loop
    // ═══════════════════════════════════════════════════════════
    //
    // accept() blocks until a client connects. When it returns:
    //   - It gives us a NEW file descriptor for this specific connection
    //   - listen_fd_ stays open and continues accepting more connections
    //   - The new fd is used for all communication with this client
    //
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        socket_t client_fd = accept(listen_fd_,
                               reinterpret_cast<struct sockaddr*>(&client_addr),
                               &client_len);

        if (!platform::is_valid(client_fd)) {
            if (running_) {
                std::cerr << "[myredis] ERROR: accept() failed: "
                          << platform::last_error() << std::endl;
            }
            continue;
        }

        // Format the client's IP:port for logging
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        std::string addr_str = std::string(client_ip) + ":"
                             + std::to_string(ntohs(client_addr.sin_port));

        std::cout << "[myredis] Client connected: " << addr_str << std::endl;

        // Spawn a dedicated thread for this client.
        // The thread will run handle_client() independently.
        client_threads_.emplace_back(&Server::handle_client, this, client_fd, addr_str);
    }
}

void Server::stop() {
    running_ = false;
    if (platform::is_valid(listen_fd_)) {
        // Closing the listen socket causes accept() to return with an error,
        // which breaks the accept loop above.
        platform::close_socket(listen_fd_);
        listen_fd_ = INVALID_SOCKET_VAL;
    }
}

void Server::handle_client(socket_t client_fd, const std::string& client_addr) {
    CommandHandler handler(db_);

    // ── Client request/response loop ──────────────────────────
    // Each iteration:
    //   1. Read a complete message (protocol handles framing)
    //   2. Parse it into a Command
    //   3. Execute the command against the database
    //   4. Send the response back
    //
    while (running_) {
        // Read one complete message from the client
        bool ok;
        std::string message = Protocol::read_message(client_fd, ok);

        if (!ok) {
            break;  // Client disconnected or protocol error
        }

        // Parse the raw message into a structured Command
        ParseResult result = Parser::parse(message);

        std::string response;
        if (!result.ok) {
            response = result.error;
        } else {
            // Handle QUIT specially — close the connection
            if (result.command.name == "QUIT") {
                Protocol::send_message(client_fd, "OK");
                break;
            }
            // Execute the command
            response = handler.execute(result.command);
        }

        // Send the response back to the client
        if (!Protocol::send_message(client_fd, response)) {
            break;  // Client disconnected mid-write
        }
    }

    // ── Cleanup ──────────────────────────────────────────────
    // Close the socket, releasing the file descriptor and telling
    // the OS to send a FIN packet to the client, gracefully closing
    // the TCP connection.
    platform::close_socket(client_fd);
    std::cout << "[myredis] Client disconnected: " << client_addr << std::endl;
}

} // namespace myredis
