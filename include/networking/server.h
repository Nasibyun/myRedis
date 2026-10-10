#pragma once
///
/// server.h — Single-threaded event loop server using select()
///
/// Architecture:
///   1. Create a TCP socket and listen for connections
///   2. Use select() to multiplex all client sockets + the listen socket
///   3. When a client socket becomes readable, read available data
///      into a per-client buffer, then process complete messages
///   4. Single-threaded — no locks needed on the database
///
/// Previous design (thread-per-client) used one thread per connection
/// with shared_mutex on the database. This design:
///   - Uses O(1) threads regardless of client count
///   - Eliminates all locking overhead (single thread = no contention)
///   - Works cross-platform via select() (both POSIX and Winsock2)
///
/// Limitations of select():
///   - fd_set has a max size (FD_SETSIZE, typically 64 on Windows, 1024 on Linux)
///   - Not as scalable as epoll/kqueue for 10k+ connections
///   - Good enough for a learning project
///
/// Pub/Sub integration:
///   The server owns a PubSubManager that is used by CommandHandlers.
///   When a client disconnects, the server cleans up its subscriptions.
///

#include "database/database.h"
#include "pubsub/pubsub.h"
#include "platform/platform.h"
#include <string>
#include <unordered_map>
#include <atomic>

namespace myredis {

/// Per-client state in the event loop.
/// Each connected client gets one of these, tracking its read buffer
/// and metadata.
struct ClientState {
    std::string read_buffer;   // Accumulated bytes not yet forming a complete message
    std::string addr;          // Human-readable "ip:port" for logging
    bool authenticated;        // Whether this client has passed AUTH (if required)

    ClientState() : authenticated(false) {}
    ClientState(const std::string& a) : addr(a), authenticated(false) {}
};

class Server {
public:
    Server(const std::string& host, int port, Database& db);
    ~Server();

    /// Start the server (blocking — runs the select() event loop)
    void start();

    /// Signal the server to shut down
    void stop();

    /// Set a password requirement. If non-empty, clients must AUTH before
    /// executing commands. Used by Milestone 13 (Configuration).
    void set_password(const std::string& password) { password_ = password; }
    const std::string& get_password() const { return password_; }

    /// Get server start time (for INFO command uptime calculation)
    std::chrono::steady_clock::time_point start_time() const { return start_time_; }

    /// Get connected client count
    size_t client_count() const { return clients_.size(); }

private:
    std::string host_;
    int port_;
    socket_t listen_fd_;              // The listening socket
    Database& db_;                    // Shared database (no longer needs locks in single-thread)
    PubSubManager pubsub_;            // Pub/sub manager
    std::atomic<bool> running_;
    std::string password_;            // Empty = no auth required
    std::chrono::steady_clock::time_point start_time_;

    // Per-client state, keyed by socket fd
    std::unordered_map<socket_t, ClientState> clients_;

    /// Process readable data on a client socket.
    /// Returns false if the client should be disconnected.
    bool process_client(socket_t client_fd);

    /// Accept a new client connection.
    void accept_client();

    /// Disconnect and clean up a client.
    void disconnect_client(socket_t client_fd);

    /// Set a socket to non-blocking mode.
    static bool set_nonblocking(socket_t sock);
};

} // namespace myredis
