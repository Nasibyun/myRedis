#include "pubsub/pubsub.h"
#include "resp/resp.h"

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

        // Return the channel name and the client's total subscription count
        int sub_count = static_cast<int>(client_channels_[client_fd].size());
        results.push_back({channel, sub_count});
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
        auto cl_it = client_channels_.find(client_fd);
        if (cl_it != client_channels_.end()) {
            to_unsub.assign(cl_it->second.begin(), cl_it->second.end());
        }
        // If client has no subscriptions, return a single ("*", 0) result
        // to match Redis behavior.
        if (to_unsub.empty()) {
            results.push_back({"*", 0});
            return results;
        }
    } else {
        to_unsub = channels;
    }

    for (const auto& channel : to_unsub) {
        // Remove client from the channel
        auto ch_it = channels_.find(channel);
        if (ch_it != channels_.end()) {
            ch_it->second.erase(client_fd);
            if (ch_it->second.empty()) {
                channels_.erase(ch_it);
            }
        }

        // Remove channel from the client's set
        auto cl_it = client_channels_.find(client_fd);
        if (cl_it != client_channels_.end()) {
            cl_it->second.erase(channel);
        }

        // Return the channel name and the client's remaining subscription count
        int sub_count = 0;
        auto cl_check = client_channels_.find(client_fd);
        if (cl_check != client_channels_.end()) {
            sub_count = static_cast<int>(cl_check->second.size());
        }
        results.push_back({channel, sub_count});
    }

    // Clean up empty client entry
    auto cl_it = client_channels_.find(client_fd);
    if (cl_it != client_channels_.end() && cl_it->second.empty()) {
        client_channels_.erase(cl_it);
    }

    return results;
}

int PubSubManager::publish(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return 0;  // No subscribers on this channel
    }

    // Build the RESP push message for subscribers.
    // Format: RESP array ["message", <channel>, <payload>]
    // This is the standard Redis RESP format for pub/sub messages.
    std::vector<std::string> parts;
    parts.push_back(Resp::bulk_string("message"));
    parts.push_back(Resp::bulk_string(channel));
    parts.push_back(Resp::bulk_string(message));
    std::string push_msg = Resp::array(parts);

    int delivered = 0;
    // Collect dead sockets to clean up after iteration
    std::vector<socket_t> dead_sockets;

    for (socket_t sub_fd : it->second) {
        // Try to send the message to each subscriber.
        // If send fails, the client has disconnected — mark for cleanup.
        if (Resp::send_response(sub_fd, push_msg)) {
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
