#pragma once
///
/// parser.h — Command tokenizer and parser
///
/// Converts raw input like "SET name Nasib" into a structured
/// Command { name="SET", args=["name", "Nasib"] }.
///
/// Handles:
/// - Whitespace splitting
/// - Double-quoted strings ("hello world")
/// - Single-quoted strings ('hello world')
/// - Escape sequences in double quotes (\n, \t, \\, \")
/// - Case-insensitive command names (converted to uppercase)
///

#include <string>
#include <vector>

namespace myredis {

/// A parsed command: a name and its arguments
struct Command {
    std::string name;               // Command name, always UPPERCASE
    std::vector<std::string> args;  // Arguments (in original case)
};

/// Result of parsing: either a valid Command or an error message
struct ParseResult {
    bool ok;
    Command command;
    std::string error;

    static ParseResult success(Command cmd) {
        return {true, std::move(cmd), ""};
    }

    static ParseResult failure(const std::string& err) {
        return {false, {}, err};
    }
};

class Parser {
public:
    /// Parse a raw input line into a Command
    static ParseResult parse(const std::string& input);

private:
    /// Split input into tokens, handling quoted strings
    static std::vector<std::string> tokenize(const std::string& input);
};

} // namespace myredis
