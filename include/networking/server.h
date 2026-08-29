#pragma once
///
/// server.h — TCP server with thread-per-client concurrency
///
/// Architecture:
///   1. Create a TCP socket
///   2. Bind it to an address:port
///   3. Listen for incoming connections
///   4. For each new connection, spawn a dedicated thread
///   5. Each thread runs a loop: read → parse → execute → respond
///
/// Thread-per-client is the simplest concurrency model.
/// It doesn't scale well beyond ~1000 clients (each thread uses
/// ~8MB of stack memory), but it's perfect for learning.
///

#include "database/database.h"
#include "platform/platform.h"
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace myredis {

class Server {
public:
    Server(const std::string& host, int port, Database& db);
    ~Server();

    /// Start the server (blocking — runs the accept loop)
    void start();

    /// Signal the server to shut down
    void stop();

private:
    std::string host_;
    int port_;
    socket_t listen_fd_;         // The listening socket file descriptor
    Database& db_;               // Shared database (thread-safe)
    std::atomic<bool> running_;
    std::vector<std::thread> client_threads_;

    /// Handle a single client's request/response loop (runs in its own thread)
    void handle_client(socket_t client_fd, const std::string& client_addr);
};

} // namespace myredis
