#include "parser/parser.h"
#include <algorithm>
#include <cctype>

namespace myredis {

std::vector<std::string> Parser::tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    size_t i = 0;

    while (i < input.size()) {
        // Skip whitespace between tokens
        while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) {
            ++i;
        }
        if (i >= input.size()) break;

        std::string token;

        if (input[i] == '"') {
            // ── Double-quoted string ──────────────────────────
            // Supports escape sequences: \" \\ \n \t
            ++i;  // skip opening quote
            while (i < input.size() && input[i] != '"') {
                if (input[i] == '\\' && i + 1 < input.size()) {
                    ++i;  // skip backslash
                    switch (input[i]) {
                        case '"':  token += '"';  break;
                        case '\\': token += '\\'; break;
                        case 'n':  token += '\n'; break;
                        case 't':  token += '\t'; break;
                        default:
                            token += '\\';
                            token += input[i];
                            break;
                    }
                } else {
                    token += input[i];
                }
                ++i;
            }
            if (i < input.size()) ++i;  // skip closing quote

        } else if (input[i] == '\'') {
            // ── Single-quoted string (no escape processing) ──
            ++i;  // skip opening quote
            while (i < input.size() && input[i] != '\'') {
                token += input[i];
                ++i;
            }
            if (i < input.size()) ++i;  // skip closing quote

        } else {
            // ── Unquoted token ───────────────────────────────
            while (i < input.size() && !std::isspace(static_cast<unsigned char>(input[i]))) {
                token += input[i];
                ++i;
            }
        }

        tokens.push_back(std::move(token));
    }

    return tokens;
}

ParseResult Parser::parse(const std::string& input) {
    auto tokens = tokenize(input);

    if (tokens.empty()) {
        return ParseResult::failure("ERR empty command");
    }

    Command cmd;
    cmd.name = tokens[0];

    // Convert command name to uppercase for case-insensitive matching
    std::transform(cmd.name.begin(), cmd.name.end(), cmd.name.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Remaining tokens are arguments
    cmd.args.assign(tokens.begin() + 1, tokens.end());

    return ParseResult::success(std::move(cmd));
}

} // namespace myredis
