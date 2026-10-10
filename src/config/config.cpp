#include "config/config.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace myredis {

Config::Config() {
    set_defaults();
}

void Config::set_defaults() {
    config_["port"] = "6399";
    config_["bind"] = "0.0.0.0";
    config_["requirepass"] = "";
    config_["save"] = "0";
    config_["loglevel"] = "notice";
    config_["maxmemory"] = "0";
}

bool Config::load_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;  // File not found is not an error
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        ++line_num;

        // Trim leading whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;  // Empty line
        line = line.substr(start);

        // Skip comments
        if (line[0] == '#') continue;

        // Parse "key value" pairs
        std::istringstream iss(line);
        std::string key, value;
        if (!(iss >> key)) continue;

        // Rest of the line is the value (allows spaces in values)
        std::getline(iss >> std::ws, value);

        // Trim trailing whitespace from value
        size_t end = value.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            value = value.substr(0, end + 1);
        }

        config_[key] = value;
    }

    return true;
}

std::string Config::get(const std::string& key) const {
    auto it = config_.find(key);
    if (it == config_.end()) return "";
    return it->second;
}

void Config::set(const std::string& key, const std::string& value) {
    config_[key] = value;
}

int Config::port() const {
    try {
        return std::stoi(get("port"));
    } catch (...) {
        return 6399;
    }
}

std::string Config::bind_addr() const {
    std::string addr = get("bind");
    return addr.empty() ? "0.0.0.0" : addr;
}

std::string Config::password() const {
    return get("requirepass");
}

int Config::save_interval() const {
    try {
        return std::stoi(get("save"));
    } catch (...) {
        return 0;
    }
}

} // namespace myredis
