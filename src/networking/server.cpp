#include "networking/server.h"
#include "resp/resp.h"
#include "parser/parser.h"
#include "commands/command_handler.h"

#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <fcntl.h>
#endif

namespace myredis {

Server::Server(const std::string& host, int port, Database& db)
    : host_(host), port_(port), listen_fd_(INVALID_SOCKET_VAL), db_(db), running_(false) {}

Server::~Server() {
    stop();
    // Close all remaining client sockets
    for (auto& [fd, state] : clients_) {
        platform::close_socket(fd);
    }
    if (platform::is_valid(listen_fd_)) {
        platform::close_socket(listen_fd_);
    }
}

bool Server::set_nonblocking(socket_t sock) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

void Server::start() {
    // ═══════════════════════════════════════════════════════════
    // STEP 1: Create a socket
    // ═══════════════════════════════════════════════════════════
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (!platform::is_valid(listen_fd_)) {
        std::cerr << "[myredis] ERROR: Failed to create socket: "
                  << platform::last_error() << std::endl;
        return;
    }

    // Allow reuse of the address immediately after the server stops.
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    // ═══════════════════════════════════════════════════════════
    // STEP 2: Bind the socket to an address
    // ═══════════════════════════════════════════════════════════
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (host_ == "0.0.0.0" || host_.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
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
    if (listen(listen_fd_, 128) < 0) {
        std::cerr << "[myredis] ERROR: Failed to listen: "
                  << platform::last_error() << std::endl;
        platform::close_socket(listen_fd_);
        listen_fd_ = INVALID_SOCKET_VAL;
        return;
    }

    // Set listen socket to non-blocking so select() wakeup + accept doesn't block
    set_nonblocking(listen_fd_);

    running_ = true;
    start_time_ = std::chrono::steady_clock::now();
    std::cout << "[myredis] Server listening on " << host_ << ":" << port_ << std::endl;
    std::cout << "[myredis] Protocol: RESP2 (Redis Serialization Protocol)." << std::endl;
    std::cout << "[myredis] Event loop mode (single-threaded, select)." << std::endl;
    std::cout << "[myredis] Ready to accept connections." << std::endl;

    // ═══════════════════════════════════════════════════════════
    // STEP 4: select() event loop
    // ═══════════════════════════════════════════════════════════
    //
    // select() monitors multiple file descriptors simultaneously.
    // It blocks until at least one fd is "ready" (has data to read,
    // or space to write). This lets us handle all clients in a
    // single thread with no locks.
    //
    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        // Always watch the listen socket for new connections
        FD_SET(listen_fd_, &read_fds);

        // Compute the highest fd (needed by select() on POSIX)
        socket_t max_fd = listen_fd_;

        // Watch all connected client sockets
        for (const auto& [fd, state] : clients_) {
            FD_SET(fd, &read_fds);
#ifndef _WIN32
            if (fd > max_fd) max_fd = fd;
#endif
        }

        // Timeout: wake every 100ms to check running_ flag and do
        // background work (active expiry can run here too).
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;  // 100ms

        int ready = select(static_cast<int>(max_fd + 1), &read_fds, nullptr, nullptr, &timeout);

        if (ready < 0) {
            if (running_) {
                // On Windows, select() returns SOCKET_ERROR if the fd_set is
                // invalid (e.g., after we close the listen socket during stop()).
                // Don't log errors during shutdown.
                std::cerr << "[myredis] ERROR: select() failed: "
                          << platform::last_error() << std::endl;
            }
            break;
        }

        if (ready == 0) {
            // Timeout — no activity. Continue loop to check running_ flag.
            continue;
        }

        // Check for new connections on the listen socket
        if (FD_ISSET(listen_fd_, &read_fds)) {
            accept_client();
        }

        // Check each client socket for incoming data.
        // We collect fds to disconnect AFTER iteration to avoid
        // modifying clients_ while iterating it.
        std::vector<socket_t> to_disconnect;

        for (auto& [fd, state] : clients_) {
            if (FD_ISSET(fd, &read_fds)) {
                if (!process_client(fd)) {
                    to_disconnect.push_back(fd);
                }
            }
        }

        for (socket_t fd : to_disconnect) {
            disconnect_client(fd);
        }
    }
}

void Server::stop() {
    running_ = false;
    if (platform::is_valid(listen_fd_)) {
        platform::close_socket(listen_fd_);
        listen_fd_ = INVALID_SOCKET_VAL;
    }
}

void Server::accept_client() {
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    socket_t client_fd = accept(listen_fd_,
                                reinterpret_cast<struct sockaddr*>(&client_addr),
                                &client_len);

    if (!platform::is_valid(client_fd)) {
        // accept() can return EWOULDBLOCK/EAGAIN on non-blocking socket
        // if the connection was already closed before we accepted it.
        return;
    }

    // Set the client socket to non-blocking
    set_nonblocking(client_fd);

    // Format the client's IP:port for logging
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    std::string addr_str = std::string(client_ip) + ":"
                         + std::to_string(ntohs(client_addr.sin_port));

    std::cout << "[myredis] Client connected: " << addr_str << std::endl;

    clients_[client_fd] = ClientState(addr_str);
}

bool Server::process_client(socket_t client_fd) {
    // Read whatever data is available from the socket
    char buf[4096];
    ssize_t bytes = platform::socket_read(client_fd, buf, sizeof(buf));

    if (bytes <= 0) {
        // bytes == 0: clean disconnect (FIN)
        // bytes < 0: error (could be EWOULDBLOCK, but we only call this
        //            when select() says the socket is readable, so a
        //            negative return means real error or disconnect)
#ifdef _WIN32
        if (bytes < 0 && WSAGetLastError() == WSAEWOULDBLOCK) {
            return true;  // Spurious wakeup, try again later
        }
#else
        if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;  // Spurious wakeup
        }
#endif
        return false;  // Client disconnected
    }

