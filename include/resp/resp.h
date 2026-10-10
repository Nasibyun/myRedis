#pragma once
///
/// resp.h — Redis Serialization Protocol (RESP2) parser and serializer
///
/// RESP2 is the standard wire protocol used by Redis since version 1.2.
/// It defines five data types, each identified by a leading byte:
///
///   +<string>\r\n                      — Simple String (e.g., +OK\r\n)
///   -<error message>\r\n               — Error (e.g., -ERR unknown command\r\n)
///   :<integer>\r\n                     — Integer (e.g., :1000\r\n)
///   $<length>\r\n<data>\r\n            — Bulk String (e.g., $6\r\nfoobar\r\n)
///   $-1\r\n                            — Null Bulk String
///   *<count>\r\n<elements...>          — Array (e.g., *2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n)
///   *-1\r\n                            — Null Array
///
/// Clients send commands as either:
///   1. RESP Arrays: *3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n
///   2. Inline commands: SET key value\r\n  (simpler, used by telnet/netcat)
///
/// This module implements:
///   - Resp::parse_command()    — Parse a client command from a read buffer
///   - Resp::serialize()        — Convert a response string to RESP wire format
///   - Resp::write_exact()      — Write bytes to a socket reliably
///
/// Milestone 12 — replaces the custom 4-byte length-prefixed protocol
/// with standard RESP, enabling compatibility with redis-cli and all
/// standard Redis client libraries.
///

#include "platform/platform.h"
#include "parser/parser.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace myredis {

/// Result of attempting to parse a command from a client's read buffer.
struct RespParseResult {
    bool ok;              // True if a complete command was successfully parsed
    bool need_more;       // True if the buffer has a partial command (need more data)
    bool protocol_error;  // True if the buffer contains invalid RESP data
    Command command;      // The parsed command (valid only if ok == true)
    size_t bytes_consumed; // Number of bytes consumed from the buffer
};

class Resp {
public:
    // ─── Parsing (client → server) ──────────────────────────────

    /// Try to parse one complete command from a client's read buffer.
    /// Supports both RESP array format and inline commands.
    ///
    /// Returns a RespParseResult:
    ///   - ok=true: a complete command was parsed; bytes_consumed tells how
    ///     many bytes to erase from the front of the buffer.
    ///   - need_more=true: the buffer has the start of a valid command but
    ///     needs more data to complete it.
    ///   - protocol_error=true: the buffer contains invalid data.
    static RespParseResult parse_command(const std::string& buffer);

    // ─── Serialization (server → client) ─────────────────────────

    /// Serialize a simple status string as RESP Simple String.
    /// Example: "OK" → "+OK\r\n"
    static std::string simple_string(const std::string& str);

    /// Serialize an error message as RESP Error.
    /// Example: "ERR unknown command" → "-ERR unknown command\r\n"
    static std::string error(const std::string& msg);

    /// Serialize an integer as RESP Integer.
    /// Example: 42 → ":42\r\n"
    static std::string integer(int64_t val);

    /// Serialize a string as RESP Bulk String.
    /// Example: "hello" → "$5\r\nhello\r\n"
    static std::string bulk_string(const std::string& str);

    /// Serialize a null bulk string: "$-1\r\n"
    static std::string null_bulk_string();

    /// Serialize a null array: "*-1\r\n"
    static std::string null_array();

    /// Serialize an empty array: "*0\r\n"
    static std::string empty_array();

    /// Serialize a vector of RESP-encoded elements as an array.
    /// Each element should already be RESP-encoded (bulk string, integer, etc.)
    static std::string array(const std::vector<std::string>& elements);

    /// Smart serializer: converts a CommandHandler response string into
    /// the appropriate RESP wire format by analyzing the response content.
    ///
    /// Handles these patterns:
    ///   "OK"                      → +OK\r\n
    ///   "PONG"                    → +OK\r\n (simple string)
    ///   "QUEUED"                  → +QUEUED\r\n
    ///   "ERR ..."                 → -ERR ...\r\n
    ///   "WRONGTYPE ..."           → -WRONGTYPE ...\r\n
    ///   "NOAUTH ..."              → -NOAUTH ...\r\n
    ///   "(integer) N"             → :N\r\n
    ///   "(nil)"                   → $-1\r\n
    ///   "(empty list)"            → *0\r\n
    ///   "\"value\""               → $<len>\r\nvalue\r\n
    ///   "1) ...\n2) ..."          → *N\r\n<elements>\r\n  (array)
    ///   Multi-line strings        → $<len>\r\n<content>\r\n  (bulk string)
    static std::string serialize_response(const std::string& response);

    // ─── Socket I/O helpers ──────────────────────────────────────

    /// Send a RESP-encoded response to a socket.
    /// Returns true on success, false on error.
    static bool send_response(socket_t sock, const std::string& resp_data);

private:
    /// Try to parse a RESP array command from the buffer.
    static RespParseResult parse_resp_array(const std::string& buffer);

    /// Try to parse an inline command from the buffer.
    static RespParseResult parse_inline(const std::string& buffer);

    /// Find \r\n in the buffer starting at 'start'.
    /// Returns the index of \r, or std::string::npos if not found.
    static size_t find_crlf(const std::string& buffer, size_t start = 0);

    /// Write exactly 'n' bytes from buffer to socket.
    /// Handles partial writes.
    static bool write_exact(socket_t sock, const char* buffer, size_t n);
};

} // namespace myredis
