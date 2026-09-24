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

// ─── List Command Tests ──────────────────────────────────────

void test_lpush_and_lrange() {
    myredis::Database db;
    // LPUSH pushes to the front, so order is reversed
    int len = db.lpush("mylist", {"a", "b", "c"});
    assert(len == 3);
    // After LPUSH a b c: list = [c, b, a]
    auto elems = db.lrange("mylist", 0, -1);
    assert(elems.size() == 3);
    assert(elems[0] == "c");
    assert(elems[1] == "b");
    assert(elems[2] == "a");
    std::cout << "  PASS: test_lpush_and_lrange" << std::endl;
}

void test_rpush_and_lrange() {
    myredis::Database db;
    int len = db.rpush("mylist", {"a", "b", "c"});
    assert(len == 3);
    // After RPUSH a b c: list = [a, b, c]
    auto elems = db.lrange("mylist", 0, -1);
    assert(elems.size() == 3);
    assert(elems[0] == "a");
    assert(elems[1] == "b");
    assert(elems[2] == "c");
    std::cout << "  PASS: test_rpush_and_lrange" << std::endl;
}

void test_lpop() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b", "c"});

    auto val = db.lpop("mylist");
    assert(val.has_value());
    assert(val.value() == "a");

    // Remaining: [b, c]
    assert(db.llen("mylist") == 2);
    std::cout << "  PASS: test_lpop" << std::endl;
}

void test_rpop() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b", "c"});

    auto val = db.rpop("mylist");
    assert(val.has_value());
    assert(val.value() == "c");

    // Remaining: [a, b]
    assert(db.llen("mylist") == 2);
    std::cout << "  PASS: test_rpop" << std::endl;
}

void test_lpop_rpop_empty() {
    myredis::Database db;
    // Pop from nonexistent key
    assert(!db.lpop("nolist").has_value());
    assert(!db.rpop("nolist").has_value());

    // Pop until empty
    db.rpush("mylist", {"a"});
    db.lpop("mylist");
    assert(!db.lpop("mylist").has_value());
    std::cout << "  PASS: test_lpop_rpop_empty" << std::endl;
}

void test_lrange_negative_indices() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b", "c", "d", "e"});

    // Last 3 elements
    auto elems = db.lrange("mylist", -3, -1);
    assert(elems.size() == 3);
    assert(elems[0] == "c");
    assert(elems[1] == "d");
    assert(elems[2] == "e");

    // Out of range
    auto empty = db.lrange("mylist", 10, 20);
    assert(empty.empty());

    std::cout << "  PASS: test_lrange_negative_indices" << std::endl;
}

void test_llen() {
    myredis::Database db;
    assert(db.llen("nolist") == 0);
    db.rpush("mylist", {"a", "b"});
    assert(db.llen("mylist") == 2);
    std::cout << "  PASS: test_llen" << std::endl;
}

void test_lindex() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b", "c"});

    assert(db.lindex("mylist", 0).value() == "a");
    assert(db.lindex("mylist", 2).value() == "c");
    assert(db.lindex("mylist", -1).value() == "c");
    assert(db.lindex("mylist", -3).value() == "a");
    assert(!db.lindex("mylist", 5).has_value());  // Out of range
    assert(!db.lindex("nolist", 0).has_value());   // Nonexistent
    std::cout << "  PASS: test_lindex" << std::endl;
}

void test_list_auto_delete_empty() {
    myredis::Database db;
    db.rpush("mylist", {"a"});
    assert(db.dbsize() == 1);

    db.lpop("mylist");
    // Empty list should be auto-deleted
    assert(db.dbsize() == 0);
    assert(db.llen("mylist") == 0);
    std::cout << "  PASS: test_list_auto_delete_empty" << std::endl;
}

void test_list_del() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b", "c"});
    assert(db.del({"mylist"}) == 1);
    assert(db.llen("mylist") == 0);
    assert(!db.lpop("mylist").has_value());
    std::cout << "  PASS: test_list_del" << std::endl;
}

void test_list_exists_keys_dbsize() {
    myredis::Database db;
    db.set("str_key", "hello");
    db.rpush("list_key", {"a", "b"});

    assert(db.exists({"str_key", "list_key"}) == 2);
    assert(db.dbsize() == 2);

    auto k = db.keys();
    assert(k.size() == 2);

    std::cout << "  PASS: test_list_exists_keys_dbsize" << std::endl;
}

void test_list_expire_and_ttl() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b"});
    assert(db.expire("mylist", 10) == true);

    int remaining = db.ttl("mylist");
    assert(remaining > 0 && remaining <= 10);

    std::cout << "  PASS: test_list_expire_and_ttl" << std::endl;
}

void test_list_expired_lazy_delete() {
    myredis::Database db;
    db.rpush("mylist", {"a", "b"});
    db.expire("mylist", 1);

    assert(db.llen("mylist") == 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    // List should be expired — llen returns 0, lpop returns nil
    assert(db.llen("mylist") == 0);
    assert(!db.lpop("mylist").has_value());

    std::cout << "  PASS: test_list_expired_lazy_delete" << std::endl;
}

void test_wrongtype_error() {
    myredis::Database db;
    // Set a string key
    db.set("mykey", "hello");

    // LPUSH on a string key should return -1 (WRONGTYPE)
    assert(db.lpush("mykey", {"a"}) == -1);
    assert(db.rpush("mykey", {"a"}) == -1);
    assert(db.llen("mykey") == -1);

    // Set a list key
    db.rpush("mylist", {"a", "b"});

    // GET on a list key should return nullopt (it only searches store_)
    assert(!db.get("mylist").has_value());

    std::cout << "  PASS: test_wrongtype_error" << std::endl;
}

void test_list_flushdb() {
    myredis::Database db;
    db.set("str", "val");
    db.rpush("list", {"a", "b"});
    assert(db.dbsize() == 2);

    db.flushdb();
    assert(db.dbsize() == 0);
    assert(db.llen("list") == 0);
    assert(!db.get("str").has_value());

    std::cout << "  PASS: test_list_flushdb" << std::endl;
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

    // List tests
    std::cout << std::endl;
    std::cout << "--- List Tests ---" << std::endl;
    test_lpush_and_lrange();
    test_rpush_and_lrange();
    test_lpop();
    test_rpop();
    test_lpop_rpop_empty();
    test_lrange_negative_indices();
    test_llen();
    test_lindex();
    test_list_auto_delete_empty();
    test_list_del();
    test_list_exists_keys_dbsize();
    test_list_expire_and_ttl();
    test_list_expired_lazy_delete();
    test_wrongtype_error();
    test_list_flushdb();

    std::cout << std::endl;
    std::cout << "All database tests passed! (" << 38 << " tests)" << std::endl;
    return 0;
}
