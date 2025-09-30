#pragma once

#include "domain/EmailSender.hpp"

class CurlEmailSender : public EmailSender {
public:
    bool send(const std::string& subject,
              const std::string& body,
              const std::vector<std::filesystem::path>& attachments) override;
};
