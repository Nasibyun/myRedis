#pragma once
///
/// command_handler.h — Routes parsed commands to database operations
///
/// Takes a parsed Command struct and a Database reference,
/// executes the appropriate operation, and returns a response string.
///
/// For Pub/Sub commands (SUBSCRIBE, UNSUBSCRIBE, PUBLISH), the handler
/// also needs access to the PubSubManager and the client's socket fd.
///
/// Milestone 14 adds transaction support (MULTI/EXEC/DISCARD/WATCH):
///   When MULTI is called, the handler enters "queuing mode" — all
///   subsequent commands are queued rather than executed immediately.
///   EXEC executes all queued commands atomically and returns an array
///   of results. WATCH enables optimistic locking.
///

#include "parser/parser.h"
#include "database/database.h"
#include "pubsub/pubsub.h"
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace myredis {

// Forward declaration
class Server;

class CommandHandler {
public:
    CommandHandler(Database& db, PubSubManager& pubsub, socket_t client_fd);

    /// Execute a parsed command and return the response string.
    /// In transaction mode, most commands are queued instead of executed.
    std::string execute(const Command& cmd);

    /// Check if this client is in subscription mode.
    bool in_subscription_mode() const;

    /// Check if this client is in a transaction (MULTI was called).
    bool in_transaction() const { return in_multi_; }

    /// Set a server reference (for INFO command)
    void set_server(Server* server) { server_ = server; }

private:
    Database& db_;
    PubSubManager& pubsub_;
    socket_t client_fd_;
    Server* server_ = nullptr;

    // ─── Transaction state ────────────────────────────────────
    bool in_multi_ = false;                  // True after MULTI, before EXEC/DISCARD
    std::vector<Command> tx_queue_;          // Queued commands
    std::vector<std::string> watched_keys_;  // Keys being watched
    bool watch_dirty_ = false;               // True if any watched key was modified

    // Snapshots of watched key values (taken at WATCH time)
    // Used to detect if watched keys changed between WATCH and EXEC
    struct WatchedKeySnapshot {
        std::string key;
        bool existed;
        std::optional<std::string> string_value;
        // For simplicity, we only snapshot string values.
        // List keys are tracked by existence + size.
        int list_size;
    };
    std::vector<WatchedKeySnapshot> watch_snapshots_;

    /// Check if any watched keys have changed since WATCH was called.
    bool watched_keys_modified();

    /// Execute a single command (used by EXEC to run queued commands).
    std::string execute_single(const Command& cmd);

    // ─── Command handlers ─────────────────────────────────────
    std::string handle_ping(const Command& cmd);
    std::string handle_set(const Command& cmd);
    std::string handle_get(const Command& cmd);
    std::string handle_del(const Command& cmd);
    std::string handle_exists(const Command& cmd);
    std::string handle_keys(const Command& cmd);
    std::string handle_dbsize(const Command& cmd);
    std::string handle_flushdb(const Command& cmd);
    std::string handle_expire(const Command& cmd);
    std::string handle_ttl(const Command& cmd);
    std::string handle_persist(const Command& cmd);
    std::string handle_save(const Command& cmd);

    // ─── List command handlers ─────────────────────────────────
    std::string handle_lpush(const Command& cmd);
    std::string handle_rpush(const Command& cmd);
    std::string handle_lpop(const Command& cmd);
    std::string handle_rpop(const Command& cmd);
    std::string handle_lrange(const Command& cmd);
    std::string handle_llen(const Command& cmd);
    std::string handle_lindex(const Command& cmd);

    // ─── Pub/Sub command handlers ──────────────────────────────
    std::string handle_subscribe(const Command& cmd);
    std::string handle_unsubscribe(const Command& cmd);
    std::string handle_publish(const Command& cmd);

    // ─── Transaction command handlers ──────────────────────────
    std::string handle_multi(const Command& cmd);
    std::string handle_exec(const Command& cmd);
    std::string handle_discard(const Command& cmd);
    std::string handle_watch(const Command& cmd);

    // ─── Config & Info command handlers ────────────────────────
    std::string handle_config(const Command& cmd);
    std::string handle_info(const Command& cmd);
};

} // namespace myredis
