#include "infra/OoklaSpeedTester.hpp"

#include <fstream>

nlohmann::json OoklaSpeedTester::runOokla(const std::filesystem::path& saveJson) {
    std::string out;
    int rc = runCmdCapture("speedtest --accept-license --accept-gdpr -f json", out);
    if (rc != 0) return nlohmann::json{{"error", {{"rc", rc}, {"stderr", out}}}};
    try {
        auto data = nlohmann::json::parse(out);
        std::ofstream(saveJson) << data.dump(2);
        return data;
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("parse json: ") + e.what()}, {"raw", out.substr(0, 4000)}};
    }
}

nlohmann::json OoklaSpeedTester::runJs(const std::filesystem::path& jsPath,
                                       const std::filesystem::path& resultJsonPath) {
    std::string out;
    int rc = runCmdCapture("node '" + jsPath.string() + "'", out);
    if (rc != 0) return nlohmann::json{{"error", {{"rc", rc}, {"stderr", out}}}};
    try {
        std::ifstream in(resultJsonPath);
        nlohmann::json data;
        in >> data;
        return data;
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("read ") + resultJsonPath.filename().string() + ": " + e.what()}};
    }
}
