#include "app/MonitorService.hpp"

#include "util/Time.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace {
std::string summarizeOokla(const nlohmann::json& d) {
    if (!d.is_object() || d.contains("error")) {
        if (d.contains("error")) {
            if (d["error"].is_object() && d["error"].contains("stderr")) {
                return std::string("Erro Ookla: ") + d["error"]["stderr"].dump();
            }
            return "Erro Ookla";
        }
        return "Erro Ookla";
    }

    double ping = d.value("ping", nlohmann::json{}).value("latency", 0.0);
    double dl_bw = d.value("download", nlohmann::json{}).value("bandwidth", 0.0);
    double ul_bw = d.value("upload", nlohmann::json{}).value("bandwidth", 0.0);
    std::string srv = d.value("server", nlohmann::json{}).value("host", "");

    std::ostringstream oss;
    oss << "Servidor: " << srv << '\n'
        << "Ping: " << ping << " ms\n"
        << "Download (bandwidth): " << (dl_bw / 1e6) << " Mbps\n"
        << "Upload (bandwidth): " << (ul_bw / 1e6) << " Mbps";
    return oss.str();
}
}  // namespace

std::vector<Report> MonitorService::runOnce(const MonitorOptions& opt) {
    std::vector<Report> reports;

    std::string mac = macs_ ? macs_->next().value_or("") : "";
    if (!mac.empty()) {
        bool ok = net_->applyMac(mac);
        if (ok)
            std::cout << "📡 MAC aplicado (" << iface_ << "): " << mac << "\n";
        else
            std::cout << "⚠️  Falha ao aplicar MAC " << mac << "\n";
    } else {
        std::cout << "⚠️  mac.txt ausente ou vazio — sem rotação de MAC.\n";
    }

    auto makeReport = [&](const std::string& label) {
        Report r{};
        r.label = label;
        r.timestamp = nowStr();
        r.mac = mac.empty() ? "N/D" : mac;
        auto ip = net_->getIp();
        r.ip = ip ? *ip : "N/D";

        std::cout << "🔍 [" << label << "] Ping 8.8.8.8 ...\n";
        r.ping_ok = net_->pingOk();

        std::vector<std::string> logp{label, std::string("Ping ") + (r.ping_ok ? "OK" : "falhou")};
        std::ostringstream body;
        body << "📋 " << label << "\n"
             << "🕒 Momento: " << r.timestamp << "\n"
             << "📡 Interface: " << iface_ << "\n"
             << "🔔 MAC: " << r.mac << "\n"
             << "🌐 IP: " << r.ip << "\n";

        if (r.ping_ok) {
            std::cout << "✅ Ping OK\n";
            std::cout << "🚀 [" << label << "] Ookla speedtest (CLI) ...\n";
            r.ookla = speed_->runOokla(opt.ooklaJson);
            body << "\n⚡ Ookla CLI:\n" << summarizeOokla(r.ookla) << "\n";
            if (r.ookla.is_object() && !r.ookla.contains("error")) {
                double dl = r.ookla["download"].value("bandwidth", 0.0) / 1e6;
                double ul = r.ookla["upload"].value("bandwidth", 0.0) / 1e6;
                std::ostringstream t;
                t.setf(std::ios::fixed);
                t.precision(2);
                t << "Ookla DL " << dl << " UL " << ul;
                logp.push_back(t.str());
            } else {
                logp.push_back("Ookla erro");
            }

            std::cout << "📊 [" << label << "] Script JS de velocidade ...\n";
            r.js_data = speed_->runJs(opt.jsPath, opt.jsResultJson);
            body << "\n📈 Speedtest JS (resumo bruto JSON):\n" << r.js_data.dump(2).substr(0, 6000) << "\n";
            if (r.js_data.is_object() && !r.js_data.contains("error") &&
                r.js_data.contains("download_mbps") && r.js_data.contains("upload_mbps") &&
                r.js_data["download_mbps"].is_number() && r.js_data["upload_mbps"].is_number()) {
                std::ostringstream t;
                t.setf(std::ios::fixed);
                t.precision(2);
                t << "JS DL " << r.js_data["download_mbps"].get<double>()
                  << " UL " << r.js_data["upload_mbps"].get<double>();
                logp.push_back(t.str());
            } else {
                logp.push_back("JS erro/dados incompletos");
            }
        } else {
            std::cout << "❌ Sem conectividade (ping falhou).\n";
            body << "\n❌ Sem conectividade (ping falhou).\n";
        }

        std::ostringstream lr;
        for (size_t i = 0; i < logp.size(); ++i) {
            if (i) lr << " | ";
            lr << logp[i];
        }
        r.log_result = lr.str();
        log_->append(r.timestamp, r.mac, r.ip, r.log_result);
        r.body_text = body.str();
        return r;
    };

    reports.push_back(makeReport("Teste inicial"));

    if (opt.useRelay && relay_) {
        std::cout << "🔄 Resetando modem via relé ...\n";
        try {
            bool acionou = relay_->pulse(static_cast<double>(opt.relayDelaySeconds), true);
            if (acionou)
                std::cout << "✅ Ciclo do relé concluído.\n";
            else
                std::cout << "⚠️  Relé não pôde ser acionado.\n";
        } catch (...) {
            std::cout << "⚠️  Falha no reset via GPIO.\n";
        }
        int wait_time = std::max(opt.relayDelaySeconds, opt.postResetWaitMin);
        std::cout << "⏱️  Aguardando retorno da conectividade (timeout " << wait_time << "s) ...\n";
        if (net_->waitConnectivity(wait_time))
            std::cout << "✅ Conectividade restabelecida após reset.\n";
        else
            std::cout << "⚠️  Conectividade não voltou no tempo esperado após reset.\n";

        reports.push_back(makeReport("Teste pós-reset"));
    }

    return reports;
}

void MonitorService::sendEmailSummary(const std::vector<Report>& reps,
                                      const std::vector<std::filesystem::path>& attachments) {
    std::ostringstream body;
    for (auto& r : reps) {
        body << r.body_text << "\n\n";
    }
    body << "🗒️ Entradas adicionadas ao arquivo connection_log.csv:\n";
    for (auto& r : reps) {
        body << "- " << r.timestamp << " | " << r.log_result << "\n";
    }
    mail_->send("Relatório de Teste de Conexão (Raspberry)", body.str(), attachments);
}
