///
/// test_transactions.cpp — Unit tests for MULTI/EXEC/DISCARD/WATCH
///
/// Tests the transaction command handler logic directly by calling
/// CommandHandler methods. Since the handler uses Database and PubSubManager
/// references, we create real instances for testing.
///

#include "commands/command_handler.h"
#include "database/database.h"
#include "pubsub/pubsub.h"
#include <cassert>
#include <iostream>
#include <string>

// Helper to create a Command struct
static myredis::Command make_cmd(const std::string& name,
                                  const std::vector<std::string>& args = {}) {
    myredis::Command cmd;
    cmd.name = name;
    cmd.args = args;
    return cmd;
}

// ─── MULTI/EXEC Tests ──────────────────────────────────────────

void test_multi_exec_basic() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    // MULTI starts a transaction
    std::string result = handler.execute(make_cmd("MULTI"));
    assert(result == "OK");
    assert(handler.in_transaction());

    // Commands are queued, not executed
    result = handler.execute(make_cmd("SET", {"name", "Nasib"}));
    assert(result == "QUEUED");

    result = handler.execute(make_cmd("SET", {"age", "25"}));
    assert(result == "QUEUED");

    result = handler.execute(make_cmd("GET", {"name"}));
    assert(result == "QUEUED");

    // During transaction, the values are NOT yet in the database
    auto val = db.get("name");
    assert(!val.has_value());

    // EXEC executes all queued commands
    result = handler.execute(make_cmd("EXEC"));
    assert(!handler.in_transaction());

    // Check the response contains results for all 3 commands
    assert(result.find("OK") != std::string::npos);
    assert(result.find("\"Nasib\"") != std::string::npos);

    // Now the values should be in the database
    val = db.get("name");
    assert(val.has_value());
    assert(val.value() == "Nasib");

    val = db.get("age");
    assert(val.has_value());
    assert(val.value() == "25");

    std::cout << "  PASS: test_multi_exec_basic" << std::endl;
}

void test_multi_exec_empty() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    handler.execute(make_cmd("MULTI"));
    std::string result = handler.execute(make_cmd("EXEC"));
    assert(result == "(empty list)");
    assert(!handler.in_transaction());

    std::cout << "  PASS: test_multi_exec_empty" << std::endl;
}

void test_multi_nested() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    handler.execute(make_cmd("MULTI"));
    std::string result = handler.execute(make_cmd("MULTI"));
    assert(result == "ERR MULTI calls can not be nested");

    std::cout << "  PASS: test_multi_nested" << std::endl;
}

void test_exec_without_multi() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    std::string result = handler.execute(make_cmd("EXEC"));
    assert(result == "ERR EXEC without MULTI");

    std::cout << "  PASS: test_exec_without_multi" << std::endl;
}

// ─── DISCARD Tests ─────────────────────────────────────────────

void test_discard() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"key1", "val1"}));
    handler.execute(make_cmd("SET", {"key2", "val2"}));

    std::string result = handler.execute(make_cmd("DISCARD"));
    assert(result == "OK");
    assert(!handler.in_transaction());

    // Commands should NOT have been executed
    auto val = db.get("key1");
    assert(!val.has_value());

    std::cout << "  PASS: test_discard" << std::endl;
}

void test_discard_without_multi() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    std::string result = handler.execute(make_cmd("DISCARD"));
    assert(result == "ERR DISCARD without MULTI");

    std::cout << "  PASS: test_discard_without_multi" << std::endl;
}

// ─── WATCH Tests ───────────────────────────────────────────────

void test_watch_no_modification() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    db.set("counter", "10");

    // Watch a key
    std::string result = handler.execute(make_cmd("WATCH", {"counter"}));
    assert(result == "OK");

    // Start transaction
    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"counter", "11"}));

    // No modification to "counter" between WATCH and EXEC
    result = handler.execute(make_cmd("EXEC"));
    // Should succeed
    assert(result.find("OK") != std::string::npos);

    auto val = db.get("counter");
    assert(val.has_value());
    assert(val.value() == "11");

    std::cout << "  PASS: test_watch_no_modification" << std::endl;
}

