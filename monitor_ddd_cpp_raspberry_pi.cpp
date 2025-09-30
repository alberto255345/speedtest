# Project: monitor-ddd-cpp (Raspberry Pi)
# Estrutura DDD + Clean
# ├── CMakeLists.txt
# ├── Makefile
# ├── .env (exemplo)
# ├── mac.txt
# ├── src/
# │   ├── main.cpp
# │   ├── app/MonitorService.hpp
# │   ├── app/MonitorService.cpp
# │   ├── domain/Report.hpp
# │   ├── domain/NetworkAdapter.hpp
# │   ├── domain/SpeedTester.hpp
# │   ├── domain/Relay.hpp
# │   ├── domain/EmailSender.hpp
# │   ├── domain/Logger.hpp
# │   ├── domain/MacProvider.hpp
# │   ├── infra/LinuxNetworkAdapter.hpp
# │   ├── infra/LinuxNetworkAdapter.cpp
# │   ├── infra/OoklaSpeedTester.hpp
# │   ├── infra/OoklaSpeedTester.cpp
# │   ├── infra/JsSpeedTester.hpp
# │   ├── infra/JsSpeedTester.cpp
# │   ├── infra/WiringPiRelay.hpp
# │   ├── infra/WiringPiRelay.cpp
# │   ├── infra/CurlEmailSender.hpp
# │   ├── infra/CurlEmailSender.cpp
# │   ├── infra/FileCsvLogger.hpp
# │   ├── infra/FileCsvLogger.cpp
# │   ├── infra/MacListProvider.hpp
# │   ├── infra/MacListProvider.cpp
# │   ├── util/Process.hpp
# │   ├── util/Process.cpp
# │   ├── util/Env.hpp
# │   ├── util/Env.cpp
# │   ├── util/Time.hpp
# │   └── util/Time.cpp

