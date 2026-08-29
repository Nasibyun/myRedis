///
/// test_database.cpp — Unit tests for the in-memory database
///
/// Tests SET, GET, DEL, EXISTS, KEYS, DBSIZE, FLUSHDB operations
/// including edge cases like nonexistent keys and overwrites.
///

#include "database/database.h"
#include <cassert>
#include <iostream>

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

int main() {
    std::cout << "=== Database Tests ===" << std::endl;

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

    std::cout << std::endl;
    std::cout << "All database tests passed! (" << 11 << " tests)" << std::endl;
    return 0;
}
