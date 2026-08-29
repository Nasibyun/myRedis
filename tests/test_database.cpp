///
/// test_database.cpp — Unit tests for the in-memory database
///
/// Tests SET, GET, DEL, EXISTS, KEYS, DBSIZE, FLUSHDB operations
/// including edge cases like nonexistent keys and overwrites.
/// Also tests TTL/expiry functionality (EXPIRE, TTL, PERSIST).
///

#include "database/database.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

// ─── Core Command Tests ────────────────────────────────────────

void test_set_and_get() {
    myredis::Database db;
    db.set("name", "Nasib");
    auto val = db.get("name");
    assert(val.has_value());
    assert(val.value() == "Nasib");
    std::cout << "  PASS: test_set_and_get" << std::endl;
}

void test_get_nonexistent_key() {
    myredis::Database db;
    auto val = db.get("does_not_exist");
    assert(!val.has_value());
    std::cout << "  PASS: test_get_nonexistent_key" << std::endl;
}

void test_overwrite_value() {
    myredis::Database db;
    db.set("key", "value1");
    db.set("key", "value2");
    auto val = db.get("key");
    assert(val.has_value());
    assert(val.value() == "value2");
    std::cout << "  PASS: test_overwrite_value" << std::endl;
}

void test_del_existing_keys() {
    myredis::Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.set("c", "3");
    int deleted = db.del({"a", "b", "nonexistent"});
    assert(deleted == 2);
    assert(!db.get("a").has_value());
    assert(!db.get("b").has_value());
    assert(db.get("c").has_value());
    std::cout << "  PASS: test_del_existing_keys" << std::endl;
}

void test_del_nonexistent_key() {
    myredis::Database db;
    int deleted = db.del({"nothing"});
    assert(deleted == 0);
    std::cout << "  PASS: test_del_nonexistent_key" << std::endl;
}

void test_exists() {
    myredis::Database db;
    db.set("a", "1");
    db.set("b", "2");
    assert(db.exists({"a"}) == 1);
    assert(db.exists({"a", "b"}) == 2);
    assert(db.exists({"a", "c"}) == 1);
    assert(db.exists({"c", "d"}) == 0);
    std::cout << "  PASS: test_exists" << std::endl;
}

void test_keys() {
    myredis::Database db;
    assert(db.keys().empty());
    db.set("x", "1");
    db.set("y", "2");
    auto k = db.keys();
    assert(k.size() == 2);
    // Keys may be in any order (unordered_map)
    bool has_x = false, has_y = false;
    for (const auto& key : k) {
        if (key == "x") has_x = true;
        if (key == "y") has_y = true;
    }
    assert(has_x && has_y);
    std::cout << "  PASS: test_keys" << std::endl;
}

void test_dbsize() {
    myredis::Database db;
    assert(db.dbsize() == 0);
    db.set("a", "1");
    assert(db.dbsize() == 1);
    db.set("b", "2");
    assert(db.dbsize() == 2);
    db.del({"a"});
    assert(db.dbsize() == 1);
    std::cout << "  PASS: test_dbsize" << std::endl;
}

void test_flushdb() {
    myredis::Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.set("c", "3");
    assert(db.dbsize() == 3);
    db.flushdb();
    assert(db.dbsize() == 0);
    assert(!db.get("a").has_value());
    assert(!db.get("b").has_value());
    assert(!db.get("c").has_value());
    std::cout << "  PASS: test_flushdb" << std::endl;
}

void test_empty_string_value() {
    myredis::Database db;
    db.set("key", "");
    auto val = db.get("key");
    assert(val.has_value());
    assert(val.value() == "");
    std::cout << "  PASS: test_empty_string_value" << std::endl;
}

void test_special_characters() {
    myredis::Database db;
    db.set("key with spaces", "value\nwith\nnewlines");
    auto val = db.get("key with spaces");
    assert(val.has_value());
    assert(val.value() == "value\nwith\nnewlines");
    std::cout << "  PASS: test_special_characters" << std::endl;
}

// ─── TTL / Expiry Tests ───────────────────────────────────────

void test_expire_and_ttl() {
    myredis::Database db;
    db.set("session", "abc123");

    // Set a 10-second TTL
    assert(db.expire("session", 10) == true);

    // TTL should be positive (roughly 9-10 seconds)
    int remaining = db.ttl("session");
    assert(remaining > 0 && remaining <= 10);

    std::cout << "  PASS: test_expire_and_ttl" << std::endl;
}

void test_expire_nonexistent_key() {
    myredis::Database db;
    // Can't set TTL on a key that doesn't exist
    assert(db.expire("ghost", 60) == false);
    std::cout << "  PASS: test_expire_nonexistent_key" << std::endl;
}

