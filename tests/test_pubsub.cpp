///
/// test_pubsub.cpp — Unit tests for the Pub/Sub manager
///
/// Tests subscribe, unsubscribe, publish, subscription mode,
/// and client disconnect cleanup.
///
/// Note: Since PubSubManager uses socket_t internally for tracking
/// subscribers, we use fake socket file descriptor values in tests.
/// We can't test actual message delivery (Protocol::send_message)
/// without real sockets, but we CAN test all the subscription
/// management logic — which channels are subscribed, counts,
/// cleanup on disconnect, etc.
///
/// For the publish() method, we test that it returns 0 when there
/// are no valid sockets (since our fake fds will fail send_message).
///

#include "pubsub/pubsub.h"
#include <cassert>
#include <iostream>

// ─── Subscribe / Unsubscribe Tests ─────────────────────────────

void test_subscribe_single_channel() {
    myredis::PubSubManager mgr;

    // Use fake socket fd values for testing
    socket_t client = 100;
    auto results = mgr.subscribe(client, {"news"});

    assert(results.size() == 1);
    assert(results[0].first == "news");
    assert(results[0].second == 1);  // 1 channel subscribed

    assert(mgr.is_subscribed(client));
    assert(mgr.active_channel_count() == 1);
    assert(mgr.total_subscription_count() == 1);

    std::cout << "  PASS: test_subscribe_single_channel" << std::endl;
}

void test_subscribe_multiple_channels() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    auto results = mgr.subscribe(client, {"news", "sports", "weather"});

    assert(results.size() == 3);
    assert(results[0].first == "news");
    assert(results[0].second == 1);  // 1st channel
    assert(results[1].first == "sports");
    assert(results[1].second == 2);  // 2nd channel
    assert(results[2].first == "weather");
    assert(results[2].second == 3);  // 3rd channel

    assert(mgr.active_channel_count() == 3);
    assert(mgr.total_subscription_count() == 3);

    std::cout << "  PASS: test_subscribe_multiple_channels" << std::endl;
}

void test_subscribe_duplicate_channel() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    mgr.subscribe(client, {"news"});
    auto results = mgr.subscribe(client, {"news"});  // Subscribe again

    // Should still show 1 total subscription (sets don't duplicate)
    assert(results.size() == 1);
    assert(results[0].second == 1);
    assert(mgr.total_subscription_count() == 1);

    std::cout << "  PASS: test_subscribe_duplicate_channel" << std::endl;
}

void test_subscribe_multiple_clients() {
    myredis::PubSubManager mgr;
    socket_t client1 = 100;
    socket_t client2 = 200;

    mgr.subscribe(client1, {"news"});
    mgr.subscribe(client2, {"news"});

    assert(mgr.active_channel_count() == 1);  // Same channel
    assert(mgr.total_subscription_count() == 2);  // Two clients
    assert(mgr.is_subscribed(client1));
    assert(mgr.is_subscribed(client2));

    std::cout << "  PASS: test_subscribe_multiple_clients" << std::endl;
}

void test_unsubscribe_single_channel() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    mgr.subscribe(client, {"news", "sports"});
    auto results = mgr.unsubscribe(client, {"news"});

    assert(results.size() == 1);
    assert(results[0].first == "news");
    assert(results[0].second == 1);  // Still subscribed to "sports"

    assert(mgr.is_subscribed(client));
    assert(mgr.active_channel_count() == 1);  // Only "sports" remains

    std::cout << "  PASS: test_unsubscribe_single_channel" << std::endl;
}

void test_unsubscribe_all_channels() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    mgr.subscribe(client, {"news", "sports", "weather"});
    auto results = mgr.unsubscribe(client, {});  // Empty = unsubscribe all

    assert(results.size() == 3);

    // After unsubscribing from all, client should not be in subscription mode
    assert(!mgr.is_subscribed(client));
    assert(mgr.active_channel_count() == 0);
    assert(mgr.total_subscription_count() == 0);

    std::cout << "  PASS: test_unsubscribe_all_channels" << std::endl;
}

