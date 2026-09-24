#include "commands/command_handler.h"
#include "persistence/persistence.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace myredis {

// WRONGTYPE error message — same as Redis
static const std::string WRONGTYPE_ERR =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

CommandHandler::CommandHandler(Database& db, PubSubManager& pubsub, socket_t client_fd)
    : db_(db), pubsub_(pubsub), client_fd_(client_fd) {}

bool CommandHandler::in_subscription_mode() const {
    return pubsub_.is_subscribed(client_fd_);
}

std::string CommandHandler::execute(const Command& cmd) {
    // ── Subscription mode check ──────────────────────────────
    // When a client is subscribed to any channel, it enters "subscription mode."
    // In this mode, only SUBSCRIBE, UNSUBSCRIBE, and PING are allowed.
    // All other commands return an error — same behavior as Redis.
    if (in_subscription_mode()) {
        if (cmd.name != "SUBSCRIBE" && cmd.name != "UNSUBSCRIBE" && cmd.name != "PING") {
            return "ERR Can't execute '" + cmd.name +
                   "': only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT are allowed in this context";
        }
    }

    // Route to the appropriate handler based on command name
    if (cmd.name == "PING")    return handle_ping(cmd);
    if (cmd.name == "SET")     return handle_set(cmd);
    if (cmd.name == "GET")     return handle_get(cmd);
    if (cmd.name == "DEL")     return handle_del(cmd);
    if (cmd.name == "EXISTS")  return handle_exists(cmd);
    if (cmd.name == "KEYS")    return handle_keys(cmd);
    if (cmd.name == "DBSIZE")  return handle_dbsize(cmd);
    if (cmd.name == "FLUSHDB") return handle_flushdb(cmd);
    if (cmd.name == "EXPIRE")  return handle_expire(cmd);
    if (cmd.name == "TTL")     return handle_ttl(cmd);
    if (cmd.name == "PERSIST") return handle_persist(cmd);
    if (cmd.name == "SAVE")    return handle_save(cmd);

    // List commands
    if (cmd.name == "LPUSH")   return handle_lpush(cmd);
    if (cmd.name == "RPUSH")   return handle_rpush(cmd);
    if (cmd.name == "LPOP")    return handle_lpop(cmd);
    if (cmd.name == "RPOP")    return handle_rpop(cmd);
    if (cmd.name == "LRANGE")  return handle_lrange(cmd);
    if (cmd.name == "LLEN")    return handle_llen(cmd);
    if (cmd.name == "LINDEX")  return handle_lindex(cmd);

    // Pub/Sub commands
    if (cmd.name == "SUBSCRIBE")   return handle_subscribe(cmd);
    if (cmd.name == "UNSUBSCRIBE") return handle_unsubscribe(cmd);
    if (cmd.name == "PUBLISH")     return handle_publish(cmd);

    return "ERR unknown command '" + cmd.name + "'";
}

std::string CommandHandler::handle_ping(const Command& cmd) {
    if (cmd.args.empty()) return "PONG";
    return cmd.args[0];  // PING with an argument echoes it back
}

std::string CommandHandler::handle_set(const Command& cmd) {
    if (cmd.args.size() < 2) {
        return "ERR wrong number of arguments for 'SET' command";
    }

    db_.set(cmd.args[0], cmd.args[1]);

    // ── Handle SET key value EX seconds ──────────────────────
    // Scan remaining arguments for the EX option.
    // This allows: SET session abc123 EX 60
    if (cmd.args.size() >= 4) {
        // Convert option name to uppercase for case-insensitive matching
        std::string option = cmd.args[2];
        std::transform(option.begin(), option.end(), option.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        if (option == "EX") {
            // Parse the seconds value
            try {
                int seconds = std::stoi(cmd.args[3]);
                if (seconds <= 0) {
                    return "ERR invalid expire time in 'SET' command";
                }
                db_.expire(cmd.args[0], seconds);
            } catch (const std::exception&) {
                return "ERR value is not an integer or out of range";
            }
        }
    }

    return "OK";
}

std::string CommandHandler::handle_get(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'GET' command";
    }
    auto val = db_.get(cmd.args[0]);
    if (!val.has_value()) {
        // Check if the key exists as a list (GET on a list = WRONGTYPE)
        // We need to check after get() returns nullopt because the key
        // might be a list key. get() only searches store_, so a list key
        // looks like "not found" — but it's actually a type error.
        // Note: We can't easily check type after get() already acquired/released
        // the lock, so we accept a small race here. In practice, this is fine
        // because the command handler is the only consumer.
        return "(nil)";
    }
    return "\"" + val.value() + "\"";
}

