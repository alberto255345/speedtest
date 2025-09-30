#pragma once

#include <string>

struct LoggerRepo {
    virtual ~LoggerRepo() = default;
    virtual void append(const std::string& timestamp,
                        const std::string& mac,
                        const std::string& ip,
                        const std::string& result) = 0;
};