void test_unsubscribe_last_channel_exits_mode() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    mgr.subscribe(client, {"news"});
    assert(mgr.is_subscribed(client));

    mgr.unsubscribe(client, {"news"});
    assert(!mgr.is_subscribed(client));

    std::cout << "  PASS: test_unsubscribe_last_channel_exits_mode" << std::endl;
}

void test_unsubscribe_nonexistent_channel() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    mgr.subscribe(client, {"news"});
    auto results = mgr.unsubscribe(client, {"sports"});  // Not subscribed to this

    assert(results.size() == 1);
    assert(results[0].first == "sports");
    assert(results[0].second == 1);  // Still subscribed to "news"

    assert(mgr.is_subscribed(client));

    std::cout << "  PASS: test_unsubscribe_nonexistent_channel" << std::endl;
}

void test_unsubscribe_no_subscriptions() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    // Unsubscribe from all when not subscribed to anything
    auto results = mgr.unsubscribe(client, {});
    assert(results.size() == 1);
    assert(results[0].first == "*");
    assert(results[0].second == 0);

    std::cout << "  PASS: test_unsubscribe_no_subscriptions" << std::endl;
}

// ─── Subscription Mode Tests ──────────────────────────────────

void test_is_subscribed_not_subscribed() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    assert(!mgr.is_subscribed(client));

    std::cout << "  PASS: test_is_subscribed_not_subscribed" << std::endl;
}

void test_subscription_mode_lifecycle() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    // Not in subscription mode initially
    assert(!mgr.is_subscribed(client));

    // Enter subscription mode
    mgr.subscribe(client, {"news"});
    assert(mgr.is_subscribed(client));

    // Still in mode after adding another channel
    mgr.subscribe(client, {"sports"});
    assert(mgr.is_subscribed(client));

    // Still in mode after removing one channel
    mgr.unsubscribe(client, {"news"});
    assert(mgr.is_subscribed(client));

    // Exit subscription mode after removing last channel
    mgr.unsubscribe(client, {"sports"});
    assert(!mgr.is_subscribed(client));

    std::cout << "  PASS: test_subscription_mode_lifecycle" << std::endl;
}

// ─── Client Disconnect Tests ──────────────────────────────────

void test_client_disconnected_cleanup() {
    myredis::PubSubManager mgr;
    socket_t client1 = 100;
    socket_t client2 = 200;

    // Both clients subscribe to "news"
    mgr.subscribe(client1, {"news", "sports"});
    mgr.subscribe(client2, {"news"});

    assert(mgr.total_subscription_count() == 3);

    // Client 1 disconnects
    mgr.client_disconnected(client1);

    assert(!mgr.is_subscribed(client1));
    assert(mgr.is_subscribed(client2));
    assert(mgr.active_channel_count() == 1);  // Only "news" (from client2)
    assert(mgr.total_subscription_count() == 1);

    std::cout << "  PASS: test_client_disconnected_cleanup" << std::endl;
}

void test_client_disconnected_empty_channel_removed() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    mgr.subscribe(client, {"news"});
    assert(mgr.active_channel_count() == 1);

    mgr.client_disconnected(client);

    // Channel with no subscribers should be cleaned up
    assert(mgr.active_channel_count() == 0);
    assert(mgr.total_subscription_count() == 0);

    std::cout << "  PASS: test_client_disconnected_empty_channel_removed" << std::endl;
}

void test_client_disconnected_no_subscriptions() {
    myredis::PubSubManager mgr;
    socket_t client = 100;

    // Disconnect a client that was never subscribed — should not crash
    mgr.client_disconnected(client);

    assert(mgr.active_channel_count() == 0);
    assert(mgr.total_subscription_count() == 0);

    std::cout << "  PASS: test_client_disconnected_no_subscriptions" << std::endl;
}

// ─── Publish Tests ────────────────────────────────────────────

