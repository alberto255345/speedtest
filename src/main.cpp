#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "app/MonitorService.hpp"
#include "infra/CurlEmailSender.hpp"
#include "infra/FileCsvLogger.hpp"
#include "infra/LinuxNetworkAdapter.hpp"
#include "infra/MacListProvider.hpp"
#include "infra/OoklaSpeedTester.hpp"
#include "infra/WiringPiRelay.hpp"
#include "util/Env.hpp"

struct Args {
    bool once = false;
    bool no_relay = false;
    int relay_pin_bcm = 17;
    int relay_delay = 30;
    int cooldown = 3 * 60 * 60;
    std::filesystem::path js{"test.js"};
    std::filesystem::path json_out{"result.json"};
};

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--once")
            a.once = true;
        else if (s == "--no-relay")
            a.no_relay = true;
        else if (s == "--relay-pin" && i + 1 < argc)
            a.relay_pin_bcm = std::stoi(argv[++i]);
        else if (s == "--relay-delay-seconds" && i + 1 < argc)
            a.relay_delay = std::stoi(argv[++i]);
        else if (s == "--cooldown-seconds" && i + 1 < argc)
            a.cooldown = std::stoi(argv[++i]);
        else if (s == "--js" && i + 1 < argc)
            a.js = argv[++i];
        else if (s == "--json" && i + 1 < argc)
            a.json_out = argv[++i];
        else
            std::cerr << "Parâmetro desconhecido: " << s << "\n";
    }
    return a;
}

std::filesystem::path exeDir() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return std::filesystem::current_path();
    buf[len] = '\0';
    return std::filesystem::path(buf).parent_path();
}

int main(int argc, char** argv) {
    if (geteuid() != 0) std::cerr << "⚠️  Execute como root (sudo) para trocar MAC e usar GPIO.\n";

    auto here = exeDir();
    loadDotenv(here / ".env");

    std::string NET_IFACE = getenvOr("NET_IFACE", "eth0");
    int RELAY_PIN_ENV = std::stoi(getenvOr("RELAY_PIN", "17"));

    Args args = parseArgs(argc, argv);
    if (args.relay_pin_bcm == 17 && RELAY_PIN_ENV != 17) args.relay_pin_bcm = RELAY_PIN_ENV;

    auto net = std::make_shared<LinuxNetworkAdapter>(NET_IFACE);
    auto speed = std::make_shared<OoklaSpeedTester>();
    std::shared_ptr<Relay> relay = args.no_relay ? nullptr : std::make_shared<WiringPiRelay>(args.relay_pin_bcm);
    auto mail = std::make_shared<CurlEmailSender>();

    auto LOG_FILE = here / "connection_log.csv";
    auto logger = std::make_shared<FileCsvLogger>(LOG_FILE);

    auto macs = std::make_shared<MacListProvider>(here / "mac.txt", here / "mac_index.txt");

    MonitorService svc(NET_IFACE, net, speed, relay, mail, logger, macs, LOG_FILE);

    MonitorOptions opt;
    opt.jsPath = args.js.is_absolute() ? args.js : (here / args.js);
    opt.jsResultJson = args.json_out.is_absolute() ? args.json_out : (here / args.json_out);
    opt.ooklaJson = here / "ookla_result.json";
    opt.useRelay = (relay != nullptr);
    opt.relayDelaySeconds = args.relay_delay;
    opt.postResetWaitMin = 90;

    while (true) {
        auto reports = svc.runOnce(opt);
        std::vector<std::filesystem::path> atts;
        if (std::filesystem::exists(opt.ooklaJson)) atts.push_back(opt.ooklaJson);
        if (std::filesystem::exists(opt.jsResultJson)) atts.push_back(opt.jsResultJson);
        auto rcsv = here / "results.csv";
        if (std::filesystem::exists(rcsv)) atts.push_back(rcsv);
        if (std::filesystem::exists(LOG_FILE)) atts.push_back(LOG_FILE);
        svc.sendEmailSummary(reports, atts);

        if (args.once) break;
        int secs = std::max(0, args.cooldown);
        std::cout << "⏳ Aguardando " << (secs / 3600) << "h para repetir ...\n";
        std::this_thread::sleep_for(std::chrono::seconds(secs));
    }

    return 0;
}
