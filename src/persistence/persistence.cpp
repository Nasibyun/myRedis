#include "persistence/persistence.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <vector>

namespace myredis {

// ─── CRC32 Lookup Table ───────────────────────────────────────
// Precomputed table for the standard CRC32 polynomial (0xEDB88320).
// This is the same polynomial used by zlib, gzip, PNG, etc.
// Each entry is computed as: crc32(i) for a single byte i.
//
// Why a table? Computing CRC32 bit-by-bit is slow (8 iterations per byte).
// A 256-entry lookup table reduces it to 1 lookup + 2 XORs per byte.
//
static uint32_t generate_crc32_entry(uint32_t index) {
    uint32_t crc = index;
    for (int bit = 0; bit < 8; ++bit) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xEDB88320u;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

// We use a helper to initialize the table since constexpr arrays
// with loops aren't trivial in C++17. This runs once at startup.
struct CRC32TableInit {
    uint32_t table[256];
    CRC32TableInit() {
        for (uint32_t i = 0; i < 256; ++i) {
            table[i] = generate_crc32_entry(i);
        }
    }
};

static const CRC32TableInit crc32_init;
const uint32_t Persistence::crc32_table[256] = {
    // This will be populated by the static initializer above.
    // We need a different approach — let's use the init struct directly.
};

// We'll use the crc32_init.table directly in compute_crc32.

// ─── Binary I/O Helpers ───────────────────────────────────────

void Persistence::write_uint32(std::ofstream& out, uint32_t val) {
    // Write in big-endian (most significant byte first)
    // This ensures the file format is portable across architectures.
    uint8_t bytes[4];
    bytes[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((val >>  8) & 0xFF);
    bytes[3] = static_cast<uint8_t>((val      ) & 0xFF);
    out.write(reinterpret_cast<const char*>(bytes), 4);
}

bool Persistence::read_uint32(std::ifstream& in, uint32_t& val) {
    uint8_t bytes[4];
    if (!in.read(reinterpret_cast<char*>(bytes), 4)) return false;
    val = (static_cast<uint32_t>(bytes[0]) << 24)
        | (static_cast<uint32_t>(bytes[1]) << 16)
        | (static_cast<uint32_t>(bytes[2]) <<  8)
        | (static_cast<uint32_t>(bytes[3])      );
    return true;
}

void Persistence::write_int64(std::ofstream& out, int64_t val) {
    uint8_t bytes[8];
    uint64_t uval = static_cast<uint64_t>(val);
    bytes[0] = static_cast<uint8_t>((uval >> 56) & 0xFF);
    bytes[1] = static_cast<uint8_t>((uval >> 48) & 0xFF);
    bytes[2] = static_cast<uint8_t>((uval >> 40) & 0xFF);
    bytes[3] = static_cast<uint8_t>((uval >> 32) & 0xFF);
    bytes[4] = static_cast<uint8_t>((uval >> 24) & 0xFF);
    bytes[5] = static_cast<uint8_t>((uval >> 16) & 0xFF);
    bytes[6] = static_cast<uint8_t>((uval >>  8) & 0xFF);
    bytes[7] = static_cast<uint8_t>((uval      ) & 0xFF);
    out.write(reinterpret_cast<const char*>(bytes), 8);
}

bool Persistence::read_int64(std::ifstream& in, int64_t& val) {
    uint8_t bytes[8];
    if (!in.read(reinterpret_cast<char*>(bytes), 8)) return false;
    uint64_t uval = (static_cast<uint64_t>(bytes[0]) << 56)
                   | (static_cast<uint64_t>(bytes[1]) << 48)
                   | (static_cast<uint64_t>(bytes[2]) << 40)
                   | (static_cast<uint64_t>(bytes[3]) << 32)
                   | (static_cast<uint64_t>(bytes[4]) << 24)
                   | (static_cast<uint64_t>(bytes[5]) << 16)
                   | (static_cast<uint64_t>(bytes[6]) <<  8)
                   | (static_cast<uint64_t>(bytes[7])      );
    val = static_cast<int64_t>(uval);
    return true;
}

void Persistence::write_string(std::ofstream& out, const std::string& s) {
    write_uint32(out, static_cast<uint32_t>(s.size()));
    if (!s.empty()) {
        out.write(s.data(), static_cast<std::streamsize>(s.size()));
    }
}

bool Persistence::read_string(std::ifstream& in, std::string& s) {
    uint32_t len;
    if (!read_uint32(in, len)) return false;
    s.resize(len);
    if (len > 0) {
        if (!in.read(&s[0], len)) return false;
    }
    return true;
}

// ─── CRC32 Computation ───────────────────────────────────────

uint32_t Persistence::compute_crc32(const std::string& filepath, size_t length) {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return 0;

    uint32_t crc = 0xFFFFFFFFu;
    const size_t CHUNK_SIZE = 4096;
    char buffer[CHUNK_SIZE];
    size_t remaining = length;

    while (remaining > 0 && in) {
        size_t to_read = std::min(remaining, CHUNK_SIZE);
        in.read(buffer, static_cast<std::streamsize>(to_read));
        auto bytes_read = static_cast<size_t>(in.gcount());

        for (size_t i = 0; i < bytes_read; ++i) {
            uint8_t byte = static_cast<uint8_t>(buffer[i]);
            uint8_t index = static_cast<uint8_t>((crc ^ byte) & 0xFF);
            crc = (crc >> 8) ^ crc32_init.table[index];
        }

        remaining -= bytes_read;
    }

    return crc ^ 0xFFFFFFFFu;
}

// ─── Save ─────────────────────────────────────────────────────

bool Persistence::save(const std::string& filepath, Database& db) {
    // Take a snapshot of the database under a lock.
    // This minimizes the time the database is locked — we only need
    // to copy the maps, then we can write to disk without holding the lock.
    DatabaseSnapshot snapshot = db.get_snapshot();

    // Write to a temporary file first, then rename.
    // This prevents data loss if the server crashes mid-write.
    std::string tmp_path = filepath + ".tmp";
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::cerr << "[myredis] ERROR: Cannot open " << tmp_path
                  << " for writing" << std::endl;
        return false;
    }

    auto now = std::chrono::steady_clock::now();

    // Count non-expired entries (both string and list keys)
    uint32_t entry_count = 0;
    for (const auto& [key, value] : snapshot.store) {
        (void)value;
        auto exp_it = snapshot.expiry.find(key);
        if (exp_it != snapshot.expiry.end() && exp_it->second <= now) {
            continue;  // Skip expired keys
        }
        ++entry_count;
    }
    for (const auto& [key, list] : snapshot.list_store) {
        (void)list;
        auto exp_it = snapshot.expiry.find(key);
        if (exp_it != snapshot.expiry.end() && exp_it->second <= now) {
            continue;  // Skip expired keys
        }
        ++entry_count;
    }

    // ── Write header ──
    out.write(MAGIC, MAGIC_LEN);
    write_uint32(out, entry_count);

    // ── Write string entries ──
    for (const auto& [key, value] : snapshot.store) {
        auto exp_it = snapshot.expiry.find(key);
        bool expired = (exp_it != snapshot.expiry.end() && exp_it->second <= now);
        if (expired) continue;

        bool has_ttl = (exp_it != snapshot.expiry.end());

        // Type tag: string
        uint8_t type_tag = TYPE_STRING;
        out.write(reinterpret_cast<const char*>(&type_tag), 1);

        // Flags
        uint8_t flags = has_ttl ? FLAG_HAS_TTL : 0;
        out.write(reinterpret_cast<const char*>(&flags), 1);

        // Key
        write_string(out, key);

        // Value
        write_string(out, value);

        // TTL (remaining milliseconds from now)
        if (has_ttl) {
            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                exp_it->second - now
            ).count();
            write_int64(out, static_cast<int64_t>(remaining_ms));
        }
    }

    // ── Write list entries ──
    for (const auto& [key, list] : snapshot.list_store) {
        auto exp_it = snapshot.expiry.find(key);
        bool expired = (exp_it != snapshot.expiry.end() && exp_it->second <= now);
        if (expired) continue;

        bool has_ttl = (exp_it != snapshot.expiry.end());

        // Type tag: list
        uint8_t type_tag = TYPE_LIST;
        out.write(reinterpret_cast<const char*>(&type_tag), 1);

        // Flags
        uint8_t flags = has_ttl ? FLAG_HAS_TTL : 0;
        out.write(reinterpret_cast<const char*>(&flags), 1);

        // Key
        write_string(out, key);

        // List elements: [element_count][elem1_len][elem1_data]...
        write_uint32(out, static_cast<uint32_t>(list.size()));
        for (const auto& elem : list) {
            write_string(out, elem);
        }

        // TTL (remaining milliseconds from now)
        if (has_ttl) {
            auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                exp_it->second - now
            ).count();
            write_int64(out, static_cast<int64_t>(remaining_ms));
        }
    }