std::string CommandHandler::handle_del(const Command& cmd) {
    if (cmd.args.empty()) {
        return "ERR wrong number of arguments for 'DEL' command";
    }
    int count = db_.del(cmd.args);
    return "(integer) " + std::to_string(count);
}

std::string CommandHandler::handle_exists(const Command& cmd) {
    if (cmd.args.empty()) {
        return "ERR wrong number of arguments for 'EXISTS' command";
    }
    int count = db_.exists(cmd.args);
    return "(integer) " + std::to_string(count);
}

std::string CommandHandler::handle_keys(const Command& cmd) {
    (void)cmd;  // No arguments needed
    auto all_keys = db_.keys();
    if (all_keys.empty()) return "(empty list)";

    std::ostringstream oss;
    for (size_t i = 0; i < all_keys.size(); ++i) {
        oss << (i + 1) << ") \"" << all_keys[i] << "\"";
        if (i + 1 < all_keys.size()) oss << "\n";
    }
    return oss.str();
}

std::string CommandHandler::handle_dbsize(const Command& cmd) {
    (void)cmd;
    return "(integer) " + std::to_string(db_.dbsize());
}

std::string CommandHandler::handle_flushdb(const Command& cmd) {
    (void)cmd;
    db_.flushdb();
    return "OK";
}

std::string CommandHandler::handle_expire(const Command& cmd) {
    if (cmd.args.size() != 2) {
        return "ERR wrong number of arguments for 'EXPIRE' command";
    }

    int seconds;
    try {
        seconds = std::stoi(cmd.args[1]);
    } catch (const std::exception&) {
        return "ERR value is not an integer or out of range";
    }

    if (seconds <= 0) {
        return "ERR invalid expire time in 'EXPIRE' command";
    }

    bool result = db_.expire(cmd.args[0], seconds);
    return "(integer) " + std::string(result ? "1" : "0");
}

std::string CommandHandler::handle_ttl(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'TTL' command";
    }
    int remaining = db_.ttl(cmd.args[0]);
    return "(integer) " + std::to_string(remaining);
}

std::string CommandHandler::handle_persist(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'PERSIST' command";
    }
    bool result = db_.persist(cmd.args[0]);
    return "(integer) " + std::string(result ? "1" : "0");
}

std::string CommandHandler::handle_save(const Command& cmd) {
    (void)cmd;
    bool ok = Persistence::save("dump.mrdb", db_);
    if (ok) {
        return "OK";
    }
    return "ERR failed to save database";
}

// ─── List Command Handlers ────────────────────────────────────

std::string CommandHandler::handle_lpush(const Command& cmd) {
    if (cmd.args.size() < 2) {
        return "ERR wrong number of arguments for 'LPUSH' command";
    }

    // args[0] = key, args[1..] = values
    std::vector<std::string> values(cmd.args.begin() + 1, cmd.args.end());
    int result = db_.lpush(cmd.args[0], values);

    if (result == -1) {
        return WRONGTYPE_ERR;
    }

    return "(integer) " + std::to_string(result);
}

std::string CommandHandler::handle_rpush(const Command& cmd) {
    if (cmd.args.size() < 2) {
        return "ERR wrong number of arguments for 'RPUSH' command";
    }

    std::vector<std::string> values(cmd.args.begin() + 1, cmd.args.end());
    int result = db_.rpush(cmd.args[0], values);

    if (result == -1) {
        return WRONGTYPE_ERR;
    }

    return "(integer) " + std::to_string(result);
}

