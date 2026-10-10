#include "resp/resp.h"

#include <sstream>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace myredis {

// ═══════════════════════════════════════════════════════════════
//  Helper: find \r\n in the buffer
// ═══════════════════════════════════════════════════════════════

size_t Resp::find_crlf(const std::string& buffer, size_t start) {
    for (size_t i = start; i + 1 < buffer.size(); ++i) {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
            return i;
        }
    }
    return std::string::npos;
}

// ═══════════════════════════════════════════════════════════════
//  Parsing: Client → Server
// ═══════════════════════════════════════════════════════════════

RespParseResult Resp::parse_command(const std::string& buffer) {
    if (buffer.empty()) {
        return {false, true, false, {}, 0};
    }

    // RESP array commands start with '*'
    if (buffer[0] == '*') {
        return parse_resp_array(buffer);
    }

    // Everything else is treated as an inline command
    return parse_inline(buffer);
}

RespParseResult Resp::parse_resp_array(const std::string& buffer) {
    // Format: *<count>\r\n$<len>\r\n<data>\r\n...
    //
    // Example: *3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n

    // Step 1: Read the element count line  *N\r\n
    size_t crlf = find_crlf(buffer, 0);
    if (crlf == std::string::npos) {
        return {false, true, false, {}, 0};  // Need more data
    }

    // Parse the count
    std::string count_str = buffer.substr(1, crlf - 1);
    int count;
    try {
        count = std::stoi(count_str);
    } catch (...) {
        return {false, false, true, {}, 0};  // Protocol error
    }

    if (count < 0) {
        // Null array — shouldn't happen for commands, but handle gracefully
        return {false, false, true, {}, 0};
    }

    if (count == 0) {
        return {false, false, true, {}, 0};  // Empty command
    }

    // Step 2: Read each bulk string element
    size_t pos = crlf + 2;  // Skip past *N\r\n
    std::vector<std::string> parts;

    for (int i = 0; i < count; ++i) {
        // Each element should be a bulk string: $<len>\r\n<data>\r\n
        if (pos >= buffer.size()) {
            return {false, true, false, {}, 0};  // Need more data
        }

        if (buffer[pos] != '$') {
            return {false, false, true, {}, 0};  // Protocol error
        }

        // Find the length line
        crlf = find_crlf(buffer, pos);
        if (crlf == std::string::npos) {
            return {false, true, false, {}, 0};  // Need more data
        }

        // Parse the bulk string length
        std::string len_str = buffer.substr(pos + 1, crlf - pos - 1);
        int len;
        try {
            len = std::stoi(len_str);
        } catch (...) {
            return {false, false, true, {}, 0};  // Protocol error
        }

        if (len < 0) {
            // Null bulk string in a command — treat as empty string
            parts.push_back("");
            pos = crlf + 2;
            continue;
        }

        // Check if we have the complete bulk string data + trailing \r\n
        size_t data_start = crlf + 2;
        if (data_start + static_cast<size_t>(len) + 2 > buffer.size()) {
            return {false, true, false, {}, 0};  // Need more data
        }

        // Verify trailing \r\n after the data
        if (buffer[data_start + len] != '\r' || buffer[data_start + len + 1] != '\n') {
            return {false, false, true, {}, 0};  // Protocol error
        }

        parts.push_back(buffer.substr(data_start, len));
        pos = data_start + len + 2;
    }

    // Build the Command struct
    Command cmd;
    cmd.name = parts[0];
    // Convert command name to uppercase (Redis commands are case-insensitive)
    std::transform(cmd.name.begin(), cmd.name.end(), cmd.name.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    for (size_t i = 1; i < parts.size(); ++i) {
        cmd.args.push_back(parts[i]);
    }

    return {true, false, false, cmd, pos};
}

RespParseResult Resp::parse_inline(const std::string& buffer) {
    // Inline format: COMMAND arg1 arg2 ...\r\n
    // Also supports just \n for telnet compatibility
    //
    // We look for \r\n or \n to find the end of the command line.

    size_t crlf = find_crlf(buffer, 0);
    size_t newline = buffer.find('\n');

    size_t line_end;
    size_t consume;

    if (crlf != std::string::npos) {
        line_end = crlf;
        consume = crlf + 2;
    } else if (newline != std::string::npos) {
        line_end = newline;
        consume = newline + 1;
    } else {
        // No complete line yet — need more data
        // Safety: reject absurdly long lines without a newline
        if (buffer.size() > 64 * 1024) {
            return {false, false, true, {}, 0};
        }
        return {false, true, false, {}, 0};
    }

    std::string line = buffer.substr(0, line_end);

    // Trim leading/trailing whitespace
    size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string::npos) {
        // Empty line — consume it and report need_more
        return {false, true, false, {}, consume};
    }
    line = line.substr(start);

    // Use the existing parser to tokenize the inline command.
    // The Parser already handles quoted strings and escape sequences.
    ParseResult result = Parser::parse(line);

    if (!result.ok) {
        // Parse error — return it as a protocol error with the error message
        // We'll let the caller send the error response
        Command err_cmd;
        err_cmd.name = "";
        return {false, false, true, err_cmd, consume};
    }

    return {true, false, false, result.command, consume};
}

// ═══════════════════════════════════════════════════════════════
//  Serialization: Server → Client
// ═══════════════════════════════════════════════════════════════

