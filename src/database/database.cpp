#include "database/database.h"

namespace myredis {

void Database::set(const std::string& key, const std::string& value) {
    // unique_lock = exclusive write access
    // No other thread can read or write while we hold this lock
    std::unique_lock lock(mutex_);
    store_[key] = value;
}

std::optional<std::string> Database::get(const std::string& key) const {
    // shared_lock = shared read access
    // Multiple threads can hold shared locks simultaneously,
    // but no thread can acquire a unique_lock while shared locks are held
    std::shared_lock lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) {
        return std::nullopt;
    }
    return it->second;
}

int Database::del(const std::vector<std::string>& keys) {
    std::unique_lock lock(mutex_);
    int count = 0;
    for (const auto& key : keys) {
        // erase() returns the number of elements removed (0 or 1)
        count += static_cast<int>(store_.erase(key));
    }
    return count;
}

int Database::exists(const std::vector<std::string>& keys) const {
    std::shared_lock lock(mutex_);
    int count = 0;
    for (const auto& key : keys) {
        if (store_.count(key)) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> Database::keys() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(store_.size());
    for (const auto& [key, value] : store_) {
        (void)value;  // Unused — we only want keys
        result.push_back(key);
    }
    return result;
}

size_t Database::dbsize() const {
    std::shared_lock lock(mutex_);
    return store_.size();
}

void Database::flushdb() {
    std::unique_lock lock(mutex_);
    store_.clear();
}

} // namespace myredis
