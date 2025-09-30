#include "infra/CurlEmailSender.hpp"

#include "util/Env.hpp"

#include <curl/curl.h>
#include <filesystem>
#include <iostream>

namespace {
struct curl_slist* addRecipient(struct curl_slist* list, const std::string& addr) {
    return curl_slist_append(list, addr.c_str());
}
}  // namespace

bool CurlEmailSender::send(const std::string& subject,
                           const std::string& body,
                           const std::vector<std::filesystem::path>& attachments) {
    std::string EMAIL_USER = getenvOr("EMAIL_USER", "");
    std::string EMAIL_PASS = getenvOr("EMAIL_PASS", "");
    std::string EMAIL_TO = getenvOr("EMAIL_TO", "");
    std::string SMTP_SERVER = getenvOr("SMTP_SERVER", "smtp.gmail.com");
    std::string SMTP_PORT = getenvOr("SMTP_PORT", "587");
    std::string USE_SSL = getenvOr("EMAIL_USE_SSL", "0");

    if (EMAIL_USER.empty() || EMAIL_PASS.empty() || EMAIL_TO.empty()) {
        std::cerr << "❌ EMAIL_USER/EMAIL_PASS/EMAIL_TO ausentes no .env\n";
        return false;
    }

    bool use_ssl = (USE_SSL == "1" || USE_SSL == "true" || USE_SSL == "True");
    std::string url = std::string(use_ssl ? "smtps://" : "smtp://") + SMTP_SERVER + ":" + SMTP_PORT;

    CURL* curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_USERNAME, EMAIL_USER.c_str());
    curl_easy_setopt(curl, CURLOPT_PASSWORD, EMAIL_PASS.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    if (!use_ssl) curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(CURLUSESSL_ALL));
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, EMAIL_USER.c_str());

    struct curl_slist* recipients = nullptr;
    recipients = addRecipient(recipients, EMAIL_TO);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

    curl_mime* mime = curl_mime_init(curl);
    {
        curl_mimepart* part = curl_mime_addpart(mime);
        std::string hdr = "To: " + EMAIL_TO + "\r\nFrom: " + EMAIL_USER + "\r\nSubject: " + subject + "\r\n";
        curl_mime_data(part, hdr.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(part, "text/rfc822-headers");
    }
    {
        curl_mimepart* text = curl_mime_addpart(mime);
        curl_mime_data(text, body.c_str(), CURL_ZERO_TERMINATED);
        curl_mime_type(text, "text/plain; charset=utf-8");
    }
    for (const auto& p : attachments) {
        if (!std::filesystem::exists(p)) continue;
        curl_mimepart* att = curl_mime_addpart(mime);
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