void test_watch_with_modification() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    db.set("counter", "10");

    // Watch a key
    handler.execute(make_cmd("WATCH", {"counter"}));

    // Another "client" modifies the key
    db.set("counter", "99");

    // Start transaction
    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"counter", "11"}));

    // EXEC should fail because watched key was modified
    std::string result = handler.execute(make_cmd("EXEC"));
    assert(result == "(nil)");

    // Value should remain as "99" (the modification by the other client)
    auto val = db.get("counter");
    assert(val.has_value());
    assert(val.value() == "99");

    std::cout << "  PASS: test_watch_with_modification" << std::endl;
}

void test_watch_key_created() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    // Watch a key that doesn't exist
    handler.execute(make_cmd("WATCH", {"newkey"}));

    // Another "client" creates the key
    db.set("newkey", "surprise");

    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"newkey", "myvalue"}));

    // EXEC should fail
    std::string result = handler.execute(make_cmd("EXEC"));
    assert(result == "(nil)");

    std::cout << "  PASS: test_watch_key_created" << std::endl;
}

void test_watch_key_deleted() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    db.set("ephemeral", "gone-soon");

    handler.execute(make_cmd("WATCH", {"ephemeral"}));

    // Another "client" deletes the key
    db.del({"ephemeral"});

    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"ephemeral", "back"}));

    std::string result = handler.execute(make_cmd("EXEC"));
    assert(result == "(nil)");

    std::cout << "  PASS: test_watch_key_deleted" << std::endl;
}

void test_watch_inside_multi() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    handler.execute(make_cmd("MULTI"));
    std::string result = handler.execute(make_cmd("WATCH", {"key"}));
    assert(result == "ERR WATCH inside MULTI is not allowed");

    std::cout << "  PASS: test_watch_inside_multi" << std::endl;
}

void test_watch_no_args() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    std::string result = handler.execute(make_cmd("WATCH"));
    assert(result == "ERR wrong number of arguments for 'WATCH' command");

    std::cout << "  PASS: test_watch_no_args" << std::endl;
}

// ─── Mixed Transaction Tests ──────────────────────────────────

void test_multi_with_list_commands() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("RPUSH", {"tasks", "a", "b", "c"}));
    handler.execute(make_cmd("LLEN", {"tasks"}));
    handler.execute(make_cmd("LPOP", {"tasks"}));

    std::string result = handler.execute(make_cmd("EXEC"));

    // Should contain results for all 3 commands
    assert(result.find("(integer) 3") != std::string::npos);
    assert(result.find("\"a\"") != std::string::npos);

    // Verify final state
    auto val = db.lpop("tasks");
    assert(val.has_value());
    assert(val.value() == "b");

    std::cout << "  PASS: test_multi_with_list_commands" << std::endl;
}

void test_multi_exec_multiple_times() {
    myredis::Database db;
    myredis::PubSubManager pubsub;
    myredis::CommandHandler handler(db, pubsub, 100);

    // First transaction
    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"x", "1"}));
    handler.execute(make_cmd("EXEC"));

    // Second transaction
    handler.execute(make_cmd("MULTI"));
    handler.execute(make_cmd("SET", {"y", "2"}));
    handler.execute(make_cmd("EXEC"));

    auto x = db.get("x");
    auto y = db.get("y");
    assert(x.has_value() && x.value() == "1");
    assert(y.has_value() && y.value() == "2");

    std::cout << "  PASS: test_multi_exec_multiple_times" << std::endl;
}

int main() {
    std::cout << "=== Transaction Tests ===" << std::endl;

    // MULTI/EXEC
    std::cout << std::endl;
    std::cout << "--- MULTI/EXEC Tests ---" << std::endl;
    test_multi_exec_basic();
    test_multi_exec_empty();
    test_multi_nested();
    test_exec_without_multi();

    // DISCARD
    std::cout << std::endl;
    std::cout << "--- DISCARD Tests ---" << std::endl;
    test_discard();
    test_discard_without_multi();

    // WATCH
    std::cout << std::endl;
    std::cout << "--- WATCH Tests ---" << std::endl;
    test_watch_no_modification();
    test_watch_with_modification();
    test_watch_key_created();
    test_watch_key_deleted();
    test_watch_inside_multi();
    test_watch_no_args();

    // Mixed
    std::cout << std::endl;
    std::cout << "--- Mixed Transaction Tests ---" << std::endl;
    test_multi_with_list_commands();
    test_multi_exec_multiple_times();

    std::cout << std::endl;
    std::cout << "All transaction tests passed! (14 tests)" << std::endl;
    return 0;
}
