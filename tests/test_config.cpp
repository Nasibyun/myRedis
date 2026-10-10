///
/// test_config.cpp — Unit tests for the Config manager
///

#include "config/config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdio>

void test_default_values() {
    myredis::Config config;

    assert(config.port() == 6399);
    assert(config.bind_addr() == "0.0.0.0");
    assert(config.password().empty());
    assert(config.save_interval() == 0);

    std::cout << "  PASS: test_default_values" << std::endl;
}

void test_load_config_file() {
    // Create a temporary config file
    const std::string filepath = "test_myredis.conf";
    {
        std::ofstream file(filepath);
        file << "# This is a comment\n";
        file << "port 7000\n";
        file << "bind 127.0.0.1\n";
        file << "requirepass secret123\n";
        file << "save 300\n";
        file << "\n";  // Empty line
        file << "loglevel debug\n";
    }

    myredis::Config config;
    bool loaded = config.load_file(filepath);
    assert(loaded);

    assert(config.port() == 7000);
    assert(config.bind_addr() == "127.0.0.1");
    assert(config.password() == "secret123");
    assert(config.save_interval() == 300);
    assert(config.get("loglevel") == "debug");

    // Clean up
    std::remove(filepath.c_str());

    std::cout << "  PASS: test_load_config_file" << std::endl;
}

void test_load_missing_file() {
    myredis::Config config;
    bool loaded = config.load_file("nonexistent_file.conf");
    assert(!loaded);

    // Defaults should still work
    assert(config.port() == 6399);

    std::cout << "  PASS: test_load_missing_file" << std::endl;
}

void test_runtime_set() {
    myredis::Config config;

    config.set("port", "8080");
    assert(config.port() == 8080);

    config.set("requirepass", "newpass");
    assert(config.password() == "newpass");

    config.set("custom_key", "custom_value");
    assert(config.get("custom_key") == "custom_value");

    std::cout << "  PASS: test_runtime_set" << std::endl;
}

void test_get_nonexistent_key() {
    myredis::Config config;
    assert(config.get("nonexistent") == "");

    std::cout << "  PASS: test_get_nonexistent_key" << std::endl;
}

void test_config_with_spaces_in_value() {
    const std::string filepath = "test_spaces.conf";
    {
        std::ofstream file(filepath);
        file << "motd Welcome to MyRedis Server\n";
    }

    myredis::Config config;
    config.load_file(filepath);
    assert(config.get("motd") == "Welcome to MyRedis Server");

    std::remove(filepath.c_str());

    std::cout << "  PASS: test_config_with_spaces_in_value" << std::endl;
}

void test_config_comments_and_empty_lines() {
    const std::string filepath = "test_comments.conf";
    {
        std::ofstream file(filepath);
        file << "# Comment line\n";
        file << "\n";
        file << "   # Indented comment\n";
        file << "port 9000\n";
        file << "\n\n\n";
        file << "bind 10.0.0.1\n";
    }

    myredis::Config config;
    config.load_file(filepath);
    assert(config.port() == 9000);
    assert(config.bind_addr() == "10.0.0.1");

    std::remove(filepath.c_str());

    std::cout << "  PASS: test_config_comments_and_empty_lines" << std::endl;
}

int main() {
    std::cout << "=== Config Tests ===" << std::endl;
    std::cout << std::endl;

    test_default_values();
    test_load_config_file();
    test_load_missing_file();
    test_runtime_set();
    test_get_nonexistent_key();
    test_config_with_spaces_in_value();
    test_config_comments_and_empty_lines();

    std::cout << std::endl;
    std::cout << "All config tests passed! (7 tests)" << std::endl;
    return 0;
}
