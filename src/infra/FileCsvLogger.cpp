#include "infra/FileCsvLogger.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

void FileCsvLogger::ensureHeader() {
    std::error_code ec;
    if (!std::filesystem::exists(csv_, ec)) {
        std::ofstream out(csv_);
        out << "timestamp;mac;ip;resultado\n";
    }
}

void FileCsvLogger::append(const std::string& ts,
                           const std::string& mac,
                           const std::string& ip,
                           const std::string& result) {
    ensureHeader();
    std::string safe = result;
    std::replace(safe.begin(), safe.end(), '\n', ' ');
    std::replace(safe.begin(), safe.end(), '\r', ' ');
    std::replace(safe.begin(), safe.end(), ';', ',');
    std::ofstream out(csv_, std::ios::app);
    out << ts << ';' << mac << ';' << ip << ';' << safe << "\n";
}
