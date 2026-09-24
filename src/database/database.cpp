#include "database/database.h"
#include "persistence/persistence.h"
#include <algorithm>  // std::min

namespace myredis {

Database::Database() = default;

Database::~Database() {
    stop_expiry_thread();
}

// ─── Type Checking ─────────────────────────────────────────────

KeyType Database::key_type_unlocked(const std::string& key) const {
    if (store_.count(key)) return KeyType::STRING;
    if (list_store_.count(key)) return KeyType::LIST;
    return KeyType::NONE;
}

bool Database::key_exists_unlocked(const std::string& key) const {
    return store_.count(key) > 0 || list_store_.count(key) > 0;
}

// ─── Core Commands ─────────────────────────────────────────────

void Database::set(const std::string& key, const std::string& value) {
    // unique_lock = exclusive write access
    // No other thread can read or write while we hold this lock
    std::unique_lock lock(mutex_);

    // SET on a list key: remove the list first (Redis behavior —
    // SET always creates a string, overwriting whatever was there)
    list_store_.erase(key);

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
        // Try to erase from string store
        size_t removed = store_.erase(key);
        // Try to erase from list store
        removed += list_store_.erase(key);
        if (removed > 0) {
            // Also clean up the expiry map
            expiry_.erase(key);
            ++count;
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
        if (key_exists_unlocked(key)) {
            ++count;
        }
    }
    return count;
}

std::vector<std::string> Database::keys() {
    std::unique_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(store_.size() + list_store_.size());

    // Collect non-expired string keys; lazily delete any expired ones we find
    std::vector<std::string> expired_keys;

    for (const auto& [key, value] : store_) {
        (void)value;
        if (is_expired_unlocked(key)) {
            expired_keys.push_back(key);
        } else {
            result.push_back(key);
        }
    }

    // Collect non-expired list keys
    for (const auto& [key, list] : list_store_) {
        (void)list;
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

    // Count only non-expired keys from both maps
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

    for (const auto& [key, list] : list_store_) {
        (void)list;
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
    list_store_.clear();
    expiry_.clear();
}

// ─── List Commands ─────────────────────────────────────────────

int Database::lpush(const std::string& key, const std::vector<std::string>& values) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
    }

    // Type check: if the key exists as a string, it's a WRONGTYPE error
    if (store_.count(key)) {
        return -1;  // WRONGTYPE
    }

    // Push each value to the front of the deque
    auto& list = list_store_[key];
    for (const auto& val : values) {
        list.push_front(val);
    }

    return static_cast<int>(list.size());
}

int Database::rpush(const std::string& key, const std::vector<std::string>& values) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
    }

    // Type check
    if (store_.count(key)) {
        return -1;  // WRONGTYPE
    }

    auto& list = list_store_[key];
    for (const auto& val : values) {
        list.push_back(val);
    }

    return static_cast<int>(list.size());
}

std::optional<std::string> Database::lpop(const std::string& key) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return std::nullopt;
    }

    auto it = list_store_.find(key);
    if (it == list_store_.end()) {
        return std::nullopt;  // Key doesn't exist (or is a string — handler checks type)
    }

    auto& list = it->second;
    if (list.empty()) {
        list_store_.erase(it);
        expiry_.erase(key);
        return std::nullopt;
    }

    std::string val = std::move(list.front());
    list.pop_front();

    // Auto-delete empty lists (Redis behavior)
    if (list.empty()) {
        list_store_.erase(it);
        expiry_.erase(key);
    }

    return val;
}

std::optional<std::string> Database::rpop(const std::string& key) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return std::nullopt;
    }

    auto it = list_store_.find(key);
    if (it == list_store_.end()) {
        return std::nullopt;
    }

    auto& list = it->second;
    if (list.empty()) {
        list_store_.erase(it);
        expiry_.erase(key);
        return std::nullopt;
    }

    std::string val = std::move(list.back());
    list.pop_back();

    // Auto-delete empty lists (Redis behavior)
    if (list.empty()) {
        list_store_.erase(it);
        expiry_.erase(key);
    }

    return val;
}

std::vector<std::string> Database::lrange(const std::string& key, int start, int stop) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return {};
    }

    auto it = list_store_.find(key);
    if (it == list_store_.end()) {
        return {};  // Key doesn't exist
    }

    const auto& list = it->second;
    int len = static_cast<int>(list.size());

    // Normalize negative indices (Redis behavior)
    if (start < 0) start = len + start;
    if (stop < 0) stop = len + stop;

    // Clamp to valid range
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;

    // If start > stop after normalization, return empty
    if (start > stop || start >= len) {
        return {};
    }

    std::vector<std::string> result;
    result.reserve(stop - start + 1);
    for (int i = start; i <= stop; ++i) {
        result.push_back(list[static_cast<size_t>(i)]);
    }

    return result;
}

int Database::llen(const std::string& key) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return 0;
    }

    // Type check
    if (store_.count(key)) {
        return -1;  // WRONGTYPE
    }

    auto it = list_store_.find(key);
    if (it == list_store_.end()) {
        return 0;
    }

    return static_cast<int>(it->second.size());
}

std::optional<std::string> Database::lindex(const std::string& key, int index) {
    std::unique_lock lock(mutex_);

    // Lazy expiry
    if (is_expired_unlocked(key)) {
        remove_expired_unlocked(key);
        return std::nullopt;
    }

    auto it = list_store_.find(key);
    if (it == list_store_.end()) {
        return std::nullopt;
    }

    const auto& list = it->second;
    int len = static_cast<int>(list.size());

    // Normalize negative index
    if (index < 0) index = len + index;

    // Bounds check
    if (index < 0 || index >= len) {
        return std::nullopt;
    }

    return list[static_cast<size_t>(index)];
}

// ─── TTL Commands ──────────────────────────────────────────────

bool Database::expire(const std::string& key, int seconds) {
    std::unique_lock lock(mutex_);

    // Can't set TTL on a key that doesn't exist (in either map)
    if (!key_exists_unlocked(key)) {
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

    // Check if the key exists at all (in either map)
    if (!key_exists_unlocked(key)) {
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

    // Check if the key exists (in either map)
    if (!key_exists_unlocked(key)) {
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
            list_store_.erase(key);
            expiry_.erase(key);
        }
    }
}

// ─── Persistence Snapshot ─────────────────────────────────────

DatabaseSnapshot Database::get_snapshot() {
    // shared_lock allows concurrent reads to continue while we copy.
    // We only need a consistent view — we're not modifying anything.
    std::shared_lock lock(mutex_);
    DatabaseSnapshot snap;
    snap.store = store_;
    snap.list_store = list_store_;
    snap.expiry = expiry_;
    return snap;
}

void Database::load_snapshot(const DatabaseSnapshot& snapshot) {
    // unique_lock — we're replacing the entire database
    std::unique_lock lock(mutex_);
    store_ = snapshot.store;
    list_store_ = snapshot.list_store;
    expiry_ = snapshot.expiry;
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
    list_store_.erase(key);
    expiry_.erase(key);
}

} // namespace myredis
