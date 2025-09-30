#pragma once

#include "domain/EmailSender.hpp"
#include "domain/Logger.hpp"
#include "domain/MacProvider.hpp"
#include "domain/NetworkAdapter.hpp"
#include "domain/Relay.hpp"
#include "domain/Report.hpp"
#include "domain/SpeedTester.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct MonitorOptions {
    std::filesystem::path jsPath;
    std::filesystem::path jsResultJson;
    std::filesystem::path ooklaJson;
    bool useRelay = true;
    int relayDelaySeconds = 30;
    int postResetWaitMin = 90;
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
        : iface_(std::move(iface)),
          net_(std::move(net)),
          speed_(std::move(speed)),
          relay_(std::move(relay)),
          mail_(std::move(mail)),
          log_(std::move(log)),
          macs_(std::move(macs)),
          logCsv_(std::move(logCsv)) {}

    std::vector<Report> runOnce(const MonitorOptions& opt);
    void sendEmailSummary(const std::vector<Report>& reps,
                          const std::vector<std::filesystem::path>& atts);

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
