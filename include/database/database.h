#pragma once
///
/// database.h — Thread-safe in-memory key-value store
///
/// Uses std::unordered_map for O(1) average-case GET/SET/DEL.
/// Protected by std::shared_mutex to allow concurrent reads
/// while serializing writes.
///

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>
#include <shared_mutex>

namespace myredis {

class Database {
public:
    /// SET key value — store a key-value pair (overwrites if exists)
    void set(const std::string& key, const std::string& value);

    /// GET key — returns the value, or nullopt if key doesn't exist
    std::optional<std::string> get(const std::string& key) const;

    /// DEL key [key ...] — delete one or more keys, returns count deleted
    int del(const std::vector<std::string>& keys);

    /// EXISTS key [key ...] — returns how many of the given keys exist
    int exists(const std::vector<std::string>& keys) const;

    /// KEYS — returns all keys in the database
    std::vector<std::string> keys() const;

    /// DBSIZE — returns the number of keys
    size_t dbsize() const;

    /// FLUSHDB — delete all keys
    void flushdb();

private:
    // shared_mutex allows multiple concurrent readers (shared_lock)
    // but only one writer at a time (unique_lock).
    // 'mutable' lets us lock the mutex even in const methods (like get).
    mutable std::shared_mutex mutex_;

    // The actual data store. Keys and values are both strings,
    // just like Redis.
    std::unordered_map<std::string, std::string> store_;
};

} // namespace myredis
