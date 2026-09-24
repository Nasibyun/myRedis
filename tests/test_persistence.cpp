///
/// test_persistence.cpp — Unit tests for RDB-style persistence
///
/// Tests save/load roundtrip, TTL preservation, expired key skipping,
/// and corruption detection (bad magic header, bad CRC32).
///

#include "persistence/persistence.h"
#include "database/database.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdio>  // std::remove

static const std::string TEST_FILE = "test_dump.mrdb";

/// Helper: clean up test file
void cleanup_test_file() {
    std::remove(TEST_FILE.c_str());
    std::remove((TEST_FILE + ".tmp").c_str());
}

void test_save_and_load_basic() {
    cleanup_test_file();

    // Save
    {
        myredis::Database db;
        db.set("name", "Nasib");
        db.set("lang", "C++");
        db.set("version", "17");

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    // Load into a fresh database
    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        assert(db.get("name").has_value());
        assert(db.get("name").value() == "Nasib");
        assert(db.get("lang").has_value());
        assert(db.get("lang").value() == "C++");
        assert(db.get("version").has_value());
        assert(db.get("version").value() == "17");
        assert(db.dbsize() == 3);
    }

    cleanup_test_file();
    std::cout << "  PASS: test_save_and_load_basic" << std::endl;
}

void test_save_and_load_with_ttl() {
    cleanup_test_file();

    {
        myredis::Database db;
        db.set("session", "abc123");
        db.expire("session", 60);  // 60-second TTL

        db.set("permanent", "data");
        // No TTL on "permanent"

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        // Both keys should be loaded
        assert(db.get("session").has_value());
        assert(db.get("session").value() == "abc123");
        assert(db.get("permanent").has_value());
        assert(db.get("permanent").value() == "data");

        // "session" should have a positive TTL (roughly 59-60 seconds)
        int ttl = db.ttl("session");
        assert(ttl > 0 && ttl <= 60);

        // "permanent" should have no TTL
        assert(db.ttl("permanent") == -1);
    }

    cleanup_test_file();
    std::cout << "  PASS: test_save_and_load_with_ttl" << std::endl;
}

void test_expired_keys_skipped_on_save() {
    cleanup_test_file();

    {
        myredis::Database db;
        db.set("alive", "yes");
        db.set("dying", "soon");
        db.expire("dying", 1);  // 1-second TTL

        // Wait for expiration
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        // Only "alive" should be loaded
        assert(db.get("alive").has_value());
        assert(!db.get("dying").has_value());
        assert(db.dbsize() == 1);
    }

    cleanup_test_file();
    std::cout << "  PASS: test_expired_keys_skipped_on_save" << std::endl;
}

void test_load_nonexistent_file() {
    cleanup_test_file();

    myredis::Database db;
    bool ok = myredis::Persistence::load("nonexistent_file.mrdb", db);
    assert(!ok);  // Should return false, not crash
    assert(db.dbsize() == 0);

    std::cout << "  PASS: test_load_nonexistent_file" << std::endl;
}

void test_corrupt_magic_header() {
    cleanup_test_file();

    // Write a file with a bad magic header
    {
        std::ofstream out(TEST_FILE, std::ios::binary);
        out.write("BADMAGIC!", 9);
        uint8_t zeros[8] = {0};
        out.write(reinterpret_cast<const char*>(zeros), 8);  // count + crc
    }

    myredis::Database db;
    bool ok = myredis::Persistence::load(TEST_FILE, db);
    assert(!ok);  // Should fail
    assert(db.dbsize() == 0);

    cleanup_test_file();
    std::cout << "  PASS: test_corrupt_magic_header" << std::endl;
}

