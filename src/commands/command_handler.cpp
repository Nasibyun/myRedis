#include "commands/command_handler.h"
#include "persistence/persistence.h"
#include "networking/server.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <chrono>

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
    if (in_subscription_mode()) {
        if (cmd.name != "SUBSCRIBE" && cmd.name != "UNSUBSCRIBE" && cmd.name != "PING") {
            return "ERR Can't execute '" + cmd.name +
                   "': only (P)SUBSCRIBE / (P)UNSUBSCRIBE / PING / QUIT are allowed in this context";
        }
    }

    // ── Transaction commands are always executed immediately ───
    if (cmd.name == "MULTI")   return handle_multi(cmd);
    if (cmd.name == "EXEC")    return handle_exec(cmd);
    if (cmd.name == "DISCARD") return handle_discard(cmd);
    if (cmd.name == "WATCH")   return handle_watch(cmd);

    // ── Queue commands if in transaction mode ─────────────────
    if (in_multi_) {
        tx_queue_.push_back(cmd);
        return "QUEUED";
    }

    return execute_single(cmd);
}

std::string CommandHandler::execute_single(const Command& cmd) {
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

    // Config & Info commands
    if (cmd.name == "CONFIG")  return handle_config(cmd);
    if (cmd.name == "INFO")    return handle_info(cmd);

    return "ERR unknown command '" + cmd.name + "'";
}

std::string CommandHandler::handle_ping(const Command& cmd) {
    if (cmd.args.empty()) return "PONG";
    return cmd.args[0];
}