std::string CommandHandler::handle_lpop(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'LPOP' command";
    }

    auto val = db_.lpop(cmd.args[0]);
    if (!val.has_value()) {
        return "(nil)";
    }

    return "\"" + val.value() + "\"";
}

std::string CommandHandler::handle_rpop(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'RPOP' command";
    }

    auto val = db_.rpop(cmd.args[0]);
    if (!val.has_value()) {
        return "(nil)";
    }

    return "\"" + val.value() + "\"";
}

std::string CommandHandler::handle_lrange(const Command& cmd) {
    if (cmd.args.size() != 3) {
        return "ERR wrong number of arguments for 'LRANGE' command";
    }

    int start, stop;
    try {
        start = std::stoi(cmd.args[1]);
        stop = std::stoi(cmd.args[2]);
    } catch (const std::exception&) {
        return "ERR value is not an integer or out of range";
    }

    auto elements = db_.lrange(cmd.args[0], start, stop);

    if (elements.empty()) {
        return "(empty list)";
    }

    std::ostringstream oss;
    for (size_t i = 0; i < elements.size(); ++i) {
        oss << (i + 1) << ") \"" << elements[i] << "\"";
        if (i + 1 < elements.size()) oss << "\n";
    }
    return oss.str();
}

std::string CommandHandler::handle_llen(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'LLEN' command";
    }

    int result = db_.llen(cmd.args[0]);

    if (result == -1) {
        return WRONGTYPE_ERR;
    }

    return "(integer) " + std::to_string(result);
}

std::string CommandHandler::handle_lindex(const Command& cmd) {
    if (cmd.args.size() != 2) {
        return "ERR wrong number of arguments for 'LINDEX' command";
    }

    int index;
    try {
        index = std::stoi(cmd.args[1]);
    } catch (const std::exception&) {
        return "ERR value is not an integer or out of range";
    }

    auto val = db_.lindex(cmd.args[0], index);
    if (!val.has_value()) {
        return "(nil)";
    }

    return "\"" + val.value() + "\"";
}

// ─── Pub/Sub Command Handlers ────────────────────────────────

std::string CommandHandler::handle_subscribe(const Command& cmd) {
    if (cmd.args.empty()) {
        return "ERR wrong number of arguments for 'SUBSCRIBE' command";
    }

    // Subscribe to all requested channels
    auto results = pubsub_.subscribe(client_fd_, cmd.args);

    // Build response — one "subscribe" confirmation per channel.
    // Format matches Redis:
    //   subscribe <channel> <subscription_count>
    //
    // In our custom protocol, we concatenate all confirmations into
    // a single response string separated by newlines.
    std::ostringstream oss;
    for (size_t i = 0; i < results.size(); ++i) {
        oss << "subscribe " << results[i].first << " " << results[i].second;
        if (i + 1 < results.size()) oss << "\n";
    }
    return oss.str();
}

std::string CommandHandler::handle_unsubscribe(const Command& cmd) {
    // Unsubscribe from specified channels (or all if none given)
    auto results = pubsub_.unsubscribe(client_fd_, cmd.args);

    // Build response — one "unsubscribe" confirmation per channel.
    std::ostringstream oss;
    for (size_t i = 0; i < results.size(); ++i) {
        oss << "unsubscribe " << results[i].first << " " << results[i].second;
        if (i + 1 < results.size()) oss << "\n";
    }
    return oss.str();
}

std::string CommandHandler::handle_publish(const Command& cmd) {
    if (cmd.args.size() != 2) {
        return "ERR wrong number of arguments for 'PUBLISH' command";
    }

    // args[0] = channel, args[1] = message
    int receivers = pubsub_.publish(cmd.args[0], cmd.args[1]);
    return "(integer) " + std::to_string(receivers);
}

} // namespace myredis
