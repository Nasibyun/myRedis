#pragma once
///
/// database.h — Thread-safe in-memory key-value store with TTL support
///
/// Uses std::unordered_map for O(1) average-case GET/SET/DEL.
/// Protected by std::shared_mutex to allow concurrent reads
/// while serializing writes.
///
/// Supports two value types:
///   - Strings: stored in store_ (SET/GET)
///   - Lists: stored in list_store_ as std::deque (LPUSH/RPUSH/LPOP/etc.)
///
/// A key exists in at most one map. Type mismatches return WRONGTYPE errors.
///
/// Keys can optionally have a time-to-live (TTL). Expired keys
/// are removed via two strategies:
///   1. Lazy expiry: checked on every read (GET, EXISTS, etc.)
///   2. Active expiry: a background thread periodically purges expired keys
///

#include <string>
#include <unordered_map>
#include <vector>
#include <deque>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace myredis {

// Forward declaration — defined in persistence.h
struct DatabaseSnapshot;

/// The type of value stored under a key.
/// Used for type checking — you can't GET a list or LPUSH a string.
enum class KeyType {
    NONE,    // Key does not exist
    STRING,  // Key holds a string value (store_)
    LIST     // Key holds a list value (list_store_)
};

class Database {
public:
    Database();
    ~Database();

    // ─── Type Checking ─────────────────────────────────────────

    /// Returns the type of a key (NONE, STRING, or LIST).
    /// Does NOT perform lazy expiry — caller should check expiry separately.
    /// Does NOT lock — caller must hold a lock.
    KeyType key_type_unlocked(const std::string& key) const;

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

    // ─── List Commands ─────────────────────────────────────────

    /// LPUSH key value [value ...] — push values to the head of a list.
    /// Creates the list if it doesn't exist. Returns the new list length.
    /// Returns -1 if the key holds a non-list value (WRONGTYPE).
    int lpush(const std::string& key, const std::vector<std::string>& values);

    /// RPUSH key value [value ...] — push values to the tail of a list.
    /// Creates the list if it doesn't exist. Returns the new list length.
    /// Returns -1 if the key holds a non-list value (WRONGTYPE).
    int rpush(const std::string& key, const std::vector<std::string>& values);

    /// LPOP key — remove and return the head element.
    /// Returns nullopt if the key doesn't exist. Returns "" sentinel with
    /// special meaning if WRONGTYPE (caller checks key_type for this).
    std::optional<std::string> lpop(const std::string& key);

    /// RPOP key — remove and return the tail element.
    /// Returns nullopt if the key doesn't exist.
    std::optional<std::string> rpop(const std::string& key);

    /// LRANGE key start stop — return elements from start to stop (inclusive).
    /// Negative indices count from end (-1 = last element).
    /// Returns empty vector if key doesn't exist.
    std::vector<std::string> lrange(const std::string& key, int start, int stop);

    /// LLEN key — return the length of the list.
    /// Returns 0 if key doesn't exist, -1 if WRONGTYPE.
    int llen(const std::string& key);

    /// LINDEX key index — return element at the given index.
    /// Negative indices count from end. Returns nullopt if out of range.
    std::optional<std::string> lindex(const std::string& key, int index);

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

    // ─── Persistence ───────────────────────────────────────────

    /// Get a consistent snapshot of the database under a shared lock.
    /// Used by Persistence::save() to minimize lock hold time.
    DatabaseSnapshot get_snapshot();

    /// Replace the entire database with a snapshot (used by Persistence::load).
    void load_snapshot(const DatabaseSnapshot& snapshot);

private:
    // shared_mutex allows multiple concurrent readers (shared_lock)
    // but only one writer at a time (unique_lock).
    // 'mutable' lets us lock the mutex even in const methods (like get).
    mutable std::shared_mutex mutex_;

    // The actual data store for string values.
    // Keys and values are both strings, just like Redis.
    std::unordered_map<std::string, std::string> store_;

    // List data store. Keys map to deques of strings.
    // A key exists in AT MOST ONE of store_ or list_store_ (never both).
    // std::deque gives O(1) push/pop at both ends — perfect for Redis lists.
    std::unordered_map<std::string, std::deque<std::string>> list_store_;

    // Expiry map: only keys WITH a TTL appear here.
    // The value is the absolute time point at which the key expires.
    // Keys without a TTL are NOT in this map (they live forever).
    // Works for BOTH string and list keys.
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

    /// Remove an expired key from store_, list_store_, and expiry_.
    /// Does NOT lock — caller must hold a lock.
    void remove_expired_unlocked(const std::string& key);

    /// Check if a key exists in either store_ or list_store_.
    /// Does NOT check expiry. Does NOT lock.
    bool key_exists_unlocked(const std::string& key) const;
};

} // namespace myredis
