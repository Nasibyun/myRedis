#pragma once
///
/// command_handler.h — Routes parsed commands to database operations
///
/// Takes a parsed Command struct and a Database reference,
/// executes the appropriate operation, and returns a response string.
///

#include "parser/parser.h"
#include "database/database.h"
#include <string>

namespace myredis {

class CommandHandler {
public:
    explicit CommandHandler(Database& db);

    /// Execute a parsed command and return the response string.
    std::string execute(const Command& cmd);

private:
    Database& db_;

    std::string handle_ping(const Command& cmd);
    std::string handle_set(const Command& cmd);
    std::string handle_get(const Command& cmd);
    std::string handle_del(const Command& cmd);
    std::string handle_exists(const Command& cmd);
    std::string handle_keys(const Command& cmd);
    std::string handle_dbsize(const Command& cmd);
    std::string handle_flushdb(const Command& cmd);
};

} // namespace myredis
