#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

struct SpeedTester {
    virtual ~SpeedTester() = default;
    virtual nlohmann::json runOokla(const std::filesystem::path& saveJson) = 0;
    virtual nlohmann::json runJs(const std::filesystem::path& jsPath,
                                 const std::filesystem::path& resultJsonPath) = 0;
};
