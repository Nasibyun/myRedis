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

#include "parser/parser.h"
#include "database/database.h"
#include "pubsub/pubsub.h"
#include <string>

namespace myredis {

class CommandHandler {
public:
    CommandHandler(Database& db, PubSubManager& pubsub, socket_t client_fd);

    /// Execute a parsed command and return the response string.
    std::string execute(const Command& cmd);

    /// Check if this client is in subscription mode.
    /// When in subscription mode, only SUBSCRIBE, UNSUBSCRIBE, and PING are allowed.
    bool in_subscription_mode() const;

private:
    Database& db_;
    PubSubManager& pubsub_;
    socket_t client_fd_;  // The socket fd of the client this handler serves

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
};

} // namespace myredis
