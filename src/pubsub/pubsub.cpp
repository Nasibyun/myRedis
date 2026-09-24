#include "pubsub/pubsub.h"
#include "protocol/protocol.h"

#include <algorithm>

namespace myredis {

std::vector<std::pair<std::string, int>> PubSubManager::subscribe(
    socket_t client_fd,
    const std::vector<std::string>& channels) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, int>> results;

    for (const auto& channel : channels) {
        // Add client to the channel's subscriber set
        channels_[channel].insert(client_fd);

        // Add channel to the client's subscription set
        client_channels_[client_fd].insert(channel);

        // Return (channel_name, total_channels_this_client_is_subscribed_to)
        // This matches Redis behavior: the third element of the subscribe
        // response is the total number of channels the client is subscribed to.
        int client_sub_count = static_cast<int>(client_channels_[client_fd].size());
        results.emplace_back(channel, client_sub_count);
    }

    return results;
}

std::vector<std::pair<std::string, int>> PubSubManager::unsubscribe(
    socket_t client_fd,
    const std::vector<std::string>& channels) {

    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, int>> results;

    // If no channels specified, unsubscribe from all
    std::vector<std::string> to_unsub;
    if (channels.empty()) {
        auto it = client_channels_.find(client_fd);
        if (it != client_channels_.end()) {
            to_unsub.assign(it->second.begin(), it->second.end());
        }
        // If client has no subscriptions and no channels given, return count 0
        if (to_unsub.empty()) {
            results.emplace_back("*", 0);
            return results;
        }
    } else {
        to_unsub = channels;
    }

    for (const auto& channel : to_unsub) {
        // Remove client from the channel's subscriber set
        auto ch_it = channels_.find(channel);
        if (ch_it != channels_.end()) {
            ch_it->second.erase(client_fd);
            // Clean up empty channels
            if (ch_it->second.empty()) {
                channels_.erase(ch_it);
            }
        }

        // Remove channel from client's subscription set
        auto cl_it = client_channels_.find(client_fd);
        if (cl_it != client_channels_.end()) {
            cl_it->second.erase(channel);
            // Clean up if client has no more subscriptions
            if (cl_it->second.empty()) {
                client_channels_.erase(cl_it);
            }
        }

        // Return remaining subscription count for this client
        int remaining = 0;
        auto cl_check = client_channels_.find(client_fd);
        if (cl_check != client_channels_.end()) {
            remaining = static_cast<int>(cl_check->second.size());
        }
        results.emplace_back(channel, remaining);
    }

    return results;
}

int PubSubManager::publish(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return 0;  // No subscribers on this channel
    }

    // Build the message to push to subscribers.
    // Format: "message <channel> <message>"
    // This is our custom format — when we implement RESP (Milestone 12),
    // this will become a proper RESP array.
    std::string push_msg = "message " + channel + " " + message;

    int delivered = 0;
    // Collect dead sockets to clean up after iteration
    std::vector<socket_t> dead_sockets;

    for (socket_t sub_fd : it->second) {
        // Try to send the message to each subscriber.
        // If send fails, the client has disconnected — mark for cleanup.
        if (Protocol::send_message(sub_fd, push_msg)) {
            ++delivered;
        } else {
            dead_sockets.push_back(sub_fd);
        }
    }

    // Clean up dead sockets (clients that disconnected)
    for (socket_t dead_fd : dead_sockets) {
        // Remove from this channel
        it->second.erase(dead_fd);

        // Remove from reverse index
        auto cl_it = client_channels_.find(dead_fd);
        if (cl_it != client_channels_.end()) {
            cl_it->second.erase(channel);
            if (cl_it->second.empty()) {
                client_channels_.erase(cl_it);
            }
        }
    }

    // Clean up empty channel
    if (it->second.empty()) {
        channels_.erase(it);
    }

    return delivered;
}

bool PubSubManager::is_subscribed(socket_t client_fd) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_channels_.find(client_fd);
    return it != client_channels_.end() && !it->second.empty();
}

void PubSubManager::client_disconnected(socket_t client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto cl_it = client_channels_.find(client_fd);
    if (cl_it == client_channels_.end()) {
        return;  // Client had no subscriptions
    }

    // Remove client from every channel it was subscribed to
    for (const auto& channel : cl_it->second) {
        auto ch_it = channels_.find(channel);
        if (ch_it != channels_.end()) {
            ch_it->second.erase(client_fd);
            if (ch_it->second.empty()) {
                channels_.erase(ch_it);
            }
        }
    }

    // Remove the client's reverse index entry
    client_channels_.erase(cl_it);
}

size_t PubSubManager::active_channel_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return channels_.size();
}

size_t PubSubManager::total_subscription_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = 0;
    for (const auto& pair : client_channels_) {
        total += pair.second.size();
    }
    return total;
}

} // namespace myredis