void test_ttl_no_expiry() {
    myredis::Database db;
    db.set("permanent", "data");
    // Key with no TTL returns -1
    assert(db.ttl("permanent") == -1);
    std::cout << "  PASS: test_ttl_no_expiry" << std::endl;
}

void test_ttl_nonexistent_key() {
    myredis::Database db;
    // Non-existent key returns -2
    assert(db.ttl("ghost") == -2);
    std::cout << "  PASS: test_ttl_nonexistent_key" << std::endl;
}

void test_expired_key_lazy_delete() {
    myredis::Database db;
    db.set("temp", "data");
    db.expire("temp", 1);  // 1-second TTL

    // Key should exist immediately
    assert(db.get("temp").has_value());

    // Wait for it to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // Key should be gone (lazy expiry on GET)
    assert(!db.get("temp").has_value());
    assert(db.ttl("temp") == -2);

    std::cout << "  PASS: test_expired_key_lazy_delete" << std::endl;
}

void test_persist() {
    myredis::Database db;
    db.set("key", "value");
    db.expire("key", 60);

    // TTL should be set
    assert(db.ttl("key") > 0);

    // Remove the TTL
    assert(db.persist("key") == true);

    // Now TTL should be -1 (no expiry)
    assert(db.ttl("key") == -1);

    // Persist on a key without TTL returns false
    assert(db.persist("key") == false);

    std::cout << "  PASS: test_persist" << std::endl;
}

void test_set_clears_expiry() {
    myredis::Database db;
    db.set("key", "value1");
    db.expire("key", 60);
    assert(db.ttl("key") > 0);

    // SET should clear the TTL
    db.set("key", "value2");
    assert(db.ttl("key") == -1);
    assert(db.get("key").value() == "value2");

    std::cout << "  PASS: test_set_clears_expiry" << std::endl;
}

void test_del_clears_expiry() {
    myredis::Database db;
    db.set("key", "value");
    db.expire("key", 60);

    db.del({"key"});

    // Key is gone, so TTL returns -2
    assert(db.ttl("key") == -2);

    std::cout << "  PASS: test_del_clears_expiry" << std::endl;
}

void test_flushdb_clears_expiry() {
    myredis::Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.expire("a", 60);
    db.expire("b", 60);

    db.flushdb();

    assert(db.ttl("a") == -2);
    assert(db.ttl("b") == -2);

    std::cout << "  PASS: test_flushdb_clears_expiry" << std::endl;
}

void test_keys_excludes_expired() {
    myredis::Database db;
    db.set("alive", "yes");
    db.set("dying", "soon");
    db.expire("dying", 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    auto k = db.keys();
    assert(k.size() == 1);
    assert(k[0] == "alive");

    std::cout << "  PASS: test_keys_excludes_expired" << std::endl;
}

void test_dbsize_excludes_expired() {
    myredis::Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.expire("b", 1);

    assert(db.dbsize() == 2);  // Both alive initially

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    assert(db.dbsize() == 1);  // Only "a" remains

    std::cout << "  PASS: test_dbsize_excludes_expired" << std::endl;
}

void test_exists_excludes_expired() {
    myredis::Database db;
    db.set("a", "1");
    db.set("b", "2");
    db.expire("b", 1);

    assert(db.exists({"a", "b"}) == 2);  // Both alive

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    assert(db.exists({"a", "b"}) == 1);  // Only "a"
    assert(db.exists({"b"}) == 0);

    std::cout << "  PASS: test_exists_excludes_expired" << std::endl;
}

int main() {
    std::cout << "=== Database Tests ===" << std::endl;

    // Core tests
    test_set_and_get();
    test_get_nonexistent_key();
    test_overwrite_value();
    test_del_existing_keys();
    test_del_nonexistent_key();
    test_exists();
    test_keys();
    test_dbsize();
    test_flushdb();
    test_empty_string_value();
    test_special_characters();

    // TTL tests
    std::cout << std::endl;
    std::cout << "--- TTL / Expiry Tests ---" << std::endl;
    test_expire_and_ttl();
    test_expire_nonexistent_key();
    test_ttl_no_expiry();
    test_ttl_nonexistent_key();
    test_expired_key_lazy_delete();
    test_persist();
    test_set_clears_expiry();
    test_del_clears_expiry();
    test_flushdb_clears_expiry();
    test_keys_excludes_expired();
    test_dbsize_excludes_expired();
    test_exists_excludes_expired();

    std::cout << std::endl;
    std::cout << "All database tests passed! (" << 23 << " tests)" << std::endl;
    return 0;
}
