#include "commands/command_handler.h"
#include <sstream>

namespace myredis {

CommandHandler::CommandHandler(Database& db) : db_(db) {}

std::string CommandHandler::execute(const Command& cmd) {
    // Route to the appropriate handler based on command name
    if (cmd.name == "PING")    return handle_ping(cmd);
    if (cmd.name == "SET")     return handle_set(cmd);
    if (cmd.name == "GET")     return handle_get(cmd);
    if (cmd.name == "DEL")     return handle_del(cmd);
    if (cmd.name == "EXISTS")  return handle_exists(cmd);
    if (cmd.name == "KEYS")    return handle_keys(cmd);
    if (cmd.name == "DBSIZE")  return handle_dbsize(cmd);
    if (cmd.name == "FLUSHDB") return handle_flushdb(cmd);

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
    return "OK";
}

std::string CommandHandler::handle_get(const Command& cmd) {
    if (cmd.args.size() != 1) {
        return "ERR wrong number of arguments for 'GET' command";
    }
    auto val = db_.get(cmd.args[0]);
    if (!val.has_value()) return "(nil)";
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

} // namespace myredis