    out.flush();

    if (!out.good()) {
        std::cerr << "[myredis] ERROR: Write failed for " << tmp_path << std::endl;
        out.close();
        std::remove(tmp_path.c_str());
        return false;
    }

    out.close();

    // ── Compute and append CRC32 ──
    // Reopen the temp file to compute CRC of everything written so far
    std::ifstream check_in(tmp_path, std::ios::binary | std::ios::ate);
    if (!check_in) {
        std::cerr << "[myredis] ERROR: Cannot reopen " << tmp_path
                  << " for CRC computation" << std::endl;
        std::remove(tmp_path.c_str());
        return false;
    }
    size_t data_size = static_cast<size_t>(check_in.tellg());
    check_in.close();

    uint32_t crc = compute_crc32(tmp_path, data_size);

    // Append CRC32 to the file
    std::ofstream append_out(tmp_path, std::ios::binary | std::ios::app);
    if (!append_out) {
        std::remove(tmp_path.c_str());
        return false;
    }
    write_uint32(append_out, crc);
    append_out.close();

    // ── Atomic rename ──
    // Remove old file, rename tmp → final.
    // On Windows, std::rename fails if the target exists, so remove first.
    std::remove(filepath.c_str());
    if (std::rename(tmp_path.c_str(), filepath.c_str()) != 0) {
        std::cerr << "[myredis] ERROR: Failed to rename " << tmp_path
                  << " to " << filepath << std::endl;
        std::remove(tmp_path.c_str());
        return false;
    }

