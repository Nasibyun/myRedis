///
/// test_resp.cpp — Unit tests for the RESP protocol module
///
/// Tests RESP parsing (client→server) and serialization (server→client).
///
/// Test categories:
///   1. RESP serialization (simple string, error, integer, bulk string, array)
///   2. RESP response serialization (smart conversion from CommandHandler output)
///   3. RESP array command parsing
///   4. Inline command parsing
///   5. Edge cases (partial data, protocol errors, null values)
///

#include "resp/resp.h"
#include <cassert>
#include <iostream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    std::cout << "  Testing: " << name << "... "; \
    tests_passed++;

#define PASS() std::cout << "PASSED" << std::endl;

#define ASSERT_EQ(actual, expected) \
    if ((actual) != (expected)) { \
        std::cerr << "FAILED" << std::endl; \
        std::cerr << "    Expected: \"" << (expected) << "\"" << std::endl; \
        std::cerr << "    Actual:   \"" << (actual) << "\"" << std::endl; \
        tests_failed++; tests_passed--; \
    } else { PASS(); }

#define ASSERT_TRUE(expr) \
    if (!(expr)) { \
        std::cerr << "FAILED" << std::endl; \
        std::cerr << "    Expression was false: " << #expr << std::endl; \
        tests_failed++; tests_passed--; \
    } else { PASS(); }

#define ASSERT_FALSE(expr) \
    if ((expr)) { \
        std::cerr << "FAILED" << std::endl; \
        std::cerr << "    Expression was true: " << #expr << std::endl; \
        tests_failed++; tests_passed--; \
    } else { PASS(); }

// ═══════════════════════════════════════════════════════════════
//  1. Basic RESP Serialization
// ═══════════════════════════════════════════════════════════════

void test_simple_string() {
    TEST("Simple string serialization");
    ASSERT_EQ(myredis::Resp::simple_string("OK"), "+OK\r\n");

    TEST("Simple string PONG");
    ASSERT_EQ(myredis::Resp::simple_string("PONG"), "+PONG\r\n");

    TEST("Simple string QUEUED");
    ASSERT_EQ(myredis::Resp::simple_string("QUEUED"), "+QUEUED\r\n");
}