void test_publish_no_subscribers() {
    myredis::PubSubManager mgr;

    // Publishing to a channel with no subscribers returns 0
    int delivered = mgr.publish("news", "hello world");
    assert(delivered == 0);

    std::cout << "  PASS: test_publish_no_subscribers" << std::endl;
}

void test_publish_with_fake_sockets() {
    myredis::PubSubManager mgr;
    socket_t client = 999;  // Fake fd — send will fail

    mgr.subscribe(client, {"news"});

    // Publish will try to send to the fake socket; send_message will fail.
    // The dead socket should be cleaned up automatically.
    int delivered = mgr.publish("news", "hello");
    assert(delivered == 0);  // Send failed, so 0 delivered

    // After failed delivery, the dead socket should be cleaned up
    assert(!mgr.is_subscribed(client));
    assert(mgr.active_channel_count() == 0);

    std::cout << "  PASS: test_publish_with_fake_sockets" << std::endl;
}

// ─── Counter Tests ────────────────────────────────────────────

void test_active_channel_count() {
    myredis::PubSubManager mgr;
    socket_t c1 = 100, c2 = 200;

    assert(mgr.active_channel_count() == 0);

    mgr.subscribe(c1, {"news"});
    assert(mgr.active_channel_count() == 1);

    mgr.subscribe(c2, {"news"});  // Same channel
    assert(mgr.active_channel_count() == 1);

    mgr.subscribe(c2, {"sports"});  // New channel
    assert(mgr.active_channel_count() == 2);

    mgr.unsubscribe(c1, {"news"});
    assert(mgr.active_channel_count() == 2);  // c2 still on news

    mgr.unsubscribe(c2, {"news"});
    assert(mgr.active_channel_count() == 1);  // Only sports

    std::cout << "  PASS: test_active_channel_count" << std::endl;
}

void test_total_subscription_count() {
    myredis::PubSubManager mgr;
    socket_t c1 = 100, c2 = 200;

    assert(mgr.total_subscription_count() == 0);

    mgr.subscribe(c1, {"news", "sports"});
    assert(mgr.total_subscription_count() == 2);

    mgr.subscribe(c2, {"news"});
    assert(mgr.total_subscription_count() == 3);

    mgr.unsubscribe(c1, {"news"});
    assert(mgr.total_subscription_count() == 2);

    mgr.client_disconnected(c2);
    assert(mgr.total_subscription_count() == 1);  // Only c1 on "sports"

    std::cout << "  PASS: test_total_subscription_count" << std::endl;
}

int main() {
    std::cout << "=== Pub/Sub Tests ===" << std::endl;

    // Subscribe/Unsubscribe
    std::cout << std::endl;
    std::cout << "--- Subscribe / Unsubscribe Tests ---" << std::endl;
    test_subscribe_single_channel();
    test_subscribe_multiple_channels();
    test_subscribe_duplicate_channel();
    test_subscribe_multiple_clients();
    test_unsubscribe_single_channel();
    test_unsubscribe_all_channels();
    test_unsubscribe_last_channel_exits_mode();
    test_unsubscribe_nonexistent_channel();
    test_unsubscribe_no_subscriptions();

    // Subscription mode
    std::cout << std::endl;
    std::cout << "--- Subscription Mode Tests ---" << std::endl;
    test_is_subscribed_not_subscribed();
    test_subscription_mode_lifecycle();

    // Client disconnect
    std::cout << std::endl;
    std::cout << "--- Client Disconnect Tests ---" << std::endl;
    test_client_disconnected_cleanup();
    test_client_disconnected_empty_channel_removed();
    test_client_disconnected_no_subscriptions();

    // Publish
    std::cout << std::endl;
    std::cout << "--- Publish Tests ---" << std::endl;
    test_publish_no_subscribers();
    test_publish_with_fake_sockets();

    // Counters
    std::cout << std::endl;
    std::cout << "--- Counter Tests ---" << std::endl;
    test_active_channel_count();
    test_total_subscription_count();

    std::cout << std::endl;
    std::cout << "All pub/sub tests passed! (18 tests)" << std::endl;
    return 0;
}