    std::cout << "[myredis] Database saved to " << filepath
              << " (" << entry_count << " keys)" << std::endl;
    return true;
}

// ─── Load ─────────────────────────────────────────────────────

bool Persistence::load(const std::string& filepath, Database& db) {
    std::ifstream in(filepath, std::ios::binary | std::ios::ate);
    if (!in) {
        // File doesn't exist — that's fine, not an error
        return false;
    }

    // Get file size for CRC validation
    size_t file_size = static_cast<size_t>(in.tellg());
    if (file_size < MAGIC_LEN + 4 + 4) {
        // Too small: needs at least magic(9) + count(4) + crc(4)
        std::cerr << "[myredis] ERROR: " << filepath
                  << " is too small to be a valid dump file" << std::endl;
        return false;
    }

    // ── Validate CRC32 ──
    size_t data_size = file_size - 4;  // Everything except the trailing CRC
    in.seekg(0);

    uint32_t computed_crc = compute_crc32(filepath, data_size);

    // Read the stored CRC (last 4 bytes)
    in.seekg(static_cast<std::streamoff>(data_size));
    uint32_t stored_crc;
    if (!read_uint32(in, stored_crc)) {
        std::cerr << "[myredis] ERROR: Cannot read CRC from " << filepath << std::endl;
        return false;
    }

    if (computed_crc != stored_crc) {
        std::cerr << "[myredis] ERROR: CRC mismatch in " << filepath
                  << " (file is corrupt)" << std::endl;
        return false;
    }

    // ── Read header ──
    in.seekg(0);
    char magic[MAGIC_LEN];
    if (!in.read(magic, MAGIC_LEN) || std::memcmp(magic, MAGIC, MAGIC_LEN) != 0) {
        std::cerr << "[myredis] ERROR: Invalid magic header in " << filepath << std::endl;
        return false;
    }

    uint32_t entry_count;
    if (!read_uint32(in, entry_count)) {
        std::cerr << "[myredis] ERROR: Cannot read entry count from " << filepath << std::endl;
        return false;
    }

    // ── Read entries ──
    auto now = std::chrono::steady_clock::now();
    DatabaseSnapshot snapshot;

    for (uint32_t i = 0; i < entry_count; ++i) {
        // Type tag
        uint8_t type_tag;
        if (!in.read(reinterpret_cast<char*>(&type_tag), 1)) {
            std::cerr << "[myredis] ERROR: Unexpected EOF at entry " << i << std::endl;
            return false;
        }

        // Flags
        uint8_t flags;
        if (!in.read(reinterpret_cast<char*>(&flags), 1)) {
            std::cerr << "[myredis] ERROR: Unexpected EOF at entry " << i << std::endl;
            return false;
        }

        bool has_ttl = (flags & FLAG_HAS_TTL) != 0;

        // Key
        std::string key;
        if (!read_string(in, key)) {
            std::cerr << "[myredis] ERROR: Cannot read key at entry " << i << std::endl;
            return false;
        }

        if (type_tag == TYPE_STRING) {
            // ── String value ──
            std::string value;
            if (!read_string(in, value)) {
                std::cerr << "[myredis] ERROR: Cannot read value at entry " << i << std::endl;
                return false;
            }

            // TTL
            int64_t ttl_ms = 0;
            if (has_ttl) {
                if (!read_int64(in, ttl_ms)) {
                    std::cerr << "[myredis] ERROR: Cannot read TTL at entry " << i << std::endl;
                    return false;
                }

                // If the TTL has already elapsed since we saved, skip this key
                if (ttl_ms <= 0) continue;
            }

            snapshot.store[key] = value;

            if (has_ttl) {
                snapshot.expiry[key] = now + std::chrono::milliseconds(ttl_ms);
            }
        } else if (type_tag == TYPE_LIST) {
            // ── List value ──
            uint32_t elem_count;
            if (!read_uint32(in, elem_count)) {
                std::cerr << "[myredis] ERROR: Cannot read list element count at entry " << i << std::endl;
                return false;
            }

            std::deque<std::string> list;
            for (uint32_t j = 0; j < elem_count; ++j) {
                std::string elem;
                if (!read_string(in, elem)) {
                    std::cerr << "[myredis] ERROR: Cannot read list element " << j
                              << " at entry " << i << std::endl;
                    return false;
                }
                list.push_back(std::move(elem));
            }

            // TTL
            int64_t ttl_ms = 0;
            if (has_ttl) {
                if (!read_int64(in, ttl_ms)) {
                    std::cerr << "[myredis] ERROR: Cannot read TTL at entry " << i << std::endl;
                    return false;
                }

                if (ttl_ms <= 0) continue;  // Skip expired
            }

            snapshot.list_store[key] = std::move(list);

            if (has_ttl) {
                snapshot.expiry[key] = now + std::chrono::milliseconds(ttl_ms);
            }
        } else {
            // Unknown type — skip gracefully for forward compatibility
            std::cerr << "[myredis] WARNING: Unknown type tag " << static_cast<int>(type_tag)
                      << " at entry " << i << ", skipping" << std::endl;
            return false;
        }
    }

    // ── Load into database ──
    db.load_snapshot(snapshot);

    std::cout << "[myredis] Loaded " << snapshot.store.size() + snapshot.list_store.size()
              << " keys from " << filepath << std::endl;
    return true;
}

} // namespace myredis