void test_corrupt_crc() {
    cleanup_test_file();

    // First, save a valid file
    {
        myredis::Database db;
        db.set("key", "value");
        myredis::Persistence::save(TEST_FILE, db);
    }

    // Corrupt a byte in the middle of the file
    {
        std::fstream f(TEST_FILE, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(10);  // Past the magic header
        char bad_byte = 0xFF;
        f.write(&bad_byte, 1);
    }

    myredis::Database db;
    bool ok = myredis::Persistence::load(TEST_FILE, db);
    assert(!ok);  // CRC mismatch
    assert(db.dbsize() == 0);

    cleanup_test_file();
    std::cout << "  PASS: test_corrupt_crc" << std::endl;
}

void test_empty_database_save_and_load() {
    cleanup_test_file();

    {
        myredis::Database db;
        // Save an empty database
        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        db.set("pre_existing", "data");  // This should be overwritten

        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        // Database should be empty (load replaces everything)
        assert(db.dbsize() == 0);
        assert(!db.get("pre_existing").has_value());
    }

    cleanup_test_file();
    std::cout << "  PASS: test_empty_database_save_and_load" << std::endl;
}

void test_special_characters_roundtrip() {
    cleanup_test_file();

    {
        myredis::Database db;
        db.set("binary\x00key", "value\nwith\nnewlines");
        db.set("emoji", "hello \xF0\x9F\x98\x80 world");
        db.set("empty_val", "");

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        assert(db.get("emoji").has_value());
        assert(db.get("emoji").value() == "hello \xF0\x9F\x98\x80 world");
        assert(db.get("empty_val").has_value());
        assert(db.get("empty_val").value() == "");
    }

    cleanup_test_file();
    std::cout << "  PASS: test_special_characters_roundtrip" << std::endl;
}

// ─── List Persistence Tests ──────────────────────────────────

void test_save_and_load_list() {
    cleanup_test_file();

    {
        myredis::Database db;
        db.rpush("fruits", {"apple", "banana", "cherry"});
        db.lpush("numbers", {"3", "2", "1"});  // list = [1, 2, 3]

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        // Check fruits list
        assert(db.llen("fruits") == 3);
        auto fruits = db.lrange("fruits", 0, -1);
        assert(fruits.size() == 3);
        assert(fruits[0] == "apple");
        assert(fruits[1] == "banana");
        assert(fruits[2] == "cherry");

        // Check numbers list
        assert(db.llen("numbers") == 3);
        auto nums = db.lrange("numbers", 0, -1);
        assert(nums[0] == "1");
        assert(nums[1] == "2");
        assert(nums[2] == "3");

        assert(db.dbsize() == 2);
    }

    cleanup_test_file();
    std::cout << "  PASS: test_save_and_load_list" << std::endl;
}

void test_save_and_load_list_with_ttl() {
    cleanup_test_file();

    {
        myredis::Database db;
        db.rpush("mylist", {"a", "b", "c"});
        db.expire("mylist", 60);  // 60-second TTL

        db.rpush("permanent_list", {"x", "y"});
        // No TTL on permanent_list

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        // Both lists should be loaded
        assert(db.llen("mylist") == 3);
        assert(db.llen("permanent_list") == 2);

        // mylist should have a positive TTL
        int ttl = db.ttl("mylist");
        assert(ttl > 0 && ttl <= 60);

        // permanent_list should have no TTL
        assert(db.ttl("permanent_list") == -1);
    }

    cleanup_test_file();
    std::cout << "  PASS: test_save_and_load_list_with_ttl" << std::endl;
}

void test_save_and_load_mixed_types() {
    cleanup_test_file();

    {
        myredis::Database db;
        db.set("name", "Nasib");
        db.set("lang", "C++");
        db.rpush("tasks", {"code", "test", "deploy"});
        db.lpush("stack", {"C++", "Python"});  // stack = [Python, C++]
        db.expire("lang", 30);
        db.expire("tasks", 45);

        bool ok = myredis::Persistence::save(TEST_FILE, db);
        assert(ok);
    }

    {
        myredis::Database db;
        bool ok = myredis::Persistence::load(TEST_FILE, db);
        assert(ok);

        // Strings
        assert(db.get("name").value() == "Nasib");
        assert(db.get("lang").value() == "C++");

        // Lists
        auto tasks = db.lrange("tasks", 0, -1);
        assert(tasks.size() == 3);
        assert(tasks[0] == "code");

        auto stack = db.lrange("stack", 0, -1);
        assert(stack.size() == 2);
        assert(stack[0] == "Python");
        assert(stack[1] == "C++");

        // TTLs preserved
        assert(db.ttl("name") == -1);
        assert(db.ttl("lang") > 0);
        assert(db.ttl("tasks") > 0);
        assert(db.ttl("stack") == -1);

        assert(db.dbsize() == 4);
    }

    cleanup_test_file();
    std::cout << "  PASS: test_save_and_load_mixed_types" << std::endl;
}

int main() {
    std::cout << "=== Persistence Tests ===" << std::endl;

    test_save_and_load_basic();
    test_save_and_load_with_ttl();
    test_expired_keys_skipped_on_save();
    test_load_nonexistent_file();
    test_corrupt_magic_header();
    test_corrupt_crc();
    test_empty_database_save_and_load();
    test_special_characters_roundtrip();

    std::cout << std::endl;
    std::cout << "--- List Persistence Tests ---" << std::endl;
    test_save_and_load_list();
    test_save_and_load_list_with_ttl();
    test_save_and_load_mixed_types();

    std::cout << std::endl;
    std::cout << "All persistence tests passed! (11 tests)" << std::endl;
    return 0;
}

