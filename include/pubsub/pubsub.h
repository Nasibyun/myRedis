#pragma once
///
/// pubsub.h — Publish/Subscribe messaging manager
///
/// Implements Redis-like Pub/Sub channels for inter-client communication.
///
/// Architecture:
///   - channels_ maps channel names → set of subscribed client sockets
///   - client_channels_ maps client sockets → set of subscribed channels
///   - Protected by its own std::mutex (separate from Database mutex)
///   - Messages are pushed directly to subscriber sockets via the wire protocol
///
/// Subscription mode:
///   When a client subscribes to any channel, it enters "subscription mode."
///   In this mode, only SUBSCRIBE, UNSUBSCRIBE, and PING commands are allowed.
///   All other commands return an error.
///
/// Message format pushed to subscribers:
///   "message <channel> <payload>"
///
/// Thread safety:
///   All public methods are thread-safe. The PubSubManager uses its own
///   mutex to avoid blocking database operations during pub/sub activity.
///

#include "platform/platform.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <vector>
#include <mutex>

namespace myredis {

class PubSubManager {
public:
    PubSubManager() = default;

    /// Subscribe a client socket to one or more channels.
    /// Returns a vector of (channel_name, subscriber_count) pairs for
    /// each channel subscribed — used to build the response messages.
    std::vector<std::pair<std::string, int>> subscribe(
        socket_t client_fd,
        const std::vector<std::string>& channels);

    /// Unsubscribe a client socket from the given channels.
    /// If channels is empty, unsubscribes from ALL channels.
    /// Returns a vector of (channel_name, remaining_sub_count) pairs.
    /// remaining_sub_count is how many channels this client is STILL subscribed to.
    std::vector<std::pair<std::string, int>> unsubscribe(
        socket_t client_fd,
        const std::vector<std::string>& channels);

    /// Publish a message to a channel.
    /// Sends the message to all subscribers of that channel.
    /// Returns the number of clients that received the message.
    int publish(const std::string& channel, const std::string& message);

    /// Check if a client is currently in subscription mode.
    /// A client is in subscription mode if it is subscribed to at least one channel.
    bool is_subscribed(socket_t client_fd) const;

    /// Clean up when a client disconnects.
    /// Removes the client from all channels it was subscribed to.
    void client_disconnected(socket_t client_fd);

    /// Get the number of channels that have at least one subscriber.
    size_t active_channel_count() const;

    /// Get the total number of subscriptions across all clients.
    size_t total_subscription_count() const;

private:
    mutable std::mutex mutex_;

    // Channel → set of subscriber sockets
    // This is the primary index: "who is listening on channel X?"
    std::unordered_map<std::string, std::set<socket_t>> channels_;

    // Client → set of channels they're subscribed to
    // This is the reverse index: "what channels is client Y listening on?"
    // Needed for efficient cleanup on disconnect and UNSUBSCRIBE with no args.
    std::unordered_map<socket_t, std::unordered_set<std::string>> client_channels_;
};

} // namespace myredis
