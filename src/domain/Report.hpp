#pragma once

#include <nlohmann/json.hpp>
#include <string>

struct Report {
    std::string label;
    std::string timestamp;
    std::string mac;
    std::string ip;
    bool ping_ok = false;
    nlohmann::json ookla;
    nlohmann::json js_data;
    std::string body_text;
    std::string log_result;
};
