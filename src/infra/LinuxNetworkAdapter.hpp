#pragma once

#include "domain/NetworkAdapter.hpp"
#include "util/Process.hpp"

#include <optional>
#include <string>

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
