#include "util/Env.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace {
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
}  // namespace

void loadDotenv(const std::filesystem::path& dotenvPath) {
    std::error_code ec;
    if (!std::filesystem::exists(dotenvPath, ec)) return;

    std::ifstream in(dotenvPath);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        auto pos = s.find('=');
        if (pos == std::string::npos) continue;
        std::string key = trim(s.substr(0, pos));
        std::string val = trim(s.substr(pos + 1));
        if ((!val.empty() && val.front() == '"' && val.back() == '"') ||
            (!val.empty() && val.front() == '\'' && val.back() == '\'')) {
            val = val.substr(1, val.size() - 2);
        }
        setenv(key.c_str(), val.c_str(), 0);
    }
}

std::string getenvOr(const std::string& key, const std::string& defVal) {
    const char* v = std::getenv(key.c_str());
    return v ? std::string(v) : defVal;
}
