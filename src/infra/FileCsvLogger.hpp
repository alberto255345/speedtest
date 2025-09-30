#pragma once

#include "domain/Logger.hpp"

#include <filesystem>

class FileCsvLogger : public LoggerRepo {
public:
    explicit FileCsvLogger(std::filesystem::path csv) : csv_(std::move(csv)) {}

    void append(const std::string& timestamp,
                const std::string& mac,
                const std::string& ip,
                const std::string& result) override;

private:
    std::filesystem::path csv_;

    void ensureHeader();
};
