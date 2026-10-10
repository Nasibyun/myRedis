#pragma once
///
/// config.h — Server configuration manager
///
/// Loads settings from a myredis.conf file on startup and provides
/// runtime CONFIG GET / CONFIG SET support.
///
/// Config file format (key-value pairs, one per line):
///   port 6399
///   bind 0.0.0.0
///   requirepass mysecretpassword
///   save 300
///   loglevel notice
///   maxmemory 0
///
/// Lines starting with # are comments. Empty lines are ignored.
///

#include <string>
#include <unordered_map>

namespace myredis {

class Config {
public:
    Config();

    /// Load configuration from a file.
    /// Returns true if file was found and parsed (missing file is not an error).
    bool load_file(const std::string& filepath);

    /// Get a config value. Returns empty string if not set.
    std::string get(const std::string& key) const;

    /// Set a config value at runtime.
    void set(const std::string& key, const std::string& value);

    /// Get the port (default: 6399)
    int port() const;

    /// Get the bind address (default: "0.0.0.0")
    std::string bind_addr() const;

    /// Get the password (empty = no auth required)
    std::string password() const;

    /// Get auto-save interval in seconds (0 = disabled)
    int save_interval() const;

    /// Get all config entries as key-value pairs
    const std::unordered_map<std::string, std::string>& all() const { return config_; }

private:
    std::unordered_map<std::string, std::string> config_;

    /// Set default values
    void set_defaults();
};

} // namespace myredis
