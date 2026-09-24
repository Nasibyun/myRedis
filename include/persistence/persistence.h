#pragma once
///
/// persistence.h — RDB-style binary persistence
///
/// Serializes the entire database to a binary file (dump.mrdb)
/// and deserializes it back on startup.
///
/// File format (v1 — strings only):
///   [9 bytes]  Magic header: "MYREDIS01"
///   [4 bytes]  Entry count (uint32, big-endian)
///   For each entry:
///     [1 byte]   Type tag (0x00 = string, 0x01 = list)
///     [1 byte]   Flags: bit 0 = has_ttl
///     [4 bytes]  Key length (uint32, big-endian)
///     [N bytes]  Key data
///     --- if string (type 0x00): ---
///       [4 bytes]  Value length (uint32, big-endian)
///       [N bytes]  Value data
///     --- if list (type 0x01): ---
///       [4 bytes]  Element count (uint32, big-endian)
///       For each element:
///         [4 bytes]  Element length (uint32, big-endian)
///         [N bytes]  Element data
///     --- if has_ttl: ---
///       [8 bytes]  TTL remaining in milliseconds (int64, big-endian)
///   [4 bytes]  CRC32 checksum of all preceding bytes
///

#include "database/database.h"
#include <string>
#include <cstdint>
#include <deque>
#include <fstream>

namespace myredis {

/// Snapshot of the database state at a point in time.
/// Captured under a lock so persistence doesn't block the DB for long.
struct DatabaseSnapshot {
    std::unordered_map<std::string, std::string> store;
    std::unordered_map<std::string, std::deque<std::string>> list_store;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry;
};

class Persistence {
public:
    static constexpr const char* MAGIC = "MYREDIS01";
    static constexpr size_t MAGIC_LEN = 9;
    static constexpr uint8_t TYPE_STRING = 0x00;
    static constexpr uint8_t TYPE_LIST   = 0x01;
    static constexpr uint8_t FLAG_HAS_TTL = 0x01;

    /// Save the current database state to a file.
    /// Returns true on success, false on I/O error.
    static bool save(const std::string& filepath, Database& db);

    /// Load a database from a file, replacing current contents.
    /// Returns true on success, false on error (file not found, corrupt, bad CRC).
    static bool load(const std::string& filepath, Database& db);

private:
    // ─── Binary I/O helpers ────────────────────────────────────

    /// Write a uint32 in big-endian (network byte order)
    static void write_uint32(std::ofstream& out, uint32_t val);

    /// Read a uint32 in big-endian
    static bool read_uint32(std::ifstream& in, uint32_t& val);

    /// Write an int64 in big-endian
    static void write_int64(std::ofstream& out, int64_t val);

    /// Read an int64 in big-endian
    static bool read_int64(std::ifstream& in, int64_t& val);

    /// Write a length-prefixed string
    static void write_string(std::ofstream& out, const std::string& s);

    /// Read a length-prefixed string
    static bool read_string(std::ifstream& in, std::string& s);

    // ─── CRC32 ────────────────────────────────────────────────

    /// Compute CRC32 checksum of file contents (excluding last 4 bytes)
    static uint32_t compute_crc32(const std::string& filepath, size_t length);

    /// CRC32 lookup table (generated at compile time)
    static const uint32_t crc32_table[256];
};

} // namespace myredis
