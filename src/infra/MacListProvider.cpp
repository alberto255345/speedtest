#include "infra/MacListProvider.hpp"

#include <algorithm>
#include <fstream>
#include <system_error>
#include <vector>

namespace {
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
}  // namespace

std::optional<std::string> MacListProvider::next() {
    std::error_code ec;
    if (!std::filesystem::exists(list_, ec)) return std::nullopt;

    std::ifstream in(list_);
    if (!in.is_open()) return std::nullopt;

    std::vector<std::string> macs;
    for (std::string line; std::getline(in, line);) {
        auto s = trim(line);
        if (!s.empty()) macs.push_back(s);
    }
    if (macs.empty()) return std::nullopt;

    long idxv = 0;
    if (std::filesystem::exists(idx_, ec)) {
        std::ifstream ii(idx_);
        long v = 0;
        if (ii >> v) idxv = v;
    }

    std::string mac = macs[static_cast<size_t>(idxv % macs.size())];
    {
        std::ofstream oi(idx_, std::ios::trunc);
        oi << ((idxv + 1) % macs.size());
    }
    return mac;
}