    auto& state = clients_[client_fd];
    state.read_buffer.append(buf, static_cast<size_t>(bytes));

    // ── RESP Protocol Processing ─────────────────────────────
    // Parse complete commands from the buffer using RESP protocol.
    // Supports both RESP array format (from redis-cli, client libs)
    // and inline commands (from telnet, our CLI client).
    // We loop because a single read might contain multiple commands.
    while (!state.read_buffer.empty()) {
        RespParseResult parse = Resp::parse_command(state.read_buffer);

        if (parse.protocol_error) {
            // Send RESP error and disconnect
            std::string err = Resp::error("ERR Protocol error");
            Resp::send_response(client_fd, err);
            return false;
        }

        if (parse.need_more) {
            // Consume any bytes that were consumed (e.g., empty lines)
            if (parse.bytes_consumed > 0) {
                state.read_buffer.erase(0, parse.bytes_consumed);
                continue;
            }
            break;  // Wait for more data
        }

        if (!parse.ok) {
            break;  // Need more data or nothing to do
        }

        // Consume the parsed bytes from the buffer
        state.read_buffer.erase(0, parse.bytes_consumed);

        // Safety: reject absurdly large buffers
        if (state.read_buffer.size() > 64 * 1024 * 1024) {
            return false;
        }

        const Command& cmd = parse.command;
        std::string response;

        // Handle QUIT
        if (cmd.name == "QUIT") {
            Resp::send_response(client_fd, Resp::simple_string("OK"));
            return false;  // Will trigger disconnect
        }

        // Create a per-command handler (lightweight — just holds references)
        CommandHandler handler(db_, pubsub_, client_fd);
        handler.set_server(this);

        // AUTH check: if a password is set and client hasn't authenticated,
        // only allow AUTH and QUIT commands.
        if (!password_.empty() && !state.authenticated) {
            if (cmd.name == "AUTH") {
                if (cmd.args.size() != 1) {
                    response = "ERR wrong number of arguments for 'AUTH' command";
                } else if (cmd.args[0] == password_) {
                    state.authenticated = true;
                    response = "OK";
                } else {
                    response = "ERR invalid password";
                }
            } else {
                response = "NOAUTH Authentication required.";
            }
        } else {
            // Execute the command
            response = handler.execute(cmd);
        }

        // Serialize the response to RESP wire format and send
        std::string resp_data = Resp::serialize_response(response);
        if (!Resp::send_response(client_fd, resp_data)) {
            return false;  // Write failed — disconnect
        }
    }

    return true;
}

void Server::disconnect_client(socket_t client_fd) {
    auto it = clients_.find(client_fd);
    std::string addr = (it != clients_.end()) ? it->second.addr : "unknown";

    // Clean up pub/sub subscriptions
    pubsub_.client_disconnected(client_fd);

    // Close the socket
    platform::close_socket(client_fd);

    // Remove from our client map
    clients_.erase(client_fd);

    std::cout << "[myredis] Client disconnected: " << addr << std::endl;
}

} // namespace myredis