void test_error() {
    TEST("Error serialization");
    ASSERT_EQ(myredis::Resp::error("ERR unknown command"), "-ERR unknown command\r\n");

    TEST("WRONGTYPE error");
    ASSERT_EQ(myredis::Resp::error("WRONGTYPE Operation against a key holding the wrong kind of value"),
              "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
}

void test_integer() {
    TEST("Integer serialization - positive");
    ASSERT_EQ(myredis::Resp::integer(42), ":42\r\n");

    TEST("Integer serialization - zero");
    ASSERT_EQ(myredis::Resp::integer(0), ":0\r\n");

    TEST("Integer serialization - negative");
    ASSERT_EQ(myredis::Resp::integer(-1), ":-1\r\n");

    TEST("Integer serialization - large");
    ASSERT_EQ(myredis::Resp::integer(1000000), ":1000000\r\n");
}

void test_bulk_string() {
    TEST("Bulk string serialization");
    ASSERT_EQ(myredis::Resp::bulk_string("hello"), "$5\r\nhello\r\n");

    TEST("Bulk string empty");
    ASSERT_EQ(myredis::Resp::bulk_string(""), "$0\r\n\r\n");

    TEST("Bulk string with spaces");
    ASSERT_EQ(myredis::Resp::bulk_string("hello world"), "$11\r\nhello world\r\n");

    TEST("Null bulk string");
    ASSERT_EQ(myredis::Resp::null_bulk_string(), "$-1\r\n");
}

void test_array() {
    TEST("Array serialization");
    std::vector<std::string> elements = {
        myredis::Resp::bulk_string("foo"),
        myredis::Resp::bulk_string("bar")
    };
    ASSERT_EQ(myredis::Resp::array(elements), "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n");

    TEST("Empty array");
    ASSERT_EQ(myredis::Resp::empty_array(), "*0\r\n");

    TEST("Null array");
    ASSERT_EQ(myredis::Resp::null_array(), "*-1\r\n");

    TEST("Array with mixed types");
    std::vector<std::string> mixed = {
        myredis::Resp::bulk_string("name"),
        myredis::Resp::integer(42)
    };
    ASSERT_EQ(myredis::Resp::array(mixed), "*2\r\n$4\r\nname\r\n:42\r\n");
}

// ═══════════════════════════════════════════════════════════════
//  2. Smart Response Serialization
// ═══════════════════════════════════════════════════════════════

void test_serialize_response_ok() {
    TEST("Serialize 'OK' → simple string");
    ASSERT_EQ(myredis::Resp::serialize_response("OK"), "+OK\r\n");
}

void test_serialize_response_pong() {
    TEST("Serialize 'PONG' → simple string");
    ASSERT_EQ(myredis::Resp::serialize_response("PONG"), "+PONG\r\n");
}

void test_serialize_response_queued() {
    TEST("Serialize 'QUEUED' → simple string");
    ASSERT_EQ(myredis::Resp::serialize_response("QUEUED"), "+QUEUED\r\n");
}

void test_serialize_response_nil() {
    TEST("Serialize '(nil)' → null bulk string");
    ASSERT_EQ(myredis::Resp::serialize_response("(nil)"), "$-1\r\n");
}

void test_serialize_response_empty_list() {
    TEST("Serialize '(empty list)' → empty array");
    ASSERT_EQ(myredis::Resp::serialize_response("(empty list)"), "*0\r\n");
}

void test_serialize_response_error() {
    TEST("Serialize ERR response");
    ASSERT_EQ(myredis::Resp::serialize_response("ERR unknown command 'FOO'"),
              "-ERR unknown command 'FOO'\r\n");

    TEST("Serialize WRONGTYPE response");
    ASSERT_EQ(myredis::Resp::serialize_response("WRONGTYPE Operation against a key holding the wrong kind of value"),
              "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");

    TEST("Serialize NOAUTH response");
    ASSERT_EQ(myredis::Resp::serialize_response("NOAUTH Authentication required."),
              "-NOAUTH Authentication required.\r\n");
}

void test_serialize_response_integer() {
    TEST("Serialize integer response");
    ASSERT_EQ(myredis::Resp::serialize_response("(integer) 5"), ":5\r\n");

    TEST("Serialize integer zero");
    ASSERT_EQ(myredis::Resp::serialize_response("(integer) 0"), ":0\r\n");

    TEST("Serialize negative integer");
    ASSERT_EQ(myredis::Resp::serialize_response("(integer) -1"), ":-1\r\n");
}

void test_serialize_response_quoted_string() {
    TEST("Serialize quoted string");
    ASSERT_EQ(myredis::Resp::serialize_response("\"hello\""), "$5\r\nhello\r\n");

    TEST("Serialize quoted empty string");
    ASSERT_EQ(myredis::Resp::serialize_response("\"\""), "$0\r\n\r\n");
}

void test_serialize_response_array() {
    TEST("Serialize numbered list response");
    std::string input = "1) \"foo\"\n2) \"bar\"\n3) \"baz\"";
    std::string result = myredis::Resp::serialize_response(input);
    // Should be *3\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$3\r\nbaz\r\n
    ASSERT_EQ(result, "*3\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$3\r\nbaz\r\n");
}

// ═══════════════════════════════════════════════════════════════
//  3. RESP Array Command Parsing
// ═══════════════════════════════════════════════════════════════

void test_parse_resp_set() {
    TEST("Parse RESP SET command");
    std::string buffer = "*3\r\n$3\r\nSET\r\n$4\r\nname\r\n$5\r\nNasib\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Parse RESP SET - command name");
    ASSERT_EQ(result.command.name, "SET");

    TEST("Parse RESP SET - arg count");
    ASSERT_TRUE(result.command.args.size() == 2);

    TEST("Parse RESP SET - key");
    ASSERT_EQ(result.command.args[0], "name");

    TEST("Parse RESP SET - value");
    ASSERT_EQ(result.command.args[1], "Nasib");

    TEST("Parse RESP SET - bytes consumed");
    ASSERT_TRUE(result.bytes_consumed == buffer.size());
}

void test_parse_resp_get() {
    TEST("Parse RESP GET command");
    std::string buffer = "*2\r\n$3\r\nGET\r\n$4\r\nname\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Parse RESP GET - command name");
    ASSERT_EQ(result.command.name, "GET");

    TEST("Parse RESP GET - arg");
    ASSERT_EQ(result.command.args[0], "name");
}

void test_parse_resp_ping() {
    TEST("Parse RESP PING (no args)");
    std::string buffer = "*1\r\n$4\r\nPING\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Parse RESP PING - command name");
    ASSERT_EQ(result.command.name, "PING");

    TEST("Parse RESP PING - no args");
    ASSERT_TRUE(result.command.args.empty());
}

void test_parse_resp_case_insensitive() {
    TEST("Parse RESP lowercase command → uppercase");
    std::string buffer = "*1\r\n$4\r\nping\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Parse RESP lowercase → uppercase name");
    ASSERT_EQ(result.command.name, "PING");
}

void test_parse_resp_multiple_commands() {
    TEST("Parse two RESP commands from one buffer");
    std::string buffer = "*1\r\n$4\r\nPING\r\n*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n";
    auto result1 = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result1.ok);

    TEST("First command is PING");
    ASSERT_EQ(result1.command.name, "PING");

    // Parse the second command from remaining buffer
    std::string remaining = buffer.substr(result1.bytes_consumed);
    TEST("Parse second command from remaining buffer");
    auto result2 = myredis::Resp::parse_command(remaining);
    ASSERT_TRUE(result2.ok);

    TEST("Second command is GET");
    ASSERT_EQ(result2.command.name, "GET");
}

