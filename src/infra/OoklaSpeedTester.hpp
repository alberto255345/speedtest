#pragma once

#include "domain/SpeedTester.hpp"
#include "util/Process.hpp"

class OoklaSpeedTester : public SpeedTester {
public:
    nlohmann::json runOokla(const std::filesystem::path& saveJson) override;
    nlohmann::json runJs(const std::filesystem::path& jsPath,
                         const std::filesystem::path& resultJsonPath) override;
};