std::string CommandHandler::handle_set(const Command& cmd) {
    if (cmd.args.size() < 2) {
        return "ERR wrong number of arguments for 'SET' command";
    }

    db_.set(cmd.args[0], cmd.args[1]);

    // Handle SET key value EX seconds
    if (cmd.args.size() >= 4) {
        std::string option = cmd.args[2];
        std::transform(option.begin(), option.end(), option.begin(),
                       [](unsigned char c) { return std::toupper(c); });

        if (option == "EX") {
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
    (void)cmd;
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

    auto results = pubsub_.subscribe(client_fd_, cmd.args);

    std::ostringstream oss;
    for (size_t i = 0; i < results.size(); ++i) {
        oss << "subscribe " << results[i].first << " " << results[i].second;
        if (i + 1 < results.size()) oss << "\n";
    }
    return oss.str();
}

std::string CommandHandler::handle_unsubscribe(const Command& cmd) {
    auto results = pubsub_.unsubscribe(client_fd_, cmd.args);

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

    int receivers = pubsub_.publish(cmd.args[0], cmd.args[1]);
    return "(integer) " + std::to_string(receivers);
}

// ─── Transaction Command Handlers ────────────────────────────

std::string CommandHandler::handle_multi(const Command& cmd) {
    (void)cmd;
    if (in_multi_) {
        return "ERR MULTI calls can not be nested";
    }
    in_multi_ = true;
    tx_queue_.clear();
    return "OK";
}

std::string CommandHandler::handle_exec(const Command& cmd) {
    (void)cmd;
    if (!in_multi_) {
        return "ERR EXEC without MULTI";
    }

    // Check if any watched keys were modified
    if (!watched_keys_.empty() && watched_keys_modified()) {
        // Transaction aborted due to WATCH — clear state
        in_multi_ = false;
        tx_queue_.clear();
        watched_keys_.clear();
        watch_snapshots_.clear();
        watch_dirty_ = false;
        return "(nil)";  // Redis returns nil array when WATCH fails
    }

    // Execute all queued commands atomically
    // In our single-threaded event loop, this is naturally atomic
    // (no other client can interleave commands).
    std::ostringstream oss;
    for (size_t i = 0; i < tx_queue_.size(); ++i) {
        std::string result = execute_single(tx_queue_[i]);
        oss << (i + 1) << ") " << result;
        if (i + 1 < tx_queue_.size()) oss << "\n";
    }

    // Clear transaction state
    std::string response = tx_queue_.empty() ? "(empty list)" : oss.str();
    in_multi_ = false;
    tx_queue_.clear();
    watched_keys_.clear();
    watch_snapshots_.clear();
    watch_dirty_ = false;

    return response;
}

std::string CommandHandler::handle_discard(const Command& cmd) {
    (void)cmd;
    if (!in_multi_) {
        return "ERR DISCARD without MULTI";
    }

    in_multi_ = false;
    tx_queue_.clear();
    // Note: DISCARD does NOT clear watches (same as Redis)
    return "OK";
}

std::string CommandHandler::handle_watch(const Command& cmd) {
    if (cmd.args.empty()) {
        return "ERR wrong number of arguments for 'WATCH' command";
    }
    if (in_multi_) {
        return "ERR WATCH inside MULTI is not allowed";
    }

    // Snapshot the current state of each watched key
    for (const auto& key : cmd.args) {
        watched_keys_.push_back(key);

        WatchedKeySnapshot snap;
        snap.key = key;
        auto val = db_.get(key);
        if (val.has_value()) {
            snap.existed = true;
            snap.string_value = val.value();
            snap.list_size = -1;  // Not a list
        } else {
            // Check if it's a list
            int llen = db_.llen(key);
            if (llen >= 0) {
                snap.existed = true;
                snap.string_value = std::nullopt;
                snap.list_size = llen;
            } else {
                snap.existed = false;
                snap.string_value = std::nullopt;
                snap.list_size = -1;
            }
        }
        watch_snapshots_.push_back(snap);
    }

    return "OK";
}

bool CommandHandler::watched_keys_modified() {
    for (const auto& snap : watch_snapshots_) {
        auto val = db_.get(snap.key);
        if (snap.existed) {
            if (snap.string_value.has_value()) {
                // Was a string — check if value changed
                if (!val.has_value() || val.value() != snap.string_value.value()) {
                    return true;
                }
            } else if (snap.list_size >= 0) {
                // Was a list — check if size changed or no longer exists
                int llen = db_.llen(snap.key);
                if (llen != snap.list_size) {
                    return true;
                }
            }
        } else {
            // Key didn't exist — check if it now exists
            if (val.has_value() || db_.llen(snap.key) >= 0) {
                return true;
            }
        }
    }
    return false;
}

// ─── Config & Info Command Handlers ──────────────────────────

std::string CommandHandler::handle_config(const Command& cmd) {
    if (cmd.args.empty()) {
        return "ERR wrong number of arguments for 'CONFIG' command";
    }

    std::string subcmd = cmd.args[0];
    std::transform(subcmd.begin(), subcmd.end(), subcmd.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (subcmd == "GET") {
        if (cmd.args.size() != 2) {
            return "ERR wrong number of arguments for 'CONFIG GET' command";
        }
        // For now, return known config keys
        // In a full implementation, this would query the Config object
        std::string param = cmd.args[1];
        if (param == "save" || param == "port" || param == "bind" ||
            param == "requirepass" || param == "loglevel" || param == "maxmemory") {
            return "1) \"" + param + "\"\n2) \"\"";
        }
        return "(empty list)";
    }

    if (subcmd == "SET") {
        if (cmd.args.size() != 3) {
            return "ERR wrong number of arguments for 'CONFIG SET' command";
        }
        // Runtime config changes
        return "OK";
    }

    return "ERR Unknown subcommand '" + subcmd + "'";
}

std::string CommandHandler::handle_info(const Command& cmd) {
    (void)cmd;

    std::ostringstream oss;

    // Server section
    oss << "# Server\n";
    oss << "myredis_version:1.0.0\n";
    oss << "tcp_port:6399\n";

    if (server_) {
        auto now = std::chrono::steady_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - server_->start_time()).count();
        oss << "uptime_in_seconds:" << uptime << "\n";
        oss << "uptime_in_days:" << (uptime / 86400) << "\n";

        // Clients section
        oss << "\n# Clients\n";
        oss << "connected_clients:" << server_->client_count() << "\n";
    }

    // Keyspace section
    oss << "\n# Keyspace\n";
    oss << "db0:keys=" << db_.dbsize() << "\n";

    return oss.str();
}

} // namespace myredis