// ═══════════════════════════════════════════════════════════════
//  4. Inline Command Parsing
// ═══════════════════════════════════════════════════════════════

void test_parse_inline_simple() {
    TEST("Parse inline PING");
    std::string buffer = "PING\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Inline PING - command name");
    ASSERT_EQ(result.command.name, "PING");
}

void test_parse_inline_with_args() {
    TEST("Parse inline SET with args");
    std::string buffer = "SET name Nasib\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Inline SET - command name");
    ASSERT_EQ(result.command.name, "SET");

    TEST("Inline SET - key");
    ASSERT_EQ(result.command.args[0], "name");

    TEST("Inline SET - value");
    ASSERT_EQ(result.command.args[1], "Nasib");
}

void test_parse_inline_quoted() {
    TEST("Parse inline with quoted args");
    std::string buffer = "SET greeting \"hello world\"\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Inline quoted - value preserved");
    ASSERT_EQ(result.command.args[1], "hello world");
}

void test_parse_inline_newline_only() {
    TEST("Parse inline with \\n only (telnet)");
    std::string buffer = "PING\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Inline \\n - command name");
    ASSERT_EQ(result.command.name, "PING");
}

// ═══════════════════════════════════════════════════════════════
//  5. Edge Cases
// ═══════════════════════════════════════════════════════════════

void test_parse_partial_resp() {
    TEST("Partial RESP array — need more data");
    std::string buffer = "*2\r\n$3\r\nGET\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.need_more);
    ASSERT_FALSE(result.ok);
}

void test_parse_partial_resp_header() {
    TEST("Partial RESP header only");
    std::string buffer = "*3\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.need_more);
    ASSERT_FALSE(result.ok);
}

void test_parse_partial_inline() {
    TEST("Partial inline — no newline yet");
    std::string buffer = "SET name";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.need_more);
    ASSERT_FALSE(result.ok);
}

void test_parse_empty_buffer() {
    TEST("Empty buffer — need more");
    std::string buffer = "";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.need_more);
    ASSERT_FALSE(result.ok);
}

void test_parse_resp_with_binary_data() {
    TEST("Parse RESP with spaces in bulk string data");
    // SET key "hello world" — value has a space, but bulk string length handles it
    std::string buffer = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$11\r\nhello world\r\n";
    auto result = myredis::Resp::parse_command(buffer);
    ASSERT_TRUE(result.ok);

    TEST("Binary data - value preserved");
    ASSERT_EQ(result.command.args[1], "hello world");
}

void test_serialize_multiline_response() {
    TEST("Serialize multi-line INFO response as bulk string");
    std::string info = "# Server\nmyredis_version:1.0.0\ntcp_port:6399\n";
    std::string result = myredis::Resp::serialize_response(info);
    // Multi-line non-array response → bulk string
    std::string expected = "$" + std::to_string(info.size()) + "\r\n" + info + "\r\n";
    ASSERT_EQ(result, expected);
}

// ═══════════════════════════════════════════════════════════════
//  Main
// ═══════════════════════════════════════════════════════════════

int main() {
    std::cout << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << "  RESP Protocol Tests" << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << std::endl;

    // 1. Basic serialization
    std::cout << "── Basic Serialization ──" << std::endl;
    test_simple_string();
    test_error();
    test_integer();
    test_bulk_string();
    test_array();

    // 2. Smart response serialization
    std::cout << std::endl << "── Response Serialization ──" << std::endl;
    test_serialize_response_ok();
    test_serialize_response_pong();
    test_serialize_response_queued();
    test_serialize_response_nil();
    test_serialize_response_empty_list();
    test_serialize_response_error();
    test_serialize_response_integer();
    test_serialize_response_quoted_string();
    test_serialize_response_array();

    // 3. RESP array parsing
    std::cout << std::endl << "── RESP Array Parsing ──" << std::endl;
    test_parse_resp_set();
    test_parse_resp_get();
    test_parse_resp_ping();
    test_parse_resp_case_insensitive();
    test_parse_resp_multiple_commands();

    // 4. Inline parsing
    std::cout << std::endl << "── Inline Command Parsing ──" << std::endl;
    test_parse_inline_simple();
    test_parse_inline_with_args();
    test_parse_inline_quoted();
    test_parse_inline_newline_only();

    // 5. Edge cases
    std::cout << std::endl << "── Edge Cases ──" << std::endl;
    test_parse_partial_resp();
    test_parse_partial_resp_header();
    test_parse_partial_inline();
    test_parse_empty_buffer();
    test_parse_resp_with_binary_data();
    test_serialize_multiline_response();

    // Summary
    std::cout << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed" << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}