std::string Resp::simple_string(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string Resp::error(const std::string& msg) {
    return "-" + msg + "\r\n";
}

std::string Resp::integer(int64_t val) {
    return ":" + std::to_string(val) + "\r\n";
}

std::string Resp::bulk_string(const std::string& str) {
    return "$" + std::to_string(str.size()) + "\r\n" + str + "\r\n";
}

std::string Resp::null_bulk_string() {
    return "$-1\r\n";
}

std::string Resp::null_array() {
    return "*-1\r\n";
}

std::string Resp::empty_array() {
    return "*0\r\n";
}

std::string Resp::array(const std::vector<std::string>& elements) {
    std::string result = "*" + std::to_string(elements.size()) + "\r\n";
    for (const auto& elem : elements) {
        result += elem;
    }
    return result;
}

std::string Resp::serialize_response(const std::string& response) {
    if (response.empty()) {
        return null_bulk_string();
    }

    // ── Simple status responses ────────────────────────────────
    if (response == "OK") {
        return simple_string("OK");
    }
    if (response == "PONG") {
        return simple_string("PONG");
    }
    if (response == "QUEUED") {
        return simple_string("QUEUED");
    }

    // ── Null ───────────────────────────────────────────────────
    if (response == "(nil)") {
        return null_bulk_string();
    }

    // ── Empty list / array ────────────────────────────────────
    if (response == "(empty list)") {
        return empty_array();
    }

    // ── Error responses ───────────────────────────────────────
    // Errors start with "ERR", "WRONGTYPE", or "NOAUTH"
    if (response.substr(0, 4) == "ERR " ||
        response.substr(0, 3) == "ERR") {
        return error(response);
    }
    if (response.substr(0, 10) == "WRONGTYPE ") {
        return error(response);
    }
    if (response.substr(0, 7) == "NOAUTH ") {
        return error(response);
    }

    // ── Integer responses ─────────────────────────────────────
    // Pattern: "(integer) N"
    if (response.size() > 10 && response.substr(0, 10) == "(integer) ") {
        try {
            int64_t val = std::stoll(response.substr(10));
            return integer(val);
        } catch (...) {
            // Fall through to bulk string
        }
    }

    // ── Quoted string responses ───────────────────────────────
    // Pattern: "\"value\""  — a quoted value from GET, LPOP, etc.
    if (response.size() >= 2 && response.front() == '"' && response.back() == '"') {
        std::string inner = response.substr(1, response.size() - 2);
        return bulk_string(inner);
    }

    // ── Array responses ───────────────────────────────────────
    // Pattern: "1) ...\n2) ...\n3) ..."
    // Used by KEYS, LRANGE, EXEC, SUBSCRIBE, etc.
    if (response.size() >= 3 && response[0] >= '1' && response[0] <= '9' && response[1] == ')') {
        std::vector<std::string> elements;
        std::istringstream iss(response);
        std::string line;

        while (std::getline(iss, line)) {
            // Each line: "N) content"
            size_t paren = line.find(") ");
            if (paren != std::string::npos) {
                std::string content = line.substr(paren + 2);

                // Check if the content is a quoted string
                if (content.size() >= 2 && content.front() == '"' && content.back() == '"') {
                    elements.push_back(bulk_string(content.substr(1, content.size() - 2)));
                } else {
                    // Recursively serialize each element (could be "OK", "(integer) N", etc.)
                    elements.push_back(serialize_response(content));
                }
            } else {
                // Fallback: just serialize the whole line as a bulk string
                elements.push_back(bulk_string(line));
            }
        }

        return array(elements);
    }

    // ── Pub/Sub push messages ─────────────────────────────────
    // Pattern: "subscribe <channel> <count>" or "unsubscribe <channel> <count>"
    // or "message <channel> <payload>"
    if (response.substr(0, 10) == "subscribe " ||
        response.substr(0, 12) == "unsubscribe " ||
        response.substr(0, 8) == "message ") {
        // Parse the parts and format as a RESP array
        std::istringstream iss(response);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(iss, line)) {
            lines.push_back(line);
        }

        // If it's multi-line (multiple subscribe confirmations), concatenate arrays
        std::string result;
        for (const auto& l : lines) {
            std::istringstream ls(l);
            std::string type, channel, third;
            ls >> type >> channel;
            std::getline(ls >> std::ws, third);

            std::vector<std::string> parts;
            parts.push_back(bulk_string(type));
            parts.push_back(bulk_string(channel));

            if (type == "message") {
                // message <channel> <payload>
                parts.push_back(bulk_string(third));
            } else {
                // subscribe/unsubscribe <channel> <count>
                try {
                    int64_t count = std::stoll(third);
                    parts.push_back(integer(count));
                } catch (...) {
                    parts.push_back(bulk_string(third));
                }
            }
            result += array(parts);
        }
        return result;
    }

    // ── Multi-line responses (INFO, etc.) ─────────────────────
    // Anything with newlines that doesn't match the array pattern
    // gets sent as a bulk string (preserving newlines).
    if (response.find('\n') != std::string::npos) {
        return bulk_string(response);
    }

    // ── Default: bulk string ──────────────────────────────────
    return bulk_string(response);
}

// ═══════════════════════════════════════════════════════════════
//  Socket I/O
// ═══════════════════════════════════════════════════════════════

bool Resp::write_exact(socket_t sock, const char* buffer, size_t n) {
    size_t total = 0;
    while (total < n) {
        ssize_t bytes = platform::socket_write(sock, buffer + total, n - total);
        if (bytes <= 0) {
            return false;
        }
        total += static_cast<size_t>(bytes);
    }
    return true;
}

bool Resp::send_response(socket_t sock, const std::string& resp_data) {
    return write_exact(sock, resp_data.data(), resp_data.size());
}

} // namespace myredis
