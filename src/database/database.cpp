#include "database/database.h"
#include <algorithm>  // std::min

namespace myredis {

Database::Database() = default;

Database::~Database() {
    stop_expiry_thread();
}

// ─── Core Commands ─────────────────────────────────────────────

void Database::set(const std::string& key, const std::string& value) {
    // unique_lock = exclusive write access
    // No other thread can read or write while we hold this lock
    std::unique_lock lock(mutex_);
    store_[key] = value;
    // SET clears any existing TTL (same behavior as Redis).
    // This is important: if a key had a 10-second TTL and you SET it again,
    // the new value lives forever unless you explicitly EXPIRE it again.
    expiry_.erase(key);
}

std::optional<std::string> Database::get(const std::string& key) {
    // We need a unique_lock (not shared) because lazy expiry might
    // mutate store_ and expiry_. This is a tradeoff: slightly less
    // read concurrency in exchange for automatic cleanup.
    std::unique_lock lock(mutex_);

    // Lazy expiry: check if the key is expired before returning it
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return std::nullopt;
    }

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
        size_t removed = store_.erase(key);
        if (removed > 0) {
            // Also clean up the expiry map
            expiry_.erase(key);
            count += static_cast<int>(removed);
        }
    }
    return count;
}

int Database::exists(const std::vector<std::string>& keys) {
    std::unique_lock lock(mutex_);
    int count = 0;
    for (const auto& key : keys) {
        // Lazy expiry check
        if (is_expired_unlocked(key)) {
            remove_expired_unlocked(key);
            continue;
        }
        if (store_.count(key)) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> Database::keys() {
    std::unique_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(store_.size());

    // Collect non-expired keys; lazily delete any expired ones we find
    std::vector<std::string> expired_keys;
    for (const auto& [key, value] : store_) {
        (void)value;
        if (is_expired_unlocked(key)) {
            expired_keys.push_back(key);
        } else {
            result.push_back(key);
        }
    }
    // Clean up expired keys we discovered
    for (const auto& key : expired_keys) {
        remove_expired_unlocked(key);
    }

    return result;
}

size_t Database::dbsize() {
    std::unique_lock lock(mutex_);

    // Count only non-expired keys
    size_t count = 0;
    std::vector<std::string> expired_keys;
    for (const auto& [key, value] : store_) {
        (void)value;
        if (is_expired_unlocked(key)) {
            expired_keys.push_back(key);
        } else {
            ++count;
        }
    }
    for (const auto& key : expired_keys) {
        remove_expired_unlocked(key);
    }

    return count;
}

void Database::flushdb() {
    std::unique_lock lock(mutex_);
    store_.clear();
    expiry_.clear();
}

// ─── TTL Commands ──────────────────────────────────────────────

bool Database::expire(const std::string& key, int seconds) {
    std::unique_lock lock(mutex_);

    // Can't set TTL on a key that doesn't exist
    if (store_.find(key) == store_.end()) {
        return false;
    }

    // Check if the key is already expired (lazy check)
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return false;
    }

    // Set the expiry time to now + seconds
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::seconds(seconds);
    expiry_[key] = deadline;
    return true;
}

int Database::ttl(const std::string& key) {
    std::unique_lock lock(mutex_);

    // Check if the key exists at all
    if (store_.find(key) == store_.end()) {
        return -2;  // Key does not exist
    }

    // Check if it's expired (lazy)
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return -2;  // Expired = doesn't exist
    }

    // Check if the key has a TTL
    auto it = expiry_.find(key);
    if (it == expiry_.end()) {
        return -1;  // Key exists but has no TTL
    }

    // Calculate remaining time
    auto now = std::chrono::steady_clock::now();
    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        it->second - now
    ).count();

    return static_cast<int>(remaining);
}

bool Database::persist(const std::string& key) {
    std::unique_lock lock(mutex_);

    // Check if the key exists
    if (store_.find(key) == store_.end()) {
        return false;
    }

    // Check if it's expired
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return false;
    }

    // Remove the TTL (if any)
    return expiry_.erase(key) > 0;
}

// ─── Background Expiry ────────────────────────────────────────

void Database::start_expiry_thread() {
    expiry_running_ = true;
    expiry_thread_ = std::thread(&Database::expiry_loop, this);
}

void Database::stop_expiry_thread() {
    if (expiry_running_) {
        expiry_running_ = false;
        // Wake the thread so it can check the flag and exit
        expiry_cv_.notify_all();
        if (expiry_thread_.joinable()) {
            expiry_thread_.join();
        }
    }
}

void Database::expiry_loop() {
    while (expiry_running_) {
        // Sleep for 100ms, but wake up early if stop_expiry_thread() is called.
        // Using a condition_variable instead of sleep_for allows clean shutdown.
        {
            std::unique_lock<std::mutex> cv_lock(expiry_cv_mutex_);
            expiry_cv_.wait_for(cv_lock, std::chrono::milliseconds(100), [this] {
                return !expiry_running_.load();
            });
        }

        if (!expiry_running_) break;

        // ── Active expiry scan ──────────────────────────────
        // Sample up to 20 keys from the expiry map and delete
        // any that have passed their deadline. This is the same
        // strategy Redis uses (randomized sampling).
        std::unique_lock lock(mutex_);

        if (expiry_.empty()) continue;

        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> to_delete;
        int sampled = 0;
        constexpr int MAX_SAMPLE = 20;

        for (auto it = expiry_.begin(); it != expiry_.end() && sampled < MAX_SAMPLE; ++it, ++sampled) {
            if (it->second <= now) {
                to_delete.push_back(it->first);
            }
        }

        for (const auto& key : to_delete) {
            store_.erase(key);
            expiry_.erase(key);
        }
    }
}

// ─── Helpers ───────────────────────────────────────────────────

bool Database::is_expired_unlocked(const std::string& key) const {
    auto it = expiry_.find(key);
    if (it == expiry_.end()) {
        return false;  // No TTL = not expired
    }
    return it->second <= std::chrono::steady_clock::now();
}

void Database::remove_expired_unlocked(const std::string& key) {
    store_.erase(key);
    expiry_.erase(key);
}

} // namespace myredis
