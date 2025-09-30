#pragma once

#include <optional>
#include <string>

struct NetworkAdapter {
    virtual ~NetworkAdapter() = default;
    virtual bool applyMac(const std::string& mac) = 0;
    virtual bool waitConnectivity(int timeout_sec) = 0;
    virtual bool pingOk() = 0;
    virtual std::optional<std::string> getIp() = 0;
};
