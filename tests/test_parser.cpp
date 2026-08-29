///
/// test_parser.cpp — Unit tests for the command parser
///
/// Tests tokenization, quoted strings, escape sequences,
/// case insensitivity, and various malformed inputs.
///

#include "parser/parser.h"
#include <cassert>
#include <iostream>

void test_simple_command() {
    auto result = myredis::Parser::parse("SET name Nasib");
    assert(result.ok);
    assert(result.command.name == "SET");
    assert(result.command.args.size() == 2);
    assert(result.command.args[0] == "name");
    assert(result.command.args[1] == "Nasib");
    std::cout << "  PASS: test_simple_command" << std::endl;
}

void test_case_insensitive_command() {
    auto result = myredis::Parser::parse("get mykey");
    assert(result.ok);
    assert(result.command.name == "GET");
    assert(result.command.args[0] == "mykey");
    std::cout << "  PASS: test_case_insensitive_command" << std::endl;
}

void test_mixed_case() {
    auto result = myredis::Parser::parse("SeT Key Value");
    assert(result.ok);
    assert(result.command.name == "SET");
    assert(result.command.args[0] == "Key");    // args keep original case
    assert(result.command.args[1] == "Value");
    std::cout << "  PASS: test_mixed_case" << std::endl;
}

void test_double_quoted_string() {
    auto result = myredis::Parser::parse("SET greeting \"Hello World\"");
    assert(result.ok);
    assert(result.command.args.size() == 2);
    assert(result.command.args[0] == "greeting");
    assert(result.command.args[1] == "Hello World");
    std::cout << "  PASS: test_double_quoted_string" << std::endl;
}

void test_single_quoted_string() {
    auto result = myredis::Parser::parse("SET msg 'hello world'");
    assert(result.ok);
    assert(result.command.args[1] == "hello world");
    std::cout << "  PASS: test_single_quoted_string" << std::endl;
}

void test_escape_sequences() {
    auto result = myredis::Parser::parse("SET key \"hello\\nworld\"");
    assert(result.ok);
    assert(result.command.args[1] == "hello\nworld");
    std::cout << "  PASS: test_escape_sequences" << std::endl;
}

void test_escaped_quote() {
    auto result = myredis::Parser::parse("SET key \"he said \\\"hi\\\"\"");
    assert(result.ok);
    assert(result.command.args[1] == "he said \"hi\"");
    std::cout << "  PASS: test_escaped_quote" << std::endl;
}

void test_no_args() {
    auto result = myredis::Parser::parse("PING");
    assert(result.ok);
    assert(result.command.name == "PING");
    assert(result.command.args.empty());
    std::cout << "  PASS: test_no_args" << std::endl;
}

void test_empty_input() {
    auto result = myredis::Parser::parse("");
    assert(!result.ok);
    assert(!result.error.empty());
    std::cout << "  PASS: test_empty_input" << std::endl;
}

void test_whitespace_only() {
    auto result = myredis::Parser::parse("   \t  ");
    assert(!result.ok);
    std::cout << "  PASS: test_whitespace_only" << std::endl;
}

void test_extra_whitespace() {
    auto result = myredis::Parser::parse("  SET   name   Nasib  ");
    assert(result.ok);
    assert(result.command.name == "SET");
    assert(result.command.args.size() == 2);
    assert(result.command.args[0] == "name");
    assert(result.command.args[1] == "Nasib");
    std::cout << "  PASS: test_extra_whitespace" << std::endl;
}

void test_many_args() {
    auto result = myredis::Parser::parse("DEL key1 key2 key3 key4 key5");
    assert(result.ok);
    assert(result.command.name == "DEL");
    assert(result.command.args.size() == 5);
    std::cout << "  PASS: test_many_args" << std::endl;
}

void test_single_char_command() {
    auto result = myredis::Parser::parse("X");
    assert(result.ok);
    assert(result.command.name == "X");
    assert(result.command.args.empty());
    std::cout << "  PASS: test_single_char_command" << std::endl;
}

int main() {
    std::cout << "=== Parser Tests ===" << std::endl;

    test_simple_command();
    test_case_insensitive_command();
    test_mixed_case();
    test_double_quoted_string();
    test_single_quoted_string();
    test_escape_sequences();
    test_escaped_quote();
    test_no_args();
    test_empty_input();
    test_whitespace_only();
    test_extra_whitespace();
    test_many_args();
    test_single_char_command();

    std::cout << std::endl;
    std::cout << "All parser tests passed! (" << 13 << " tests)" << std::endl;
    return 0;
}
