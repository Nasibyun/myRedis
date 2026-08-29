#pragma once
///
/// database.h — Thread-safe in-memory key-value store with TTL support
///
/// Uses std::unordered_map for O(1) average-case GET/SET/DEL.
/// Protected by std::shared_mutex to allow concurrent reads
/// while serializing writes.
///
/// Keys can optionally have a time-to-live (TTL). Expired keys
/// are removed via two strategies:
///   1. Lazy expiry: checked on every read (GET, EXISTS, etc.)
///   2. Active expiry: a background thread periodically purges expired keys
///

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace myredis {

class Database {
public:
    Database();
    ~Database();

    // ─── Core Commands ─────────────────────────────────────────

    /// SET key value — store a key-value pair (overwrites if exists)
    /// Also clears any existing TTL on the key (same behavior as Redis).
    void set(const std::string& key, const std::string& value);

    /// GET key — returns the value, or nullopt if key doesn't exist (or is expired)
    std::optional<std::string> get(const std::string& key);

    /// DEL key [key ...] — delete one or more keys, returns count deleted
    int del(const std::vector<std::string>& keys);

    /// EXISTS key [key ...] — returns how many of the given keys exist (excluding expired)
    int exists(const std::vector<std::string>& keys);

    /// KEYS — returns all keys in the database (excluding expired)
    std::vector<std::string> keys();

    /// DBSIZE — returns the number of keys (excluding expired)
    size_t dbsize();

    /// FLUSHDB — delete all keys and their TTLs
    void flushdb();

    // ─── TTL Commands ──────────────────────────────────────────

    /// EXPIRE key seconds — set a TTL on an existing key.
    /// Returns true if the TTL was set, false if the key doesn't exist.
    bool expire(const std::string& key, int seconds);

    /// TTL key — returns the remaining TTL in seconds.
    ///   Returns -1 if the key exists but has no TTL.
    ///   Returns -2 if the key does not exist (or is expired).
    int ttl(const std::string& key);

    /// PERSIST key — remove the TTL from a key.
    /// Returns true if a TTL was removed, false otherwise.
    bool persist(const std::string& key);

    // ─── Background Expiry ─────────────────────────────────────

    /// Start the background expiry thread.
    /// Should be called once before the server starts accepting connections.
    void start_expiry_thread();

    /// Stop the background expiry thread.
    /// Should be called during shutdown.
    void stop_expiry_thread();

private:
    // shared_mutex allows multiple concurrent readers (shared_lock)
    // but only one writer at a time (unique_lock).
    // 'mutable' lets us lock the mutex even in const methods (like get).
    mutable std::shared_mutex mutex_;

    // The actual data store. Keys and values are both strings,
    // just like Redis.
    std::unordered_map<std::string, std::string> store_;

    // Expiry map: only keys WITH a TTL appear here.
    // The value is the absolute time point at which the key expires.
    // Keys without a TTL are NOT in this map (they live forever).
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry_;

    // ─── Background expiry thread ──────────────────────────────
    std::thread expiry_thread_;
    std::atomic<bool> expiry_running_{false};
    std::mutex expiry_cv_mutex_;             // protects the condition variable
    std::condition_variable expiry_cv_;       // used to wake the thread for shutdown

    /// The background expiry loop. Wakes every 100ms, samples keys
    /// from the expiry map, and deletes any that have passed their deadline.
    void expiry_loop();

    // ─── Helpers ───────────────────────────────────────────────

    /// Check if a key is expired. Does NOT lock — caller must hold a lock.
    bool is_expired_unlocked(const std::string& key) const;

    /// Remove an expired key from both store_ and expiry_. Does NOT lock.
    void remove_expired_unlocked(const std::string& key);
};

} // namespace myredis
