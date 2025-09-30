#include "infra/LinuxNetworkAdapter.hpp"

#include <chrono>
#include <cstdio>
#include <sstream>
#include <thread>

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
            auto dev = line.substr(pos + 1);
            if (dev == iface_) {
                conn = name;
                break;
            }
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
        std::fprintf(stderr, "⚠️  ip link set dev %s address %s falhou: %s\n", iface_.c_str(), mac.c_str(), out.c_str());
    }
    return rc == 0;
}

bool LinuxNetworkAdapter::applyMac(const std::string& mac) {
    std::printf("🔁 Trocando MAC em %s para %s ...\n", iface_.c_str(), mac.c_str());
    bool ok = nmcliSetClonedMac(mac) || iplinkSetMac(mac);
    if (!ok) {
        std::printf("❌ Falha ao aplicar MAC.\n");
        return false;
    }
    if (waitConnectivity(90)) {
        std::printf("✅ Rede voltou após troca de MAC.\n");
        return true;
    }
    std::printf("⚠️  Rede não voltou dentro do timeout após troca de MAC.\n");
    return false;
}

bool LinuxNetworkAdapter::waitConnectivity(int timeout_sec) {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count() < timeout_sec) {
        if (pingOk()) return true;
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    return false;
}

bool LinuxNetworkAdapter::pingOk() {
    std::string out;
    return runCmdCapture("ping -c 3 -W 2 8.8.8.8", out) == 0;
}

std::optional<std::string> LinuxNetworkAdapter::getIp() {
    std::string out;
    if (runCmdCapture("ip -4 addr show dev " + iface_, out) != 0) return std::nullopt;

    std::istringstream iss(out);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("inet ") != std::string::npos) {
            auto pos = line.find("inet ");
            auto rest = line.substr(pos + 5);
            auto sp = rest.find(' ');
            auto ipmask = rest.substr(0, sp);
            auto slash = ipmask.find('/');
            if (slash != std::string::npos) return ipmask.substr(0, slash);
        }
    }
    return std::nullopt;
}