########################################
# CMakeLists.txt
########################################
cmake_minimum_required(VERSION 3.16)
project(monitor_ddd_cpp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(JSON REQUIRED nlohmann_json)
# wiringPi ainda existe nos repositórios do RPi OS
pkg_check_modules(WIRINGPI REQUIRED wiringPi)

include_directories(
  ${CURL_INCLUDE_DIRS}
  ${JSON_INCLUDE_DIRS}
  ${WIRINGPI_INCLUDE_DIRS}
  src
)

file(GLOB SRC
  src/main.cpp
  src/app/*.cpp
  src/infra/*.cpp
  src/util/*.cpp
)

add_executable(monitor ${SRC})
target_link_libraries(monitor
  ${CURL_LIBRARIES}
  ${WIRINGPI_LIBRARIES}
)

########################################
# Makefile (alternativo simples ao CMake)
########################################
CXX=g++
CXXFLAGS=-std=c++17 -O2 -Wall -Wextra -Isrc `pkg-config --cflags libcurl wiringPi`
LDFLAGS=`pkg-config --libs libcurl wiringPi`
SRC=$(shell find src -name '*.cpp')
OBJ=$(SRC:.cpp=.o)

all: monitor

monitor: $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) monitor

########################################
# .env (exemplo)
########################################
# NET_IFACE=wlan0
# RELAY_PIN=17
# EMAIL_USER=seu_email@dominio
# EMAIL_PASS=sua_senha
# EMAIL_TO=destinatario@dominio
# SMTP_SERVER=smtp.gmail.com
# SMTP_PORT=587
# EMAIL_USE_SSL=0

########################################
# src/domain/Report.hpp
########################################
#pragma once
#include <string>
#include <nlohmann/json.hpp>

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

########################################
# src/domain/NetworkAdapter.hpp
########################################
#pragma once
#include <string>
#include <optional>

struct NetworkAdapter {
    virtual ~NetworkAdapter() = default;
    virtual bool applyMac(const std::string& mac) = 0;          // tenta nmcli e fallback ip link
    virtual bool waitConnectivity(int timeout_sec) = 0;          // ping até timeout
    virtual bool pingOk() = 0;
    virtual std::optional<std::string> getIp() = 0;
};

########################################
# src/domain/SpeedTester.hpp
########################################
#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>

struct SpeedTester {
    virtual ~SpeedTester() = default;
    virtual nlohmann::json runOokla(const std::filesystem::path& saveJson) = 0;
    virtual nlohmann::json runJs(const std::filesystem::path& jsPath,
                                 const std::filesystem::path& resultJsonPath) = 0;
};

########################################
# src/domain/Relay.hpp
########################################
#pragma once

struct Relay {
    virtual ~Relay() = default;
    virtual bool pulse(double seconds, bool active_high = true) = 0;
};

########################################
# src/domain/EmailSender.hpp
########################################
#pragma once
#include <string>
#include <vector>
#include <filesystem>

struct EmailSender {
    virtual ~EmailSender() = default;
    virtual bool send(const std::string& subject,
                      const std::string& body,
                      const std::vector<std::filesystem::path>& attachments) = 0;
};

########################################
# src/domain/Logger.hpp
########################################
#pragma once
#include <string>

struct LoggerRepo {
    virtual ~LoggerRepo() = default;
    virtual void append(const std::string& timestamp,
                        const std::string& mac,
                        const std::string& ip,
                        const std::string& result) = 0;
};

########################################
# src/domain/MacProvider.hpp
########################################
#pragma once
#include <string>
#include <optional>

struct MacProvider {
    virtual ~MacProvider() = default;
    virtual std::optional<std::string> next() = 0; // sequencial circular
};

########################################
# src/util/Process.hpp
########################################
#pragma once
#include <string>

int runCmdCapture(const std::string& cmd, std::string& output);
bool hasCmd(const std::string& name);

########################################
# src/util/Process.cpp
########################################
#include "util/Process.hpp"
#include <cstdio>
#include <string>
#include <sys/wait.h>

int runCmdCapture(const std::string& cmd, std::string& output) {
    std::string full = cmd + " 2>&1";
    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe) return -1;
    char buf[4096];
    output.clear();
    while (fgets(buf, sizeof buf, pipe)) output += buf;
    int rc = pclose(pipe);
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
}

bool hasCmd(const std::string& name) {
    std::string out;
    return runCmdCapture("which " + name, out) == 0;
}

########################################
# src/util/Env.hpp
########################################
#pragma once
#include <string>
#include <filesystem>

void loadDotenv(const std::filesystem::path& dotenvPath);
std::string getenvOr(const std::string& key, const std::string& defVal);

########################################
# src/util/Env.cpp
########################################
#include "util/Env.hpp"
#include <cstdlib>
#include <fstream>
#include <algorithm>

static std::string trim(const std::string &s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

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
        std::string val = trim(s.substr(pos+1));
        if ((!val.empty() && val.front()=='"' && val.back()=='"') ||
            (!val.empty() && val.front()=='\'' && val.back()=='\')) {
            val = val.substr(1, val.size()-2);
        }
        setenv(key.c_str(), val.c_str(), 0); // não sobrescreve se já existe
    }
}

std::string getenvOr(const std::string& key, const std::string& defVal) {
    const char* v = std::getenv(key.c_str());
    return v ? std::string(v) : defVal;
}

########################################
# src/util/Time.hpp
########################################
#pragma once
#include <string>

std::string nowStr();

########################################
# src/util/Time.cpp
########################################
#include "util/Time.hpp"
#include <ctime>

std::string nowStr() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buff[32];
    std::strftime(buff, sizeof(buff), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buff);
}

########################################
# src/infra/LinuxNetworkAdapter.hpp
########################################
#pragma once
#include "domain/NetworkAdapter.hpp"
#include "util/Process.hpp"
#include <string>
#include <optional>

class LinuxNetworkAdapter : public NetworkAdapter {
public:
    explicit LinuxNetworkAdapter(std::string iface) : iface_(std::move(iface)) {}
    bool applyMac(const std::string& mac) override;
    bool waitConnectivity(int timeout_sec) override;
    bool pingOk() override;
    std::optional<std::string> getIp() override;
private:
    std::string iface_;
    bool nmcliSetClonedMac(const std::string& mac);
    bool iplinkSetMac(const std::string& mac);
};

########################################
# src/infra/LinuxNetworkAdapter.cpp
########################################
#include "infra/LinuxNetworkAdapter.hpp"
#include <thread>
#include <chrono>

bool LinuxNetworkAdapter::nmcliSetClonedMac(const std::string& mac) {
    if (!hasCmd("nmcli")) return false;
    std::string out;
    if (runCmdCapture("nmcli -t -f NAME,DEVICE connection show --active", out) != 0) return false;
    std::string conn;
    {
        std::istringstream iss(out);
        std::string line;
        while (std::getline(iss, line)) {
            auto pos = line.find(':');
            if (pos == std::string::npos) continue;
            auto name = line.substr(0, pos);
            auto dev  = line.substr(pos+1);
            if (dev == iface_) { conn = name; break; }
        }
    }
    if (conn.empty()) return false;

    std::string devinfo;
    runCmdCapture("nmcli -t -f GENERAL.TYPE device show " + iface_, devinfo);
    std::string prop = devinfo.find("wifi") != std::string::npos
        ? "802-11-wireless.cloned-mac-address"
        : "802-ethernet.cloned-mac-address";

    runCmdCapture("nmcli connection modify '" + conn + "' " + prop + " " + mac, out);
    runCmdCapture("nmcli connection down '" + conn + "'", out);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    runCmdCapture("nmcli connection up '" + conn + "'", out);
    return true;
}

bool LinuxNetworkAdapter::iplinkSetMac(const std::string& mac) {
    std::string out;
    runCmdCapture("ip link set " + iface_ + " down", out);
    int rc = runCmdCapture("ip link set dev " + iface_ + " address " + mac, out);
    runCmdCapture("ip link set " + iface_ + " up", out);
    if (rc != 0) {
        fprintf(stderr, "\xE2\x9A\xA0\xEF\xB8\x8F  ip link set dev %s address %s falhou: %s\n",
                iface_.c_str(), mac.c_str(), out.c_str());
    }
    return rc == 0;
}

bool LinuxNetworkAdapter::applyMac(const std::string& mac) {
    printf("\xF0\x9F\x94\x81 Trocando MAC em %s para %s ...\n", iface_.c_str(), mac.c_str());
    bool ok = nmcliSetClonedMac(mac) || iplinkSetMac(mac);
    if (!ok) {
        printf("\xE2\x9D\x8C Falha ao aplicar MAC.\n");
        return false;
    }
    if (waitConnectivity(90)) {
        printf("\xE2\x9C\x85 Rede voltou após troca de MAC.\n");
        return true;
    }
    printf("\xE2\x9A\xA0\xEF\xB8\x8F  Rede não voltou dentro do timeout após troca de MAC.\n");
    return false;
}

bool LinuxNetworkAdapter::waitConnectivity(int timeout_sec) {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-t0).count() < timeout_sec) {
        if (pingOk()) return true;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    return false;
}

bool LinuxNetworkAdapter::pingOk() {
    std::string out; return runCmdCapture("ping -c 3 -W 2 8.8.8.8", out) == 0;
}

std::optional<std::string> LinuxNetworkAdapter::getIp() {
    std::string out;
    if (runCmdCapture("ip -4 addr show dev " + iface_, out) != 0) return std::nullopt;
    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        auto s = line; // já vem com espaços
        if (s.find("inet ") != std::string::npos) {
            auto pos = s.find("inet ");
            auto rest = s.substr(pos + 5);
            auto sp = rest.find(' ');
            auto ipmask = rest.substr(0, sp);
            auto slash = ipmask.find('/');
            if (slash != std::string::npos) return ipmask.substr(0, slash);
        }
    }
    return std::nullopt;
}

########################################
# src/infra/OoklaSpeedTester.hpp
########################################
#pragma once
#include "domain/SpeedTester.hpp"
#include "util/Process.hpp"

class OoklaSpeedTester : public SpeedTester {
public:
    nlohmann::json runOokla(const std::filesystem::path& saveJson) override;
    nlohmann::json runJs(const std::filesystem::path& jsPath,
                         const std::filesystem::path& resultJsonPath) override;
};

########################################
# src/infra/OoklaSpeedTester.cpp
########################################
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
        return nlohmann::json{{"error", std::string("parse json: ")+e.what()}, {"raw", out.substr(0,4000)}};
    }
}

nlohmann::json OoklaSpeedTester::runJs(const std::filesystem::path& jsPath,
                                       const std::filesystem::path& resultJsonPath) {
    std::string out;
    int rc = runCmdCapture("node '" + jsPath.string() + "'", out);
    if (rc != 0) return nlohmann::json{{"error", {{"rc", rc}, {"stderr", out}}}};
    try {
        std::ifstream in(resultJsonPath);
        nlohmann::json data; in >> data; return data;
    } catch (const std::exception& e) {
        return nlohmann::json{{"error", std::string("read ")+resultJsonPath.filename().string()+": "+e.what()}};
    }
}

########################################
# src/infra/JsSpeedTester.hpp
########################################
#pragma once
// Mantido junto com OoklaSpeedTester (já implementa runJs). Arquivo opcional.

########################################
# src/infra/WiringPiRelay.hpp
########################################
#pragma once
#include "domain/Relay.hpp"
#include <wiringPi.h>

class WiringPiRelay : public Relay {
public:
    explicit WiringPiRelay(int bcmPin) : bcmPin_(bcmPin) {
        // Usar numeração BCM diretamente
        if (wiringPiSetupGpio() == -1) {
            ok_ = false;
        } else {
            pinMode(bcmPin_, OUTPUT);
            digitalWrite(bcmPin_, LOW);
            ok_ = true;
        }
    }
    bool pulse(double seconds, bool active_high = true) override {
        if (!ok_) return false;
        int on  = active_high ? HIGH : LOW;
        int off = active_high ? LOW  : HIGH;
        digitalWrite(bcmPin_, on);
        delay((unsigned int)(seconds*1000));
        digitalWrite(bcmPin_, off);
        return true;
    }
private:
    int bcmPin_;
    bool ok_ = false;
};

########################################
# src/infra/CurlEmailSender.hpp
########################################
#pragma once
#include "domain/EmailSender.hpp"

class CurlEmailSender : public EmailSender {
public:
    bool send(const std::string& subject,
              const std::string& body,
              const std::vector<std::filesystem::path>& attachments) override;
};

########################################
# src/infra/CurlEmailSender.cpp
########################################
#include "infra/CurlEmailSender.hpp"
#include "util/Env.hpp"
#include <curl/curl.h>
#include <iostream>

static struct curl_slist* add_recipient(struct curl_slist *list, const std::string &addr) {
    return curl_slist_append(list, addr.c_str());
}

bool CurlEmailSender::send(const std::string& subject,
                           const std::string& body,
                           const std::vector<std::filesystem::path>& attachments) {
    std::string EMAIL_USER = getenvOr("EMAIL_USER", "");
    std::string EMAIL_PASS = getenvOr("EMAIL_PASS", "");
    std::string EMAIL_TO   = getenvOr("EMAIL_TO",   "");
    std::string SMTP_SERVER= getenvOr("SMTP_SERVER","smtp.gmail.com");
    std::string SMTP_PORT  = getenvOr("SMTP_PORT",  "587");
    std::string USE_SSL    = getenvOr("EMAIL_USE_SSL","0");

    if (EMAIL_USER.empty() || EMAIL_PASS.empty() || EMAIL_TO.empty()) {
        std::cerr << "❌ EMAIL_USER/EMAIL_PASS/EMAIL_TO ausentes no .env\n";
        return false;
    }
    bool use_ssl = (USE_SSL=="1" || USE_SSL=="true" || USE_SSL=="True");
    std::string url = std::string(use_ssl ? "smtps://" : "smtp://") + SMTP_SERVER + ":" + SMTP_PORT;

    CURL *curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_USERNAME, EMAIL_USER.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, EMAIL_PASS.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!use_ssl) curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_ALL); // STARTTLS
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, EMAIL_USER.c_str());

    struct curl_slist *recipients = nullptr;
    recipients = add_recipient(recipients, EMAIL_TO);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_mime *mime = curl_mime_init(curl);
    // header
    {
        curl_mimepart *part = curl_mime_addpart(mime);
        std::string hdr = "To: " + EMAIL_TO + "\r\nFrom: " + EMAIL_USER + "\r\nSubject: " + subject + "\r\n";
        curl_mime_data(part, hdr.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "text/rfc822-headers");
    }
    // body
    {
        curl_mimepart *text = curl_mime_addpart(mime);
        curl_mime_data(text, body.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(text, "text/plain; charset=utf-8");
    }
    // attachments
    for (const auto& p : attachments) {
        if (!std::filesystem::exists(p)) continue;
        curl_mimepart *att = curl_mime_addpart(mime);
        curl_mime_filedata(att, p.string().c_str());
        curl_mime_filename(att, p.filename().string().c_str());
        curl_mime_type(att, "application/octet-stream");
    }

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(recipients);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "❌ Falha enviando email: " << curl_easy_strerror(res) << "\n";
        return false;
    }
    std::cout << "✉️  Email enviado.\n";
    return true;
}

########################################
# src/infra/FileCsvLogger.hpp
########################################
#pragma once
#include "domain/Logger.hpp"
#include <filesystem>

class FileCsvLogger : public LoggerRepo {
public:
    explicit FileCsvLogger(std::filesystem::path csv): csv_(std::move(csv)) {}
    void append(const std::string& timestamp,
                const std::string& mac,
                const std::string& ip,
                const std::string& result) override;
private:
    std::filesystem::path csv_;
    void ensureHeader();
};

########################################
# src/infra/FileCsvLogger.cpp
########################################
#include "infra/FileCsvLogger.hpp"
#include <fstream>
#include <algorithm>

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
    out << ts << ";" << mac << ";" << ip << ";" << safe << "\n";
}

########################################
# src/infra/MacListProvider.hpp
########################################
#pragma once
#include "domain/MacProvider.hpp"
#include <filesystem>

class MacListProvider : public MacProvider {
public:
    MacListProvider(std::filesystem::path list, std::filesystem::path idx)
        : list_(std::move(list)), idx_(std::move(idx)) {}
    std::optional<std::string> next() override;
private:
    std::filesystem::path list_;
    std::filesystem::path idx_;
};

########################################
# src/infra/MacListProvider.cpp
########################################
#include "infra/MacListProvider.hpp"
#include <fstream>
#include <vector>
#include <algorithm>

static std::string trim2(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b==std::string::npos) return "";
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e-b+1);
}

std::optional<std::string> MacListProvider::next() {
    std::error_code ec;
    if (!std::filesystem::exists(list_, ec)) return std::nullopt;
    std::ifstream in(list_);
    if (!in.is_open()) return std::nullopt;
    std::vector<std::string> macs;
    for (std::string line; std::getline(in, line); ) {
        auto s = trim2(line);
        if (!s.empty()) macs.push_back(s);
    }
    if (macs.empty()) return std::nullopt;

    long idxv = 0;
    if (std::filesystem::exists(idx_, ec)) {
        std::ifstream ii(idx_); long v=0; if (ii>>v) idxv=v;
    }
    std::string mac = macs[(size_t)(idxv % macs.size())];
    {
        std::ofstream oi(idx_, std::ios::trunc);
        oi << ((idxv + 1) % macs.size());
    }
    return mac;
}

########################################
# src/app/MonitorService.hpp
########################################
#pragma once
#include <vector>
#include <filesystem>
#include <memory>
#include "domain/Report.hpp"
#include "domain/NetworkAdapter.hpp"
#include "domain/SpeedTester.hpp"
#include "domain/Relay.hpp"
#include "domain/EmailSender.hpp"
#include "domain/Logger.hpp"
#include "domain/MacProvider.hpp"

struct MonitorOptions {
    std::filesystem::path jsPath;
    std::filesystem::path jsResultJson;
    std::filesystem::path ooklaJson;
    bool useRelay = true;
    int relayDelaySeconds = 30;
    int postResetWaitMin = 90; // >= 90
};

class MonitorService {
public:
    MonitorService(std::string iface,
                   std::shared_ptr<NetworkAdapter> net,
                   std::shared_ptr<SpeedTester> speed,
                   std::shared_ptr<Relay> relay,
                   std::shared_ptr<EmailSender> mail,
                   std::shared_ptr<LoggerRepo> log,
                   std::shared_ptr<MacProvider> macs,
                   std::filesystem::path logCsv)
        : iface_(std::move(iface)), net_(std::move(net)), speed_(std::move(speed)), relay_(std::move(relay)),
          mail_(std::move(mail)), log_(std::move(log)), macs_(std::move(macs)), logCsv_(std::move(logCsv)) {}

    std::vector<Report> runOnce(const MonitorOptions& opt);
    void sendEmailSummary(const std::vector<Report>& reps, const std::vector<std::filesystem::path>& atts);

private:
    std::string iface_;
    std::shared_ptr<NetworkAdapter> net_;
    std::shared_ptr<SpeedTester> speed_;
    std::shared_ptr<Relay> relay_;
    std::shared_ptr<EmailSender> mail_;
    std::shared_ptr<LoggerRepo> log_;
    std::shared_ptr<MacProvider> macs_;
    std::filesystem::path logCsv_;
};

########################################
# src/app/MonitorService.cpp
########################################
#include "app/MonitorService.hpp"
#include "util/Time.hpp"
#include <sstream>
#include <iostream>

static std::string summarizeOokla(const nlohmann::json& d) {
    if (!d.is_object() || d.contains("error")) {
        if (d.contains("error")) {
            if (d["error"].is_object() && d["error"].contains("stderr"))
                return std::string("Erro Ookla: ") + d["error"]["stderr"].dump();
            return "Erro Ookla";
        }
        return "Erro Ookla";
    }
    double ping = d.value("ping", nlohmann::json{}).value("latency", 0.0);
    double dl_bw = d.value("download", nlohmann::json{}).value("bandwidth", 0.0);
    double ul_bw = d.value("upload",   nlohmann::json{}).value("bandwidth", 0.0);
    std::string srv = d.value("server", nlohmann::json{}).value("host", "");
    std::ostringstream oss;
    oss << "Servidor: " << srv << "\n"
        << "Ping: " << ping << " ms\n"
        << "Download (bandwidth): " << (dl_bw/1e6) << " Mbps\n"
        << "Upload (bandwidth): "   << (ul_bw/1e6) << " Mbps";
    return oss.str();
}

std::vector<Report> MonitorService::runOnce(const MonitorOptions& opt) {
    std::vector<Report> reports;

    // 1) Rotação de MAC
    std::string mac = macs_ ? macs_->next().value_or("") : "";
    if (!mac.empty()) {
        bool ok = net_->applyMac(mac);
        if (ok) std::cout << "\xF0\x9F\x93\xA1 MAC aplicado (" << iface_ << "): " << mac << "\n";
        else    std::cout << "\xE2\x9A\xA0\xEF\xB8\x8F  Falha ao aplicar MAC " << mac << "\n";
    } else {
        std::cout << "\xE2\x9A\xA0\xEF\xB8\x8F  mac.txt ausente ou vazio — sem rotação de MAC.\n";
    }

    auto makeReport = [&](const std::string& label){
        Report r{};
        r.label = label;
        r.timestamp = nowStr();
        r.mac = mac.empty()? "N/D" : mac;
        auto ip = net_->getIp();
        r.ip = ip ? *ip : "N/D";

        std::cout << "\xF0\x9F\x94\x8D [" << label << "] Ping 8.8.8.8 ...\n";
        r.ping_ok = net_->pingOk();

        std::vector<std::string> logp{label, std::string("Ping ") + (r.ping_ok?"OK":"falhou")};
        std::ostringstream body;
        body << "\xF0\x9F\x93\x8B " << label << "\n"
             << "\xF0\x9F\x95\x92 Momento: " << r.timestamp << "\n"
             << "\xF0\x9F\x93\xA1 Interface: " << iface_ << "\n"
             << "\xF0\x9F\x86\x94 MAC: " << r.mac << "\n"
             << "\xF0\x9F\x8C\x90 IP: "  << r.ip  << "\n";

        if (r.ping_ok) {
            std::cout << "\xE2\x9C\x85 Ping OK\n";
            std::cout << "\xF0\x9F\x9A\x80 [" << label << "] Ookla speedtest (CLI) ...\n";
            r.ookla = speed_->runOokla(opt.ooklaJson);
            body << "\n\xE2\x9A\xA1 Ookla CLI:\n" << summarizeOokla(r.ookla) << "\n";
            if (r.ookla.is_object() && !r.ookla.contains("error")) {
                double dl = r.ookla["download"].value("bandwidth", 0.0)/1e6;
                double ul = r.ookla["upload"].value("bandwidth", 0.0)/1e6;
                std::ostringstream t; t.setf(std::ios::fixed); t.precision(2);
                t << "Ookla DL " << dl << " UL " << ul; logp.push_back(t.str());
            } else logp.push_back("Ookla erro");

            std::cout << "\xF0\x9F\x93\x8A [" << label << "] Script JS de velocidade ...\n";
            r.js_data = speed_->runJs(opt.jsPath, opt.jsResultJson);
            body << "\n\xF0\x9F\x93\x88 Speedtest JS (resumo bruto JSON):\n"
                 << r.js_data.dump(2).substr(0,6000) << "\n";
            if (r.js_data.is_object() && !r.js_data.contains("error") &&
                r.js_data.contains("download_mbps") && r.js_data.contains("upload_mbps") &&
                r.js_data["download_mbps"].is_number() && r.js_data["upload_mbps"].is_number()) {
                std::ostringstream t; t.setf(std::ios::fixed); t.precision(2);
                t << "JS DL " << r.js_data["download_mbps"].get<double>()
                  << " UL " << r.js_data["upload_mbps"].get<double>();
                logp.push_back(t.str());
            } else logp.push_back("JS erro/dados incompletos");
        } else {
            std::cout << "\xE2\x9D\x8C Sem conectividade (ping falhou).\n";
            body << "\n\xE2\x9D\x8C Sem conectividade (ping falhou).\n";
        }

        std::ostringstream lr;
        for (size_t i=0;i<logp.size();++i){ if(i) lr<<" | "; lr<<logp[i]; }
        r.log_result = lr.str();
        log_->append(r.timestamp, r.mac, r.ip, r.log_result);
        r.body_text = body.str();
        return r;
    };

    reports.push_back(makeReport("Teste inicial"));

    // 3) Reset modem
    if (opt.useRelay && relay_) {
        std::cout << "\xF0\x9F\x94\x84 Resetando modem via relé ...\n";
        try {
            bool acionou = relay_->pulse((double)opt.relayDelaySeconds, true);
            if (acionou) std::cout << "\xE2\x9C\x85 Ciclo do relé concluído.\n";
            else         std::cout << "\xE2\x9A\xA0\xEF\xB8\x8F  Relé não pôde ser acionado.\n";
        } catch(...) {
            std::cout << "\xE2\x9A\xA0\xEF\xB8\x8F  Falha no reset via GPIO.\n";
        }
        int wait_time = std::max(opt.relayDelaySeconds, opt.postResetWaitMin);
        std::cout << "\xE2\x8F\xB1  Aguardando retorno da conectividade (timeout " << wait_time << "s) ...\n";
        if (net_->waitConnectivity(wait_time))
            std::cout << "\xE2\x9C\x85 Conectividade restabelecida após reset.\n";
        else
            std::cout << "\xE2\x9A\xA0\xEF\xB8\x8F  Conectividade não voltou no tempo esperado após reset.\n";

        reports.push_back(makeReport("Teste pós-reset"));
    }

    return reports;
}

void MonitorService::sendEmailSummary(const std::vector<Report>& reps,
                                      const std::vector<std::filesystem::path>& attachments) {
    std::ostringstream body;
    for (auto &r : reps) { body << r.body_text << "\n\n"; }
    body << "\xF0\x9F\x97\x92\xEF\xB8\x8F Entradas adicionadas ao arquivo connection_log.csv:\n";
    for (auto &r : reps) { body << "- " << r.timestamp << " | " << r.log_result << "\n"; }
    mail_->send("Relatório de Teste de Conexão (Raspberry)", body.str(), attachments);
}

########################################
# src/main.cpp
########################################
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <memory>
#include <unistd.h>

#include "util/Env.hpp"
#include "infra/LinuxNetworkAdapter.hpp"
#include "infra/OoklaSpeedTester.hpp"
#include "infra/WiringPiRelay.hpp"
#include "infra/CurlEmailSender.hpp"
#include "infra/FileCsvLogger.hpp"
#include "infra/MacListProvider.hpp"
#include "app/MonitorService.hpp"

struct Args {
    bool once=false; bool no_relay=false;
    int relay_pin_bcm=17; int relay_delay=30; int cooldown=3*60*60;
    std::filesystem::path js{"test.js"};
    std::filesystem::path json_out{"result.json"};
};

static Args parseArgs(int argc, char** argv){
    Args a; for (int i=1;i<argc;i++){ std::string s=argv[i];
        if(s=="--once") a.once=true;
        else if(s=="--no-relay") a.no_relay=true;
        else if(s=="--relay-pin" && i+1<argc) a.relay_pin_bcm=std::stoi(argv[++i]);
        else if(s=="--relay-delay-seconds" && i+1<argc) a.relay_delay=std::stoi(argv[++i]);
        else if(s=="--cooldown-seconds" && i+1<argc) a.cooldown=std::stoi(argv[++i]);
        else if(s=="--js" && i+1<argc) a.js=argv[++i];
        else if(s=="--json" && i+1<argc) a.json_out=argv[++i];
        else std::cerr<<"Parâmetro desconhecido: "<<s<<"\n";
    } return a;
}

static std::filesystem::path exeDir(){
    char buf[4096]; ssize_t len=readlink("/proc/self/exe", buf, sizeof(buf)-1);
    if(len<=0) return std::filesystem::current_path(); buf[len]='\0';
    return std::filesystem::path(buf).parent_path();
}

int main(int argc, char** argv){
    if(geteuid()!=0) std::cerr<<"\xE2\x9A\xA0\xEF\xB8\x8F  Execute como root (sudo) para trocar MAC e usar GPIO.\n";

    auto HERE = exeDir();
    loadDotenv(HERE/".env");

    std::string NET_IFACE = getenvOr("NET_IFACE","eth0");
    int RELAY_PIN_ENV = std::stoi(getenvOr("RELAY_PIN","17"));

    Args args = parseArgs(argc, argv);
    if(args.relay_pin_bcm==17 && RELAY_PIN_ENV!=17) args.relay_pin_bcm = RELAY_PIN_ENV;

    auto net   = std::make_shared<LinuxNetworkAdapter>(NET_IFACE);
    auto speed = std::make_shared<OoklaSpeedTester>();
    std::shared_ptr<Relay> relay = args.no_relay ? nullptr : std::make_shared<WiringPiRelay>(args.relay_pin_bcm);
    auto mail  = std::make_shared<CurlEmailSender>();

    auto LOG_FILE = HERE/"connection_log.csv";
    auto logger   = std::make_shared<FileCsvLogger>(LOG_FILE);

    auto macs = std::make_shared<MacListProvider>(HERE/"mac.txt", HERE/"mac_index.txt");

    MonitorService svc(
        NET_IFACE, net, speed, relay, mail, logger, macs, LOG_FILE
    );

    MonitorOptions opt;
    opt.jsPath = args.js.is_absolute()? args.js : (HERE/args.js);
    opt.jsResultJson = args.json_out.is_absolute()? args.json_out : (HERE/args.json_out);
    opt.ooklaJson = HERE/"ookla_result.json";
    opt.useRelay = (relay!=nullptr);
    opt.relayDelaySeconds = args.relay_delay;
    opt.postResetWaitMin = 90;

    while(true){
        auto reports = svc.runOnce(opt);
        std::vector<std::filesystem::path> atts;
        if(std::filesystem::exists(opt.ooklaJson)) atts.push_back(opt.ooklaJson);
        if(std::filesystem::exists(opt.jsResultJson)) atts.push_back(opt.jsResultJson);
        auto rcsv = HERE/"results.csv"; if(std::filesystem::exists(rcsv)) atts.push_back(rcsv);
        if(std::filesystem::exists(LOG_FILE)) atts.push_back(LOG_FILE);
        svc.sendEmailSummary(reports, atts);

        if(args.once) break;
        int secs = std::max(0, args.cooldown);
        std::cout << "\xE2\x8F\xB3 Aguardando " << (secs/3600) << "h para repetir ...\n";
        std::this_thread::sleep_for(std::chrono::seconds(secs));
    }
    return 0;
}
